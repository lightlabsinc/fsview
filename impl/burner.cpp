/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "burner.h"

#include "impl/attrib.h"

#include <sstream>

off64_t Planner::append( const Extent & extent )
{
    // FIXME make doAppend();
    if( extent.length < 0 ) { printf( "Extent length %lx < 0\n", extent.length ); abort(); }
    off64_t cur = offset();
    if( extent.length ) { _extlist.push_back( extent ); } // FIXME protect at template method level
    _offset += extent.length;
    return cur;
}

void Planner::commit()
{
    _burner->reserve( offset() );
    off64_t trackOff = 0;
    for( auto & extent : _extlist )
    {
        if( extent.length )
        {
            _burner->append( extent );
            trackOff += extent.length;
            if( _burner->offset() > trackOff )
            {
                printf( "Extent %lx+%lx caused overflow %lx > %lx\n",
                        extent.offset, extent.length,
                        _burner->offset(), trackOff );
            };
        }
    } // FIXME protect at template method level
    _extlist.clear();
    _burner->commit();
}

blksize_t Planner::copad( Planner & outPlanner, Planner & tmpPlanner )
{
    blksize_t commonBlock = std::max( outPlanner.blockSize(),
                                      tmpPlanner.blockSize() );
    outPlanner.padTo( commonBlock );
    tmpPlanner.padTo( commonBlock );
    return commonBlock;
}

off64_t VectBurner::append( const Extent & extent )
{
    off64_t cur = offset();
    if( extent.medium && extent.medium->data() )
    {
        const char * ptr = ( const char * ) extent.medium->data() + extent.offset;
        _out_vec.insert( _out_vec.cend(), ptr, ptr + extent.length );
    }
    else { _out_vec.resize( cur + extent.length, '\0' ); }
    return cur;
}

FileBurner::FileBurner( int fd, bool autoclose ) : _out_fd( fd ), _aclose( autoclose && isValid() ) {}

FileBurner::FileBurner( const char * path ) : FileBurner( open( path, O_RDWR | O_CREAT | O_TRUNC, CREAT_MODE ), true )
{ if( _out_fd < 0 ) { perror( path ); abort(); } }

FileBurner::~FileBurner() { if( _aclose ) { close( _out_fd ); } }

off64_t FileBurner::offset() const { return lseek( _out_fd, 0, SEEK_CUR ); }

off64_t FileBurner::append( const Extent & extent )
{
    auto placement = offset();
    if( extent.length ) { extent.writeToFd( _out_fd ); }
    return placement;
}

TempBurner::TempBurner( blksize_t blkSz ) : FileBurner( memfd_open( "tempfd", O_RDWR ), true ), _blk_sz( blkSz ) {}

ZRAMBurner::ZRAMBurner( const char * device, const char * sysFs )
    : FileBurner( open( device, O_RDWR ) )
    , _dev_node( device )
    , _sys_path( sysFs )
    , _sys_fs_control( opendir( sysFs ) )
    , _blks( 0 )
{
    int tempFd = fd();
    if( tempFd >= 0 )
    {
        size_t blks = 0;
        if( ( ioctl( tempFd, BLKBSZGET, &blks ) >= 0 )
            && ( fstat64( tempFd, &_d_stat ) >= 0 ) ) { _blks = blks; }
        else { perror( "blkSz(zram)" ); }
        // don't close( tempFd ) here - keep open unless reserve() changes the disk size
    }
    else { perror( "fopen(zram)" ); }
}

void ZRAMBurner::reserve( off64_t size )
{
    if( _out_fd >= 0 ) { close( fd() ); }
    size = roundUp( size );
    setAttr( "reset", "1" );
    std::stringstream sstr;  sstr << size;
    setAttr( "disksize", sstr.str().c_str() );
    // MOREINFO make fd() lazy instead? really?
    _out_fd = open( _dev_node.c_str(), O_RDWR );
    if( _out_fd < 0 ) { perror( "reopen" ); abort(); }
}

void ZRAMBurner::setAttr( const char * attr, const char * value )
{
    setAttrib( dirfd( _sys_fs_control ), attr, value );
}

DiskBurner::DiskBurner( const char * name, const char * ctrlNode )
    : _ioc_comm( New<VectBurner>( sizeof( __u64 ) ) )
    , _dm_table_builder( _ioc_comm )
    , _display_name( name )
    , _control_fd( open( ctrlNode, O_RDWR ) )
{
    memset( &_header, 0, sizeof( _header ) );
    _header.version[0] = DM_VERSION_MAJOR;
    _header.version[1] = 0; // MINOR too high
    _header.version[2] = 0; // omit PATCHLEVEL
    strncpy( _header.name, name, DM_NAME_LEN );

    // if the device exists, tear it down
    _header.data_start = 0;
    _header.data_size = sizeof( _header );
    _header.flags = DM_SUSPEND_FLAG;
    if( ioctl( _control_fd, DM_DEV_SUSPEND, &_header ) < 0 )
    { perror( "Can't suspend device" ); }

    _header.dev = 0;
    if( ioctl( _control_fd, DM_DEV_REMOVE, &_header ) < 0 )
    { perror( "Can't destroy device" ); }

    _header.dev = 0;
    _header.flags = DM_READONLY_FLAG;
    if( ioctl( _control_fd, DM_DEV_CREATE, &_header ) < 0 )
    { perror( "Can't create device" ); abort(); }

    _dm_table_builder.append( TempExtent( _header ) );
}

off64_t DiskBurner::append( const Extent & extent )
{
    off64_t cur = offset();
    // it's safe to fill in spec.next - target_count is what matters
    struct dm_target_spec spec;
    spec.status = 0;
    spec.sector_start = cur / blockSize();
    spec.length = extent.length / blockSize();
    bool mappable = extent.medium.get()
                    && extent.medium->blockDevice()
                    && extent.medium->isDirectDevice();
    const char * type;
    spec.next = sizeof( spec );
    std::string parm;
    if( mappable )
    {
        type = "linear";
        dev_t devId = extent.medium->blockDevice();
        std::stringstream target;
        target << major( devId ) << ":" << minor( devId ) << " " << extent.offset / blockSize();
        parm = target.str();
    }
    else
    {
        type = "zero";
        // empty parameters still expand to a null-term dword
    }
    strncpy( spec.target_type, type, DM_MAX_TYPE_NAME );
    parm.resize( _ioc_comm->roundUp( parm.size() + 1 ), '\0' );
    spec.next += parm.size();
    _dm_table_builder.append( CopyExtent<struct dm_target_spec>( spec ) );
    _dm_table_builder.append( VectExtent<std::string>( parm ) );
    _offset += extent.length;
    _header.target_count++;
    return cur;
}

void DiskBurner::commit()
{
    _header.dev = 0;
    _header.data_start = sizeof( _header );
    _header.data_size = _dm_table_builder.offset();
    _header.flags = DM_READONLY_FLAG;
    _dm_table_builder.commit();

    if( false ) { dumpOutput( "/sdcard/dm.dmp" ); }

    if( ioctl( _control_fd, DM_TABLE_LOAD, _ioc_comm->data() ) < 0 ) { perror( "DM_TABLE_LOAD" ); abort(); }

    _header.data_start = 0;
    _header.data_size = sizeof( _header );
    _header.target_count = 0;
    _header.flags = 0; // DM_SUSPEND_FLAG == 0; DM_PERSISTENT_DEV_FLAG == 0
    if( ioctl( _control_fd, DM_DEV_SUSPEND, &_header ) < 0 ) { perror( "DM_DEV_SUSPEND" ); abort(); }

    _dev = _header.dev;
}

void DiskBurner::dumpHeader() const
{
    printf( "%016llx data:%d+%d [%s -> %s] targets:%d open:%d %08x\n",
            _header.dev, _header.data_start, _header.data_size, _header.name, _header.uuid,
            _header.target_count, _header.open_count, _header.flags );
}

void DiskBurner::dumpOutput( const char * outPath ) const
{
    int dump = open( outPath, O_WRONLY | O_CREAT | O_TRUNC, CREAT_MODE );
    WrapToGo( _ioc_comm ).writeToFd( dump );
    close( dump );
}
