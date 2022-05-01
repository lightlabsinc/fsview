/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "impl/volume.h"

bool Original::useEntry( const RawDirEnt * entry ) const { return allowName( entry->d_name ); }

void Original::onFolder( PathEntry * folder )
{
    pathTable.push_back( folder );
    folder->traverse();
    folder->closeFd();
}

void Original::onFileFd( FileEntry * fEntry )
{
    fileTable.push_back( fEntry );
    // fchmod( lastFd, S_IRUSR | S_IRGRP | S_IROTH ); etc.
    chart( layout[fEntry] = locator->resolve( *fEntry ) );
}

void Volume::bookSpace( bool scratch, bool scrooge, off64_t extra )
{
    _scratch = scratch;
    _scrooge = scrooge;
    xtraRoom = extra;
}

void Volume::setTitles( const char * system, const char * volume )
{
    std::string s = system;
    for( auto & c : s ) { EnsureD<char>( c ); }

    std::string v = volume;
    for( auto & c : v ) { EnsureD<char>( c ); }

    setLabels( s.c_str(), v.c_str() );
}

void Volume::represent( Original & tree, Ptr<Burner> outImage, Ptr<Burner> tmpImage )
{
    // Note that the MergeExtents optimization is only possible
    // if the block size on the source device meets or exceeds
    // the block size on the CD. Blocks of 1K or less (i.e. the
    // 512-byte block of a typical legacy storage) won't fit in
    // the 2K-block CD layout. In that case, we combine extents
    // into single-extent file records on the target CD device.
    // THAT LOGIC NEEDS TO BE IMPLEMENTED AS A SEPARATE STRATEGY

    adjust( tree, *outImage, *tmpImage );
    tree.optimize( blockSize() );
    adjustSlaves( tree, *outImage, *tmpImage );

    Planner outPlanner( outImage );
    Planner tmpPlanner( tmpImage );

    outPlanner.requestBlockSize( blockSize() );

    // planReserved is called from within plan():
    // the slave cannot request space from master
    Colonies srcToTrg = plan( tree, outPlanner, tmpPlanner );
    planComplete( tree, outPlanner, tmpPlanner, srcToTrg );

    // this is generic converter phase again
    tmpPlanner.commit();
    outPlanner.commit();

    // trim(); // TODO!
}

void Volume::adjust( const Original & tree, const Medium & outImage, const Medium & tmpImage )
{
    // decide without hint. optimize as we wish.
    // TODO check std::max( outImage.blockSize(), tmpImage.blockSize() ) ) for regular ones
    blksize_t inMask = tree.granularity();
    blksize_t myMask = sizeRange();
    blksize_t mask = inMask & myMask;
    if( !mask )
    {
        printf( "Tree too granular for FS to support: in %lx & out %lx\n", inMask, myMask );
        abort();
    }
    blksize_t want = blockSize(); // default or user-provided
    if( !want )
    {
        want = std::max( outImage.blockSize(),
                         tmpImage.blockSize() );
    }
    if( !want )
    {
        // just keep it natural
        want = sysconf(_SC_PAGESIZE);
    }
    want = std::max( want, Blocks::MAPPER_BS );

    blksize_t size = ( want & mask ) ? want
                     : want > mask
                     ? ( mask & ~( mask >> 1 ) ) // highest mask bit
                     : ( mask & ~( mask << 1 ) ); // lowest mask bit

    setBlockSize( size );
}

blksize_t Volume::getHint( Original & tree, const Medium & outImage, const Medium & tmpImage )
{
    return hybrid ? hybrid->blkSzHint( tree, outImage, tmpImage ) : 0;
}

void Volume::adjustSlaves( Original & tree, const Medium & outImage, const Medium & tmpImage )
{
    if( hybrid ) { hybrid->masterAdjusted( tree, outImage, tmpImage, blockSize() ); }
}

void Volume::planReserved( const Original & tree, Planner & outPlanner, Planner & tmpPlanner, off64_t cap )
{
    off64_t cur = outPlanner.offset();
    if( hybrid )
    {
        hybrid->masterReserved( tree, outPlanner, tmpPlanner, cap );
    }
    off64_t len = outPlanner.offset() - cur;
    if( len < cap )
    {
        outPlanner.append( ZeroExtent( cap - len ) );
    }
    else if( len > cap )
    {
        printf( "Breach of trust: allowed %lx written %lx", cap, len );
        abort();
    }
}

void Volume::planComplete( Original & tree, Planner & outPlanner, Planner & tmpPlanner, const Colonies & srcToTrg )
{
    if( hybrid ) { hybrid->masterComplete( tree, outPlanner, tmpPlanner, srcToTrg ); }
}
