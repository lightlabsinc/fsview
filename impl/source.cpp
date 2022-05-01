/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "source.h"

#include <libgen.h>

#include <string>

void EntryLand::setAsRoot( Hierarchy * fs ) { root = fs; parent = nullptr; }

void EntryLand::setParent( PathEntry * dir ) { parent = dir; root = dir->root; }

int EntryLand::depth() const { return parent ? parent->depth() + 1 : 0; }

bool EntryStat::statFd( int fd ) { return fstat64( lastFd = fd, &stat ) >= 0; }

void EntryStat::closeFd() { close( lastFd ); lastFd = -1; }

// in fact, we might not have to call this function at all:
// openat() can be used for all or most practical purposes.
const char * Entry::nativePath() const { return absPath.c_str(); }

bool Entry::offerFd( const char * entry, bool relative )
{
    auto flags = openFlags();
    return offerFd( relative ? openat( parent->lastFd, entry, flags ) : open( entry, flags ) );
}

void Entry::setPath( const char * path, bool relative )
{
    if( relative && parent )
    {
        absPath.append( parent->nativePath() );
        absPath.push_back( '/' );
    }
    absPath.append( path );
}

void Entry::setName( const char * rawName ) { decoded = root->decoder->decode( rawName ); }

bool PathEntry::describe( int fd )
{
    return statFd( fd ) && ( lastDir = fdopendir( fd ) );
}

void PathEntry::activate() { root->onFolder( this ); }

void PathEntry::insertFile( const char * path ) { placeChild( New<FileEntry>(), path, false ); }

void PathEntry::insertPath( const char * path ) { placeChild( New<PathEntry>(), path, false ); }

void PathEntry::appendFile( const char * path ) { placeChild( New<FileEntry>(), path, true ); }

void PathEntry::appendPath( const char * path ) { placeChild( New<PathEntry>(), path, true ); }

void PathEntry::insertStat( const char * path )
{
    // It's not exactly efficient to stat the path at this point and re-stat it again, but
    // passing an optional stat64 structure down the call flow would be a greater change.
    // After all, insert* is used to process command line parameters,
    // and we aren't expecting many of them. (Yes you can post this comment on TheDailyWTF.)

    struct stat st;
    if( lstat( path, &st ) >= 0 )
    {
        if( S_ISREG( st.st_mode ) ) { insertFile( path ); }
        else if( S_ISDIR( st.st_mode ) ) { insertPath( path ); }
        else
        {
            fprintf( stderr, "Unsupported file type (%08x): %s\n",
                     st.st_mode, path );
        }
    }
    else { perror( path ); }
}

void PathEntry::placeChild( Ptr<Entry> child, const char * path, bool relative )
{
    child->setParent( this );
    const char* rpath = path;
    std::string copy;
    if( relative )
	{
		copy = path;
		copy.push_back('\0');
		rpath = basename( &copy[0] );
	}
    child->setName( rpath );
    PlaceEntry( child, path, relative );
}

void PlaceEntry( Ptr<Entry> child, const char * path, bool relative )
{
    if( child->offerFd( path, relative ) )
    {
        child->setPath( path, relative );
        if( child->parent ) { child->parent->entries.push_back( child ); }
        child->activate();
    }
}

bool PathEntry::isValidChild( const RawDirEnt & entry )
{
    return strcmp( entry.d_name, "." )
           && strcmp( entry.d_name, ".." );
}

void PathEntry::traverse()
{
    if( mute ) { return; }
    RawDirEnt * ptr;
    RawDirEnt entry;

    // don't follow symbolic links: there are no in CFDS
    // we don't support pipes, sockets or ch/blk devices
    while( lastDir && ( readdir64_r( lastDir, &entry, &ptr ), ptr ) )
    {
        if( isValidChild( entry ) && root->useEntry( ptr ) )
        {
            switch( entry.d_type )
            {
                case DT_REG:
                    appendFile( entry.d_name );
                    break;
                case DT_DIR:
                    appendPath( entry.d_name );
                    break;
            }
        }
    }
}

void PathEntry::closeFd()
{
    if( lastDir )
    {
        closedir( lastDir );
        lastDir = nullptr;
        lastFd = -1;
    }
}

bool FileEntry::describe( int fd ) { return statFd( fd ); }

void FileEntry::activate() { root->onFileFd( this ); }

int FileEntry::fd() const { return lastFd < 0 ? lastFd = open( path(), O_RDONLY ) : lastFd; }

FileEntry::operator Extent()
{
    Extent extent;
    extent.medium = Temp<Medium>( this );
    extent.offset = 0;
    extent.length = stat.st_size;
    return extent;
}

void Hierarchy::openRoot( const char * path, bool traverse )
{
    fsRoot = New<PathEntry>();
    fsRoot->mute = !traverse;
    fsRoot->setAsRoot( this );
    PlaceEntry( fsRoot, path, false );
}

void Hierarchy::fakeRoot()
{
    fsRoot = New<PathEntry>();
    fsRoot->mute = true;
    fsRoot->setAsRoot( this );
    onFolder( fsRoot.get() );
}

