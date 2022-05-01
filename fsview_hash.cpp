/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "impl/unique.h"

#include "allsys.h"

int main( int argc, char ** argv )
{
    bool out = argc == 2;
    bool set = argc == 3;
    if( !out && !set ) { return EINVAL; }
    const char * val = argv[argc - 1];
    auto crc = Crc32( val ); crc &= INT_MAX;

    union
    {
        char buf[8];
        uint64_t dw;
    };
    dw = 0;
    size_t i = 0;
    for( ; i < 6; ++i )
    {
        char c = crc % 36;
        c += ( c < 10 ) ? '0' : ( 'A' - 10 );
        buf[i] = c;
        crc /= 36;
    }

    if( set )
    { return __system_property_set( argv[1], buf ); }
    else { return printf( "%s\n", buf ) < 0; } // out
}
