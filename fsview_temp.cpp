/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved.
 * Licensed under the MIT license.
 */

#include "conf/config.h"

#include "impl/unique.h"
#include "impl/vfat32.h"

blksize_t bestBlkSize( off64_t size )
{
    // 512-byte sectors are misunderstood by
    // Mac OS X; she thinks they are FAT16.
    // So let's instead start with 1K:
    // if( size < ( 64L << 20 ) )   // 64m
    // { return 512; }
    if( size < ( 128L << 20 ) )    // 128m
    { return 1024; }
    if( size < ( 256L << 20 ) )    // 256m
    { return 2048; }
    else if( size < ( 1L << 30 ) )  // 1G
    { return 4096; }
    else if( size < ( 1L << 34 ) ) // 16G
    { return 8192; }
    else
    { return 16384; }
}

// fsview_temp --size=30m [--sparse] --label=LSD606DBD75 --trg=/sdcard/temp.img
// fsview_temp --size=30m [--sparse] --label=LSD606DBD75 --trg=/mnt/temp.img
int main( int argc, char ** argv )
{
    TempConf cfg;
    cfg.parse( argc, argv );

    // create a writable FAT32 partition of desired size
    Ptr<Burner> outImage = New<FileBurner>( cfg.target );
    Ptr<Burner> tmpImage = New<TempBurner>();

    Original tree;
    tree.decoder = New<UTF8Homebrew>();

    if( cfg.root )
    { tree.openRoot( cfg.root, true ); }
    else { tree.fakeRoot(); }

    VF::VFat32Out fat;
    fat.setBlockSize( bestBlkSize( cfg.size ) );
    fat.setTitles( cfg.system, cfg.vLabel );
    fat.bookSpace( true, false, cfg.size );
    fat.represent( tree, outImage, tmpImage );
    return 0;
}
