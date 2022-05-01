/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef ATTRIBS_H
#define ATTRIBS_H

#include "conf/cmdarg.h"

// raw "sysfs style" file facilities
namespace {}

//struct OpenFD
//{
//    OpenFD( const char * path, int mode, bool failOnError );
//    OpenFD( int fd );
//    OpenFD operator/( const char * entry ) const;
//    ~OpenFD();

//private:
//    int _fd;
//};

/// Set a sysfs/procfs attribute by opening and writing a file.
void setAttrib( int dirFd, const char * attr, const char * value );

/// Read a sysfs/procfs attribute by opening and reading a file.
void getAttrib( int dirFd, const char * attr, ConfCb::OnAttr onVal );

#endif // ATTRIBS_H
