/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef RETVAL_H
#define RETVAL_H

#include "wrapper.h"

#include "conf/config.h"

#include "impl/extent.h"
#include "impl/source.h"
#include "impl/device.h"
#include "impl/unique.h"

// Proposed return codes...
constexpr const int kCantOpenRootFolder = -1;
constexpr const int kCantWriteTargetOut = -2;

template <typename T> using Predicate = std::function<bool( const T & )>;

/// A source file set representation that's both file tree and disk block aware.
/// Defines a few default policies that can be partially overridden by children.
struct Original : public Hierarchy, public Geometry
{
    /// The file extent locator (injected dependency).
    Ptr<ILocator> locator = New<NoLocator>();

    /// A source file name validator. Default is "allow all".
    ///
    /// FIXME this is a poorly internationalized implementation.
    /// Should at least accept well-formed UTF-8...
    Predicate<const char *> allowName = []( const char * ) { return true; };

    /// Allow or bypass a directory entry based on its name.
    bool useEntry( const RawDirEnt * entry ) const override;

    /// When a folder is encountered, automatically traverse and close its fd.
    void onFolder( PathEntry * folder ) override;

    /// When a regular file is encountered, resolve its extents and NOT close its fd.
    void onFileFd( FileEntry * fEntry ) override;

    // byproducts
    Index<PathEntry> pathTable; ///< This will become e.g. a CDFS PathTable
    Index<FileEntry> fileTable; ///< This will become the file area.

    // byproducts
    std::map<Entry *, ExtentList> layout; ///< Source Extent map. Only files, not folders.
};

/// This interface is co-implemented by Volume\s that describe *the same file area* in an
/// alternative way. Example: a "monster CD" that's both an HFS+ (Mac) and a CDFS volume.
/// Planning such "slave" volumes is subject to constraints coming from the master volume
/// that authoritatively lays out the disk space delegating its unused areas to the slave.
struct Hybrid
{
    /// Return a desired minimal block size. E.g. if the master supports 1k-4k blocks and
    /// the slave is CDFS (2k blocks), returning 2k here constrains the range to 2k-4k.
    /// The outImage is typically a DiskBurner and the tmpImage is then a ZRamBurner.
    /// For the scratch partition, outImage is a FileBurner and tmpImage is a TempBurner.
    virtual blksize_t blkSzHint( const Original & tree,
                                 const Medium & outImage, const Medium & tmpImage ) = 0;

    /// The master has adjusted the original tree based on block sizes. "Adjusted" means
    /// e.g. merging source extents together to reduce mapping fragmentation.
    /// At this point, the (source -> target) disk area mapping is fully defined, as well
    /// as the size of the file area (with dead-weight gaps resulting from extent merge).
    virtual void masterAdjusted( const Original & tree,
                                 const Medium & outImage, const Medium & tmpImage,
                                 blksize_t blkSz ) = 0;

    /// This method is called when the master must reserve (leave unused) a space range
    /// and, instead of simply skipping/zeroing it, delegates it to slaves for filling in.
    /// An example is CDFS delegating out its first 64 reserved sectors (32k).
    /// @param[in] cap is the size of the reserved area.
    virtual void masterReserved( const Original & tree,
                                 Planner & outPlanner, Planner & tmpPlanner,
                                 off64_t cap ) = 0;

    /// This method is called when the master is done writing both the file area and its
    /// file system metadata. The slave is free to append all it wants.
    /// @param[in] cols is the final layout of the target file area.
    virtual void masterComplete( const Original & tree,
                                 Planner & outPlanner, Planner & tmpPlanner,
                                 const Colonies & cols ) = 0;

protected:
    virtual ~Hybrid() = default;
};

/// A generic target file system volume.
struct Volume : public Blocks
{
    /// Return a bitwise OR mask of allowed logical block/cluster sizes.
    /// A filesystem property defined by its standard and optionally constrained by us.
    virtual blksize_t sizeRange() const = 0;

    /// Choose the logical block size to use.
    virtual void setBlockSize( blksize_t blkSz ) = 0;

    /// Reserve space on the target device.
    /// @param[in] scratch  treat the target as a scratch partition that's mostly
    /// occupied by free space, not files. Affects metadata generation efficiency.
    /// @param[in] scrooge  aggressively find and claim gaps between files, e.g.
    /// preventing FAT32/HFS+ "lost cluster" warnings. If unset, it's possible to
    /// claim certain clusters/logical blocks as neither free space nor file space.
    /// @param[in] extra  extra free space to reserve beyond projected source files.
    void bookSpace( bool scratch, bool scrooge, off64_t extra );

    /// Set the originating OS name and the volume name, sanitizing the inputss.
    void setTitles( const char * system, const char * volume );

    /// The "workhorse" method: lay out a source file tree on the output device
    /// using the temporary device for on-the-fly generated filesystem metadata.
    void represent( Original & tree, Ptr<Burner> outImage, Ptr<Burner> tmpImage );

protected: // abstract
    /// Set the originating OS name and the volume name, assuming sanitized inputs.
    virtual void setLabels( const char * system, const char * volume ) = 0;

    /// Adjust the volume parameters (block sizes, metadata area sizes...) to satisfy
    /// the provided source file tree and the output media.
    void adjust( const Original & tree, const Medium & outImage, const Medium & tmpImage );

    /// Actually lay out the target volume, appending the resulting extents to Planners.
    /// Since optimal layout strategies are highly FS-specific, it is the responsibility
    /// of this method to append on-the-fly generated extents to the temporary medium,
    /// wrapToGo() ranges of the temporary medium and append them to the output medium.
    virtual Colonies plan( const Original & tree, Planner & outPlanner, Planner & tmpPlanner ) = 0;

    /// Request a block size hint from assigned slaves, if any slaves are assigned.
    blksize_t getHint( Original & tree, const Medium & outImage, const Medium & tmpImage );

    /// Send masterAdjusted() to attached slaves, if any.
    void adjustSlaves( Original & tree, const Medium & outImage, const Medium & tmpImage );

    /// Send masterReserved() to attached slaves, if any, letting them use a master-reserved disk area.
    void planReserved( const Original & tree, Planner & outPlanner, Planner & tmpPlanner, off64_t cap );

    /// Send masterComplete() to attached slaves, if any, letting them append anything to the end of the disk.
    void planComplete( Original & tree, Planner & outPlanner, Planner & tmpPlanner, const Colonies & srcToTrg );

    bool _scratch = false; ///< writable/temporary partition, favor free space in allocation tables
    bool _scrooge = false; ///< claim as much free space as possible. ***not implemented yet (no use case) ***
    off64_t xtraRoom = 0L; ///< a hint how much room to reserve. (may reserve more.)

    Hybrid * hybrid = nullptr;
};

#endif // RETVAL_H
