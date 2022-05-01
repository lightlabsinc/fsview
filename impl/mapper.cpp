/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "mapper.h"

namespace
{
template <typename Rec> union DMChain
{
    Rec * rec;
    unsigned char * next;
    bool step()
    {
        return ( rec->next )
               ? next += rec->next, true : false;
    }
    DMChain( Rec * dmRec ) : rec( dmRec ) {}
};

template<size_t S>
std::string AtMost( const char * src )
{
    char buf[S + 1];
    buf[S] = '\0';
    strncpy( buf, src, S );
    return buf;
}

}//private

// MOREINFO extract inline int runVerb( int verb ) { return ioctl( _fd, verb, data() ); } ?

void Mapper::listDevices( std::map<std::string, dev_t> & out )
{
    listDevices( [&out]( const std::string & name, dev_t num ) { out[name] = num; } );
}

void Mapper::listDevices( std::map<dev_t, std::string> & out )
{
    listDevices( [&out]( const std::string & name, dev_t num ) { out[num] = name; } );
}

void Mapper::listDevices( std::function<void( const std::string &, dev_t )> out )
{
    // even if we can't list devices, file-based API should work, so we simply exit here
    if( elasticQuery( DM_LIST_DEVICES ) < 0 ) { perror( "DM_LIST_DEVICES" ); return; }

    if( dmw().data_size )
    {
        DMChain<struct dm_name_list> dmRec( dmw().device_names );
        do
        {
            // sanity check, in case the kernel sends something infinite
            // of course dm sends the device name null-t, but who knows?
            // sanity check complete
            out( AtMost<DM_NAME_LEN>( dmRec.rec->name ), dmRec.rec->dev );
        }
        while( dmRec.step() ) ;
    }
}

int Mapper::deviceStatus( const char * name )
{
    strncpy( dmw().name, name, DM_NAME_LEN );
    dmw().data_start = 0;
    dmw().data_size = sizeof( dm_ioctl );
    dmw().dev = 0;
    dmw().flags = 0; // DM_QUERY_INACTIVE_TABLE_FLAG for the pending table
    return ioctl( _fd, DM_DEV_STATUS, data() );
}

int Mapper::elasticQuery( int verb )
{
    int rc;
    while( dmw().data_size = buf.size(),
           ( rc = ioctl( _fd, verb, data() ) ) >= 0 && ( dmw().flags & DM_BUFFER_FULL_FLAG ) )
    {
        buf.resize( buf.size() << 1, 0 );
    }
    return rc;
}
