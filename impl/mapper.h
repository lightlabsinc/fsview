/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef MAPPER_H
#define MAPPER_H

#include "wrapper.h"

#include "impl/attrib.h"
#include "impl/extent.h" //?

/// A helper class to access Linux Device Mapper queries.
/// Uses the same ioctl API as the DiskBurner; the underlying API
/// types are the same but the exchange buffer layout is different.
struct Mapper
{
    /// The master exchange structure (dm_ioctl) followed by
    /// supplementary structure arrays sharing a base pointer.
struct __attribute( ( packed ) ) dmwizbuf : public dm_ioctl
    {
        dm_name_list device_names[0];
        dm_target_deps target_deps[0];
        dm_target_spec target_spec[0];
        dm_target_versions targets[0];
    };

    /// Open the control node, typically /dev/device-mapper
    Mapper( const char * ctrlNode, bool readOnly, size_t extra = 0 )
        : _fd( ctrlNode ? open( ctrlNode, readOnly ? O_RDONLY : O_RDWR ) : -1 )
    {
        buf.resize( sizeof( dm_ioctl ) + extra, 0 );
        dmw().version[0] = DM_VERSION_MAJOR;
        dmw().version[1] = 0; // MINOR too high
        dmw().version[2] = 0; // omit PATCHLEVEL
    }

    bool isValid() const { return _fd >= 0; }

    ~Mapper() { if( isValid() ) { close( _fd ); } }

    struct dmwizbuf & dmw() { return *reinterpret_cast<struct dmwizbuf * >( data() ); }

    /// list devices
    void listDevices( std::map<dev_t, std::string> & out );
    void listDevices( std::map<std::string, dev_t> & out );
    void listDevices( std::function<void( const std::string &, dev_t )> out );

    int deviceStatus( const char * name );

private:
    inline void * data() { return buf.data(); }
    int elasticQuery( int verb );
    std::vector<char> buf;
    int _fd;
};

#endif // MAPPER_H
