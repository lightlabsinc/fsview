/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "cmdarg.h"

#include <cstdlib>
#include <algorithm>

namespace
{

/// Parser of the decimal suffix ("giga", "mega", "kilo")
long multiplier( const char * str )
{
    char c = *str | 0x20;
    switch( c )
    {
        case 'g': return 1L << 30;
        case 'm': return 1L << 20;
        case 'k': return 1L << 10;
        default: return 1;
    }
}

}//private

void ConfCb::raiseMissing( const char * name )
{
    fprintf( stderr, "Required suboption: %s\n", name );
    abort();
}

void ConfCb::raiseMissing( std::map<StrKey, bool> & mustHave )
{
    for( auto & opt : mustHave )
    {
        if( opt.second )
        {
            raiseMissing( opt.first );
        }
    }
}

void ConfCb::expectFlag( const char * name, bool & lvFlag )
{
    expectFlag( name, [&lvFlag]() { lvFlag = true; } );
}

void ConfCb::expectFlag( const char * name, OnFlag onFlag )
{
    expectImpl( name, [onFlag]( char * ) { onFlag(); }, no_argument );
}

void ConfCb::expectAtoi( const char * name, int & lIntVal )
{
    expectAtoi( name, [&lIntVal]( int number ) { lIntVal = number; } );
}

void ConfCb::expectAtoi( const char * name, OnAtoi onAtoi )
{
    expectAttr( name, [onAtoi]( char * value ) { onAtoi( atoi( value ) ); } );
}

void ConfCb::expectAtol( const char * name, long & lIntVal )
{
    expectAtol( name, [&lIntVal]( int number ) { lIntVal = number; } );
}

void ConfCb::expectAtol( const char * name, OnAtol onAtol )
{
    expectAttr( name, [onAtol]( char * value )
    {
        char * endPtr = nullptr;
        onAtol( strtol( value, &endPtr, 0 )
                * multiplier( endPtr ) );
    } );
}

void ConfCb::expectAttr( const char * name, const char *& lAttr )
{
    expectAttr( name, [&lAttr]( char * value ) { lAttr = value; } );
}

void ConfCb::expectAttr( const char * name, OnAttr onAttr )
{
    expectImpl( name, onAttr, required_argument );
}

void CmdArgs::expectImpl( const char * name, OnAttr onAv, int validation )
{
    options.push_back( {name, validation, nullptr, nextIndex() } );
    callbacks.push_back( onAv );
}

void CmdArgs::parse( int & argc, char ** & argv )
{
    // close the argument list
    options.push_back( {0, 0, 0, 0} );
    opterr = 0; // suppress errors
    int topic, value;

    // https://linux.die.net/man/3/getopt_long
    while( ( topic = -1,
             value = getopt_long_only( argc, argv,
                                       "", options.data(),
                                       &topic ) ) >= 0 )
    { if( topic >= 0 ) { callbacks.at( topic )( optarg ); } }

    // process anonymous arguments
    main( argc -= optind, argv += optind );
}

void SubOpt::expectImpl( const char * name, OnAttr onAttr, int validation )
{
    bool required = ( required_argument == validation );
    callbacks[name] = onAttr;
    minimal[name] = required;
}

void SubOpt::expectName( const char *& name )
{
    onOther = [&name]( const char * attr, const char * )
    {
        name = attr;
    };
}

void SubOpt::expectName( const char *& name, const char *& value )
{
    onOther = [&name, &value]( const char * attr, const char * val )
    {
        name = attr; value = val;
    };
}

void SubOpt::parse( char * options )
{
    const char * delim = "= \t,";
    const char * termin = delim + 1;
    auto mustHave = minimal;
    while( options && *options )
    {
        skipAnyOf( options, delim );
        if( !*options ) { return; }
        char * token = options;
        seekUntil( options, delim );
        char * optvalue = nullptr;
        if( *options )
        {
            if( *delim == *options ) // avpair
            {
                *options++ = '\0';
                optvalue = options;
                seekUntil( options, termin );
            }
            if( *options )
            {
                *options++ = '\0';
            }
        }
        auto itr = callbacks.find( token );
        if( itr == callbacks.end() )
        { onOther( token, optvalue ); }
        else
        {
            mustHave[token] = false;
            itr->second( optvalue );
        }
    }
    raiseMissing( mustHave );
}

void SubOpt::skipAnyOf( char *& input, const char * expr )
{
    while( *input && strchr( expr, *input ) ) { input++; }
}

void SubOpt::seekUntil( char *& input, const char * expr )
{
    while( *input && !strchr( expr, *input ) ) { input++; }
}

