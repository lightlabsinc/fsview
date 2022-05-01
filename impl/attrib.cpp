/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "attrib.h"
#include "allsys.h"

#include <functional>

// just some reasonable buffer size
#ifdef PAGE_SIZE
#define BUF_SIZE PAGE_SIZE
#else
#define BUF_SIZE 4096
#endif

// stash for future refactoring once we are covered

//struct AutoFd
//{
//    inline AutoFd( int fd, std::function<void()> onError = []() {} )
//        : _fd( fd ) { if( !hasFd() ) { onError(); } }

//    inline void apply( std::function < T( int )> with ) const { if( hasFd() ) { with( fd() ); } }
//    inline int fd() const { return _fd; }
//    inline bool hasFd() const { return _fd >= 0; }
//    inline ~AutoFd() { if( hasFd() ) { close( _fd ); } }
//    int _fd;
//};

// define operators / and /=

//OpenFD::OpenFD( const char * path, int mode, bool failOnError )
//    : _fd( open( path, mode ) )
//{
//    if( failOnError && ( _fd < 0 ) )
//    {
//        perror( path ); abort();
//    }
//}

//OpenFD::OpenFD( int fd ) : _fd( fd ) {}

//OpenFD::~OpenFD() { if( _fd >= 0 ) { close( _fd ); } }

void setAttrib( int dirFd, const char * attr, const char * value )
{
    int fd = openat( dirFd, attr, O_WRONLY | O_TRUNC );
    if( fd >= 0 ) { write( fd, value, strlen( value ) ); close( fd ); }
    else { perror( attr ); abort(); }
}

void getAttrib( int dirFd, const char * attr, ConfCb::OnAttr onVal )
{
    char buf[BUF_SIZE];
    int fd = openat( dirFd, attr, O_RDONLY );
    if( fd >= 0 )
    {
        int len = read( fd, buf, sizeof( buf ) - 1 );
        buf[len + 1] = 0;
        onVal( buf );
        close( fd );
    }
    else { perror( attr ); abort(); }
}
