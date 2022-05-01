/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "config.h"

#include <cstdlib>

void StdArgs::rerouteOut( const char * path )
{
    expectAttr( path, []( char * path ) { redirect( stdout, path ); } );
}

void StdArgs::rerouteErr( const char * path )
{
    expectAttr( path, []( char * path ) { redirect( stderr, path ); } );
}

const char * StdArgs::familiarize()
{
    uname( &whoami );
    return whoami.sysname;
}

StdArgs::StdArgs()
{
    rerouteOut( "out" );
    rerouteErr( "err" );
}

CtrlConf::CtrlConf()
{
    system = familiarize();
    expectAttr( "dm-control", dmControl );
    expectAttr( "dev-catalog", dev_cat );
    expectAttr( "num-catalog", num_cat );
}

void MkfsConf::exclude( char * pattern )
{
    // dont't tokenize. use multiple options for multiple patterns.
    ex.push_back( pattern );
}

void MkfsConf::mkfs( MkfsConf::FSType fs )
{
    if( hybridAllowed( fs ) ) { fsType |= fs; lastFs = fs; }
    else
    {
        printf( "Unsupported hybrid: %x+=%x\n", fsType, fs );
        abort();
    }
}

void MkfsConf::substitute( const char * found, const char * used )
{
    subst.push_back( { found, used } );
}

void MkfsConf::mapDevices( std::function<dev_t( const char * )> locate,
                           std::function<void( dev_t, dev_t )> put ) const
{
    for( auto & mapping : subst )
    {
        dev_t found = locate( mapping.first );
        dev_t used = locate( mapping.second );
        put( found, used );
    }
}

off64_t MkfsConf::tolerance()
{
    return extent_gap >= 0 ? extent_gap
           : isTargetCopied() ? 1L << 25 : 1L << 30;
}

void MkfsConf::setLanes( int laneCnt )
{
    if( laneCnt <= 0 )
    { printf( "Lane count must be a positive integer" ); abort(); }
    if( laneCnt & ( laneCnt - 1 ) )
    { printf( "Lane count must be a power of 2" ); abort(); }
    if( laneCnt > 4 )
    { printf( "%d lanes -- are you sure?", laneCnt ); }
    lanes = laneCnt;
}

MkfsConf::MkfsConf()
{
    // Output location
    expectAttr( "trg", target );

    // Metadata hosting
    expectAttr( "tmp", buffer );
    expectAttr( "zram-control", zrControl ); // use a temp file if undefined

    // Output format(s)
    fs_opt.expectFlag( "files", [this]() { mkfs( FS_Files ); } );
    fs_opt.expectFlag( "fat32", [this]() { mkfs( FS_Fat32 ); } );
    fs_opt.expectFlag( "cdfs",  [this]() { mkfs( FS_CDFS );  } );
    fs_opt.expectFlag( "hfsx",  [this]() { mkfs( FS_HFSX );  } );
    fs_opt.expectAttr( "label", [this]( const char * label )
    {
        labels[lastFs] = label;
    } );

    expectAttr( "mkfs", &fs_opt, &SubOpt::parse );

    // Surface substitutions
    ds_opt.onOther = [this]( const char * key, const char * value ) { substitute( key, value ); };
    expectAttr( "subst", &ds_opt, &SubOpt::parse );

    // Correlation
    expectFlag( "jam-inodes", inode_jam );

    // Reorganization (relevance: FAT)
    expectAtol( "gap", extent_gap );
    expectAtoi( "lanes", [this]( int lanes ) { setLanes( lanes ); } );
    expectFlag( "wipe-dust", star_dust );

    // Miscellaneous options
    expectFlag( "crawl", crawl_fds );
    expectFlag( "memfd", use_memfd );
    expectFlag( "daemonize", daemonize );
    expectFlag( "wait-term", daemonize );
    ok_opt.onOther = [this]( const char * key, const char * value )
    {
        setOnDone.push_back( Assignment( key, value ) );
    };
    expectAttr( "setprop", &ok_opt, &SubOpt::parse );

    // Extra entries are allowed as a comma-separated list (sysprop-friendly)
    expectAttr( "root", [this]( const char * root ) { entries.insert( entries.begin(), root ); } );
    in_opt.onOther = [this]( const char * key, const char * ) { entries.push_back( key ); };
    expectAttr( "include", &in_opt, &SubOpt::parse );
    expectArgs( [this]( int argc, char ** argv )
    {
        entries.insert( entries.end(), argv, argv + argc );
        if( !entries.size() ) { printf( "No input!" ); abort(); }
    } );
    expectAttr( "exclude", &ex_opt, &SubOpt::parse );
    ex_opt.onOther = [this]( const char * key, const char * ) { ex.push_back( key ); };
}

ForkConf::ForkConf()
{
    expectAttr( "unmount", unmount );
    expectAtol( "retries", retries );
    expectAtol( "zero-in", zoffset ); // purecd
    expectAttr( "src", forkSrc );
    expectAttr( "trg", forkTrg );
}

NameConf::NameConf()
{
    expectAttr( "tmp-catalog", tmp_cat ); // for mknod()
    expectAttr( "properties", setprop );
    expectAttr( "property", oneprop );
}

TempConf::TempConf()
{
    expectAttr( "trg", target );
    expectAtol( "size", size );
    expectAttr( "root", root );
    expectAttr( "label", vLabel );
}
