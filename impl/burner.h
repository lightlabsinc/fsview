/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef BURNER_H
#define BURNER_H

#include "wrapper.h"

#include "impl/extent.h"

/// A Burner writes a sequence of Extent\s to a Medium.
/// This is similar to the "burn" function of a CD burner app.
/// Its possible targets include:
/// - a disk mapper-backed block device
/// - a virtual /dev/zram block device
/// - a regular file
/// - a memory chunk
/// - zero/blackhole
class Burner : public Medium, public IAppend
{
public:
    Burner() = default;

    static const constexpr mode_t CREAT_MODE = S_IRUSR | S_IWUSR | S_IRGRP;

    /// A target medium validity check.
    virtual bool isValid() const = 0;

    /// Allocate the needed space on the backing Medium.
    virtual void reserve( off64_t /*size*/ ) {}
};

/// A Planner collects a series of Extents before writing them to a Burner
/// in one transaction. This is similar to dropping files onto a CD burning
/// app window before pressing the "burn" button.
class Planner : public IAppend, public Blocks
{
public:
    inline Planner( Ptr<Burner> burner ) :
        _burner( burner ), _clientSz( 1 ) {} // MOREINFO generalize delegate to IAppend?

    /// Return the block size maintained by this Planner.
    blksize_t blockSize() const override { return std::max( _clientSz, _burner->blockSize() ); }

    /// Set the minimum block size required by the client. E.g. if the backing medium supports
    /// 512b blocks and the client is writing a 4k-clustered FAT disk, the client block size
    /// needs to be adjusted to 4k to make sure extents are placed at logical cluster boundaries.
    void requestBlockSize( blksize_t sz ) { if( _clientSz < sz ) { _clientSz = sz; } }

    /// Return the current offset. The next appended extent will have this offset.
    off64_t offset() const override { return _offset; }

    /// Append an extent (that is, put it into the FIFO list).
    off64_t append( const Extent & extent ) override;

    /// Return the Medium backed by this Planner.
    Ptr<Medium> medium() { return _burner; }

    /// Write ("burn") the stored extent sequence to the Burner provided on construction,
    virtual void commit() override;

    /// Automatically pad the current offset to the maintained block size.
    inline off64_t autoPad() { return padTo( blockSize() ); }

    /// "Seal the current box and put it on the truck": return the Extent of the backing Burner
    /// since the provided offset to the current offset padded to the maintained block size.
    /// This is an often-used idiom when a sequence of small extents packed on an intermediate storage
    /// is further "burned" to the final storage as a single extent of the intermediate storage.
    /// An example is writing in-memory filesystem metadata to the target disk mapper device. Since memory
    /// cannot be mapped as a block device directly, memory data are written to a ZRam device, which is mapped.
    inline Extent wrapToGo( off64_t since ) { autoPad(); return Extent( since, offset() - since, _burner ); }
    // TODO extract the (often used) append-and-wrap idiom for 1 extent

    /// Pad two Planner\s' current offsets to satisfy their block size requirements simultaneosly.
    static blksize_t copad( Planner & left, Planner & right );

private:
    Ptr<Burner> _burner;
    ExtentList _extlist;
    blksize_t _clientSz;

private:
    off64_t _offset = 0;
};

/// A Burner backed by a byte vector, to ensure contiguity.
class VectBurner : public Burner
{
public:
    VectBurner( blksize_t blkSz = 1 ) : _blk_sz( blkSz ) {}
    void reserve( off64_t size ) override { _out_vec.reserve( size ); }
    blksize_t blockSize() const override { return _blk_sz; }
    bool isValid() const override { return true; }
    off64_t offset() const override { return _out_vec.size(); }
    off64_t append( const Extent & extent ) override;
    const void * data() const override { return ( void * ) _out_vec.data(); }
private:
    blksize_t _blk_sz;
    std::vector<char> _out_vec;
};

/// A Burner backed by a file.
class FileBurner : public Burner
{
public:
    /// Construct a Burner backed by a pre-open fd (autoclose to assume ownership).
    FileBurner( int fd, bool autoclose = true );

    /// Construct a Burner to write a file at a given path.
    FileBurner( const char * path );
    ~FileBurner();

    blksize_t blockSize() const override { return 1; }     // a file is similar to a character device
    bool isValid() const override { return _out_fd >= 0; }
    off64_t offset() const override;
    off64_t append( const Extent & extent ) override;
    void commit() override { fsync( _out_fd ); }
    int fd() const override { return _out_fd; }

protected:
    int _out_fd;
    bool _aclose;
};

/// A Burner backed by a temporary, memory-resident file ("memfd").
struct TempBurner : public FileBurner
{
    TempBurner( blksize_t blkSz = 1 );
    blksize_t blockSize() const override { return _blk_sz; }
    blksize_t _blk_sz;
};

/// A Burner backed by a ZRam (compressible RAM) virtual drive.
/// https://www.kernel.org/doc/Documentation/blockdev/zram.txt
///
/// Presents temporary data (such as virtual filesystem metadata)
///  as block device ranges mappable by dm-linear.
class ZRAMBurner : public FileBurner
{
public:
    /// Create a ZRam burner from a block device path and the control node path.
    ZRAMBurner( const char * device, const char * sysFs );
    ~ZRAMBurner() { if( _sys_fs_control ) { closedir( _sys_fs_control ); } }
    blksize_t blockSize() const override { return _blks; }
    dev_t blockDevice() const override { return _d_stat.st_rdev; }
    bool isDirectDevice() const override { return true; }
    bool isValid() const override { return _blks; }
    void reserve( off64_t size ) override;

private: // impl
    void setAttr( const char * attr, const char * value );

private:
    std::string _dev_node;
    std::string _sys_path;
    struct stat64 _d_stat;
    DIR * _sys_fs_control;
    blksize_t _blks;
};

/// A Burner backed by the disk mapper kernel module.
/// https://www.kernel.org/doc/Documentation/device-mapper/
/// Builds the actual virtual disks exposed to the user machine.
class DiskBurner : public Burner
{
public:
    DiskBurner( const char * name, const char * ctrlNode ); // "userdata", "virtualcd"...
    blksize_t blockSize() const override { return MAPPER_BS; } // the hardware-compatible 512-byte sector
    bool isDirectDevice() const override { return true; }
    bool isValid() const override { return _control_fd >= 0; }
    off64_t offset() const override { return _offset; }
    off64_t append( const Extent & extent ) override;
    void commit() override;
    dev_t blockDevice() const override { return _dev; }

    ~DiskBurner() { if( isValid() ) { close( _control_fd ); } }

private://impl
    void dumpHeader() const;
    void dumpOutput( const char * outPath ) const;

private://data
    Ptr<VectBurner> _ioc_comm;
    Planner _dm_table_builder;
    std::string _display_name;
    int _control_fd;
    struct dm_ioctl _header;
    dev_t _dev = 0; // defined after commit()
    off64_t _offset = 0;
};

#endif // BURNER_H
