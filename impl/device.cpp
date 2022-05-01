/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "device.h"

#include "conf/config.h"
#include "impl/mapper.h"
#include "impl/burner.h"

blksize_t AsLowerBound( blksize_t mask )
{
    mask |= mask << ( 1 << 0 );
    mask |= mask << ( 1 << 1 );
    mask |= mask << ( 1 << 2 );
    mask |= mask << ( 1 << 3 );
    mask |= mask << ( 1 << 4 );
    mask |= mask << ( 1 << 5 );
    return mask;
}

blksize_t AsUpperBound( blksize_t mask )
{
    mask |= mask >> ( 1 << 0 );
    mask |= mask >> ( 1 << 1 );
    mask |= mask >> ( 1 << 2 );
    mask |= mask >> ( 1 << 3 );
    mask |= mask >> ( 1 << 4 );
    mask |= mask >> ( 1 << 5 );
    return mask;
}

Ptr<DiskMedium> DeviceMap::surface( dev_t device, blksize_t blkSize )
{
    Ptr<DiskMedium> & out = media[device];
    // substitute[device] must be defined
    return out || ( out = New<DiskMedium>( substitute[device], blkSize ) ), out;
}

size_t ExtentIoc::S( int extents ) { return sizeof( struct fiemap ) + extents * sizeof( struct fiemap_extent ); }

ExtentIoc::ExtentIoc() : extCount( 1 ), fem( ( struct fiemap * ) malloc( S( extCount ) ) )
{
    fem->fm_start = 0;
    fem->fm_flags = 0; // FIEMAP_FLAG_CACHE? rly needed?
}

ExtentIoc::ExtentIoc( MkfsConf & cfg ) : ExtentIoc() // pull the substitution map
{
    // untested! may need read+write access.
    std::map<std::string, dev_t> virtNames;
    if( cfg.dmControl )
    {
        Mapper mapper( cfg.dmControl, true, Blocks::MAPPER_BS );
        mapper.listDevices( virtNames );

        // don't include the disk being built in the source list!
        // MOREINFO take another look when config is simplified..
        if( cfg.isTargetMapped() )
        {
            virtNames.erase( std::string( cfg.target ) );
        }
    }

    struct stat64 st;
    dev_t major, minor;
    cfg.mapDevices( [&]( const char * devName )
    {
        if( !devName || !*devName )
        { printf( "Device name not defined\n" ); abort(); }
        else { printf( "Mapping device name: %s\n", devName ); }

        if( sscanf( devName, "%lx:%lx", &major, &minor ) == 2 )
        {
            return ( dev_t ) makedev( major, minor );
        };
        if( ( devName[0] == '/' ) && ( stat64( devName, &st ) >= 0 ) )
        {
            return ( dev_t ) st.st_rdev;
        }
        return virtNames.at( std::string( devName ) ); // fails if not found
    },
    [&]( dev_t found, dev_t used )
    {
        subst( found, used );
    } );
}

void ExtentIoc::reserve( size_t newCount )
{
    if( newCount > extCount )
    {
        fem = ( struct fiemap * ) realloc( fem, S( newCount ) );
        extCount = newCount;
    }
}

ExtentList ExtentIoc::resolve( const Extent & source )
{
    return peek( source, Correction::Naive );
}

ExtentList ExtentIoc::peek( const Extent & source, Correction co )
{
    ExtentList xList;

    Ptr<DiskMedium> medium = surface( source.medium->blockDevice(),
                                      source.medium->blockSize() );
    fem->fm_start = source.offset;
    fem->fm_length = source.length;
    fem->fm_extent_count = 0;
    fem->fm_flags = ( co == Correction::Fsync ) ? FIEMAP_FLAG_SYNC : 0;
    off64_t budget = fosterHouse.get() ? adoptionBudget : -1;
    int fd = source.medium->fd();
    if( ioctl( fd, FS_IOC_FIEMAP, fem ) >= 0 )
    {
        reserve( fem->fm_extent_count = fem->fm_mapped_extents );
        if( ioctl( source.medium->fd(), FS_IOC_FIEMAP, fem ) >= 0 )
        {
            for( size_t extNo = 0; extNo < fem->fm_mapped_extents; extNo++ )
            {
                struct fiemap_extent & rawx = fem->fm_extents[extNo];

                bool cantMap = false;

                // Need to synchronize:
                // FIEMAP_EXTENT_UNKNOWN [covers FIEMAP_EXTENT_DELALLOC or drive unavailable (can't be)]
                if( rawx.fe_flags & FIEMAP_EXTENT_UNKNOWN )
                {
                    if( co != Correction::Fsync )
                    {
                        return peek( source, Correction::Fsync );
                    }

                    fprintf( stderr, "Logical extent %lx+%lx unallocated - fsync failed\n",
                             ( off64_t ) rawx.fe_logical,
                             ( off64_t ) rawx.fe_length );
                    cantMap = true;
                }

                // Need to copy out (kernel 4.x, newer EXT4):
                // FIEMAP_EXTENT_ENCODED [covers FIEMAP_EXTENT_DATA_ENCRYPTED]
                // FIEMAP_EXTENT_NOT_ALIGNED [covers FIEMAP_EXTENT_DATA_INLINE, FIEMAP_EXTENT_DATA_TAIL]
                if( rawx.fe_flags & ( FIEMAP_EXTENT_ENCODED | FIEMAP_EXTENT_NOT_ALIGNED ) )
                {
                    fprintf( stderr, "Logical extent %lx+%lx inlined or encoded\n",
                             ( off64_t ) rawx.fe_logical,
                             ( off64_t ) rawx.fe_length );
                    cantMap = true;
                }

                // cantMap |= !( rand() & 0x7 ); // uncomment to test unresolved mapping protection

                if( cantMap )
                {
                    off64_t offset = fosterHouse.get() ? fosterHouse->offset() : 0;
                    if( offset + ( off64_t ) rawx.fe_length <= budget )
                    {
                        // transfer the file to a temporary storage and expose the temporary medium
                        Extent logical( rawx.fe_logical, rawx.fe_length, source.medium );
                        Extent wrapped = fosterHouse->wrapToGo( fosterHouse->append( logical ) );
                        xList.emplace_back( wrapped );
                        continue;
                    }
                    else
                    {
                        fprintf( stderr, "*** Adoption budget exceeded! %lx+%lx<%lx\n",
                                 offset, ( off64_t ) rawx.fe_length, budget );

                        // expose a zero medium instead of the (insecure!) zero offset
                        Extent blank( 0, rawx.fe_length, New<ZeroMedium>() );
                        xList.emplace_back( blank );
                        continue;
                    }
                }

                // Need to wait until the data have flushed:
                // FIEMAP_EXTENT_UNWRITTEN
                if( rawx.fe_flags & FIEMAP_EXTENT_UNWRITTEN )
                {
                    // don't wait here. put the fd in the waitlog.
                    // TODO: review the waitlog once the rest is done
                    fprintf( stderr, "Physical extent %lx+%lx not yet written\n",
                             ( off64_t ) rawx.fe_physical,
                             ( off64_t ) rawx.fe_length );
                    waitlog.push_back( fd );
                }

                Extent extent( rawx.fe_physical, rawx.fe_length, medium );
                xList.emplace_back( extent );
            }
        }
    }

    return xList;
}

void Geometry::chart( const ExtentList & extents )
{
    if( extents.empty() ) { return; }
    auto itr = extents.begin();
    while( true )
    {
        const Extent & extent = *itr;
        chart( extent );
        mask |= extent.offset;
        if( ++itr != extents.end() ) { mask |= extent.length; }
        else { break; }
    }
}

void Geometry::chart( const Extent & extent )
{
    auto medium = extent.medium;
    uintptr_t medId = medium->id();
    auto & ptr = dMap[medId];
    if( !ptr ) { ptr = medium; }
    Territory & terr = plan[medId];
    terr[extent.offset] = extent.offset + extent.length;
}

void Geometry::MergeExtents( Territory & extents, off64_t tolerance )
{
    auto itr = extents.begin();
    while( itr != extents.end() )
    {
        auto next = itr; ++next;
        while( next != extents.end() && next->first <= itr->second + tolerance )
            // && ( ( next->first - itr->first ) % clusterSize == 0 ) )
        {
            itr->second = next->second;
            next = extents.erase( next );
        }
        ++itr;
    }
}

std::map<off64_t, size_t> Geometry::BreakByLanes( const Territory & extents, off64_t clusterSz )
{
    std::map<off64_t, size_t> dist;
    auto itr = extents.begin();
    while( itr != extents.end() )
    {
        off64_t remainder = itr->first % clusterSz;
        auto dtr = dist.find( remainder );
        if( dtr == dist.end() )
        { dist[remainder] = 1; }
        else { dtr->second++; }
        ++itr;
    }
    return dist;
}

off64_t Geometry::TotalLength( const Territory & extents )
{
    return std::accumulate( extents.begin(),
                            extents.end(),
                            0L,
                            []( off64_t total, const Territory::value_type & range )
    {
        return total + range.second - range.first;
    } );
}

off64_t Geometry::totalLength() const
{
    return std::accumulate( plan.begin(), plan.end(), 0L, []( off64_t total, const Planetary::value_type & affinity )
    {
        return total + TotalLength( affinity.second );
    } );
}

blksize_t Geometry::granularity( blksize_t mapperBlock ) const
{
    // RETURNS:
    // Only the least significant bit in the reported bit mask matters.
    // The LSB establishes the upper bound on the file system block size.

    // CLIENT USE:
    // The physical media, in turn, establish the lower bound.
    // The absolute lower bound is Blocks::MAPPER_BS (the physical sector).

    // The outbound device burners establish the preferred value, but it turns out
    // not to matter much. The USB device driver seems to support block-unaligned
    // access properly. To avoid performance penalties, it's good enough to choose
    // larger default block sizes, as we do with fsview_temp.

    // Of course the burner block size needs to be taken into account when the FS
    // volume writer switches between the two. It's being done manually for now.

    // So here is the adjustment algorithm:
    // 1. Identify the allowed range:
    // AT MOST the medium granularity
    // WITHIN range allowed by the FS
    // 2. Identify the desired value:
    // - u - provided by the caller
    // - o - matching max(out, tmp)
    // - max(u||o, physical_sector)
    // 3. Choose the bit closest to
    // ...the desired value.

    // CAVEATS:
    // We are not dealing here with sophisticated ways of reducing the granularity,
    // such as finding a single granular offset ("skew") or a plurality of granular
    // offsets ("lanes"). We assume that any unaligned offset or length invalidates
    // the alignment, whether a particular offset can be canceled out with another
    // offset similarly misaligned.

    for( auto & mapping : dMap )
    {
        auto & medium = mapping.second;
        if( medium->isAligned() )
        {
            blksize_t srcBlkSz = medium->blockSize();
            if( srcBlkSz < mapperBlock )
            {
                dev_t devId = medium->blockDevice();
                printf( "Device %u:%u has blocks of %lu less than mappable %lu\n", major( devId ), minor( devId ),
                        srcBlkSz, mapperBlock );
                abort();
            }
        }
    }

    return ~( AsLowerBound( mask ) << 1 );
}

void Geometry::analyze( blksize_t blkSz, const Territory & extents, blksize_t targetBlkSz, off64_t net ) const
{
    printf( "Remainder breakdown under a larger (%lu) cluster:\n", targetBlkSz );
    auto dist = BreakByLanes( extents, targetBlkSz );
    struct ExtentStat
    {
        size_t number;
        off64_t gross;
    };
    size_t multiextent = 0;
    off64_t multigross = 0;
    off64_t smallThres = 1 << 18; // [4] = { 1 << 16, 1 << 18, 1 << 20, 1 << 22, };
    size_t smallextent = 0;
    off64_t smallgross = 0;
    size_t equatorBars = 320;
    for( auto & sample : dist )
    {
        Territory lane;
        for( auto & extent : extents )
        { if( ( ( extent.first - sample.first ) % targetBlkSz ) == 0 ) { lane.insert( extent ); } }

        for( auto itr = lane.begin(); itr != lane.end(); )
        {
            auto & extent = *itr;
            if( extent.second - extent.first <= smallThres/*targetBlkSz*/ )
            {
                smallextent++;
                smallgross += Blocks::roundUp( extent.second - extent.first, targetBlkSz );
                itr = lane.erase( itr );
            }
            else { itr ++; }
        }

        MergeExtents( lane, 1L << 30 );
        off64_t gross = TotalLength( lane );
        printf( "%lu\tof\t%lu bytes, %lu cumulative\n", sample.second, sample.first, gross );
        multiextent += lane.size();
        multigross += gross;

        std::string longitude;
        longitude.resize( equatorBars, '.' );
        off64_t sliceSize = extents.rbegin()->second / ( equatorBars - 1 );
        for( auto & extent : lane )
        {
            size_t left = extent.first / sliceSize;
            size_t iright = ( extent.second - sliceSize / 2 ) / sliceSize;
            size_t eright = extent.second / sliceSize;
            while( left < iright ) { longitude[left++] = '#'; }
            if( longitude[eright] == '.' ) { longitude[eright] = '='; }
        }
        printf( "%s\n", longitude.c_str() );
    }
    off64_t fatPrint = net * sizeof( uint32_t ) / blkSz;
    printf( "Extents after laning: %lu ∑: %ld over{lap|flow}: %.1f%%\n"
            "Small extents (<%ld): %lu ∑: %ld M:%.0f\n"
            "Laning compaction ratio:%.1f%% +small extents eat %.1f%%\n\n",
            multiextent, multigross, 100.f * ( multigross - net ) / net,
            smallThres, smallextent, smallgross, 1.f * smallgross / smallextent,
            100.f * multigross / net * blkSz / targetBlkSz,
            100.f * smallgross / fatPrint );
}

void Geometry::analyze( blksize_t targetBlkSz )
{
    for( auto & presence : plan )
    {
        med_id medId = presence.first;
        if( dMap.at( medId )->isAligned() ) // don't analyze file media
        {
            blksize_t blkSz = dMap.at( medId )->blockSize();
            Territory & extents = presence.second;
            // the below, until *, is mere analysis
            off64_t net = TotalLength( extents ); // FIXME cache!!!
            analyze( blkSz, extents, targetBlkSz, net );
            // the above, until *, is mere analysis
        }
    }
}

void Geometry::optimize( blksize_t targetBlkSz )
{
    for( auto & presence : plan )
    {
        med_id medId = presence.first;
        blksize_t blkSz = dMap.at( medId )->blockSize();
        if( !dMap.at( medId )->isAligned() ) { continue; } // don't optimize uncharted files
        if( 0 ) // fixme enable when cost evaluation for laning moves in
        {
            if( blkSz < targetBlkSz )
            {
                printf( "Can't use extent merging on device %lx\n: block size %lu\n", medId, blkSz );
                continue;
            }
        }
        Territory & extents = presence.second;

        // this is the cut-off point, all the below goes to Territory
        off64_t net = TotalLength( extents );
        printf( "Extents before/pre merging: %lu\n\n", extents.size() );

        //        blksize_t gaps[] =
        //        {
        //            // keep it to 0 and within same file to allow larger clusters than local blocks
        //            0L, blkSz,
        //            1L << 20,
        //            1L << 22, 1L << 23, 1L << 24,
        //            1L << 25, 1L << 30, // TOOD optimize (multiply relative leak by range count?)
        //        };

        // for( blksize_t gap : gaps ) { ... }
        printf( "Merging extents with gap <= %lu\n", gap );
        MergeExtents( extents, gap );
        off64_t gross = TotalLength( extents );
        printf( "Extents after / past merging: %lu ∑: %ld leak: % .1f % % \n",
                extents.size(), gross, 100.f * ( gross - net ) / net );
    }
}

Colonies Geometry::writeFiles( IAppend & out, blksize_t blkSz ) const
{
    Colonies colonies;
    colonies.areaOffset = out.offset();
    for( auto & presence : plan )
    {
        med_id medId = presence.first;
        Ptr<Medium> surface = dMap.at( medId );
        const Territory & extents = presence.second; // this needs to be "laned"
        Territory & disk2Cd = colonies.plan[medId];
        for( auto & linearr : extents )
        {
            disk2Cd[linearr.first] = out.append( Extent( linearr.first, linearr.second - linearr.first, surface ) );
            out.padTo( blkSz ); // no-op in case of disk mapping
        }
    }
    return colonies;
}

off64_t Colonies::withinDisk( const Extent & xt ) const
{
    auto & src2trg = plan.at( xt.medium->id() );
    auto bridge = src2trg.upper_bound( xt.offset );
    --bridge; // first GT, or end() if all keys are LE; move to last LE
    return xt.offset - bridge->first + bridge->second;
}

off64_t Colonies::withinArea( const Extent & xt ) const
{
    return withinDisk( xt ) - areaOffset;
}
