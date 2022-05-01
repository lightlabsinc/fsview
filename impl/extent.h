/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef EXTENT_H
#define EXTENT_H

#include "wrapper.h"

#include <functional>
#include <string>

/// A range in unidimensional signed 64-bit space, with simple arithmetic.
struct Range
{
    off64_t offset;
    off64_t length;

    inline Range translate( off64_t by ) const
    { return { offset + by, length }; }
    inline Range translate( off64_t from, off64_t to ) const
    { return translate( to - from ); }
};

/// A signed 64-bit address/offset space delimited by fixed-size blocks.
struct Blocks
{
    /// The standard 512-byte sector, a minimal size sector understood by the Device Mapper.
    static constexpr const blksize_t MAPPER_BS = 1 << 9;

    /// Round an offset up to the next block boundary.
    inline off64_t roundUp( off64_t pos ) const { return roundUp( pos, blockSize() ); }

    /// Return a padding to the next block boundary.
    inline off64_t padding( off64_t pos ) const { return padding( pos, blockSize() ); }

    /// Round an offset up to the next block boundary, specifying the block size.
    static off64_t roundUp( off64_t pos, blksize_t blkSz ) { return ( pos + blkSz - 1 ) & ~( blkSz - 1 ); }

    /// Return a padding to the next block boundary, specifying the block size.
    static off64_t padding( off64_t pos, blksize_t blkSz ) { return roundUp( pos, blkSz ) - pos; }

    /// Return the first block of an extent that starts at a provided address.
    inline blkcnt_t firstBlk( off64_t start ) const
    { return start / blockSize(); }

    /// Return the last block in an extent that ends at a provided address.
    inline blkcnt_t lastBlk( off64_t end ) const
    { return firstBlk( end - 1 ); }

    /// Returns the first block of a range. Fail if the range starts in the middle of a block.
    inline blkcnt_t firstBlk( const Range & range ) const
    {
        if( range.offset % blockSize() ) { abort(); }
        return firstBlk( range.offset );
    }

    /// Returns the last block of a range.
    inline blkcnt_t lastBlk( const Range & range ) const
    { return lastBlk( range.offset + range.length ); }

    /// Return the block size. The implementation may be extremely nontrivial.
    virtual blksize_t blockSize() const = 0;

    virtual ~Blocks() = default;
};

struct Extent;
using ExtentList = std::list<Extent>;

/// A handful of definitions to represent "fixups"/"amendments" -
/// stored actions to update a location in a file (fd) or in memory
/// to store a value (typically a Range property, such as offset or length)
/// that is only known Later in the computation.
struct Later
{
    typedef std::function<void( int, off64_t )> Land;
    typedef std::function<void( const Range & )> Use;
    typedef std::function<void( int )> FdAction; // e.g. &fdatasync
    static inline void NoAction( int ) {}

    /// Store a future-known Range property as an arbitrary type P at a currently-known offset
    /// within a currently-known fd.
    /// E.g. the fd may represent a file system storage, and the offset a directory entry that will
    /// be populated with a future-known location of a file "burned" to the storage after the entry.
    /// @param[in]  fd      the target file descriptor
    /// @param[in]  offset  the target offset
    /// @param[in]  field   the field. Only the type of the value matters.
    /// @param[in]  assign  the assignment function that represents
    ///                     the later-provided Range as a value of type P.
    /// @param[in]  commit  the post-assignment hook that finalizes the write (e.g. flushes the file).
    template <typename P>
    static inline Use Store( int fd, off64_t offset, const P & field,
                             std::function<void( P &, const Range & )> assign,
                             FdAction commit = &Later::NoAction )
    {
        return [fd, offset, field, assign, commit]( const Range & rvalue )
        {
            auto lvalue = field;
            assign( lvalue, rvalue );
            // POSIX specific part
            pwrite( fd, &lvalue, sizeof( P ), offset );
            commit( fd );
        };
    }

    /// Store a currently-known value of an arbitrary type P at a currently-known offset
    /// from a future-provided base in a future-provided fd. (In other words - "assign a
    /// known value to a known field in an object stored at a future-known file location".)
    /// @param[in] offset   the field offset
    /// @param[in] value    the field value to assign
    template <typename P>
    static inline Land Store( off64_t offset, const P & value )
    {
        return [offset, value]( int fd, off64_t base )
        {
            // POSIX specific part
            pwrite( fd, &value, sizeof( P ), base + offset );
        };
    }

    /// Store a future-known Range property as a currently-known type P
    /// at a currently-known offset from a future-retrievable memory address
    /// (i.e. into a currently-known field of a future-located object).
    /// @param data     the function to retrieve the base ("object") address.
    /// @param offset   the field offset in the object.
    /// @param field    the field. Only the type of the value matters.
    /// @param assign   the assignment function that represents
    ///                     the later-provided Range as a value of type P.
    template <typename P>
    static inline Use Store( std::function<char * ()> data, off64_t offset, const P & field,
                             std::function<void( P &, const Range & )> assign )
    {
        return [data, offset, field, assign]( const Range & rvalue )
        {
            auto lvalue = field;
            assign( lvalue, rvalue );
            memcpy( data() + offset, &lvalue, sizeof( P ) );
        };
    }
};

typedef uintptr_t med_id;   ///< Medium id (used as a lightweight key in various maps)
static_assert( sizeof( med_id ) >= sizeof( dev_t ), "Medium ID must accommodate dev_t" );
static_assert( sizeof( med_id ) >= sizeof( ino_t ), "Medium ID must accommodate ino_t" );

/// The storage Medium, block-delimited.
struct Medium : public Blocks
{
    /// Any of the following may return undefined, but not all of them.
    /// Extents backed by data() are expected to have a zero(0) offset;
    /// but that's not strictly guaranteed and need not be enforced.
    virtual const void * data() const { return nullptr; }
    virtual const char * path() const { return nullptr; }

    /// The block device that this Medium represents or is backed by.
    virtual dev_t blockDevice() const { return 0; }

    /// Whether blockDevice() is this Medium itself rather than its backing
    /// storage; e.g. true for a block device and false for a regular file.
    virtual bool isDirectDevice() const { return false; } //=isAligned?

    /// The file descriptor may be reopenable and re-closeable to allow
    /// pooling the open descriptor slots at different processing steps
    /// fd() must be closed in the child destructor, not in the parent!
    virtual int fd() const { return -1; }

    /// Determines whether the backing storage is a block device
    virtual bool isAligned() const { return blockSize() > 1; }

    /// Identifies the medium among others of similar type
    virtual med_id id() const { return ( med_id )( void * ) this; }

    /// Writes the provided range of the medium to the provided fd.
    virtual void writeToFd( int outFd, const Range & range ) const;
};

/// medium->fd() bound as a function object
std::function<int()> FdOf( Ptr<Medium> medium );

/// medium->data() bound as a function object
std::function<char * ()> DtOf( Ptr<Medium> medium );

/// A Range within a Medium. The basic storage allocation and manipulation block.
struct Extent : public Blocks, public Range
{
    inline blksize_t blockSize() const override { return medium->blockSize(); }

    /// A default constructor to satisfy std::vector and similar containers.
    /// A default-constructed Extent represents nothing in nothing.
    Extent() = default;

    inline Extent( off64_t off, off64_t len, Ptr<Medium> pMedium )
        : Range( { off, len } ), medium( pMedium ) {}

    /// Writes the data represented by the Extent to the provided fd.
    void writeToFd( int fd ) const;

    Ptr<Medium> medium;
};

/// A Medium backed by a file with pre-populated stats.
struct FileMedium : public Medium
{
    FileMedium( int inFd, const struct stat64 & inSt ) : _fd( inFd ), _st( inSt ) {}
    FileMedium( int inFd );
    int fd() const override { return _fd; }
    med_id id() const override { return _st.st_ino; }
    dev_t blockDevice() const override;
    blksize_t blockSize() const override;
    bool isAligned() const override { return false; }
    int _fd;
    struct stat64 _st;
};
// also effectively implemented by -FileEntry- EntryState; merge?

/// A no-Medium ("/dev/zero")
struct ZeroMedium : public Medium
{
    blksize_t blockSize() const override { return MAPPER_BS; }
    bool isAligned() const override { return false; }
    virtual med_id id() const override { return 0; }
};

/// A "character device" kind of Medium, typically a memory area.
struct CharMedium : public Medium
{
    blksize_t blockSize() const override { return 1; }
};

/// A Medium representing an unowned memory location.
struct TempMedium : public CharMedium
{
    inline TempMedium( const void * ptr ) : _mem( ptr ) {}
    const void * data() const override { return _mem; }
    virtual med_id id() const override { return ( uintptr_t ) data(); }
    const void * _mem;
};

/// A Medium backed by a heap-allocated memory chunk. Assumes ownership.
struct HeapMedium : public TempMedium
{
    inline HeapMedium( void * ptr ) : TempMedium( ptr ), _heap_chunk( ptr ) {}
    inline ~HeapMedium() { free( _heap_chunk ); }
    void * _heap_chunk;
};

/// A Medium owning an arbitrary constant data value.
/// If a mutable value is passed, assumes ownership.
template<typename T>
struct CopyMedium : public CharMedium
{
    CopyMedium( T && t ) : vec( std::move( t ) ) {}
    CopyMedium( const T & t ) : vec( t ) {}
    const void * data() const override { return &vec; }
    const T vec;
};

/// A Medium owning a vector. Overrides data() to point to the vector contents
///  rather than the vector itself. If a mutable value is passed, assumes ownership.
template<typename T>
struct VectMedium : public CopyMedium<T>
{
    VectMedium( T && t ) : CopyMedium<T>( t ) {}
    VectMedium( const T & t ) : CopyMedium<T>( t ) {}
    const void * data() const override { return CopyMedium<T>::vec.data(); }
};

/// An Extent owning a vector. If a mutable value is passed, assumes ownership.
template<typename T>
struct VectExtent : public Extent
{
    VectExtent( const T & t )
        : Extent( 0, t.size() * sizeof( typename T::value_type ), New<VectMedium<T>>( t ) ) {}
    VectExtent( T && t )
        : Extent( 0, t.size() * sizeof( typename T::value_type ), New<VectMedium<T>>( std::move( t ) ) ) {}
};

/// A shorthand for an Extent in a FileMedium wrapping the provided fd.
inline Extent FileExtent( int fd, off64_t off, off64_t len ) { return Extent( off, len, New<FileMedium>( fd ) ); }

/// A shorthand for an Extent in a FileMedium wrapping the provided fd from the provided offset to the current position.
inline Extent FileExtent( int fd, off64_t off = 0 ) { return FileExtent( fd, off, lseek64( fd, 0, SEEK_CUR ) - off ); }

/// An unowning extent wrapping a memory area.
inline Extent TempExtent( const void * ptr, size_t size ) { return Extent( 0, size, New<TempMedium>( ptr ) ); }

/// An unowning extent wrapping an object/structure.
template<typename T>
inline Extent TempExtent( const T & ref ) { return Extent( 0, sizeof( T ), New<TempMedium>( &ref ) ); }

/// An owning extent capturing an object/structure by value.
template<typename T>
inline Extent CopyExtent( const T & ref ) { return Extent( 0, sizeof( T ), New<CopyMedium<T>>( ref ) ); }

/// An owning extent consuming an object/structure.
template<typename T>
inline Extent CopyExtent( T && ref ) { return Extent( 0, sizeof( T ), New<CopyMedium<T>>( ref ) ); }

/// An owning extent backed by a newly allocated chunk (prerequisite: ptr = malloc(size)).
inline Extent HeapExtent( void * ptr, size_t size ) { return Extent( 0, size, New<HeapMedium>( ptr ) ); }

/// A zero-filled extent of size len.
inline Extent ZeroExtent( off64_t len ) { return Extent( 0, len, Ptr<Medium>() ); } // zero pointer <=> ZeroMedium

/// A block device Medium.
struct DiskMedium : public Medium
{
    inline DiskMedium( dev_t dev, blksize_t blkSize )
        : device( dev )
        , bs( blkSize ? blkSize : MAPPER_BS ) {}

    dev_t blockDevice() const override { return device; }
    med_id id() const override { return device; }
    blksize_t blockSize() const override { return bs; }
    bool isAligned() const override { return true; }
    bool isDirectDevice() const override { return true; }

    dev_t device = 0;
    blksize_t bs = 0;
};

/// A Medium that generates its contents algorithmically.
struct RuleMedium : public Medium, public Later
{
    virtual size_t chunkSize() const = 0;

    void writeToFd( int outFd, const Range & range ) const override;

    /// A pure virtual method that needs implementation.
    virtual void fill( void * chunk, off64_t offset, size_t size ) const = 0;

    /// An empty collection of exceptions to the rule that needs population.
    std::map<off64_t, Land> amendments;
};

/// A RuleMedium that fills itself with '1' bits.
/// Used e.g. to populate the free/occupied bitmap of HFS.
struct BitsMedium : public RuleMedium
{
    BitsMedium( bool singleExtent = false, size_t chunk = MAPPER_BS, off64_t bits = 0 )
        : _skip_idle( singleExtent )
        , _chunk_size( chunk )
        , _bits( bits )
    {}

    inline void reserveBits( off64_t bits ) { _bits = bits; }
    blksize_t blockSize() const override { return _chunk_size; }
    size_t chunkSize() const override { return _chunk_size; }
    off64_t byteCount() const { return ( _bits + 7 ) / 8; }
    off64_t countOfFF() const { return _bits / 8; }
    inline bool hasTrailingByte() const { return _bits % 8; }
    inline char trailingByte() const { return ( 0xff00 >> ( _bits % 8 ) ); }
    void fill( void * chunk, off64_t offset, size_t size ) const override;
    bool _skip_idle;
    size_t _chunk_size;
    off64_t _bits;
};

/// An interface to a resolver of a single logical extent
///  to an extent list at the backing storage.
struct ILocator
{
    virtual ExtentList resolve( const Extent & source ) = 0;

protected:
    virtual ~ILocator() = default;
};

/// An interface to entities that pack Extent\s maintaining the padding and the current offset.
/// It is up the the implementation whether it simply lists the Extent\s (like the Planner)
/// or actually writes them to the backing storage (like the Burner).
struct IAppend
{
    IAppend() = default;
    IAppend( const IAppend & ) = delete;
    IAppend & operator=( const IAppend & ) = delete;
    virtual ~IAppend() = default;

    // abstract methods
    virtual off64_t append( const Extent & extent ) = 0;
    virtual off64_t offset() const = 0;

    virtual void commit() {} ///< Commit the listed Extent\s to the backing storage.

    // template methods
    inline off64_t append( off64_t offset, off64_t length, Ptr<Medium> medium )
    { return append( Extent( offset, length, medium ) ); }

    /// Pad the storage to a requested block size by appending a zero-filling extent if necessary.
    inline off64_t padTo( blksize_t blkSize )
    { off64_t pad = Blocks::padding( offset(), blkSize ); append( ZeroExtent( pad ) ); return pad; }
};

/// Shorthand: automatically pad the provided entity up to its next block.
template<typename T>
off64_t AutoPad( Ptr<T> burner ) { return burner->padTo( burner->blockSize() ); }

/// Shorthand: return the Extent represented by the provided entity since a past offset
/// to its current position, padding to the next block if necessary.
template<typename T>
Extent WrapToGo( Ptr<T> burner, off64_t since = 0 )
{
    AutoPad( burner );
    return Extent( since, burner->offset() - since, burner );
}

#endif // EXTENT_H
