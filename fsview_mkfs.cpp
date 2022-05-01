/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

 /* The architecture of the solution is to be re-documented.
 * Outstanding issues that MAY affect the class chart:
 * - "laning" (larger clusters on FAT32 drives achieved by shifting file
 *  extents by a sub-cluster offset);
 * Notes:
 * - objects are allocated on the heap and managed with shared pointers.
 * - there is a thin abstraction layer over STL to keep the code simple.
 * - POSIX and Linux-specific APIs are used in concrete implementations.
 * - occasional POSIX types are used in interface specifications.
 */

#include "impl/rlimit.h"
#include "impl/extent.h"
#include "impl/strenc.h"
#include "impl/source.h"
#include "impl/device.h"
#include "impl/burner.h"
#include "impl/cd9660.h"
#include "impl/vfat32.h"
#include "impl/hfplus.h"
#include "conf/config.h"
#include "impl/unique.h"

#include <iostream>
#include <regex>

// should not be included by high-level app code,
// but only by concrete component implementations
// -include "allsys.h"

// - actually support multiple drives!
// - jam inodes (that is, remember ino_t and resolve inode conflicts)
// - actually crawl (close files, reopen on demand) [optional]
// - use memfds instead of reallocated mem
// - laaaaaning... (that's gonna take long)
//

int main( int argc, char ** argv )
{
    MkfsConf cfg;
    cfg.parse( argc, argv );

    if( cfg.entries.size() ) // folder to index
    {
        if( !cfg.crawl_fds ) { RaiseFdLimit(); }

        Original tree;
        tree.gap = cfg.tolerance();
        tree.decoder = New<UTF8Homebrew>();
        if( cfg.ex.size() )
        {
            std::list<std::regex> patterns;
            for( const char * expr : cfg.ex ) { patterns.emplace_back( expr ); }
            tree.allowName = [patterns]( const char * name )
            {
                for( auto & pattern : patterns )
                { if( std::regex_match( name, pattern ) ) { return false; } }
                return true;
            };
        }

        // we allow running without cfg.target, simply to analyze the file geometry
        if( !cfg.isTargetCopied() ) { tree.locator = New<ExtentIoc>( cfg ); }

        auto itr = cfg.entries.begin();
        tree.openRoot( *itr++ );
        // supplementary files and folders
        while( itr != cfg.entries.end() )
        { tree.fsRoot->insertStat( *itr++ ); }

        printf( "Files: %lu\n", tree.fileTable.size() );
        printf( "Backing devices: %lu\n", tree.plan.size() );

        if( cfg.target )
        {
            Ptr<Burner> outImage, tmpImage;

            if( cfg.isTargetMapped() && !cfg.zrControl )
            { printf( "DM without ZRam not yet supported\n" ); abort(); } // TODO

            // differentiated based on whether the control node is provided!
            if( cfg.zrControl && cfg.buffer )
            { tmpImage = New<ZRAMBurner>( cfg.buffer, cfg.zrControl ); }
            else if( cfg.buffer && cfg.buffer[0] == '/' ) // ensure absolute
            { tmpImage = New<FileBurner>( cfg.buffer ); }
            else // memfd
            { tmpImage = New<TempBurner>(); }

            // differentiated based on whether the file path is absolute
            if( cfg.isTargetMapped() )
            { outImage = New<DiskBurner>( cfg.target, cfg.dmControl ); }
            else
            { outImage = New<FileBurner>( cfg.target ); }

            auto tagVolume = [&cfg]( Volume & vol, MkfsConf::FSType type )
            {
                vol.setTitles( cfg.system, cfg.labels[type].c_str() );
            };
            CD::CD9660Out iso; tagVolume( iso, MkfsConf::FS_CDFS );
            HP::HFPlusOut mac; tagVolume( mac, MkfsConf::FS_HFSX );
            VF::VFat32Out fat; tagVolume( fat, MkfsConf::FS_Fat32 );

            Volume * out;
            if( cfg.fsType & MkfsConf::FS_CDFS )
            {
                if( cfg.fsType & MkfsConf::FS_HFSX )
                { iso.setHybrid( mac ); }
                out = &iso;
            }
            else if( cfg.fsType & MkfsConf::FS_HFSX )
            { out = &mac; }
            else if( cfg.fsType & MkfsConf::FS_Fat32 )
            {
                out = &fat;
                // a mild version of bestBlkSize() in fsview_temp.cpp
                if( !cfg.isTargetMapped() && out->blockSize() < 2048u )
                { out->setBlockSize( 2048u ); }
            }
            else if( !cfg.fsType )
            { printf( "No filesystem requested\n" ); abort(); }
            else
            { printf( "Unsupported filesystem!\n" ); abort(); }

            out->represent( tree, outImage, tmpImage );

            for( std::pair<const char *, const char *> & props : cfg.setOnDone )
            {
                __system_property_set( props.first, props.second );
            }

            if( cfg.daemonize )
            {
                sigset_t t;
                sigemptyset( &t );
                sigaddset( &t, SIGTERM );
                int real_sig;
                sigwait( &t, &real_sig );
            }
        }
    }
    return 0;
}
