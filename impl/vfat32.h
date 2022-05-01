/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef VFAT32_H
#define VFAT32_H

#include "wrapper.h"

#include "impl/endian.h"
#include "impl/volume.h"

namespace VF
{

// Inspired by: https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system
// Simplified: https://www.pjrc.com/tech/8051/ide/fat32.html
// https://www.easeus.com/resource/fat32-disk-structure.htm
// https://homepage.cs.uri.edu/~thenry/csc414/57_FAT_Part4_TOC.pdf
// Dummy 8.3 names are produced in our own homebrew way because we need no
// sophisticated randomization and use deterministic collision prevention instead.

enum Attribute
{
    ReadOnly = 1 << 0,
    Hidden  = 1 << 1,
    System  = 1 << 2,
    Label   = 1 << 3, // fileSize=0
    Folder  = 1 << 4, // fileSize=0
    Archive = 1 << 5,
    Device  = 1 << 6,
    Reserved = 1 << 7,
};

enum AlphaCase // "Few other operating systems support it..." - ?? macOS?
{
    LowerType = 1 << 3,
    LowerBase = 1 << 4,
};

typedef LSB<uint16_t> PDT; // packed date/time

union __attribute( ( packed ) ) FatTime
{
    struct  __attribute( ( packed ) )
    {
        uint16_t bsecs: 5; // 4-0 Seconds/2 (0-29)
        uint16_t  mins: 6; // 10-5 Minutes (0-59);
        uint16_t hours: 5; // 15-11 Hours (0-23)
    } bits;
    uint16_t hword;

    void setTm( const struct tm & t, int /*centis*/ );
};

static_assert( ( sizeof( FatTime ) == 2 ), "Time packed loosely" );

union __attribute( ( packed ) ) FatDate
{
    struct __attribute( ( packed ) )
    {
        uint16_t  mday: 5; // 4-0 Day (1–31)
        uint16_t month: 4; // 8-5 Month (1–12)
        uint16_t  year: 7; // 15-9 Year (0=1980)
    } bits;
    uint16_t hword;

    void setTm( const struct tm & t, int /*centis*/ );
};

static_assert( ( sizeof( FatDate ) == 2 ), "Date packed loosely" );

struct __attribute( ( packed ) ) DirectoryEntry
{
    Text<char, 8> baseName;   // 0x00
    Text<char, 3> typeName;   // 0x08
    uint8_t attrs = 0;  // Attribute, 0x0B
    uint8_t cases = 0;  // AlphaCase, 0x0C
    uint8_t csecs;  // centitime (10-ms units since bi-second), 0x0D
    PDT ctime;      // 0x0E
    PDT cdate;      // 0x10
    PDT adate;      // 0x12
    LSB<uint16_t> hiClusterId = 0;  // 0x14
    PDT mtime;      // 0x16
    PDT mdate;      // 0x18
    LSB<uint16_t> loClusterId = 0;  // 0x1A
    LSB<uint32_t> fileSize = 0;     // 0x1C

    uint8_t checksum() const;

    void setStartCluster( blkcnt_t cluster );

    void setCTime( const struct timespec & time );
    void setMTime( const struct timespec & time );
    void setATime( const struct timespec & time );

    // shorthand: set times and length
    void setStat( const struct stat64 & stat );

    void markDir();
};

struct __attribute( ( packed ) ) LongNameEntry
{
    /*
    0x00    1   Sequence Number (bit 6: last logical, first physical LFN entry, bit 5: 0; bits 4-0: number 0x01..0x14 (0x1F), deleted entry: 0xE5)
    0x01    10  Name characters (five UCS-2 characters)
    0x0B    1   Attributes (always 0x0F)
    0x0C    1   Type (always 0x00 for VFAT LFN, other values reserved for future use; for special usage of bits 4 and 3 in SFNs see further up)
    0x0D    1   Checksum of DOS file name
    0x0E    12  Name characters (six UCS-2 characters)
    0x1A    2   First cluster (always 0x0000)
    0x1C    4   Name characters (two UCS-2 characters)*/

    uint8_t seqNo;
    char buf1[10];
    const uint8_t attrs = 0x0f;
    zero type;
    uint8_t crc;
    char buf2[12];
    zero noClusterId[2];
    char buf3[4];

    static constexpr const size_t SLICE_SZ = sizeof( buf1 ) + sizeof( buf2 ) + sizeof( buf3 );
    static size_t scatterUcs2( std::string & temp, const Unicode & in );
    void copyIn( std::string & temp, size_t seq );
};

struct __attribute( ( packed ) ) VolumeDesc
{
    const uint8_t dumbjump[0x03] = { 0xeb, 0x58, 0x90 };
    Text<char, 8> oemName = "MSDOS5.0";
    LSB<uint16_t> secSz = Blocks::MAPPER_BS;
    uint8_t clusterScc = 0; // = page/sectSize
    LSB<uint16_t> reservedScc = 20;
    // 0x10
    uint8_t fatCount = 2; // keep 2 to avoid quirky legacy OS behavior
    zero $1[0x04]; // limits not relevant to FAT32
    const uint8_t mediaType = 0xf8; // hard disk
    zero $2[0x02]; // sectors per FAT12 - 16
    // 0x18
    zero $3[0x20 - 0x18];
    LSB<uint32_t> allScc; // clusters * clusterScc
    LSB<uint32_t> fatScc; // need to provide. see (*)
    zero $4[0x02]; // Drive description / mirroring flags
    const LSB<uint16_t> version = 0;
    LSB<uint32_t> rootCl; // first root directory cluster - known after dirs mapped
    // 0x30
    LSB<uint16_t> infoSec = 1; // FS Information Sector, typically 1
    const LSB<uint16_t> backupSec = 0; // no backup sector
    zero $5[0x0C]; // "formatted"
    // 0x40
    const uint8_t diskLetter = 0;
    const uint8_t scratchpad = 0;
    const uint8_t extendedSg = 0x29;
    Data<char, 4> volumeId;
    Text<char, 11> volName; // set to camera serial number?
    const Text<char, 8> kind = "FAT32";
    // 0x5a
    zero $a[0x1fe - 0x5a];
    // 0x1fe
    const LSB<uint16_t> signature = 0xaa55;
    // 0x200

    // (*) Wiki - "The byte at offset 0x026 in this entry should never become 0x28 or 0x29
    // in order to avoid any misinterpretation with the EBPB format under non-FAT32 aware operating systems."

inline blksize_t blockSize() const { auto val = secSz; return val * clusterScc; }
inline void setBlockSize( blksize_t blkSize ) { clusterScc = blkSize / secSz; }
};

struct __attribute( ( packed ) ) SummarySec
{
    const Text<char, 4> sig0 = "RRaA"; // FS information sector signature
    zero $0[480];
    const Text<char, 4> sig1 = "rrAa"; // FS information sector signature
    LSB<uint32_t> lknFreeClusters = 0;
    LSB<uint32_t> nextFreeCluster; // = set to the topmost cluster
    zero $1[0xC];
    const unsigned char sig2[4] = { 0, 0, 0x55, 0xaa};
};

#define PADWARN "FAT32 sector 0 fields properly padded"
static_assert( ( offsetof( VolumeDesc, fatCount ) == 0x10 ), PADWARN );
static_assert( ( offsetof( VolumeDesc, fatScc )   == 0x24 ), PADWARN );
static_assert( ( offsetof( VolumeDesc, infoSec )  == 0x30 ), PADWARN );
static_assert( ( offsetof( VolumeDesc, volumeId ) == 0x43 ), PADWARN );
static_assert( ( sizeof( VolumeDesc ) == 0x200 ), PADWARN );
static_assert( ( sizeof( SummarySec ) == 0x200 ), PADWARN );

// "least significant byte", growing
struct VFatMedium : public RuleMedium
{
    VFatMedium( bool sparse ) : _favor_freespace( sparse ) {}

    void reserve( blkcnt_t blockCount );
    /// straight chain
    void setLine( blkcnt_t curFirst, blkcnt_t curLast );
    /// first is exclusive
    void shadow( blkcnt_t curFirst );
    /// last is inclusive
    void setNext( blkcnt_t lastPrev, blkcnt_t firstNext );
    /// last is inclusive
    void setLast( blkcnt_t lastLast );

    inline size_t chunkSize() const override { return _chunk_size; }
    inline blksize_t blockSize() const override;
    void fill( void * chunk, off64_t offset, size_t size ) const override;

private:
    bool _favor_freespace;
    off64_t _total_length;
    blksize_t _chunk_size;
};

struct VFat32Out : public Volume
{
    static constexpr const off64_t CLUSTER_LINK_SIZE = sizeof( uint32_t );
    // Design_of_the_FAT_file_system#BIOS_Parameter_Block (cautious side)
    blksize_t sizeRange() const override { return 63 * Blocks::MAPPER_BS; }

    inline blksize_t blockSize() const override { return _vol.blockSize(); }
    inline void setBlockSize( blksize_t blkSize ) override { _vol.setBlockSize( blkSize ); }

    inline uint8_t fatCount() const { return _vol.fatCount; }
    inline void setFatCount( uint8_t count ) { _vol.fatCount = count; }

    void setLabels( const char * system, const char * volume ) override;

    Colonies plan( const Original & tree, Planner & outPlanner, Planner & tmpPlanner ) override;

private://impl
    blkcnt_t clusterCount( const Original & tree ) const;
    off64_t planHeaders( IAppend & planner );

private://data
    VolumeDesc _vol;
    SummarySec _sec;
};

}//namespace VF

#endif // VFAT32_H
