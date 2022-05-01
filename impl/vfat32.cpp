/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "impl/vfat32.h"
#include "impl/datetm.h"
#include "impl/unique.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#define FSVIEW_FAT32_VECTORIZED_FILL 1
#define FSVIEW_FAT32_VECTORIZED_FILL_NEON 1
#elif defined(__SSE2__)
#include <immintrin.h>
#define FSVIEW_FAT32_VECTORIZED_FILL 1
#define FSVIEW_FAT32_VECTORIZED_FILL_SSE2 1
#endif

namespace
{
#if FSVIEW_FAT32_VECTORIZED_FILL_NEON
using ClusterVec = uint32x4_t;
#elif FSVIEW_FAT32_VECTORIZED_FILL_SSE2
using ClusterVec = __m128i;
#else
using ClusterVec = uint32_t;
#endif
}

namespace VF
{

static constexpr const uint32_t ENDMARK = 0x0fffffffU;
static constexpr const off64_t MAXCHUNK = 1 << 18; // 262144 bytes
static constexpr const blkcnt_t RESERVED_HEADER_BYTES = 0x800; // a minimum of 4 sectors
static constexpr const blkcnt_t MINIMUM_SEED_CLUSTERS = 2; // hard 2 breaks on small clusters
static constexpr const blkcnt_t SEEDCLS = 2;

void FatTime::setTm( const tm & t, int )
{
    bits.bsecs = t.tm_sec >> 1;
    bits.mins  = t.tm_min;
    bits.hours = t.tm_hour;
}

void FatDate::setTm( const tm & t, int )
{
    bits.mday = t.tm_mday; // 1-31
    bits.month = t.tm_mon + 1;
    bits.year = t.tm_year + 1900 - 1980;
}

uint8_t DirectoryEntry::checksum() const
{
    uint8_t * ptr = ( uint8_t * ) baseName.data;
    uint8_t crc = 0;

    while( ptr < &attrs ) { crc = ( ( crc & 1 ) << 7 ) + ( crc >> 1 ) + *ptr++; }
    return crc;
}

void DirectoryEntry::setStartCluster( blkcnt_t cluster )
{
    hiClusterId = ( cluster >> 16 ) & 0xfff; // three "f"s
    loClusterId = cluster & 0xffff; // four "f"s
}

void DirectoryEntry::setCTime( const timespec & time )
{
    SetTime( time, [this]( struct tm & t, int centis )
    {
        FatDate dt, tm;
        dt.setTm( t, centis );
        tm.setTm( t, centis );
        csecs = centis + 100 * ( t.tm_sec & 1 );
        cdate = dt.hword;
        ctime = dt.hword;
    } );
}

void DirectoryEntry::setMTime( const timespec & time )
{
    SetTime( time, [this]( struct tm & t, int centis )
    {
        FatDate dt, tm;
        dt.setTm( t, centis );
        tm.setTm( t, centis );
        mdate = dt.hword;
        mtime = tm.hword;
    } );
}

void DirectoryEntry::setATime( const timespec & time )
{
    SetTime( time, [this]( struct tm & t, int centis )
    {
        FatDate dt;
        dt.setTm( t, centis );
        adate = dt.hword;
    } );
}

void DirectoryEntry::setStat( const struct stat64 & stat )
{
    setATime( stat.st_atim );
    setMTime( stat.st_mtim );
    setCTime( stat.st_ctim );
    fileSize = attrs & Folder ? 0 : stat.st_size;
}

void DirectoryEntry::markDir() { attrs |= Folder; fileSize = 0; }

void LongNameEntry::copyIn( std::string & temp, size_t seq )
{
    const char * chunk = temp.data() + ( seq - 1 ) * SLICE_SZ;
    const char * chend = temp.data() + temp.size();
    memcpy( buf1, chunk, sizeof( buf1 ) );
    memcpy( buf2, chunk + sizeof( buf1 ), sizeof( buf2 ) );
    memcpy( buf3, chunk + sizeof( buf1 ) + sizeof( buf2 ), sizeof( buf3 ) );
    if( chunk + SLICE_SZ == chend ) { seq |= 0x40; } // first aka last
    else if( chunk + SLICE_SZ > chend ) { abort(); } // array overflow
    seqNo = seq;
}

size_t LongNameEntry::scatterUcs2( std::string & temp, const Unicode & in )
{
    CharLFN rule; // FIXME use endianness primitives explicitly (UCS2-LE, UCS2-BE)
    rule.compress( temp, in );
    size_t remainder = temp.size() % SLICE_SZ;
    if( remainder )
    {
        // terminate with 0x0000
        if( remainder++ < SLICE_SZ ) { temp.push_back( 0 ); }
        if( remainder++ < SLICE_SZ ) { temp.push_back( 0 ); }
        // pad with 0xffff
        while( remainder++ < SLICE_SZ )
        { temp.push_back( 0xff ); }
    }
    return temp.size() / SLICE_SZ;
}

// accelerated FAT writing. we may want to have multiple implementations
// of the star dust medium, such as vectorized and scalar
void VFatMedium::reserve( blkcnt_t blockCount )
{
    _total_length = blockCount * sizeof( uint32_t );
    _chunk_size = std::min( roundUp( _total_length ), MAXCHUNK );
}

void VFatMedium::setLine( blkcnt_t curFirst, blkcnt_t curLast )
{
    if( _favor_freespace )
    {
        // printf( "%lu ... %lu\n", curFirst, curLast );
        for( auto blk = curFirst; blk < curLast; )
        {
            // this seems grossly inefficient, but that's the point of
            // sparse partitions: free space is cheap, while files are
            // scarce and don't impose much burden either.
            off64_t offset = blk * sizeof( uint32_t );
            amendments[offset] = Store<LSB<uint32_t>>( offset, ++blk );
        }
    }
    else { shadow( curFirst ); } // terminate the chain that leads here
}

void VFatMedium::shadow( blkcnt_t curFirst )
{
    if( curFirst <= SEEDCLS /*2*/ ) { return; }
    off64_t offset = ( curFirst - 1 ) * sizeof( uint32_t );
    if( amendments.find( offset ) == amendments.end() )
    { amendments[offset] = Store<LSB<uint32_t>>( offset, ENDMARK ); }
}

void VFatMedium::setNext( blkcnt_t lastPrev, blkcnt_t firstNext )
{
    off64_t offset = lastPrev * sizeof( uint32_t );
    if( offset >= _total_length )
    {
        printf( "Amendment %lx outside reserved area %lx\n",
                offset, _total_length );
        abort();
    }
    amendments[offset] = Store<LSB<uint32_t>>( offset, firstNext );
}

void VFatMedium::setLast( blkcnt_t lastLast )
{
    setNext( lastLast, ENDMARK );
}

blksize_t VFatMedium::blockSize() const { return sizeof( ClusterVec ); }

void VFatMedium::fill( void * chunk, off64_t offset, size_t size ) const
{
    if( _favor_freespace )
    {
        if( !offset )
        {
            memset( chunk, 0, size );
        }
        return;
    }
    // 01 00 00 00 02 00 00 00  03 00 00 00 04 00 00 00
    ClusterVec * optr = ( ClusterVec * ) chunk;
    uint32_t startVal = offset / sizeof( uint32_t );
#if FSVIEW_FAT32_VECTORIZED_FILL
    uint32_t first[4] = { 1+startVal, 2+startVal, 3+startVal, 4+startVal };
    uint32_t wordStep = 4;
#if FSVIEW_FAT32_VECTORIZED_FILL_NEON
    ClusterVec quad = vld1q_u32( first );
    ClusterVec incr = vld1q_dup_u32( &wordStep );
#elif FSVIEW_FAT32_VECTORIZED_FILL_SSE2
    ClusterVec quad = _mm_loadu_si128( reinterpret_cast<const ClusterVec*>( first ) );
    ClusterVec incr = _mm_set1_epi32( wordStep );
#endif
#endif
    ClusterVec * last = ( ClusterVec * )( ( char * ) chunk + size );
    while( optr < last )
    {
#if FSVIEW_FAT32_VECTORIZED_FILL_NEON
        vst1q_u32( reinterpret_cast<unsigned int *>( optr++ ), quad );
        quad = vaddq_u32( quad, incr );
#elif FSVIEW_FAT32_VECTORIZED_FILL_SSE2
        _mm_storeu_si128( optr++, quad );
        quad = _mm_add_epi32( quad, incr );
#else // neither, scalar
        *optr++ = ++startVal;
#endif
    };
}

void VFat32Out::setLabels( const char * system, const char * volume )
{
    _vol.oemName = system; // revert to "MSDOS5.0" if unrecognized
    _vol.volName = volume;

    uint32_t crc = Crc32( volume );
    memcpy( _vol.volumeId.data, &crc, sizeof( uint32_t ) );
}

blkcnt_t VFat32Out::clusterCount( const Original & tree ) const
{
    blksize_t blkSize = blockSize();
    printf( "%lu bytes per cluster\n", blkSize );
    off64_t footprint = tree.totalLength(); // already rounded up
    printf( "%lu bytes used by files only\n", footprint );
    size_t entryCount = tree.fileTable.size() + tree.pathTable.size() * 4; // this, parent, this in parent, empty
    footprint += entryCount * sizeof( DirectoryEntry );
    footprint += blkSize * tree.pathTable.size(); // reserved for initial clusters of directories
    printf( "%lu bytes used by files and folders (estimate)\n", footprint );
    footprint += roundUp( xtraRoom ); // this is where we reserve free space
    // footprint += footprint / 2; // bump above the questionable 64K...
    footprint = roundUp( footprint );
    printf( "%lu bytes used by files, folders and free space\n", footprint );
    // Microsoft's EFI FAT32 specification[9] states that any FAT file system with less than 4085 clusters is FAT12,
    // else any FAT file system with less than 65525 clusters is FAT16, and otherwise it is FAT32.
    return std::max( ( blkcnt_t )( footprint / blkSize + SEEDCLS ), static_cast<blkcnt_t>( 65537UL ) ); // (65525 according to MSFT)
}

Colonies VFat32Out::plan( const Original & tree, Planner & outPlanner, Planner & tmpPlanner )
{
    // in fact, planHeaders could start here..

    // let's estimate the size of the FAT here
    // FIXME actually, we don't need this.
    // - clusters in the FAT refer to files and folders following the FAT.
    // - we can, therefore, use offsets resolved via srcToTrg as clusters.
    blkcnt_t blkCount = clusterCount( tree ); // FIXME property of layout
    // blkCount += blkCount >> 2;
    blkCount = roundUp( blkCount, blockSize() / CLUSTER_LINK_SIZE );
    off64_t fat32size = blkCount * CLUSTER_LINK_SIZE;
    _vol.fatScc = fat32size / _vol.secSz;
    printf( "FAT32 size: %lu (%lu) All: %lu\n", fat32size, _vol.fatScc, blkCount * _vol.clusterScc );
    _vol.allScc = blkCount * _vol.clusterScc;
    _sec.nextFreeCluster = blkCount - 1;
    // _sec.{lkn*, next*} kept original (ctor-assigned), only _scratch modifies them
    printf( "Reserving %lu bytes to accommodate a FAT of %lu clusters\n", fat32size, blkCount );

    Ptr<VFatMedium> faTable = New<VFatMedium>( _scratch );
    faTable->reserve( blkCount );
    struct __attribute( ( packed ) )
    {
        uint8_t data[8] = { 0xf8, // f8 is media
                            0xff, 0xff,
                            0x0f, // 0 is reserved
                            0xff, 0xff, 0xff, 0xff
                          };
    } bitflags;
    faTable->amendments[0] = Later::Store( 0, bitflags );
    if( !_scratch ) { faTable->setLast( blkCount - 1 ); }

    planHeaders( tmpPlanner ); // autopad inside
    outPlanner.append( tmpPlanner.wrapToGo( 0 ) ); // append headers
    // FAT copies. no autopad is needed here because FATs are already padded: see blkCount above
    Extent fat = tmpPlanner.wrapToGo( tmpPlanner.append( Extent( 0, fat32size, faTable ) ) );
    for( size_t i = 0; i < fatCount(); ++i ) { outPlanner.append( fat ); } // - map the FAT N times
    printf( "After reserved and FAT written: %lu\n", outPlanner.offset() ); // checkpoint

    // - files (constellations 1:1)
    Colonies srcToTrg = tree.writeFiles( outPlanner );
    srcToTrg.areaOffset -= SEEDCLS * blockSize(); // *** step two clusters back
    // WISDOM empty files with size 0 should have first cluster 0.
    printf( "After files written: %lu\n", outPlanner.offset() ); // checkpoint

    // - FAT - let's incise files here. directories are simpler (because contiguous).
    for( auto & lao : tree.layout )
    {
        // TODO own the below code within faTable ("setChain")
        const ExtentList & xl = lao.second;
        auto itr = xl.rbegin();
        if( itr != xl.rend() )
        {
            Extent curr = *itr;
            curr.offset = srcToTrg.withinArea( curr ); // FIXME amend cluster ID here, not in devices.cpp!
            // printf( "Finishing %lx+%lx\n", curr.offset, curr.length );
            blkcnt_t first = firstBlk( curr ), last = lastBlk( curr );
            faTable->setLine( first, last );
            faTable->setLast( last );
            // return point
            while( ++itr != xl.rend() )
            {
                Extent past = *itr;
                past.offset = srcToTrg.withinArea( past );
                // printf( "Linking %lx+%lx to %lx+%lx\n", past.offset, past.length, curr.offset, curr.length );
                first = firstBlk( past ), last = lastBlk( past );
                faTable->setLine( first, last );
                faTable->setNext( last, firstBlk( curr ) );
                curr = past;
            }
        }
    }

    // - directories, referencing files (leaf to root)
    off64_t outerOff = outPlanner.offset();
    off64_t innerOff = tmpPlanner.offset();
    off64_t tmpToOut = outerOff - innerOff;
    off64_t tmpToFat = tmpToOut - srcToTrg.areaOffset;

    FatVolRule rule;
    CharANSI pack;

    // similar to CDFS, slightly simpler
    std::map<PathEntry *, std::list<Later::Use>> parents;
    const blksize_t blkSz = blockSize();
    std::map<Entry *, Extent> dirLayout;

    for( auto itr = tree.pathTable.rbegin(); itr != tree.pathTable.rend(); ++itr )
    {
        PathEntry * pDir = *itr;
        auto dirOffset = tmpPlanner.offset() + tmpToFat;
        auto dirClustr = firstBlk( dirOffset );

        // see respective code in CDFS
        Ptr<Burner> dirBurner = New<VectBurner>( blkSz ); // New<TempBurner>(blkSz)
        dirBurner->reserve( blkSz );

        if( pDir->parent )
        {
            DirectoryEntry dot;

            // dot
            dot.baseName.data[0] = '.';
            dot.setStartCluster( dirClustr );
            dot.setStat( pDir->stat );
            dot.markDir();
            dirBurner->append( TempExtent<DirectoryEntry>( dot ) ); // all done for dot

            // dotdot
            PathEntry * parent = pDir->parent;
            dot.baseName.data[1] = '.';
            dot.setStat( parent->stat );
            off64_t parentOffset = dirBurner->append( TempExtent<DirectoryEntry>( dot ) );
            // TODO see comment about hiding Later::Store
            parents[parent].push_back( Later::Store<DirectoryEntry>( DtOf( dirBurner ),
                                                                     parentOffset, dot,
                                                                     [this]( DirectoryEntry & lfield, const Range & range )
            {
                lfield.setStartCluster( firstBlk( range.offset ) );
            } ) );
        }
        else
        {
            // volume label. extract as setter or ctor!
            DirectoryEntry vol;
            static_assert( sizeof( vol.baseName ) + sizeof( vol.typeName )
                           == sizeof( _vol.volName ), "Volume label size" );
            memcpy( vol.baseName.data, _vol.volName.data, sizeof( _vol.volName ) );
            vol.attrs = Attribute::Label;
            struct timespec ts;
            clock_gettime( CLOCK_REALTIME, &ts );
            vol.setMTime( ts );
            vol.setStartCluster( 0 );
            dirBurner->append( TempExtent<DirectoryEntry>( vol ) ); // all done for vol

            _vol.rootCl = dirClustr;
        }

        // (loop, loop)
        for( Ptr<Entry> pEnt : pDir->entries )
        {
            DirectoryEntry sub;
            if( pEnt->isFile() )
            {
                const ExtentList & xl = tree.layout.at( pEnt.get() );
                if( xl.size() )
                {
                    auto head = srcToTrg.withinArea( xl.front() );
                    sub.setStartCluster( firstBlk( head ) );
                }
                // else { sub.setStartCluster( 0 ); } // it's already 0>
            }
            else
            {
                sub.setStartCluster( firstBlk( dirLayout.at( pEnt.get() ) ) );
                sub.markDir();
            }
            sub.setStat( pEnt->stat );

            UniqName name( pEnt->decoded, true );
            rule.translit( name );
            rule.mixInVar( name, 0 );
            rule.decorate( name );
            if( name.conv == pEnt->decoded )
            {
                // create a short name
                uint8_t size;
                char * out;
                // TODO make a method in CharPack to accept Text<> directly, provide size&
                out = sub.baseName.data;
                size = sizeof( sub.baseName.data );
                auto citr = name.conv.cbegin();
                pack.compress( size, out, citr, citr + name.sep( 0 ) );
                if( name.seps.size() > 0 )
                {
                    out = sub.typeName.data;
                    size = sizeof( sub.typeName.data );
                    auto citr = name.conv.cbegin();
                    auto nitr = citr + name.sep( 0 ) + 1;
                    pack.compress( size, out, nitr, citr + name.sep( 1 ) );
                }
            }
            else
            {
                // create a bad short name and bag of long name entries
                off64_t numb = dirBurner->offset();
                auto & buf = sub.baseName.data;
                buf[0] = ' '; buf[1] = '\0';
                for( size_t i = 2; i < sizeof( buf ); ++i )
                { buf[i] = numb % 23; numb /= 7; }
                std::string actualName;
                auto seq = LongNameEntry::scatterUcs2( actualName, pEnt->decoded );
                LongNameEntry lfne;
                lfne.crc = sub.checksum();
                do
                {
                    lfne.copyIn( actualName, seq );
                    dirBurner->append( TempExtent<LongNameEntry>( lfne ) );
                }
                while( --seq );
            }
            dirBurner->append( TempExtent<DirectoryEntry>( sub ) );
        }
        dirBurner->append( ZeroExtent( sizeof( DirectoryEntry ) ) );

        // commit folder
        tmpPlanner.append( WrapToGo( dirBurner ) ); // directory extent roundup
        Extent ownExtent = Extent( dirOffset, dirBurner->offset(), dirBurner );
        blkcnt_t first = firstBlk( ownExtent ), last = lastBlk( ownExtent );
        faTable->setLine( first, last );
        faTable->setLast( last );
        dirLayout[pDir] = ownExtent;

        // propagate to children. DRY common code w/ CDFS!
        auto catchup = parents.find( pDir );
        if( catchup != parents.end() )
        {
            for( auto & use : catchup->second ) { use( ownExtent ); }
            //1// parents.erase( catchup ); // MOREINFO will it speed up lookup?
        }
    }

    // TODO allow extent conversion if a real file is appended; make it a fallback
    outPlanner.append( tmpPlanner.wrapToGo( innerOff ) );
    outPlanner.autoPad(); // effectively a no-op because dm blocks <= zram blocks

    // consume remaining space claimed by the FAT
    // auto nextAlloc = firstBlk( outPlanner.offset() );
    srcToTrg.areaOffset += SEEDCLS * blockSize(); // *** revert "two clusters back"
    auto endOffset = outPlanner.offset() - srcToTrg.areaOffset;
    srcToTrg.areaOffset -= SEEDCLS * blockSize(); // *** restore "two clusters back"
    auto maxOffset = blkCount * blockSize();
    auto extraRoom = maxOffset - endOffset;
    printf( "Real: %lx Area: %lx Claimed area: %lx\n", outPlanner.offset(), endOffset, maxOffset );
    if( extraRoom > 0 ) { outPlanner.append( ZeroExtent( extraRoom ) ); }
    else if( extraRoom ) { printf( "FAT underflow\n" ); abort(); }

    if( _scratch )
    {
        _sec.nextFreeCluster = endOffset / blockSize() + SEEDCLS;
        _sec.lknFreeClusters = extraRoom / blockSize();
    }

    return srcToTrg;
}

off64_t VFat32Out::planHeaders( IAppend & planner )
{
    off64_t cur = planner.offset();
    planner.append( TempExtent( _vol ) );
    planner.padTo( MAPPER_BS );
    planner.append( TempExtent( _sec ) );
    planner.append( ZeroExtent( offsetof( SummarySec, sig2 ) ) ); // 13fc
    planner.append( TempExtent( _sec.sig2 ) );
    planner.padTo( blockSize() );
    planner.append( ZeroExtent( offsetof( SummarySec, sig2 ) ) ); // 21fc
    planner.append( TempExtent( _sec.sig2 ) );
    planner.padTo( blockSize() );
    _vol.reservedScc = planner.offset() / MAPPER_BS;
    return cur;
}

}//namespace VF
