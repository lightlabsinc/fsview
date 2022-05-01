/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "wrapper.h"

#include "conf/config.h"
#include "impl/mapper.h"

struct Origami
{
    std::string name;
    bool hasDevId = false;
    dev_t devId = 0;
    std::string path;

    void setDevId( dev_t id ) { devId = id; hasDevId = true; }
};

int main( int argc, char ** argv )
{
    NameConf cfg;
    cfg.parse( argc, argv );

    if( !argc ) { return 0; }

    if( cfg.oneprop )
    {
        if( ( argc > 1 ) )
        {
            fprintf( stderr, "More than one name to query, can't use --property. "
                     "Use --properties=<property.prefix> to query multiple names.\n" );
            exit( 1 );
        }
        if( cfg.setprop )
        {
            fprintf( stderr, "Both --property and --properties set. Use one.\n" );
            exit( 1 );
        }
    }

    std::vector<Origami> requests;
    requests.resize( argc );

    std::set<dev_t> devIds; // multimap with numbers
    // std::vector<dev_t> lsdev;
    std::map<dev_t, std::string> devnm;
    // get dev_t from name

    Mapper mapper( cfg.dmControl, O_RDONLY );
    for( int i = 0; i < argc; ++i )
    {
        const char * name = argv[i];
        Origami & request = requests[i];
        request.name = name;

        // get device status. don't throw!
        if( mapper.deviceStatus( name ) >= 0 )
        {
            auto dev = mapper.dmw().dev;
            devIds.insert( dev );
            request.setDevId( dev );
        }
        else
        {
            fprintf( stderr, "Name not found: %s\n", name );
        }
    }

    int errors = 0;

    // extract as findBlkDev( onBlkDev )
    if( devIds.size() )
    {
        std::string prop;
        if( cfg.oneprop ) { prop = cfg.oneprop; }
        else if( cfg.setprop ) { prop = cfg.setprop; prop.push_back( '.' ); } // DELIMITER2
        size_t prop_trim = prop.size();

        DIR * catDir = opendir( cfg.dev_cat );
        if( catDir )
        {
            struct dirent64 entry;
            struct dirent64 * canary;
            struct stat64 st;
            while( devIds.size() && ( readdir64_r( catDir, &entry, &canary ), canary ) )
            {
                if( entry.d_type == DT_BLK
                    && fstatat64( dirfd( catDir ), entry.d_name, &st, AT_NO_AUTOMOUNT ) >= 0 )
                {
                    auto itr = devIds.find( st.st_rdev );
                    if( itr != devIds.end() )
                    {
                        devnm[st.st_rdev] = entry.d_name;
                        devIds.erase( itr );
                    }
                }
            }
            closedir( catDir );
        }
        else { perror( cfg.dev_cat ); exit( 2 ); }

        if( 0 /*cfg.tmp_cat*/ )
        {
            //
        }
        else
        {
            for( auto dev : devIds )
            {
                fprintf( stderr, "Node not found: %u:%u\n",
                         major( dev ), minor( dev ) );
            }
        }
        for( Origami & request : requests )
        {
            if( request.hasDevId )
            {
                auto itr = devnm.find( request.devId );
                if( itr != devnm.end() )
                {
                    request.path = cfg.dev_cat;
                    request.path.push_back( '/' ); // DELIMITER1
                    request.path.append( itr->second );
                    if( cfg.setprop || cfg.oneprop )
                    {
                        if( cfg.setprop ) // else oneprop
                        {
                            prop.resize( prop_trim );
                            prop.append( request.name );
                        }
                        errors |= __system_property_set( prop.c_str(),
                                                         request.path.c_str() );
                    }
                }
                printf( "%s\n", request.path.c_str() );
            }
        }
    }

    return errors; // nonzero if any __system_property_set() call has failed
}
