/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "master.h"

CHS CHS::empty( 0, 0, 0 );
CHS CHS::start( 0, 0, 1 );
CHS CHS::limit( 1023, 254, 63 );

CHS CHS::fromLBA( blkcnt_t blkid ) { return blkid > limit.vgeom() ? limit : CHS( blkid ); }

FixedDisk::FixedDisk( char type, blkcnt_t offset )
    : status( 0x80 )
    , start( offset )
    , fs( type )
    , lbaStart( offset ) {}

void FixedDisk::setSectorCount( blkcnt_t size )
{
    lbaCount = size;
    end = CHS::fromLBA( lbaStart + lbaCount ); // FIXME use setters and ctors
}

void FixedDisk::setByteEnd( off64_t size ) { setSectorCount( ( size / Blocks::MAPPER_BS ) - lbaStart ); }

void FixedDisk::setEnd( blkcnt_t blocks, blksize_t blkSz ) { setByteEnd( blocks * blkSz ); }
