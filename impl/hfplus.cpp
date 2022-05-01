/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "hfplus.h"

#include <set>

namespace HP
{

// B-tree population steps:
// - std::map<key, value> // ordered
// - write leaf nodes sequenfially | keep first keys
// - while (node_count > 1) {
// - // write index nodes in order | keep first keys
// - }
// - update the header node + mapping nodes

// NodeSpec: "Planner"
// size; List<Record>
//
// NodeBuffer: Burner
// NodeDescriptor, Record...> <offset...

char hfc_tag[] = "CLUSTERED HOT FILES B-TREE     ";

NodeSpec::NodeSpec( BTNodeKind kind ) : NodeSpec() { desc.kind = kind; }

NodeSpec::NodeSpec( BTNodeKind kind, uint8_t level ) : NodeSpec( kind ) { desc.height = level; }

size_t NodeSpec::freeSpace( blksize_t capacity, bool gross ) const
{ return capacity - size() - ( gross ? 0 : sizeof( NodeRecOff ) ); }

bool NodeSpec::fitsIn( blksize_t capacity, size_t recordSize ) const
{ return size() + recordSize + sizeof( NodeRecOff ) <= capacity; }

void NodeSpec::addRecord( const Extent & rec )
{
    recs.push_back( rec );
    offset += rec.length;
}

void NodeSpec::markRecord()
{
    offsets.insert( offsets.begin(), offset );
    desc.numRecords = desc.numRecords + 1;
}

void NodeSpec::addRecord( const ExtentList & xl )
{
    for( auto & extent : xl ) { addRecord( extent ); }
}

void NodeSpec::addRecord( const Record & record )
{
    addRecord( record.asExtentList() );
}

void NodeSpec::addRecord( Ptr<Record> record )
{
    addRecord( *record );
}

void NodeSpec::writeTo( IAppend & out, blksize_t capacity ) const
{
    // assert out aligned to capacity
    off64_t cur = out.offset();
    if( cur % Blocks::MAPPER_BS ) { printf( "E out misaligned of=%lx\n", cur ); abort(); }
    // printf( "%lu extents %lu offsets\n", recs.size(), offsets.size() );
    for( auto & record : recs ) { out.append( record ); }
    out.append( ZeroExtent( capacity - size() ) );
    out.append( TempExtent( offsets.data(), offSize() ) );
    // for( auto & off : offsets ) { printf( "@%04lx ", cur + off ); } printf( "\n" );
    if( out.offset() % Blocks::MAPPER_BS ) { printf( "X out misaligned of=%lx\n", out.offset() ); abort(); }
}

void BTHeaderRec::tuneForCatalog()
{
    keyCompareType = BTreeKCType::KCBinaryCompare; // clear for extents overflow
    attributes = BTreeAttr::kBTBigKeysMask | BTreeAttr::kBTVariableIndexKeysMask; // clear for extents overflow
    nodeSize = 8 << 10;
    maxKeyLength = sizeof( HFSPlusCatalogKey ) - sizeof( NodeKeyLen ) + 255 * sizeof( UniChar ); //=516
}

void BTHeaderRec::tuneForOverflow()
{
    keyCompareType = BTreeKCType::KCUnused;
    attributes = BTreeAttr::kBTBigKeysMask;
    nodeSize = 4 << 10;
    maxKeyLength = kHFSPlusExtentKeyMaximumLength;
}

void HFSPlusForkData::setReserved()
{
    logicalSize = 0;
    totalBlocks = 0;
    clumpSize = 0;
}

void HFSPlusForkData::setExtent( const Extent & singleExtent, blksize_t blkSz )
{
    logicalSize = singleExtent.length;
    totalBlocks = ( logicalSize + blkSz - 1 ) / blkSz;
    clumpSize = blkSz;
    extents[0].set( singleExtent.offset / blkSz, totalBlocks );
}

void HFSPlusCatalogEntry::setTimes( const struct stat64 & st )
{
    // we renumerate nodes once a collision or a forbidden ino_t is discovered
    // in addition, the root node is force-assigned 2.
    // (or maybe we just XOR them so that the root node were 2?)
    createDate = attributeModDate = DateFromTs( st.st_ctim );
    contentModDate = DateFromTs( st.st_mtim );
    accessDate = DateFromTs( st.st_atim );
    backupDate = 0; // reserved because unknown
}

// length is ignored for consistency with <
bool HFSPlusCatalogKey::operator==( const HFSPlusCatalogKey & other ) const
{ return parentID == other.parentID; }

bool HFSPlusCatalogKey::operator<( const HFSPlusCatalogKey & other ) const
{ return parentID < other.parentID; }

bool HFSPlusExtentKey::operator==( const HFSPlusExtentKey & other ) const
{ return ( fileID == other.fileID ) && ( forkType == other.forkType ) && ( startBlock == other.startBlock ); }

bool HFSPlusExtentKey::operator<( const HFSPlusExtentKey & other ) const
{
    return ( fileID < other.fileID ) ||
           ( ( fileID == other.fileID ) && ( forkType < other.forkType ) ) ||
           ( ( fileID == other.fileID ) && ( forkType == other.forkType ) && ( startBlock < other.startBlock ) );
}

int calc_checksum( unsigned char * ptr, int len )
{
    int i, cksum = 0;

    for( i = 0; i < len; i++, ptr++ )
    {
        cksum = ( cksum << 8 ) ^ ( cksum + *ptr );
    }

    return ( ~cksum );
}

HFSPlusVolumeBuilder::HFSPlusVolumeBuilder()
{
    catalogTree.header.tuneForCatalog();
    extentTree.header.tuneForOverflow();
}

void HFSPlusVolumeBuilder::setBlockSize( blksize_t blkSz )
{
    _blk_sz = blkSz;
    catalogTree.header.clumpSize = blkSz;
    extentTree.header.clumpSize = blkSz;
}

void HFSPlusVolumeBuilder::onEntry( Entry * entry, Ptr<Record> dirEntRec, HFSPlusCatalogEntry & dirEnt, CNID nodeId )
{
    CNID parentId = entry->parent ? renum( entry->parent ) : kHFSRootParentID;

    auto name = entry->decoded;
    decompo( name );
    dirEnt.setTimes( entry->stat );
    dirEnt.nodeId = nodeId;

    // std::wcout << name << std::endl;

    HFSPlusCatalogKey dirEntKey;
    dirEntKey.parentID = parentId;

    NamedRecord<HFSPlusCatalogKey> entryKeyRec( dirEntKey, name ); // For file or folder records, this is the name
    entryKeyRec.data.keyLength = entryKeyRec.size() - sizeof( entryKeyRec.data.keyLength );
    catalog.insert( std::make_pair( entryKeyRec, dirEntRec ) );

    HFSPlusCatalogKey threadKey;
    HFSPlusCatalogThread thread( entry->isDir() );
    threadKey.parentID = nodeId; //  For thread records, this is the CNID of the file or folder itself.

    NamedRecord<HFSPlusCatalogKey> threadKeyRec( threadKey ); //  For thread records, this is the empty string.
    threadKeyRec.data.keyLength = threadKeyRec.size() - sizeof( threadKeyRec.data.keyLength );
    thread.parentID = parentId; // The CNID of the parent of the file or folder

    Ptr<Record> threadRec = New<NamedRecord<HFSPlusCatalogThread>>( thread, name ); // generic
    catalog.insert( std::make_pair( threadKeyRec, threadRec ) );
}

void HFSPlusVolumeBuilder::onEntry( Entry * entry, Ptr<Record> dirEntRec, HFSPlusCatalogEntry & dirEnt )
{
    CNID nodeId = renum( entry );
    onEntry( entry, dirEntRec, dirEnt, nodeId );
}

HFSPlusExtentRecord * HFSPlusVolumeBuilder::onOverflow( CNID fileId, blkcnt_t blk )
{
    // printf( "Overflow = adding a record\n" );
    HFSPlusExtentKey overKey;
    overKey.forkType = DataForkType;
    overKey.keyLength = sizeof( overKey ) - sizeof( overKey.keyLength );
    overKey.fileID = fileId;
    overKey.startBlock = blk;
    return &overflow[overKey];
}

void HFSPlusVolumeBuilder::compactTrees()
{
    catalogTree.compactBTree( catalog );
    extentTree.compactBTree( overflow );
}

void HFPlusOut::setBlockSize( blksize_t blkSz )
{
    _vol.setBlockSize( blkSz );
    _vb.setBlockSize( blkSz );
}

void HFPlusOut::setLabels( const char * /*system*/, const char * volume )
{
    _vol_label = volume;
}

void HFPlusOut::masterAdjusted( const Original & tree,
                                const Medium & outImage, const Medium & tmpImage,
                                blksize_t blkSz )
{
    ( void )blkSz; // master bs ignored
    adjust( tree, outImage, tmpImage );
}

Colonies HFPlusOut::plan( const Original & tree, Planner & outPlanner, Planner & tmpPlanner )
{
    planHeaders( tree, outPlanner, tmpPlanner );
    Colonies srcToTrg = tree.writeFiles( outPlanner );
    masterComplete( tree, outPlanner, tmpPlanner, srcToTrg );
    return srcToTrg;
}

off64_t HFPlusOut::planHeaders( const Original & /*tree*/, Planner & outPlanner, Planner & tmpPlanner ) const
{
    printf( "own blksz=%lx, out blksz=%lx, tmp blksz=%lx\n", blockSize(),
            outPlanner.blockSize(),
            tmpPlanner.blockSize() );
    // planReserved( tree, outPlanner, tmpPlanner, 0x400 ); // 1K // FIXME allow planReserved write tmp*
    off64_t tmp = tmpPlanner.offset();
    // tmpPlanner.append( ZeroExtent( 0x400 ) ); // or a superblock / mbr / name it
    tmpPlanner.append( TempExtent( _mbr ) );
    tmpPlanner.padTo( 0x400 );
    tmpPlanner.append( TempExtent( _vol ) );
    off64_t cur = outPlanner.append( tmpPlanner.wrapToGo( tmp ) );
    // TODO "Block 42686 is not an MDB or Volume Header" - replicate to the end of the drive!
    outPlanner.autoPad();
    return cur;
}

void HFPlusOut::masterReserved( const Original & tree,
                                Planner & outPlanner, Planner & tmpPlanner,
                                off64_t /*cap*/ )
{
    if( outPlanner.offset() == 0 ) { planHeaders( tree, outPlanner, tmpPlanner ); }
    else { printf( "Master offset %ld\n", outPlanner.offset() ); abort(); }
}

ino_t TopUnused( const std::set<ino_t> & top )
{
    ino_t candy = ~( ino_t )0;
    auto itr = top.rbegin();
    while( candy <= *itr )
    {
        if( candy == *itr )
        { candy = ( *itr ) - 1; }
        itr++;
    }
    return candy;
}

void HFPlusOut::masterComplete( const Original & tree,
                                Planner & outPlanner, Planner & tmpPlanner,
                                const Colonies & srcToTrg )
{
    auto blkSz = blockSize();
    CNID iroot = tree.fsRoot->stat.st_ino;
    std::set<ino_t> inodeIds;
    _vb.renum = [iroot, &inodeIds]( Entry * entry )
    {
        // TODO consider the zero (root parent) case;
        // TODO introduce a substitution map
        // TODO start considering st_dev
        CNID ino;
        if( entry == nullptr ) { ino = kHFSRootParentID; }
        else
        {
            ino = entry->stat.st_ino;
            if( ino == iroot ) { ino = kHFSRootFolderID; }
            else { inodeIds.insert( ino ); }
        }
        return ino;
    };
    _vb.decompo = [this]( Unicode & name )
    {
        if( name.size() ) // a regular entry
        {
            ceur.correct( name );
            hang.correct( name );
        }
        else
        {
            // the root entry name is the volume name,
            // and no other entry has an empty name.
            ISO88591Flat().parse( name,
                                  _vol_label.c_str() );
        }
    };
    struct Count { size_t count = 0; };
    std::map<PathEntry *, Count> subFolderCount;
    for( auto eItr = tree.pathTable.rbegin(); eItr != tree.pathTable.rend(); )
    {
        PathEntry * pathEntry = *eItr++;
        auto entries = pathEntry->entries.size();
        auto dirEntRec = New<NamedRecord<HFSPlusCatalogFolder>>( HFSPlusCatalogFolder( entries ) ); // move-in
        dirEntRec->data.setSubFolderCount( subFolderCount[pathEntry].count );
        _vb.onEntry( pathEntry, dirEntRec, dirEntRec->data );
        subFolderCount[pathEntry->parent].count++;
    }
    for( FileEntry * fileEntry : tree.fileTable )
    {
        auto dirEntRec = New<NamedRecord<HFSPlusCatalogFile>>( HFSPlusCatalogFile() ); // move-in
        CNID fileId = _vb.renum( fileEntry ); // onEntry overloaded so that we didn't do it twice
        HFSPlusForkData & dataFork = dirEntRec->data.dataFork;
        off64_t length = fileEntry->stat.st_size;
        dataFork.logicalSize = length;
        dataFork.clumpSize = blkSz;

        auto * desc = &dataFork.extents;
        size_t extentNo = 0;
        blkcnt_t blk = 0;
        HFSPlusExtentDescriptor * pExt = nullptr;
        for( const Extent & xt : tree.layout.at( fileEntry ) ) // must be charted at this point!
        {
            if( extentNo == desc->size() )
            {
                desc = _vb.onOverflow( fileId, blk );
                extentNo = 0;
            }
            // the offset here is in source dev coordinates; adjust...
            off64_t offset = srcToTrg.withinDisk( xt );
            // the offset here is in target dev coordinates
            blkcnt_t extentLba = offset / blkSz;
            blkcnt_t lengthLba = roundUp( xt.length, blkSz ) / blkSz;

            // we are doing the job of DeepLook here. move out! (or don't do,
            // doesn't seem to cause the problem w/ Invalid extent entry...)
            // printf( "Read extent: %lu %lx+%lx\n", extentNo, offset, xt.length );
            if( pExt && ( ( pExt->startBlock + pExt->blockCount ) == extentLba ) )
            {
                pExt->blockCount = pExt->blockCount + lengthLba; // FIXME pExt->extend( lengthLba );
            }
            else
            {
                pExt = &desc->at( extentNo );
                pExt->set( extentLba, lengthLba );
                extentNo ++;
            }
            blk += lengthLba;
        }
        dataFork.totalBlocks = blk;

        // what's interesting is that the total extent count isn't saved anywhere
        _vb.onEntry( fileEntry, dirEntRec, dirEntRec->data, fileId );
    }
    // compute donut holes. add their extents into the bad block file => overflow

    _vb.compactTrees();

    // flush NodeList to tmpPlanner, wrap as a file, publish into header (cat, ovf)

    // write the nodes into tmpPlanner
    // wrapToGo
    // publish the extent into volume header

    printf( "Writing the catalog tree and the extent overflow\n" );
    _vol.catalogFile.setExtent( _vb.catalogTree.wrapToGo( outPlanner, tmpPlanner ), blkSz );
    _vol.extentsFile.setExtent( _vb.extentTree.wrapToGo( outPlanner, tmpPlanner ), blkSz );

    // blkcnt_t totalBlks = outPlanner.offset() / blkSz;
    // blksize_t blkBits = blkSz << 3;
    // blkcnt_t totalBlks = totalBlks * blkBits / ( blkBits - 1 )
    blkcnt_t blks = ( outPlanner.offset() << 3 ) / ( ( blkSz << 3 ) - 1 ) + 2;
    printf( "Writing the allocation bitmap (%lx blks)\n", blks );
    Ptr<BitsMedium> deviceOne = New<BitsMedium>( true, 1 << 16, blks );
    Extent allobits( 0, roundUp( deviceOne->byteCount() ), deviceOne );
    Extent tmpAlloc = tmpPlanner.wrapToGo( tmpPlanner.append( allobits ) );
    printf( "Temporary extent: %lx+%lx\n", tmpAlloc.offset, tmpAlloc.length );
    Extent outAlloc = outPlanner.wrapToGo( outPlanner.append( tmpAlloc ) );
    printf( "Permanent extent: %lx+%lx\n", outAlloc.offset, outAlloc.length );
    _vol.allocationFile.setExtent( outAlloc, blkSz );
    // the eventual padding of outAlloc is the "envelope" of the three.
    // this may produce extra unexpected and potentially undesired padding.
    // however, since the rest is filled with zeroes, HFS+ is satisfied.

    // _vol.attributesFile.setExtent( ZeroExtent( 0 ), blkSz );
    _vol.attributesFile.setReserved();

    // run-off
    // printf( "OldBlks=%lx(%lu)\n", blks, blks );
    blksize_t coblock = Planner::copad( outPlanner, tmpPlanner ); // may underallocate
    // printf( "Coblock=%lx(%lu)\n", coblock, coblock );
    off64_t curOff = outPlanner.offset();
    // printf( "CurOffs=%lx(%lu)\n", curOff, curOff );
    blkcnt_t curb = curOff / blockSize();
    // printf( "CurBlks=%lx(%lu)\n", curb, curb );
    if( curb <= blks ) { blks = curb + std::max( static_cast<blksize_t>( 1UL ), coblock / blockSize() ); }
    // printf( "NewBlks=%lx(%lu)\n", blks, blks );
    // cut here ...
    deviceOne->reserveBits( blks );
    _vol.totalBlocks = blks;
    // ... cut here and make setTotalBlocks()
    off64_t length = blks * blockSize();
    // printf( "Length=%lx\n", length );
    off64_t prepend = length - curOff - 0x400;
    off64_t curTmp = tmpPlanner.append( ZeroExtent( prepend ) );
    tmpPlanner.append( TempExtent( _vol ) );
    tmpPlanner.append( ZeroExtent( 0x400 - sizeof( _vol ) ) );
    outPlanner.append( tmpPlanner.wrapToGo( curTmp ) );

    //_mbr.setType( 0x17, 0 );
    //_mbr.setEnd( blks, blkSz );
    _vol.checkedDate = _vol.backupDate = 0;
    _vol.createDate = DateFromTs( tree.fsRoot->stat.st_ctim );
    _vol.modifyDate = DateFromTs( tree.fsRoot->stat.st_mtim ); // FIXME modify date
    _vol.writeCount = _vol.modifyDate;
    _vol.fileCount = tree.fileTable.size();
    _vol.folderCount = tree.pathTable.size() - 1;
    _vol.nextCatalogID = TopUnused( inodeIds );

    if( 0 )
    {
        for( auto & cataPair : _vb.catalog )
        {
            printf( "C sizeof: key=%lu=%u val=%lu\n",
                    cataPair.first.size(), ( uint16_t ) cataPair.first.data.keyLength, // NR
                    cataPair.second->size() ); // Ptr<NR>
        }
        for( auto & overPair : _vb.overflow )
        {
            printf( "O sizeof: key=%lu=%u val=%lu\n",
                    sizeof( overPair.first ), ( uint16_t ) overPair.first.keyLength, // NR
                    sizeof( overPair.second ) ); // POD
        }
    }
}

// WISDOM if a case-insensitive FS needs to be produced, revive ulcase.cpp
}
