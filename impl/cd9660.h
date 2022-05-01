/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef CD9660_H
#define CD9660_H

#include "wrapper.h"

#include "impl/extent.h"
#include "impl/unique.h"
#include "impl/source.h"
#include "impl/strenc.h"
#include "impl/endian.h"
#include "impl/volume.h"

namespace CD
{

// receive a list of files, possibly organized in a subtree of folders
// create an ISO 9660:1988 spec filesystem with Joliet Unicode support

// https://wiki.osdev.org/ISO_9660 & http://pismotec.com/cfs/jolspec.html
// An ISO 9660 filesystem begins by 32 KiB which may be used for arbitrary data.

// the number of unused byte positions after the last Directory Record
// in all Logical Sectors in which the directory is recorded. = ROUND UP!

constexpr const blksize_t kMinCdSectorSize = 2048; // 2k
constexpr const size_t kPathTbLengthCap = 1 << 16; // 65536

// a-characters: A B C D E F G H I J K L M N O P Q R S T U V W X Y Z 0 1 2 3 4 5 6 7 8 9 _
//               ! " % & ' ( ) * + , - . / : ; < = > ?
// d-characters: A B C D E F G H I J K L M N O P Q R S T U V W X Y Z 0 1 2 3 4 5 6 7 8 9 _

typedef char achar;
typedef char dchar;
// we will declare subranges later

// SEPARATOR 1 represented by the bit combination (2E)
// SEPARATOR 2 represented by the bit combination (3B)
// $ echo '.;' | hexdump
// 0000000 2e 3b 0a

/// CDFS date/time to use in the volume header(s)
union __attribute( ( packed ) ) DateTime
{
    struct
    {
        char decimal[4 + 2 + 2 + 2 + 2 + 2 + 2];
        int8_t tzoff;
    };
    char buf[sizeof( decimal ) + sizeof( tzoff )]; // FORTIFY...

    DateTime( const DateTime & other ) = default;
    DateTime & operator=( const DateTime & other ) = default;
    DateTime & setTm( const struct tm & t, int centiseconds );
    DateTime & operator=( const struct timespec & ts );
    DateTime();

    void clear() { memset( decimal, '0', sizeof( decimal ) ); tzoff = 0; }
};

static_assert( sizeof( DateTime ) == 4 + 2 + 2 + 2 + 2 + 2 + 2 + 1, "CD::DateTime wrong size" );

/// CDFS date/time to use in directory entries
struct __attribute( ( packed ) ) DirEntryDtTime
{
    DirEntryDtTime() = default; // don't initialize!
    uint8_t year, month, day, hour, minute, second, tzone;
    void setTm( const struct tm & t, int centiseconds );
    DirEntryDtTime & operator=( const struct timespec & ts );
};

/// A path table entry, excluding the name. L = little-endian.
/// The path table is a list of all directories on the drive.
/// It is laid out in ascending order of directory level
/// and alphabetically sorted within each directory level.
/// There are two versions of the path table (big-endian and little-endian).
/// Windows uses the path table to navigate a CD; Mac OS X uses directories.
template<bool L>
struct __attribute( ( packed ) ) PathTableEntry
{
    uint8_t nameLen = 0;
    uint8_t xAttrLen = 0;
    SB<uint32_t, L> extentLba;
    SB<uint16_t, L> parentDir;
    dchar name[0];

size_t textSize() const { return ( nameLen + 1 ) & ~1; }
size_t size() const { return sizeof( PathTableEntry ) + textSize(); }

void set( uint8_t nameSize, uint32_t block, uint16_t parentSeq )
{ nameLen = nameSize; extentLba = block; parentDir = parentSeq; }
};

/// A "both-endian" path table entry pair.
struct PathTableEntryPair
{
    PathTableEntry<true> lsb;
    PathTableEntry<false> msb;

    void set( const std::string & name, uint32_t block, uint16_t parentSeq )
    {
        lsb.set( name.size(), block, parentSeq );
        msb.set( name.size(), block, parentSeq );
    }
};

/// The *header* of a display name/label.
struct __attribute( ( packed ) ) DisplayName
{
    uint8_t size = 1;
    char data[1] = {0};
};

struct __attribute( ( packed ) ) DirectoryEntry
{
    uint8_t entrySz = sizeof( DirectoryEntry ); // default; may be greater
    uint8_t xAttrSz = 0;
    Bilateral<uint32_t> extentLba;
    Bilateral<uint32_t> length;
    DirEntryDtTime dateTime;
    uint8_t fileFlags = 0;
    struct { uint8_t unit = 0, gap = 0; } interleave;
    Bilateral<uint16_t> volSeqNo = 1;
    DisplayName fileName;

size_t size() const { return ( sizeof( DirectoryEntry ) + fileName.size ) & ~1; }
size_t textSize() const { return entrySz - sizeof( DirectoryEntry ) + sizeof( fileName.data ); }
};

enum VolumeType
{
    BootRecord = 0,
    PrimaryVol = 1,
    Supplement = 2, // Joliet
    PartitDesc = 3,
    Terminator = 255
};

enum XAttrFlags
{
    Hidden = 1 << 0,
    Folder = 1 << 1,
    AssocF = 1 << 2,
    Format = 1 << 3,
    OidGid = 1 << 4,
    TBCont = 1 << 7,
};

struct __attribute( ( packed ) ) VolumeDesc
{
    VolumeType type: 8;
    const char identifier[5] = { 'C', 'D', '0', '0', '1' };
    const uint8_t version = 1;
    const uint8_t flags;

VolumeDesc( VolumeType volType, uint8_t volFlags = 0 ) : type( volType ), flags( volFlags ) {}
};

struct __attribute( ( packed ) ) GenVolDesc : public VolumeDesc
{
    Text<achar, 32> systemId;   // should
    Text<dchar, 32> volumeId;   // should
    zero $1[8];
    Bilateral<uint32_t> blocks; // MUST     // fixup - offset after everything mapped
    Data<char, 32> escapeChars;
    Bilateral<uint16_t> volSet = 1;
    Bilateral<uint16_t> volSeq = 1;
    Bilateral<uint16_t> blkSz = kMinCdSectorSize;
    Bilateral<uint32_t> pTabSz; // MUST     // fixup - length after path table filled
    LSB<uint32_t> pTabLsb[2];   // MUST     // fixup - offset after directories mapped
    MSB<uint32_t> pTabMsb[2];   // MUST     // fixup - offset after the 0th table mapped
    DirectoryEntry rootDirectory;   // MUST // offset to itself is known at mapping time

    Text<dchar, 128> volumeSetId; // d1 // should
    Text<achar, 128> publisherId; // a1 // should
    Text<achar, 128> prepareById; // a1 // should
    Text<achar, 128> application; // a1 // should

    Text<dchar, 37> copyFile; // d1, S1-2 // should
    Text<dchar, 37> abstFile; // d1, S1-2 // should
    Text<dchar, 37> biblFile; // d1, S1-2 // should

    DateTime creation, modification, expiration, effective; // MUST
    const char layoutVer = 1;
    zero $3;
    // zero[512] appUsed; zero[653] reserved;

    GenVolDesc( VolumeType type, uint8_t flags, const char * escChars )
    : VolumeDesc( type, flags )
    , escapeChars( escChars )
{
    effective = timespec( { 0, 0 } );
    expiration.clear();
}
};

struct __attribute( ( packed ) ) PriVolDesc : public GenVolDesc
{
PriVolDesc() : GenVolDesc( PrimaryVol, 0, "" ) {}
};

// https://www.ibm.com/support/knowledgecenter/en/ssw_aix_71/com.ibm.aix.nlsgdrf/iso10646_ucs-2.htm
// Level 1 Does not allow combining characters.
// level 2 Allows combining marks from Thai, Indic, Hebrew, and Arabic scripts.
// Level 3 Allows combining marks, including ones for Latin, Cyrillic, and Greek.
// MOREINFO we use Level 1 for file names, don't we?

// https://www.ibm.com/developerworks/library/l-linuni/
// Surrogates: if(*src & 0xF800 == 0xD800) { *dest++ = (*src++ & 0x07ff) << 10 + (*src & 0x7ff) + 0x10000; } else *dest++ = *src;
// https://en.wikipedia.org/wiki/Talk:Joliet_(file_system)

//All UCS - 2 code points shall be allowed except for the following UCS - 2 code points :
//● All code points between( 00 )( 00 ) and ( 00 )( 1F ), inclusive.( Control Characters )
//● ( 00 )( 2A ) '*'( Asterisk )
//● ( 00 )( 2F ) '/'( Forward Slash )
//● ( 00 )( 3A ) ':'( Colon )
//● ( 00 )( 3B ) ';'( Semicolon )
//● ( 00 )( 3F ) '?'( Question Mark )
//● ( 00 )( 5C ) '\' (Backslash)

//Separator Bit Combination UCS - 2 Codepoint
//SEPARATOR 1( 2E )( 00 )( 2E )
//SEPARATOR 2( 3B )( 00 )( 3B )

//The ISO 9660: 1988 sections in question are as follows:
//● 6.8.2.2 Identification of directories
//● 7.6.2 Reserved Directory Identifiers
//● 9.1.11 File Identifier
//● 9.4.5 Directory Identifier
//These special case directory identifiers are not intended to represent characters in a graphic character set.
//These characters are placeholders, not characters. Therefore, these definitions remain unchanged on a
//volume recorded in Unicode.
//Simply put, Special Directory Identifiers shall remain as 8 - bit values, even on a UCS-2 volume, where other
//characters have been expanded to 16 - bits.

struct __attribute( ( packed ) ) JolietDesc : public GenVolDesc
{
JolietDesc() : GenVolDesc( Supplement, 0, "%/@" ) {} // is it Unicode Level 1 (no character combining) or CDFS Level 1?

void CopyMeta( const PriVolDesc & priVol );
};

struct __attribute( ( packed ) ) MapVolDesc : public VolumeDesc
{
    Text<achar, 32> systemId;   // should
    Text<dchar, 32> volumeId;   // should
    Bilateral<uint32_t> firstBlock = 0;
    Bilateral<uint32_t> blocks;

MapVolDesc() : VolumeDesc( PartitDesc, 0 ) {}
};

struct __attribute( ( packed ) ) EndVolDesc : public VolumeDesc
{
EndVolDesc() : VolumeDesc( Terminator, 0 ) {}
};

//Filenames must use d - character encoding( strD ), plus dot
//and semicolon which have to occur exactly once per filename.
//Filenames are composed of a File Name, a dot, a File Name Extension,
//a semicolon; and a version number in decimal digits.
//The latter two are usually not displayed to the user.

//There are three Levels of Interchange defined. Level 1 allows filenames with a File Name length of 8
//and an extension length of 3( like MS - DOS ). Levels 2 and 3 allow File Name
//and File Name Extension to have a combined length of up to 30 characters.

//The ECMA - 119 Directory Record format can hold composed names of up to 222 characters.
//This would violate the specs but must nevertheless be handled by a reader of the filesystem.

class CD9660Out : public Volume
{
public: // make private after moving code inside the class
    struct FS
    {
        // replace with refs, assign with ctor
        GenVolDesc * vol;
        INameRule * rule;
        ICharPack * pack;
    };

public:
    // TODO extract CDConstants as common parent of CD9660 entries
    static const constexpr blksize_t CD9660_BS = kMinCdSectorSize;
    static const constexpr size_t PATHTB_SZ = kPathTbLengthCap;

    CD9660Out( bool withUnicode = true );
    blksize_t sizeRange() const override { return kMinCdSectorSize; }
    // generalize to exposure and extract layouts? extract Follower completely?
    // also TODO make DeepLook const here
    Colonies plan( const Original & tree, Planner & outPlanner, Planner & tmpPlanner ) override;

    inline blksize_t blockSize() const override { return _pri_vol.blkSz.toInt(); }
    inline void setBlockSize( blksize_t blkSize ) override
    { _pri_vol.blkSz = _sec_vol.blkSz = blkSize; }

    void setSize( off64_t size );
    void setHybrid( Hybrid & slave ) { hybrid = &slave; }
    off64_t planHeaders( IAppend & planner ) const;
    off64_t planVolumes( IAppend & planner, std::function<void( FS & )> fsGen );

    void setLabels( const char * system, const char * volume ) override;

    // aware of <source.h>
    // -Write (FileEntry)
    // +Write (PathEntry)
    // =Write (Exposure) => Write(Exposure.fsRoot)

    // use std::static_pointer_cast to cast Entry -> PathEntry if openFlags() & O_DIRECTORY

    // FileEntry : fileTable -> offset -> store
    // VolumeDesc x PathEntry : pathTable -> offset (fixup <- rootDir)
    // end-of-dir -> offset (pathTable)
    // end-of-all -> offset (blocks)

private:// members are packed and simple-serialized; the class itself is not
    PriVolDesc _pri_vol;
    JolietDesc _sec_vol;
    MapVolDesc _map_vol;
    EndVolDesc _end_vol;

private:// implementation components - loose/unpacked and contain algorithms
    PriVolRule pri_rule;
    SecVolRule sec_rule;
    CharANSI _ansi_pack;
    CharUCS2 _ucs2_pack;

private:// high-level indexing structures (i.e. facilitation of "for" loops)
    std::map<VolumeType, FS> _volumes;
};

}//namespace CD

#endif // CD9660_H
