/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef MASTER_H
#define MASTER_H

#include "wrapper.h"

#include "impl/extent.h"
#include "impl/endian.h"
#include "impl/strenc.h"

/// Cylinder, Head, Sector geometry
struct __attribute( ( packed ) ) CHS
{
    unsigned char h;
    unsigned char s: 6;
    unsigned char hc: 2;
    unsigned char lc: 8;
    inline blkcnt_t vgeom() const { return ( ( ( ( ( hc << 8 ) + lc ) * 255 ) + h ) * 63 ) + s - 1; }
    inline CHS( unsigned int cyl, unsigned char head, unsigned char sect )
        : h( head ), s( sect ), hc( cyl >> 8 ), lc( cyl & 0xffu ) {}
    inline CHS( blkcnt_t lba )
        : CHS( lba / 63 / 255, ( lba / 63 ) % 255, ( lba % 63 ) + 1 ) {}
    static CHS empty;
    static CHS start;
    static CHS limit;
    static CHS fromLBA( blkcnt_t blkid );
};
static_assert( sizeof( CHS ) == 3, "CHS is 3 bytes" );

/// A single Master Boot Record partition
struct __attribute( ( packed ) ) FixedDisk
{
    char status = 0;
    CHS start = CHS::empty;
    char fs = 0;
    CHS end = CHS::empty;
    LSB<uint32_t> lbaStart = 0;
    LSB<uint32_t> lbaCount = 0;

    FixedDisk() = default; // inactive
    FixedDisk( char type, blkcnt_t offset );
    void setSectorCount( blkcnt_t size );
    void setByteEnd( off64_t size );
    void setEnd( blkcnt_t size, blksize_t blkSz );
};

/// The Master Boot Record partition table
struct __attribute( ( packed ) ) MBR
{
    zero $0[446];
    FixedDisk entry[4];
    const char sig1 = 0x55;
    const char sig2 = 0xaa;

    // 0xaf Apple HFS+
    // 0x96 ISO 9660
    // 0x01 FAT12 as primary
    inline void setType( char fs, blkcnt_t offset ) // make a per-entry setter
    {
        entry[0].status = 0x80;
        entry[0].fs = fs;
        entry[0].start = CHS( offset );
        entry[0].lbaStart = offset;
    }
    inline void setEnd( blkcnt_t blocks, blksize_t blkSize )
    {
        entry[0].setEnd( blocks, blkSize );
    }
};

static_assert( sizeof( FixedDisk ) == 16, "One master boot record entry is 16 bytes long." );
static_assert( sizeof( MBR ) == Blocks::MAPPER_BS, "The master boot record is one sector" );

#endif // MASTER_H
