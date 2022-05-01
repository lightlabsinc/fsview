/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef HFPLUS_H
#define HFPLUS_H

#include "wrapper.h"

#include <array>

#include "impl/endian.h"
#include "impl/strenc.h"
#include "impl/extent.h"
#include "impl/source.h"
#include "impl/volume.h"
#include "impl/master.h"

namespace HP
{
namespace PM
{
// All entries are in big-endian byte-order!

// 1 block for the Driver Descriptor Map as Block 0,
// 1 block for the partition table itself
// and 62 blocks for a maximum of 62 data partitions

//Address   Size in bytes   Contents    Required?
//Decimal   Hex
//0 0x0000  1   signature1 (ASCII value "P")    No
//1 0x0001  1   signature2 (ASCII value "M")    No
//2–3 0x0002  2   reserved    No
//4–7 0x0004  4   number of partitions (total)    Yes
//8–11    0x0008  4   starting sector of partition    Yes
//12–15   0x000C  4   size of partition (in sectors)  Yes
//16–47   0x0010  32  name of partition (fixed ASCII right-side NULL padded)  No
//48–79   0x0030  32  type of partition (fixed ASCII right-side NULL padded)  No
//80–83   0x0050  4   starting sector of data area in partition   No
//84–87   0x0054  4   size of data area in partition (in sectors) No
//88–91   0x0058  4   status of partition No
//92–95   0x005C  4   starting sector of boot code    No
//96–99   0x0060  4   size of boot code (in bytes)    No
//100–103 0x0064  4   address of bootloader code  No
//104–107 0x0068  4   reserved    No
//108–111 0x006C  4   boot code entry point   No
//112–115 0x0070  4   reserved    No
//116–119 0x0074  4   boot code checksum  No
//120–135 0x0078  16  processor type (fixed ASCII right-side NULL padded) No
//136–511 0x0088  376 reserved    No

// https://en.wikipedia.org/wiki/Apple_Partition_Map
// Apple_HFS
// Apple_Void
// Apple_Extra
// Apple_Free
// Apple_Partition_Map

//Description   System
//0x00000001    entry is valid  A/UX
//0x00000002    entry is allocated  A/UX
//0x00000004    entry in use    A/UX
//0x00000008    entry contains boot information A/UX
//0x00000010    partition is readable   A/UX
//0x00000020    partition is writable   A/UX, Macintosh
//0x00000040    boot code is position independent   A/UX
//0x00000100    partition contains chain-compatible driver  Macintosh
//0x00000200    partition contains a real driver    Macintosh
//0x00000400    partition contains a chain driver   Macintosh
//0x40000000    automatically mount at startup  Macintosh
//0x80000000    the startup partition   Macintosh
}//namespace PM

// Ref.: https://developer.apple.com/library/archive/technotes/tn/tn1150.html

// http://developer.apple.com/dev/cftype/ - the resource doesn't seem to be up,
// also actual Lumen builds use ???? instead of a Roman-character creator code.
// MOREINFO - is it possible to use "hfs+"?

// All multi-byte integer values are stored in big-endian format.

typedef MSB<uint16_t> UInt16;
typedef MSB<uint32_t> UInt32;
typedef MSB<uint64_t> UInt64;
typedef uint8_t UInt8;

typedef MSB<int16_t> SInt16;
typedef MSB<int32_t> SInt32;
typedef MSB<int64_t> SInt64;
typedef int8_t SInt8;

typedef Z<int16_t> ZInt16;
typedef Z<int32_t> ZInt32;
typedef Z<int64_t> ZInt64;

enum CatalogNodeID_Reserved
{
    kHFSRootParentID            = 1,
    kHFSRootFolderID            = 2,
    kHFSExtentsFileID           = 3,
    kHFSCatalogFileID           = 4,
    kHFSBadBlockFileID          = 5,
    kHFSAllocationFileID        = 6,
    kHFSStartupFileID           = 7,
    kHFSAttributesFileID        = 8,
    kHFSRepairCatalogFileID     = 14,
    kHFSBogusExtentFileID       = 15,
    kHFSFirstUserCatalogNodeID  = 16
};

// Note: CNID is HFSCatalogNodeID (akinda ino_t)
typedef UInt32 HFSCatalogNodeID;
typedef HFSCatalogNodeID CNID;

// The kHFSCatalogNodeIDsReusedBit in the attributes field of the volume header
// is set to indicate when CNID values have wrapped around and been reused.
// Impossible on our one-off drives.

// Note: the host may cache us, so same files MUST produce same inodes.
// The best bet seems to use original ino_t values. Compare:

/*
 * Special inodes numbers - see Linux includes fs/ext4/ext4.h
 */
//#define EXT4_BAD_INO         1  /* Bad blocks inode */
//#define EXT4_ROOT_INO      2  /* Root inode */
//#define EXT4_USR_QUOTA_INO     3  /* User quota inode */
//#define EXT4_GRP_QUOTA_INO     4  /* Group quota inode */
//#define EXT4_BOOT_LOADER_INO   5  /* Boot loader inode */
//#define EXT4_UNDEL_DIR_INO     6  /* Undelete directory inode */
//#define EXT4_RESIZE_INO        7  /* Reserved group descriptors inode */
//#define EXT4_JOURNAL_INO   8  /* Journal inode */
// 9    The "exclude" inode, for snapshots(?)
// 10   Replica inode, used for some non-upstream feature?
// 11   Traditional first non-reserved inode. Usually this is the lost+found directory. See s_first_ino in the superblock.

// ...the first regular file created on /data is, apparently, "bugreports"
// # stat -c '%i %n' /data/bugreports*                                  12 /data/bugreports

// Note that our "root" is not necessarily origin root.

struct __attribute( ( packed ) ) HFSPlusExtentDescriptor
{
    UInt32                  startBlock = 0;
    UInt32                  blockCount = 0;

void set( blkcnt_t start, blkcnt_t count ) { startBlock = start; blockCount = count; }
};

typedef std::array<HFSPlusExtentDescriptor, 8U> HFSPlusExtentRecord;
static_assert( sizeof( HFSPlusExtentRecord ) == sizeof( HFSPlusExtentDescriptor ) * 8U,
               "HFSPlusExtentRecord packed tightly" );

struct __attribute( ( packed ) ) HFSPlusForkData
{
    UInt64                  logicalSize;
    UInt32                  clumpSize;  // set to volume block size?
    UInt32                  totalBlocks;
    HFSPlusExtentRecord     extents;

    void setExtent( const Extent & singleExtent, blksize_t blkSz );
    void setReserved();
};

enum AdminFlags
{
    SF_ARCHIVED     = 1 << 0,   // File has been archived
    SF_IMMUTABLE    = 1 << 1,   // File may not be changed
    SF_APPEND       = 1 << 2,   // Writes to file may only append
};

enum OwnerFlags
{
    UF_NODUMP       = 1 << 0,  // Do not dump( back up or archive ) this file
    UF_IMMUTABLE    = 1 << 1, // File may not be changed
    UF_APPEND       = 1 << 2,  // Writes to file may only append
    UF_OPAQUE       = 1 << 3, // Directory is opaque( see below )
};

struct __attribute( ( packed ) ) HFSPlusBSDInfo
{
    const UInt32  ownerID = 99; // everyone
    const UInt32  groupID = 99; // unknown
    const UInt8   adminFlags = 0;
    const UInt8   ownerFlags = 0;
    UInt16  fileMode; // BSD file type and mode bits. flags match linux/stat.h
    union
    {
        UInt32  iNodeNum;
        UInt32  linkCount;
        UInt32  rawDevice;
    } special = {0};

    static constexpr const auto S_IRALL = S_IRUSR | S_IRGRP | S_IROTH;
    static constexpr const auto S_IWALL = S_IWUSR | S_IWGRP | S_IWOTH;
    static constexpr const auto S_IXALL = S_IXUSR | S_IXGRP | S_IXOTH;
HFSPlusBSDInfo( bool isDir ) : fileMode( S_IRALL | S_IWUSR | ( isDir ? S_IXALL | S_IFDIR : S_IFREG ) ) {}
};

enum CatalogRecordType
{
    kHFSFolderRecord            = 0x01,
    kHFSFileRecord              = 0x02,
    kHFSFolderThreadRecord      = 0x03,
    kHFSFileThreadRecord        = 0x04,
};

enum CatalogRecordFlags
{
    kHFSFileLockedBit       = 0x0000, // immutable; set?
    kHFSFileLockedMask      = 1 << kHFSFileLockedBit,
    kHFSThreadExistsBit     = 0x0001, // must be set
    kHFSThreadExistsMask    = 1 << kHFSThreadExistsBit,
    kHFSHasAttributesBit    = 0x0002,   /* object has extended attributes */
    kHFSHasSecurityBit      = 0x0003,   /* object has security data (ACLs) */
    kHFSHasFolderCountBit   = 0x0004,
    kHFSHasFolderCountMask  = 1 << kHFSHasFolderCountBit,
    kHFSHasLinkChainBit     = 0x0005,   /* has hardlink chain (inode or link) */
    kHFSHasChildLinkBit     = 0x0006,   /* folder has a child that's a dir link */
    kHFSHasDateAddedBit     = 0x0007,   /* File/Folder has the date-added stored in the finder info. */
    kHFSFastDevPinnedBit    = 0x0008,       /* this file has been pinned to the fast-device by the hot-file code on cooperative fusion */
    kHFSDoNotFastDevPinBit  = 0x0009,       /* this file can not be pinned to the fast-device */
    kHFSFastDevCandidateBit = 0x000a,      /* this item is a potential candidate for fast-dev pinning (as are any of its descendents */
    kHFSAutoCandidateBit    = 0x000b,      /* this item was automatically marked as a fast-dev candidate by the kernel */
};

// UniChar is a UInt16 that represents a character as defined in the Unicode character set
// defined by The Unicode Standard, Version 2.0 [Unicode, Inc. ISBN 0-201-48345-9].

// When using CreateTextEncoding to create a text encoding, you should set:
// TextEncodingBase     := kTextEncodingUnicodeV2_0;
// TextEncodingVariant  := kUnicodeCanonicalDecompVariant;
// TextEncodingFormat   := kUnicode16BitFormat;
// https://developer.apple.com/library/archive/technotes/tn/tn1150table.html

enum MacTextEncoding
{
    MacRoman        = 0,
    MacThai         = 21,
    MacJapanese     = 1,
    MacLaotian      = 22,
    MacChineseTrad  = 2,
    MacGeorgian     = 23,
    MacKorean       = 3,
    MacArmenian     = 24,
    MacArabic       = 4,
    MacChineseSimp  = 25,
    MacHebrew       = 5,
    MacTibetan      = 26,
    MacGreek        = 6,
    MacMongolian    = 27,
    MacCyrillic     = 7,
    MacEthiopic     = 28,
    MacDevanagari   = 9,
    MacCentralEurRoman = 29,
    MacGurmukhi     = 10,
    MacVietnamese   = 30,
    MacGujarati     = 11,
    MacExtArabic    = 31,
    MacOriya        = 12,
    MacSymbol       = 33,
    MacBengali      = 13,
    MacDingbats     = 34,
    MacTamil        = 14,
    MacTurkish      = 35,
    MacTelugu       = 15,
    MacCroatian     = 36,
    MacKannada      = 16,
    MacIcelandic    = 37,
    MacMalayalam    = 17,
    MacRomanian     = 38,
    MacSinhalese    = 18,
    MacFarsi        = 140, // (49)
    MacFarsiBit     = 49,
    MacBurmese      = 19,
    MacUkrainian    = 152, // (48)
    MacUkrainianBit = 48,
    MacKhmer        = 20,
};

typedef UInt16 UniChar;

struct __attribute( ( packed ) ) HFSUniStr255
{
    UInt16  length;
    UniChar unicode[0]; // up to 255
};

typedef std::vector<UniChar> MacString;
typedef size_t ItemCount;

// kill it - we are case sensitive
SInt32 FastUnicodeCompare( register const UniChar * str1, register ItemCount length1,
                           register const UniChar * str2, register ItemCount length2 );

// An HFSX volume may be either case-sensitive or case-insensitive. Case sensitivity (or lack
// thereof) is global to the volume; the setting applies to all file and directory names on the
// volume. To determine whether an HFSX volume is case-sensitive, use the keyCompareType field
// of the B-tree header of the catalog file. A value of kHFSBinaryCompare means the volume is
// case-sensitive. A value of kHFSCaseFolding means the volume is case-insensitive.

// Note: Do not assume that an HFSX volume is case-sensitive.
// Always use the keyCompareType to determine case-sensitivity or case-insensitivity.

// A case-insensitive HFSX volume (one whose keyCompareType is kHFSCaseFolding)
// uses the same Unicode string comparison algorithm as HFS Plus.

// For case-insensitive HFSX volumes and HFS Plus volumes,
// the nodeName must be compared in a case-insensitive way,
// as described in the Case-Insensitive String Comparison Algorithm section.

// Note: The null character (0x0000), as used in the name of the "HFS+ Private Data" directory used
// by hard links,  sort first with case-sensitive compares, but last with case-insensitive compares.

/* OSType is a 32-bit value made by packing four 1-byte characters
   together. */
typedef UInt32        FourCharCode;
typedef FourCharCode  OSType;

/* Finder flags (finderFlags, fdFlags and frFlags) */
enum FinderFlags
{
    kIsOnDesk       = 0x0001,     /* Files and folders (System 6) */
    kColor          = 0x000E,     /* Files and folders */
    kIsShared       = 0x0040,     /* Files only (Applications only) If */
    /* clear, the application needs */
    /* to write to its resource fork, */
    /* and therefore cannot be shared */
    /* on a server */
    kHasNoINITs     = 0x0080,     /* Files only (Extensions/Control */
    /* Panels only) */
    /* This file contains no INIT resource */
    kHasBeenInited  = 0x0100,     /* Files only.  Clear if the file */
    /* contains desktop database resources */
    /* ('BNDL', 'FREF', 'open', 'kind'...) */
    /* that have not been added yet.  Set */
    /* only by the Finder. */
    /* Reserved for folders */
    kHasCustomIcon  = 0x0400,     /* Files and folders */
    kIsStationery   = 0x0800,     /* Files only */
    kNameLocked     = 0x1000,     /* Files and folders */
    kHasBundle      = 0x2000,     /* Files only */
    kIsInvisible    = 0x4000,     /* Files and folders */
    kIsAlias        = 0x8000      /* Files only */
};

/* Extended flags (extendedFinderFlags, fdXFlags and frXFlags) */
enum ExtendedFlags
{
    kExtendedFlagsAreInvalid    = 0x8000, /* The other extended flags */
    /* should be ignored */
    kExtendedFlagHasCustomBadge = 0x0100, /* The file or folder has a */
    /* badge resource */
    kExtendedFlagHasRoutingInfo = 0x0004  /* The file contains routing */
                                  /* info resource */
};

// Finder info below. Note graphic types embedded in the filesystem metadata!

struct __attribute( ( packed ) ) Point
{
    SInt16              v;
    SInt16              h;
};

struct __attribute( ( packed ) ) Rect
{
    SInt16              top;
    SInt16              left;
    SInt16              bottom;
    SInt16              right;
};

struct __attribute( ( packed ) ) FileInfo   // leaf pass (MIME type???)
{
    // Rect same size as OSType[2]
    OSType    fileType;           /* The type of the file */
    OSType    fileCreator;        /* The file's creator */
    const UInt16    finderFlags = 0;
    const Point     location = {0, 0};           /* File's location in the folder. */
    ZInt16    reservedField;
};

struct __attribute( ( packed ) ) ExtendedFileInfo
{
    ZInt16    reserved1[4];
    const UInt16    extendedFinderFlags = 0;
    ZInt16    reserved2;
    const SInt32    putAwayFolderID = 0;
};

struct __attribute( ( packed ) ) FolderInfo
{
    // Rect same size as OSType[2]
    const Rect      windowBounds = {0, 0, 0, 0};       /* The position and dimension of the */
    /* folder's window */
    const UInt16    finderFlags = 0;
    const Point     location = {0, 0};           /* Folder's location in the parent */
    /* folder. If set to {0, 0}, the Finder */
    /* will place the item automatically */
    ZInt16    reservedField;
};

struct __attribute( ( packed ) ) ExtendedFolderInfo
{
    Point     scrollPosition = {0, 0};     /* Scroll position (for icon views) */
    ZInt32    reserved1;
    const UInt16    extendedFinderFlags = 0;
    ZInt16    reserved2;
    const SInt32    putAwayFolderID = 0;
};

// FileInfo and FolderInfo have the same size, so do Extended*
static_assert( ( sizeof( Rect ) == sizeof( OSType ) * 2 ), "FileInfo and FolderInfo comp sizes diverge" );
static_assert( ( sizeof( FileInfo ) == sizeof( FolderInfo ) ), "FileInfo and FolderInfo sizes diverge" );
static_assert( ( sizeof( ExtendedFileInfo ) == sizeof( ExtendedFolderInfo ) ), "sz(Extended*) diverge" );

typedef UInt16 NodeRecOff;
typedef UInt16 NodeKeyLen;

struct __attribute( ( packed ) ) HFSPlusCatalogKey
{
    // The length of the key varies with the length of the string stored in the nodeName field;
    // it occupies only the number of bytes required to hold the name.
    // The keyLength field determines the actual length of the key;
    // it varies between kHFSPlusCatalogKeyMinimumLength (6) to kHFSPlusCatalogKeyMaximumLength (516).

    NodeKeyLen          keyLength;
    HFSCatalogNodeID    parentID; // its own CNID for thread nodes
    HFSUniStr255        nodeName; // empty string for thread nodes

    bool operator==( const HFSPlusCatalogKey & other ) const;
    bool operator<( const HFSPlusCatalogKey & other ) const;
};

typedef UInt32 HFSDate; // number of seconds since midnight, January 1, 1904, GMT.

// https://www.epochconverter.com/mac
inline HFSDate DateFromTs( const timespec & ts ) { return 2082844800 + ts.tv_sec; }

struct __attribute( ( packed ) ) HFSPlusCatalogEntry
{
    const SInt16        recordType;
    UInt16              flags; // set by ctor
    UInt32              valence = 0; // The traditional Mac OS ... require folders to have a valence less than 32,767.
    HFSCatalogNodeID    nodeId; // stat - set from st_ino
    HFSDate             createDate; // stat
    HFSDate             contentModDate; // stat
    HFSDate             attributeModDate; // stat
    HFSDate             accessDate; // stat
    HFSDate             backupDate; // stat -- leave alone
    HFSPlusBSDInfo      permissions; // set by ctor

    void setTimes( const struct stat64 & st );

    HFSPlusCatalogEntry( bool isDir )
    : recordType( isDir ? kHFSFolderRecord : kHFSFileRecord )
    , flags( isDir ? 0 : kHFSThreadExistsMask )
    , permissions( isDir )
{}
};

struct __attribute( ( packed ) ) HFSPlusCatalogFolder : public HFSPlusCatalogEntry
{
    // File and Folder same until here
    FolderInfo          userInfo;
    ExtendedFolderInfo  finderInfo;
    UInt32              textEncoding = MacRoman;
    UInt32       /*sub*/folderCount = 0;

HFSPlusCatalogFolder( UInt16 entries = 0 ) : HFSPlusCatalogEntry( true ) { valence = entries; }
void setSubFolderCount( size_t entries ) { folderCount = entries; flags =  flags | kHFSHasFolderCountMask; }
};

struct __attribute( ( packed ) ) HFSPlusCatalogFile : public HFSPlusCatalogEntry
{
    // File and Folder same until here
    FileInfo            userInfo;
    ExtendedFileInfo    finderInfo;
    UInt32              textEncoding = MacRoman;
    ZInt32              reserved2;
    // Folder record ends here
    HFSPlusForkData     dataFork;       // leaf pass; populate ExtentOverflow
    HFSPlusForkData     resourceFork;   // leave alone

HFSPlusCatalogFile() : HFSPlusCatalogEntry( false ) {}
};

struct __attribute( ( packed ) ) HFSPlusCatalogThread // child of its respective entry
{
    const SInt16        recordType;
    ZInt16              reserved;
    HFSCatalogNodeID    parentID;
    HFSUniStr255        nodeName; // (duplicate)

HFSPlusCatalogThread( bool isDir ) : recordType( isDir ? kHFSFolderThreadRecord : kHFSFileThreadRecord ) {}
};

// extent overflow

enum ForkType
{
    DataForkType = 0,
    ResourceForkType = 0xff,
};

struct __attribute( ( packed ) ) HFSPlusExtentKey
{
    UInt16              keyLength;
    ForkType            forkType: 8;
    zero                pad;
    HFSCatalogNodeID    fileID;
    UInt32              startBlock;

    bool operator==( const HFSPlusExtentKey & other ) const;
    bool operator<( const HFSPlusExtentKey & other ) const;

HFSPlusExtentKey() : keyLength( sizeof( HFSPlusExtentKey ) - sizeof( keyLength ) ) {}
};

// https://developer.apple.com/library/archive/technotes/tn/tn1150.html#ExtentsOverflowFile

// Bad block extent records are always assumed to reference the data fork.
// The forkType field of the key must be 0.

// https://developer.apple.com/library/archive/technotes/tn/tn1150.html#AllocationFile

// Filled with 1, but: the allocation file may be larger than the minimum number of bits
// required for the given volume size. Any unused bits in the bitmap must be set to zero.

// B-trees

// For a practical description of the algorithms used to maintain a B-tree, see
// Algorithms in C, Robert Sedgewick, Addison-Wesley, 1992. ISBN: 0201514257.

// Many textbooks describe B-trees in which an index node contains N keys and N+1 pointers,
// and where keys less than key #X lie in the subtree pointed to by pointer #X,
// and keys greater than key #X lie in the subtree pointed to by pointer #X+1.
// (The B-tree implementor defines whether to use pointer #X or #X+1 for equal keys.)

// HFS and HFS Plus are slightly different; in a given subtree, there are no keys
// less than the first key of that subtree's root node. ("Forward tree")

// it's still important that the B-tree is in the data fork because the fork is
// part of the key used to store B-tree extents in the extents overflow file. - IRRELEVANT (each B-tree is 1 extent)

// B-trees are used for the catalog and the extent overflow file.
// kBTBigKeysMask - if set, keyLength field of the keys in index and leaf nodes is UInt16; otherwise, it is a UInt8.
// All HFS Plus B-trees use a UInt16 for their key length.
// kBTVariableIndexKeysMask - set for the HFS Plus Catalog B-tree, and cleared for the HFS Plus Extents B-tree.

// the data is always aligned on a two-byte boundary and occupies an even number of bytes.
// To meet the first alignment requirement, a pad byte must be inserted between the key and the data
// if the size of the keyLength field plus the size of the key is odd. To meet the second alignment requirement,
// a pad byte must be added after the data if the data size is odd.

// For thread records, the nodeName is the empty string.

enum BTreeAttr
{
    kBTBadCloseMask           = 0x00000001,
    kBTBigKeysMask            = 0x00000002,
    kBTVariableIndexKeysMask  = 0x00000004
};

enum BTreeTypes
{
    kHFSBTreeType           =   0,      // control file
    kUserBTreeType          = 128,      // user btree type starts from 128
    kReservedBTreeType      = 255
};

enum BTreeKCType
{
    KCUnused        = 0,
    KCCaseFolding   = 0xCF, //  Case folding (case-insensitive)
    KCBinaryCompare = 0xBC, //    Binary compare (case-sensitive)
};

/* Key and node lengths */
enum
{
    kHFSPlusExtentKeyMaximumLength = sizeof( HFSPlusExtentKey ) - sizeof( u_int16_t ),
    // kHFSExtentKeyMaximumLength  = sizeof( HFSExtentKey ) - sizeof( u_int8_t ), // not supported here
    kHFSPlusCatalogKeyMaximumLength = sizeof( HFSPlusCatalogKey ) - sizeof( u_int16_t ),
    kHFSPlusCatalogKeyMinimumLength = kHFSPlusCatalogKeyMaximumLength - sizeof( HFSUniStr255 ) + sizeof( u_int16_t ),
    // kHFSCatalogKeyMaximumLength = sizeof( HFSCatalogKey ) - sizeof( u_int8_t ),
    // kHFSCatalogKeyMinimumLength = kHFSCatalogKeyMaximumLength - ( kHFSMaxFileNameChars + 1 ) + sizeof( u_int8_t ),
    kHFSPlusCatalogMinNodeSize  = 4096,
    kHFSPlusExtentMinNodeSize   = 512,
    kHFSPlusAttrMinNodeSize     = 4096
};

struct __attribute( ( packed ) ) BTHeaderRec
{
    UInt16    treeDepth;        // root pass
    UInt32    rootNode;         // root pass
    UInt32    leafRecords;      // leaf pass
    UInt32    firstLeafNode;    // leaf pass
    UInt32    lastLeafNode;     // leaf pass
    UInt16    nodeSize; // power of two, from 512 through 32,768, inclusive
    NodeKeyLen maxKeyLength;
    UInt32    totalNodes;       // last (map) pass
    UInt32    freeNodes = 0;    // we are packed tightly
    ZInt16    reserved1;
    UInt32    clumpSize;      // misaligned
    BTreeTypes     btreeType: 8;
    BTreeKCType    keyCompareType: 8;
    UInt32    attributes;     // long aligned again
    ZInt32    reserved3[16];

    void tuneForCatalog();
    void tuneForOverflow();

BTHeaderRec() : btreeType( kHFSBTreeType ) {}
};

// 1. Each B-tree contains a single header node. The header node is
// always the first node in the B-tree. It contains the information
// needed to find other any other node in the tree.

// 2. Map nodes contain map records, which hold any allocation data
// (a bitmap that describes the free nodes in the B-tree) that
// overflows the map record in the header node.

// 3. Index nodes hold pointer records that determine the structure of the B-tree.

// 4. Leaf nodes hold data records that contain the data associated with a given key.
// The key for each data record must be unique.

// Each B-tree has a HFSPlusForkData structure in the volume header
// that describes the size and initial extents of that data fork.
// Special files do not have a resource fork.

enum BTNodeKind
{
    kBTLeafNode       = -1,
    kBTIndexNode      =  0,
    kBTHeaderNode     =  1,
    kBTMapNode        =  2
};

struct __attribute( ( packed ) ) BTNodeDescriptor
{
    UInt32    fLink = 0; // written in by chain() (if not the first one)
    UInt32    bLink = 0; // written in by chain() (if not the last one)
    BTNodeKind  kind: 8; // SInt8
    UInt8     height = 0; // written in for data nodes (index and leaf)
    UInt16    numRecords = -1; // updated by NodeSpec
    ZInt16    reserved;
};

// IMPORTANT: The list of record offsets always contains one more entry
// than there is records in the node. This entry contains the offset to
// the first byte of free space in the node, and thus indicates the size
// of the last record in the node. If there is no free space in the node,
// the entry contains its own byte offset from the start of the node.

// NOTE: In the HFS Plus hot file B-tree, this record (BTUserDataRec)
// contains general information about the hot file recording process.
typedef zero BTUserDataRec[128];

typedef UInt32 BTIndexPointer;

struct Record
{
    virtual size_t size() const = 0;
    virtual ExtentList asExtentList() const = 0;
    virtual ~Record() = default;
};

//struct StrRef
//{
//    const UniChar * name;
//    const size_t nameLen;

//    StrRef() : name( nullptr ), nameLen( 0 ) {}
//    StrRef( const UniString & str ) : name( str.data() ), nameLen( str.size() ) {}

//    size_t size() { return nameLen * sizeof( UniChar ); }
//    Extent asExtent() { return TempExtent( name, size() ); }

//    bool operator==( const StrRef & other )
//    {
//        size_t len = std::min( nameLen, other.nameLen );
//        for( size_t i = 0; i < len; ++i )
//        {
//            auto our = name[i], theirs = other.name[i];
//            if( our != theirs ) { return our < theirs; }
//        }
//        return len < other.nameLen; // "other is longer"
//    }

//    operator bool()() { return nameLen; }
//};

template<typename N> struct NamedRecord : public Record
{
    N data; // owned
    MacString name;

    explicit NamedRecord( const N & ref ) : data( ref ) {}
    NamedRecord( const N & ref, const MacString & decompo ) : NamedRecord( ref ) { setName( decompo ); }
    NamedRecord( const N & ref, const Unicode & decompo ) : NamedRecord( ref ) { setName( decompo ); }

    explicit NamedRecord( N && ref ) : data( std::move( ref ) ) {}
    NamedRecord( N && ref, const MacString & decompo ) : NamedRecord( ref ) { setName( decompo ); }
    NamedRecord( N && ref, const Unicode & decompo ) : NamedRecord( ref ) { setName( decompo ); }

    NamedRecord() {} // default values for all

    void setName( const MacString & macSt )
    {
        name = macSt;
        setNameLen();
    }

    void setName( const Unicode & decompo )
    {
        name.clear();
        name.reserve( decompo.size() );
        for( auto & wc : decompo )
        { name.push_back( wc ); }
        setNameLen();
    }

    size_t nameSize() const { return sizeof( UniChar ) * name.size(); }
    size_t size() const override { return sizeof( N ) + nameSize(); }

    bool operator==( const NamedRecord & other ) const
    { return ( data == other.data ) && ( name == other.name ); }
    bool operator<( const NamedRecord & other ) const
    { return ( data < other.data ) || ( ( data == other.data ) && ( name < other.name ) ); }

    ExtentList asExtentList() const override
    {
        return { TempExtent( data ),
                 TempExtent( name.data(), nameSize() ) };
    }

private:
    void setNameLen()
    {
        HFSUniStr255 & nn = data.nodeName;
        UInt16 & length = nn.length;
        length = name.size();
    }
};

struct NodeSpec
{
    size_t offset = 0;
    BTNodeDescriptor desc;
    std::list<Extent> recs;
    std::vector<UInt16> offsets;

    inline NodeSpec() { addRecord( TempExtent( desc ) ); markRecord(); }
    NodeSpec( BTNodeKind kind );
    NodeSpec( BTNodeKind kind, uint8_t level );

    inline size_t offSize() const { return sizeof( NodeRecOff ) * offsets.size(); }
    inline size_t size() const { return offset + offSize(); }
    size_t freeSpace( blksize_t capacity, bool gross = false ) const;
    bool fitsIn( blksize_t capacity, size_t recordSize ) const;
    inline size_t count() const { return desc.numRecords; }

    void addRecord( const Extent & one );
    void addRecord( const ExtentList & xl );
    void addRecord( const Record & record );
    void addRecord( Ptr<Record> record );
    template<typename K, typename V>
    void addRecord( const std::pair<K, V> & mapping )
    {
        addRecord( mapping.first );
        addRecord( mapping.second );
    }
    // whitelist types directly to avoid template specialization ambiguity. bad!
    void addRecord( const HFSPlusExtentKey & t ) { addRecord( TempExtent( t ) ); }
    void addRecord( const HFSPlusCatalogKey & t ) { addRecord( TempExtent( t ) ); }
    void addRecord( const HFSPlusExtentRecord & t ) { addRecord( TempExtent( t ) ); }
    void addRecord( const UInt32 & pointer ) { addRecord( TempExtent( pointer ) ); }
    void markRecord();

    void writeTo( IAppend & out, blksize_t capacity ) const;
};

// BTMapRec
// The remaining space in the header node is occupied by a third record, the map record.
// It is a bitmap that indicates which nodes in the B-tree are used and which are free.
// The bits are interpreted in the same way as the bits in the allocation file.

// All tolled, the node descriptor, header record, reserved record, and record offsets
// occupy 256 bytes of the header node. So the size of the map record (in bytes) is (!)
// nodeSize minus 256. If there are more nodes in the B-tree than can be represented by
// the map record in the header node, map nodes are used to store additional allocation data.

// In an HFS Plus B-tree, the keys in an index node are allowed to vary in size!

// HFS Plus uses the following default node sizes:
// 4 KB (8KB in Mac OS X) for the catalog file
// 1 KB (4KB in Mac OS X) for the extents overflow file
// 4 KB for the attributes file

// The node size of the catalog file must be at least kHFSPlusCatalogMinNodeSize (4096).
// The node size of the attributes file must be at least kHFSPlusAttrMinNodeSize (4096).

// It is possible for a volume to have no attributes file. If the first extent of
// the attributes file (stored in the volume header) has zero allocation blocks,
// the attributes file does not exist.

// In a nutshell, the attributes file should not be used. Neither should hard/sym links.

enum FileTypes
{
    kHardLinkFileType = 0x686C6E6B,  /* 'hlnk' */
    kHFSPlusCreator   = 0x6866732B,  /* 'hfs+' */
    kSymLinkFileType  = 0x736C6E6B, /* 'slnk' */
    kSymLinkCreator   = 0x72686170  /* 'rhap' */
};

struct __attribute( ( packed ) ) HFSPlusAttrForkData
{
    UInt32          recordType;
    ZInt32          reserved;
    HFSPlusForkData theFork;
};

struct __attribute( ( packed ) ) HFSPlusAttrExtents
{
    UInt32                  recordType;
    ZInt32                  reserved;
    HFSPlusExtentRecord     extents;
};

// Journal crc32

struct __attribute( ( packed ) ) JournalInfoBlock
{
    UInt32    flags;
    UInt32    device_signature[8];
    UInt64    offset;
    UInt64    size;
    ZInt32    reserved[32];
};

enum
{
    kJIJournalInFSMask          = 0x00000001,
    kJIJournalOnOtherDeviceMask = 0x00000002,
    kJIJournalNeedInitMask      = 0x00000004
};

struct __attribute( ( packed ) ) journal_header
{
    UInt32    magic;
    UInt32    endian;
    UInt64    start;
    UInt64    end;
    UInt64    size;
    UInt32    blhdr_size;
    UInt32    checksum;
    UInt32    jhdr_size;
};

#define JOURNAL_HEADER_MAGIC  0x4a4e4c78
#define ENDIAN_MAGIC          0x12345678

struct __attribute( ( packed ) ) block_info
{
    UInt64    bnum;
    UInt32    bsize;
    UInt32    next;
};

struct __attribute( ( packed ) ) block_list_header
{
    UInt16    max_blocks;
    UInt16    num_blocks;
    UInt32    bytes_used;
    UInt32    checksum;
    UInt32    pad;
    block_info  binfo[1];
};

static int
calc_checksum( unsigned char * ptr, int len );

//Item  Contribution to the Metadata Zone size
//Allocation Bitmap File    Physical size (totalBlocks times the volume's allocation block size) of the allocation bitmap file.
//Extents Overflow File 4MB, plus 4MB per 100GB (up to 128MB maximum)
//Journal File  8MB, plus 8MB per 100GB (up to 512MB maximum)
//Catalog File  10 bytes per KB (1GB minimum)
//Hot Files 5 bytes per KB (10MB minimum; 512MB maximum)
//Quota Users File  ...
//Quota Groups File ...

// The hot file B-tree is an ordinary file on the volume (that is, it has records in the catalog).
// It is a file named ".hotfiles.btree" in the root directory. - DON"T CREATE

#define HFC_MAGIC   0xFF28FF26
#define HFC_VERSION 1
#define HFC_DEFAULT_DURATION     (3600 * 60)
#define HFC_MINIMUM_TEMPERATURE  16
#define HFC_MAXIMUM_FILESIZE     (10 * 1024 * 1024)

#define HFC_LOOKUPTAG   0xFFFFFFFF
#define HFC_KEYLENGTH   (sizeof(HotFileKey) - sizeof(UInt32))

struct __attribute( ( packed ) ) HotFilesInfo
{
    UInt32  magic;
    UInt32  version;
    UInt32  duration;    /* duration of sample period */
    UInt32  timebase;    /* recording period start time */
    UInt32  timeleft;    /* recording period stop time */
    UInt32  threshold;
    UInt32  maxfileblks;
    UInt32  maxfilecnt;
    UInt8   tag[32];
};

struct __attribute( ( packed ) ) HotFileKey
{
    NodeKeyLen   keyLength;
    UInt8    forkType;
    UInt8    pad;
    UInt32   temperature;
    UInt32   fileID;
};

// volume header

enum VolumeFlags
{
    /* Bits 0-6 are reserved */
    kHFSVolumeHardwareLockBit       =  7,
    kHFSVolumeUnmountedBit          =  8,
    kHFSVolumeSparedBlocksBit       =  9,
    kHFSVolumeNoCacheRequiredBit    = 10,
    kHFSBootVolumeInconsistentBit   = 11,
    kHFSCatalogNodeIDsReusedBit     = 12,
    kHFSVolumeJournaledBit          = 13,
    /* Bit 14 is reserved */
    kHFSVolumeSoftwareLockBit       = 15
                                      /* Bits 16-31 are reserved */
};

struct __attribute( ( packed ) ) HFSPlusVolumeHeader
{
    const char          signature[2] = { 'H', 'X' }; // 0x4858
    const UInt16        version = 5;
    // kHFSVolumeJournaledBit => the volume has a journal. - turn off
    // kHFSVolumeSparedBlocksBit => set if "holes" are marked as bad.
    // kHFSCatalogNodeIDsReusedBit => set if CNIDs are noncontiguous.
    UInt32              attributes = kHFSVolumeUnmountedBit | kHFSCatalogNodeIDsReusedBit;
    const char          lastMountedVersion[4] = { '1', '0', '.', '0' }; // define FourCC as union!!!
    const UInt32        journalInfoBlock = 0;

    HFSDate             createDate; // stat (root)
    HFSDate             modifyDate; // current
    HFSDate             backupDate; // 0
    HFSDate             checkedDate;// 0

    UInt32              fileCount;
    UInt32              folderCount;

    UInt32              blockSize;
    UInt32              totalBlocks;
    UInt32              freeBlocks = 0;

    UInt32              nextAllocation; // may be 0
    UInt32              rsrcClumpSize; // -"-
    UInt32              dataClumpSize; // blockSize
    HFSCatalogNodeID    nextCatalogID = 0; // any unused node ID

    UInt32              writeCount; // set to hash code of the data e.g. sizes and dates
    UInt64              encodingsBitmap = 1 << MacRoman;

    UInt32              finderInfo[8];

    HFSPlusForkData     allocationFile;
    HFSPlusForkData     extentsFile;
    HFSPlusForkData     catalogFile;
    HFSPlusForkData     attributesFile;
    HFSPlusForkData     startupFile;

void setBlockSize( blksize_t blkSz ) { rsrcClumpSize = dataClumpSize = blockSize = blkSz; }
// void setDate() {}
};

// do the same to AsExtentList

inline size_t RecordSize( const Record & t ) { return t.size(); }
inline size_t RecordSize( Ptr<Record> pt ) { return pt->size(); }

inline size_t RecordSize( const HFSPlusExtentKey & t ) { return sizeof( t ); }
inline size_t RecordSize( const HFSPlusCatalogKey & t ) { return sizeof( t ); }
inline size_t RecordSize( const HFSPlusExtentRecord & t ) { return sizeof( t ); }

template<typename I, bool L>
size_t RecordSize( const SB<I, L> & t ) { return sizeof( t ); }

template<typename K, typename V>
size_t RecordSize( const std::pair<K, V> & mapping )
{
    return RecordSize( mapping.first ) +
           RecordSize( mapping.second );
}

template<typename K>
struct TreeBuilder
{
    Ptr<NodeSpec> headerRec = New<NodeSpec>( BTNodeKind::kBTHeaderNode );
    BTHeaderRec header;
    BTUserDataRec user;
    List<NodeSpec> nodeList;

    inline size_t nodeCount() const { return nodeList.size(); }

    typedef std::map<K, BTIndexPointer> IndexMap;
    std::list<IndexMap> ndxHistory;

    TreeBuilder()
    {
        headerRec->addRecord( TempExtent( header ) );
        headerRec->markRecord();
        headerRec->addRecord( TempExtent( user ) );
        headerRec->markRecord();
        nodeList.push_back( headerRec );
    }

    template<typename V>
    void compactLevel( IndexMap & indices, const std::map<K, V> & dataMap, BTNodeKind kind, size_t level )
    {
        Ptr<NodeSpec> next = New<NodeSpec>( kind, level );
        for( auto & dataPair : dataMap )
        {
            auto recLength = RecordSize( dataPair );
            if( !next->fitsIn( header.nodeSize, recLength ) ) // done when nodes are full
            {
                auto pastIndex = nodeCount();
                nodeList.push_back( next );
                Ptr<NodeSpec> prev = nodeList.back();
                next = New<NodeSpec>( kind, level );
                prev->desc.fLink = pastIndex + 1;
                next->desc.bLink = pastIndex;
            }
            if( !next->count() ) // expose key
            {
                indices[dataPair.first] = nodeCount();
            }
            auto oldOff = next->offset;
            next->addRecord( dataPair );
            if( next->offset != oldOff + recLength )
            {
                printf( "next offset %lx != old offset %lx + recLen %lx\n",
                        next->offset, oldOff, recLength );
                abort();
            }
            next->markRecord();
        }
        // when complete
        nodeList.push_back( next );
    }

    template<typename V>
    void compactBTree( const std::map<K, V> & dataMap )
    {
        IndexMap indices;

        if( dataMap.size() )
        {
            size_t level = 1; // leaf
            // compact catalog into indices:
            header.firstLeafNode = nodeCount();
            compactLevel( indices, dataMap, BTNodeKind::kBTLeafNode, level );

            // populate header after leaf pass
            header.lastLeafNode = nodeCount() - 1;
            header.leafRecords = dataMap.size();

            while( indices.size() > 1 )
            {
                ndxHistory.push_back( IndexMap() );
                IndexMap & oldIndices = ndxHistory.back();
                std::swap( indices, oldIndices );

                // compact oldIndices into indices:
                compactLevel( indices, oldIndices, BTNodeKind::kBTIndexNode, ++level );
            }

            // populate header after tree pass
            header.rootNode = nodeCount() - 1;
            header.treeDepth = level;
        }
        else
        {
            header.rootNode =
                header.treeDepth =
                    header.firstLeafNode =
                        header.lastLeafNode =
                            header.leafRecords = 0;
        }

        // complete header after bmap pass
        // NodeRecOff
        auto fs = headerRec->freeSpace( 256 ); // "gross" free space
        if( fs != 0 ) { printf( "Corrupt header record: %lx (-%lx)\n", fs, -fs ); abort(); }

        Ptr<BitsMedium> fillMed = New<BitsMedium>( false );
        auto mapNode = headerRec;
        ssize_t done = 0;

        while( true )
        {
            fillMed->reserveBits( nodeCount() );
            size_t mset = mapNode->freeSpace( header.nodeSize );
            mapNode->addRecord( Extent( done, mset, fillMed ) );
            mapNode->markRecord();
            done += mset;
            if( done < fillMed->byteCount() )
            {
                size_t lastNoNo = ( mapNode == headerRec ) ? 0 : nodeCount() - 1;
                mapNode->desc.fLink = nodeCount();
                nodeList.push_back( mapNode = New<NodeSpec>( BTNodeKind::kBTMapNode, 0 ) );
                mapNode->desc.bLink = lastNoNo;

            }
            else { break; }
        }

        header.totalNodes = nodeList.size();
    }

    off64_t writeTo( IAppend & out )
    {
        off64_t cur = out.offset();
        for( Ptr<NodeSpec> & pNode : nodeList ) { pNode->writeTo( out, header.nodeSize ); }
        return cur;
    }

    Extent wrapToGo( Planner & outPlanner, Planner & tmpPlanner )
    {
        Extent tmpExtent = tmpPlanner.wrapToGo( writeTo( tmpPlanner ) );
        printf( "Temporary extent: %lx+%lx\n", tmpExtent.offset, tmpExtent.length );
        Extent outExtent = outPlanner.wrapToGo( outPlanner.append( tmpExtent ) );
        printf( "Permanent extent: %lx+%lx\n", outExtent.offset, outExtent.length );
        return outExtent;
    }
};

struct HFSPlusVolumeBuilder
{
    std::function<CNID( Entry * )> renum;
    Decompo decompo;
    blksize_t _blk_sz;

    typedef NamedRecord<HFSPlusCatalogKey> NamedCatalogKey;
    std::map<NamedCatalogKey, Ptr<Record>> catalog; // keeps shared pointers to records
    std::map<HFSPlusExtentKey, HFSPlusExtentRecord> overflow; // directly comparable, no indirection

    TreeBuilder<NamedCatalogKey> catalogTree;
    TreeBuilder<HFSPlusExtentKey> extentTree;

    HFSPlusVolumeBuilder();
    void setBlockSize( blksize_t blkSz );
    void onEntry( Entry * entry, Ptr<Record> dirEntRec, HFSPlusCatalogEntry & dirEnt, CNID nodeId );
    void onEntry( Entry * entry, Ptr<Record> dirEntRec, HFSPlusCatalogEntry & dirEnt );
    HFSPlusExtentRecord * onOverflow( CNID fileId, blkcnt_t blk );
    void compactTrees();
};

struct HFPlusOut : public Volume, public Hybrid
{
    // allow 4k (or optionally 8k to store whole nodes in a block)
    blksize_t sizeRange() const override { return 3 * sysconf(_SC_PAGESIZE); }
    blksize_t blockSize() const override { return _vol.blockSize; }
    void setBlockSize( blksize_t blkSz ) override;
    void setLabels( const char * system, const char * volume ) override;

protected:
    Colonies plan( const Original & tree, Planner & outPlanner, Planner & tmpPlanner ) override;

private: // reusable dependencies - heavyweight at construction; should inject
    EuroDeco ceur;
    HangDeco hang;

private: // impl
    off64_t planHeaders( const Original & tree, Planner & outPlanner, Planner & tmpPlanner ) const;

private: // data
    std::string _vol_label;
    MBR _mbr;
    HFSPlusVolumeHeader _vol;
    HFSPlusVolumeBuilder _vb; // inline?

public:
    blksize_t blkSzHint( const Original & /*tree*/,
                         const Medium & /*outImage*/, const Medium & /*tmpImage*/ ) override { return 0; }
    void masterAdjusted( const Original & tree,
                         const Medium & outImage, const Medium & tmpImage,
                         blksize_t blkSz ) override;
    void masterReserved( const Original & tree,
                         Planner & outPlanner, Planner & tmpPlanner,
                         off64_t cap ) override;
    void masterComplete( const Original & tree,
                         Planner & outPlanner, Planner & tmpPlanner,
                         const Colonies & srcToTrg ) override;
};

}//namespace HP

#endif // HFPLUS_H
