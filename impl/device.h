/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef DEVICE_H
#define DEVICE_H

#include "wrapper.h"

#include "conf/config.h"
#include "impl/source.h"
#include "impl/burner.h"

/// A map of source devices containing all the files passed in.
struct DeviceMap
{
    /// Maps mounted devices to their unmounted mirrors ("surfaces").
    /// This is required because device-mapper can't access mounted drives.
    std::map<dev_t, dev_t> substitute;

    /// A registry of disk media represented by
    Map<dev_t, DiskMedium> media;

    /// Substitute the mapped device with unmapped "surface" device.
    void subst( dev_t device, dev_t surface ) { substitute[device] = surface; }

    /// Return the DiskMedium backing the provided device, possibly creating it.
    /// @param device   mounted filesystem device to represent
    /// @param blkSize  the block size of the surface device obtained from stat[64]
    Ptr<DiskMedium> surface( dev_t device, blksize_t blkSize );
};

/// An "identity locator": returns a list containing the single source extent.
/// Useful when the created virtual device is a regular file and source files
/// are simply copied into it. This is a natural test scenario.
struct NoLocator : public ILocator
{
    ExtentList resolve( const Extent & source ) { return { source }; }
};

/// A locator returning the list of actual storage device extents backing
/// the provided file. The FS_IOC_FIEMAP ioctl is used on Linux.
struct ExtentIoc : public ILocator, public DeviceMap
{
    /// reserve the backing memory chunk to accommodate newCount extents.
    void reserve( size_t newCount );

    ExtentIoc();
    ExtentIoc( MkfsConf & cfg );
    ExtentList resolve( const Extent & source );

private:
    static size_t S( int extents );

    enum Correction
    {
        Naive = 0,
        Fsync,
        Retry,
    };

    ExtentList peek( const Extent & source, Correction correction );

    size_t extCount;
    struct fiemap * fem;

    Ptr<Planner> fosterHouse;
    off64_t adoptionBudget = 0;

    std::vector<int> waitlog;
};

/// Extents of the source device occupied by the files we need to represent.
/// The key is the extent start and the value is the extent end.
/// The value is always less than or equal to the subsequent key.
/// (In an optimal representation, it is *strictly* less.)
typedef std::map<off64_t, off64_t> Territory;

/// A registry of represented area charts of the source media.
/// Note: if "laning" is implemented, affinity needs to include both dev_t AND the lane.
typedef std::map<med_id, Territory> Planetary;

/// A registry of source media.
typedef Map<med_id, Medium> DevMedia;

/// A zero-filled standard device sector.
typedef Data<uint8_t, 512> PhysicalSector; // deduplicate with MAPPER_BS, SECTOR_SZ etc.

/// Arithmetic of extent placement on the *target* ("origami") device.
/// Will account for laning when laning is in.
struct Colonies //: public Blocks // make output of optimize()
{
    /// Offset of the source extent within the target device.
    off64_t withinDisk( const Extent & xt ) const;

    /// Offset of the source extent within the file area of the target device.
    off64_t withinArea( const Extent & xt ) const;

    // everything in areaOffset reference frame divides by...
    // blksize_t blockSize() const override { return targetBlkSz; }

    // blksize_t targetBlkSz;
    off64_t areaOffset;
    // will include laning
    Planetary plan;
};

/// Set all bits above the lowest "1" bit. Used to validate granularity.
blksize_t AsLowerBound( blksize_t mask );

/// Set all bits below the highest "1" bit.
blksize_t AsUpperBound( blksize_t mask );

struct Geometry
{
    /// Registers the Extent list in the source area charts, updating the granularity mask.
    void chart( const ExtentList & extents );

    /// Registers the provided Extent in the source area charts.
    void chart( const Extent & extent );

    /// Merges extents in the provided area chart, connecting extents separated by gaps
    /// smaller than or equal to tolerance. Needed to reduce kernel load on mapping virtual
    /// "origami devices" combined of too many extents.
    static void MergeExtents( Territory & extents, off64_t tolerance );

    /// Count the number of extents having distinct sub-clusterSz offsets ("lane" analysis).
    static std::map<off64_t, size_t> BreakByLanes( const Territory & extents, off64_t clusterSz );

    /// Return the total area occupied by the extents on an extent chart.
    static off64_t TotalLength( const Territory & extents );

    /// Return the total area occupied by all extents represented.
    off64_t totalLength() const;

    /// Identify the granularity of the represented extents (largest possible block size).
    blksize_t granularity( blksize_t mapperBlock = Blocks::MAPPER_BS ) const;

    /// Display the "laning" efficiency metrics.
    void analyze( blksize_t targetBlkSz ); // must have an oput (cost estimate)

    /// Merge adjacent and close extents maintaining the provided block size.
    void optimize( blksize_t targetBlkSz ); // must have an oput (Colonies?)

    /// By "write files", all file extents have been accommodated in Planetary plan,
    /// and all overlapping, contacting and neighboring ranges have been merged.
    /// At this point, Territory::upper_bound-1 (plus translation) converts the source
    /// extent offset into the target extent offset 1:1 without considering length.
    /// The method appends all the registered extents to the provided IAppend target.
    /// The return value of this method is the location mapping from input to output.
    Colonies writeFiles( IAppend & out, blksize_t blkSz ) const; // move to Colonies!
    /// Shorthand exploiting the fact that Planner is aware of its block size (IAppend is not).
    Colonies writeFiles( Planner & out ) const { return writeFiles( out, out.blockSize() ); }

    off64_t gap = 0;
    DevMedia dMap;
    Planetary plan;
    blksize_t mask = 0;

private: // experimental
    void analyze( blksize_t blkSz, const Territory & extents, blksize_t targetBlkSz, off64_t net ) const;
};

#endif // DEVICE_H
