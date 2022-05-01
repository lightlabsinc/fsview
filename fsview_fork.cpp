/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "conf/config.h"

#include "impl/extent.h"
#include "impl/mapper.h"
#include "impl/attrib.h"
#include "impl/burner.h"

#include "allsys.h"

//void getTargetStatus( Mapper::dmwizbuf * dmw, int fd, size_t nSize ) // -> Mapper, elastic!
//{
//    dmw->data_start = sizeof( Mapper::dmwizbuf );
//    dmw->data_size = nSize;
//    dmw->dev = 0;
//    dmw->flags = DM_STATUS_TABLE_FLAG;// | DM_QUERY_INACTIVE_TABLE_FLAG; // status of table targets rather than device
//    if( ioctl( fd, DM_TABLE_STATUS, dmw ) < 0 ) { perror( "DM_TABLE_STATUS" ); exit( 2 ); }
//}

// fsview_fork --dm-control=/dev/device-mapper --num-catalog=/sys/dev/block --zero-in=32k --src virtualhd --trg virtualcd
int main( int argc, char ** argv )
{
    ForkConf cfg;
    cfg.parse( argc, argv );

    fprintf( stderr, "Forking a mapped device %s as %s\n", cfg.forkSrc, cfg.forkTrg );

    if( !cfg.dmControl ) { fprintf( stderr, "Need the --dm-control node\n" ); exit( 1 ); } // no longer possible
    if( !cfg.forkSrc ) { fprintf( stderr, "Need the --src device name\n" ); exit( 1 ); }
    if( !cfg.forkTrg ) { fprintf( stderr, "Need the --trg device name\n" ); exit( 1 ); }

    if( ( cfg.zoffset < 0 )
        || ( cfg.zoffset % Blocks::MAPPER_BS ) )
    {
        fprintf( stderr, "%ld is not a positive block size multiple.\n", cfg.zoffset );
    }

    if( cfg.zoffset )
    {
        fprintf( stderr, "Leading zeroes: %ld bytes (%lu standard sectors)\n",
                 cfg.zoffset, cfg.zoffset / Blocks::MAPPER_BS );
    }

    if( cfg.unmount )
    {
        int rt = cfg.retries;
        int umounted;
        while( ( umounted = umount2( cfg.unmount, 0/*MNT_FORCE*/ ) ) )
        {
            if( rt-- ) { perror( "umount" ); }
            else { exit( errno ); }
        }
    }

    Mapper mapper( cfg.dmControl, O_RDONLY );
    if( !mapper.isValid() ) { perror( cfg.dmControl ); exit( 1 ); }
    if( mapper.deviceStatus( cfg.forkSrc ) < 0 ) { perror( "DM_DEV_STATUS" ); exit( 1 ); }
    auto devId = mapper.dmw().dev;
    char srcDev[DM_NAME_LEN];
    snprintf( srcDev, sizeof( srcDev ), "%u:%u", major( devId ), minor( devId ) );

    // USE: expr `cat /sys/dev/block/253\:2/size` '*' 512 | if node provided; if not, summarize the table
    off64_t length = -1;

    const char * sysCatalog = cfg.num_cat; // WISDOM operator /
    if( sysCatalog )
    {
        int catFd = open( sysCatalog, O_RDONLY | O_DIRECTORY );
        if( catFd >= 0 )
        {
            int numFd = openat( catFd, srcDev, O_RDONLY | O_DIRECTORY );
            if( numFd >= 0 )
            {
                getAttrib( numFd, "size", [&length]( const char * value )
                {
                    length = strtol( value, nullptr, 0 ); // sectors!!! replace with off64_t
                } );
                close( numFd );
            }
            close( catFd );
        }
    }

    if( length == -1L )
    {
        // use devCatalog
        // BLKGETSIZE?
        // BLKGETSIZE64
    }

    if( length == -1L )
    {
        // use Mapper
    }

    if( length == -1L )
    {
        perror( srcDev );
        exit( 2 );
    }

    DiskBurner db( cfg.forkTrg, cfg.dmControl );
    if( cfg.zoffset ) { db.append( ZeroExtent( cfg.zoffset ) ); }
    db.append( Extent( cfg.zoffset, length * Blocks::MAPPER_BS - cfg.zoffset,
                       New<DiskMedium>( devId, 0 ) ) );
    db.commit();
    return 0;
}
