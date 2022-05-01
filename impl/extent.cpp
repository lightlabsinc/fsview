/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "impl/extent.h"

namespace
{
template<typename I>
bool IsPowOf2( I val ) { return !( val & ( val - 1 ) ); }

struct FileIO
{
    static void addFile( int outFd, int inFd, off64_t offset, off64_t length );
    static void addData( int outFd, const void * data, off64_t length );
    static void addZero( int outFd, off64_t length );
};

void FileIO::addFile( int outFd, int fd, off64_t offset, off64_t length )
{
    // The copy_file_range() system call first appeared in Linux 4.5
    if( fd >= 0 )
    {
        auto written = sendfile64( outFd, fd, &offset, length );
        if( written != length ) { perror( "sendfile" ); abort(); }
    }
}

void FileIO::addData( int outFd, const void * data, off64_t length )
{
    auto written = write( outFd, data, length );
    if( written != length ) { perror( "write" ); abort(); }
}

void FileIO::addZero( int outFd, off64_t length )
{
    if( length < 0 ) { abort(); }
    if( length )
    {
        auto pos = lseek64( outFd, length, SEEK_CUR );
        //static char zero = 0;
        //write( outFd, &zero, 1 );
        ftruncate64( outFd, pos );
    }
}
}

constexpr const blksize_t Blocks::MAPPER_BS; // ...deprecated in C++17

// most generic. specializations can reduce complexity.
void Medium::writeToFd( int outFd, const Range & range ) const
{
    if( data() )
    {
        FileIO::addData( outFd, ( const char * ) data() + range.offset, range.length );
    }
    else if( fd() >= 0 )
    {
        FileIO::addFile( outFd, fd(), range.offset, range.length );
    }
    else if( path() )
    {
        int fd = open( path(), O_RDONLY );
        FileIO::addFile( outFd, fd, range.offset, range.length );
        close( fd );
    }
    // else if( medium.blockDevice() ) { abort(); } // mknod, open, addFile...
    else { FileIO::addZero( outFd, range.length ); }
}

std::function<int ()> FdOf( Ptr<Medium> medium )
{
    return [medium]()
    {
        return medium->fd();
    };
}

std::function<char * ()> DtOf( Ptr<Medium> medium )
{
    return [medium]()
    {
        return ( char * ) medium->data();
    };
}

void Extent::writeToFd( int fd ) const
{
    if( medium.get() ) { medium->writeToFd( fd, *this ); }
    else { FileIO::addZero( fd, length ); }
}

FileMedium::FileMedium( int inFd ) : _fd( inFd ) { fstat64( _fd, &_st ); }

dev_t FileMedium::blockDevice() const { return _st.st_dev; }

blksize_t FileMedium::blockSize() const { return _st.st_blksize; }

void RuleMedium::writeToFd( int outFd, const Range & range ) const
{
    // careful with ZRAM seeks! (and any other block device)
    off64_t base = lseek64( outFd, 0, SEEK_CUR );
    off64_t size = chunkSize();
    void * chunk = memalign( blockSize(), size );
    off64_t last = range.offset + range.length;
    for( off64_t next = range.offset; next < last; next += size )
    {
        auto part = std::min( last - next, size );
        fill( chunk, next, part );
        write( outFd, chunk, part );
    }
    for( auto & amendment : amendments )
    { amendment.second( outFd, base ); }
}

void BitsMedium::fill( void * chunk, off64_t offset, size_t size ) const
{
    const ssize_t & ssize = size; // signed/unsigned warning
    off64_t nOfFF = countOfFF() - offset;
    off64_t nFill = std::min( nOfFF, ssize );
    if( nFill >= 0 && nFill < ssize )
    {
        if( !( _skip_idle && offset ) ) { memset( chunk, 0xff, nFill ); }
        char * data = ( char * ) chunk;
        if( hasTrailingByte() ) { data[nFill++] = trailingByte(); }
        if( nFill < ssize ) { memset( data + nFill, 0, size - nFill ); }
    }
}
