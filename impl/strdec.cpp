/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "strdec.h"

Unicode IDecoder::decode( const char * in ) // TODO remove, replace with parse() alone
{
    Unicode out;
    parse( out, in );
    return out;
}

int UTF8Homebrew::trailCount( char & c )
{
    return ( c & 0x80 ) ?
           ( c & 0x40 ) ?
           ( c & 0x20 ) ?
           ( c & 0x10 ) ?
           ( c & 0x08 ) ? FORBIDDEN
           : ( c &= 0x07, 3 ) // extended planes; use 3 bits
           : ( c &= 0x0f, 2 ) // basic multilingual; use 4 bits
           : ( c &= 0x1f, 1 ) // European alphabets; use 5 bits
           : ( c &= 0x3f, CONTINUED ) // use 6 bits
           : 0; // ANSI; leave the character as is
}

void UTF8Homebrew::parse( Unicode & out, const char * source )
{
    out.clear();
    int trailing = 0;
    wchar_t wc;
    char octet;
    while( ( octet = *source++ ) )
    {
        int leftover = trailCount( octet );
        if( leftover == CONTINUED )
        {
            if( trailing ) { wc |= octet << ( --trailing * 6 ); }
            else { wc = UnChar::UCS2; } // unexpected continuation
        }
        else
        {
            if( trailing ) { out.push_back( UnChar::UCS2 ); } // unexpected restart
            if( leftover == FORBIDDEN ) { wc = UnChar::UCS2; trailing = 0; }
            else { wc = octet << ( trailing = leftover ) * 6; }
        }
        if( !trailing ) { out.push_back( wc ); }
    }
    if( trailing ) { out.push_back( UnChar::UCS2 ); }
}

void ISO88591Flat::parse( Unicode & out, const char * source )
{
    out.clear();
    char octet;
    while( ( octet = *source++ ) ) { out.push_back( octet ); }
}

#ifdef USE_GNU_STDIO_FILEBUF
RawStreamBuf::RawStreamBuf()
    : fd( memfd_open( "conv", O_RDWR ) )
    , fb( fd, std::ios_base::in )
    , is( &fb ) {}

RawStreamBuf::~RawStreamBuf() { close( fd ); }

void RawStreamBuf::parse( Unicode & out, const char * source )
{
    auto len = strlen( source ) + 1;
    pwrite( fd, source, len, 0 );
    lseek( fd, 0, SEEK_SET );
    out.resize( len );
    is.clear();
    is.getline( &out[0], len );
    out.resize( wcslen( &out[0] ) );

    //auto size = is.gcount();
    //bool fail = !size || ( is.rdstate() & std::ios_base::failbit );
    //if( fail ) { perror( "Conversion" ); }
}
#endif

