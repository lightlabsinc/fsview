/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "conf/config.h"

#include "allsys.h"

int main( int argc, char ** argv )
{
    CtrlConf cfg;
    cfg.parse( argc, argv );

    int control_fd = open( cfg.dmControl, O_RDWR );
    if( control_fd < 0 )
    {
        perror( cfg.dmControl );
        exit( 2 );
    }

    struct dm_ioctl header;
    for( int i = 0; i < argc; ++i )
    {
        memset( &header, 0, sizeof( header ) );
        header.version[0] = DM_VERSION_MAJOR;
        header.version[1] = 0; // MINOR too high
        header.version[2] = 0; // omit PATCHLEVEL
        strncpy( header.name, argv[i], DM_NAME_LEN );

        // if the device exists, tear it down:
        // - allow pending I/O complete;
        header.data_start = 0;
        header.data_size = sizeof( header );
        header.flags = 0;
        header.dev = 0;
        if( ioctl( control_fd, DM_DEV_SUSPEND, &header ) < 0 )
        { perror( "Can't flush i/o on device" ); }

        // - suspend the device mapping;
        header.data_start = 0;
        header.data_size = sizeof( header );
        header.flags = DM_SUSPEND_FLAG;
        header.dev = 0;
        if( ioctl( control_fd, DM_DEV_SUSPEND, &header ) < 0 )
        { perror( "Can't suspend device" ); }

        // - destroy the device mapping.
        header.dev = 0;
        if( ioctl( control_fd, DM_DEV_REMOVE, &header ) < 0 )
        { perror( "Can't destroy device" ); }
    }
    close( control_fd );
    return 0;
}
