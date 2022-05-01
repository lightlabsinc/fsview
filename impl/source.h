/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef SOURCE_H
#define SOURCE_H

#include "wrapper.h"

#include "impl/strdec.h"
#include "impl/strenc.h"
#include "impl/extent.h"

struct Hierarchy;
struct PathEntry;
struct FileEntry;
typedef struct dirent64 RawDirEnt;

/// An abstract visitor to traverse the file tree.
struct Follower
{
    /// A rule whether to pick up a particular directory entry.
    /// Used to apply include/exclude filters, turn recursion on/off.
    virtual bool useEntry( const RawDirEnt * ) const { return true; }

    /// A callback to process a directory entry.
    virtual void onFolder( PathEntry * ) = 0;

    /// A callback to process a regular file entry.
    virtual void onFileFd( FileEntry * ) = 0;

    /// dependency: the native/platform charset decoder
    Ptr<IDecoder> decoder;

protected:
    virtual ~Follower() = default;
};

struct PathEntry;

/// A partial entry base that controls its placement.
struct EntryLand
{
    /// Set as a root entry in the provided hierarchy.
    void setAsRoot( Hierarchy * fs );

    /// Set as a child to the provided PathEntry
    void setParent( PathEntry * dir );

    /// Return entry depth in the file tree (0=root, etc.)
    int depth() const;

    Follower * root = nullptr;
    PathEntry * parent = nullptr;
};

/// A partial entry base that contains its stats.
struct EntryStat
{
    /// Collect fd information from the source filesystem.
    /// Assumes ownership of the file descriptor.
    bool statFd( int fd );

    /// Close the owned file descriptor.
    void closeFd();

    virtual ~EntryStat() { closeFd(); }

    struct stat64 stat;
    mutable int lastFd = -1;
};

/// An abstract entry (folder|file)
struct Entry : public EntryLand, public EntryStat
{
    virtual mode_t openFlags() const = 0;

    inline bool isDir() const { return ( openFlags() & O_DIRECTORY ); }
    inline bool isFile() const { return !isDir(); }

    /// Return the native entry path in the file system.
    const char * nativePath() const;
    std::string absPath;
    Unicode decoded;

    /// Offer a file descriptor to traverse (may be a folder).
    bool offerFd( int fd ) { return ( fd >= 0 ) && describe( fd ); }

    /// Offer a file/folder path to traverse.
    bool offerFd( const char * entry, bool relative );

    /// Set the entry physical path.
    void setPath( const char * path, bool relative );

    /// Set the entry logical name
    /// (will become its name in the erected filesystem).
    void setName( const char * rawName ); // use injected decoder

    /// Collect platform-provided information on the entry (stat[64] and handles).
    virtual bool describe( int fd ) = 0;

    /// Call the assigned Follower callback on the entry.
    virtual void activate() = 0;
};

/// A facility method to introduce an abstract child to its assigned parent.
void PlaceEntry( Ptr<Entry> child, const char * path, bool relative );

// entries will be sorted in the target charset; unsort for now
using EntryList = List<Entry>;

/// A directory entry.
struct PathEntry : public Entry
{
    mode_t openFlags() const override { return O_RDONLY | O_DIRECTORY; }

    // implementations - see descriptions in Entry
    bool describe( int fd ) override;
    void activate() override;

    /// Add a user-located file under the directory entry.
    /// Need not be an actual child in the source filesystem.
    void insertFile( const char * path );

    /// Add a user-located directory under the directory entry.
    /// Need not be an actual child in the source filesystem.
    void insertPath( const char * path );

    /// Register an existing child (regular file) to directory entry.
    void appendFile( const char * path );

    /// Register an existing child (subdirectory) to directory entry.
    void appendPath( const char * path );

    /// Stat a user-provided path and register it as a logical child
    /// whatever supported type it is (a subdirectory or a regular file).
    void insertStat( const char * path );

    /// Sort out special entries ("." for current and ".." for parent).
    static bool isValidChild( const RawDirEnt & entry );

    /// Walk through an open folder and register its children.
    /// Only regular files and subfolders are supported.
    void traverse();

    /// Release the owned handles.
    void closeFd();

    ~PathEntry() { closeFd(); }

    bool mute = false; ///< a flag to suppress automatic traversal of this folder.
    EntryList entries;
    DIR * lastDir = nullptr;

    void placeChild( Ptr<Entry> child, const char * path, bool relative );
};

/// A regular file entry.
struct FileEntry : public Entry, public Medium // FIXME duplicates FileMedium!
{
    mode_t openFlags() const override { return O_RDONLY; }

    // implementations - see descriptions in Entry
    bool describe( int fd ) override;
    void activate() override;

    // The following methods expose a FileEntry as a Medium:
    blksize_t blockSize() const override { return stat.st_blksize; }
    const char * path() const override { return nativePath(); }
    dev_t blockDevice() const override { return stat.st_dev; }
    med_id id() const override { return stat.st_ino; }
    bool isAligned() const override { return false; }
    int fd() const override;

    // The following method exposes the entire FileEntry as an Extent:
    operator Extent();
};

/// The traversed and/or user-assembled source file tree.
/// Aware of files and folders, unaware of blocks.
/// MOREINFO: inline this structure completely into Original?
struct Hierarchy : public Follower
{
    /// Add an existing filesystem folder, optionally traversing it.
    /// If not, selected children can be added with append(File|Path)().
    void openRoot( const char * path, bool traverse = true );

    /// Start with a virtual/imaginary folder.
    /// All entries must be added with insert(File|Path)(); append*() won't work.
    void fakeRoot();

    /// Close the current root and release all entries registered under it.
    inline void closeRoot() { fsRoot.reset(); }

    ~Hierarchy() { closeRoot(); }

    // structural
    Ptr<PathEntry> fsRoot;
};

// Ptr<Extent> extent() = 0; -> ExposedFile

/// We strive to make outgoing file IDs stable to allow efficient
/// file caching on the host machine side, e.g. on HFS+. However,
/// not all valid ino_t are also valid target file IDs. In case we
/// encounter a few "invalid" inode numbers, we re-enumerate them
/// in a stable fashion.
typedef std::function<ino_t( ino_t )> Renum;
inline ino_t SameIno( ino_t ino ) { return ino; }

#endif // SOURCE_H
