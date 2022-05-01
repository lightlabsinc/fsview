#include "cd9660.h"
#include "impl/datetm.h"

namespace
{
bool IsLittleEndian() { int word = 1; return( *( char * ) & word ); }

inline uint8_t QuarterOff( const struct tm & t ) { return ( int )( t.tm_gmtoff / 15 / 60 ) + 48; }

std::vector<bool> Allowed( const char * range )
{
    std::vector<bool> retVal;
    retVal.resize( UINT8_MAX + 1, 0 );
    uint8_t allowed;
    for( int i = 0; ( allowed = range[i] ); ++i ) { retVal[allowed] = true; }
    return retVal;
}

#define DCHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"
#define ACHARS  DCHARS "!\"%&'()*+,-./:;<=>?"

std::vector<bool> ARANGE = Allowed( ACHARS );
std::vector<bool> DRANGE = Allowed( DCHARS );
}

namespace CD
{

DateTime & DateTime::setTm( const tm & t, int centiseconds )
{
    sprintf( buf, "%04d""%02d""%02d"
             "%02d""%02d""%02d""%02d",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec, centiseconds );
    tzoff = QuarterOff( t ); // ... overwriting terminal 0
    return *this;
}

DateTime & DateTime::operator=( const timespec & ts )
{ SetTime( ts, TmSet( this ) ); return *this; }

DateTime::DateTime() { SetNow( TmSet( this ) ); }

void DirEntryDtTime::setTm( const tm & t, int /*centis*/ )
{
    year    = t.tm_year;
    month   = t.tm_mon + 1;
    day     = t.tm_mday;
    hour    = t.tm_hour;
    minute  = t.tm_min;
    second  = t.tm_sec;
    tzone   = QuarterOff( t );
}

DirEntryDtTime & DirEntryDtTime::operator=( const timespec & ts )
{ SetTime( ts, TmSet( this ) ); return *this; }

void JolietDesc::CopyMeta( const PriVolDesc & priVol )
{
    // origin
    systemId.dilute<MSB<char16_t>>( priVol.systemId );
    volumeId.dilute<MSB<char16_t>>( priVol.volumeId );
    // textual
    volumeSetId.dilute<MSB<char16_t>>( priVol.volumeSetId );
    publisherId.dilute<MSB<char16_t>>( priVol.publisherId );
    prepareById.dilute<MSB<char16_t>>( priVol.prepareById );
    application.dilute<MSB<char16_t>>( priVol.application );
    // file names
    copyFile.dilute<MSB<char16_t>>( priVol.copyFile );
    abstFile.dilute<MSB<char16_t>>( priVol.abstFile );
    biblFile.dilute<MSB<char16_t>>( priVol.biblFile );
}

CD9660Out::CD9660Out( bool withUnicode )
{
    // add primary
    auto & priVol = _volumes[PrimaryVol];
    priVol.vol  = &_pri_vol;
    priVol.rule = &pri_rule;
    priVol.pack = &_ansi_pack;
    if( withUnicode )
    {
        // add joliet
        auto & joliet = _volumes[Supplement];
        joliet.vol  = &_sec_vol;
        joliet.rule = &sec_rule;
        joliet.pack = &_ucs2_pack;
    }
}

Colonies CD9660Out::plan( const Original & tree, Planner & outPlanner, Planner & tmpPlanner )
{
    // write zeroes, or what the slave wants
    planReserved( tree, outPlanner, tmpPlanner, 0x8000 ); // Extent onReserved( Extent )

    // printf( "After zeroes written: %lu (%lu)\n", outPlanner.offset(), tmpPlanner.offset() );

    // write 3 descriptors - per-each-fs[PVD, SVD], end
    outPlanner.append( tmpPlanner.wrapToGo( planHeaders( tmpPlanner ) ) );
    outPlanner.autoPad(); // effectively a no-op because dm blocks <= zram blocks

    // printf( "After headers written: %lu (%lu)\n", outPlanner.offset(), tmpPlanner.offset() );

    // write files
    Colonies srcToTrg = tree.writeFiles( outPlanner );
    printf( "After files written: %lu (%lu)\n", outPlanner.offset(), tmpPlanner.offset() );

    // write per-each-fs[folders, path-table[L], path-table[B]]
    off64_t innerOff = tmpPlanner.offset();
    off64_t tmpToOut = outPlanner.offset() - innerOff;
    planVolumes( outPlanner, [&]( CD9660Out::FS & vol )
    {
        StdRand shuf;
        std::map<PathEntry *, std::list<Later::Use>> parents;
        struct FolDef
        {
            Extent extent;
            Unicode conv;
            std::string encName;
        };
        std::map<Entry *, FolDef> fsFolders;
        const blksize_t blkSz = vol.vol->blkSz;

        DirectoryEntry & dot = vol.vol->rootDirectory;
        // we will reuse the field along the way; but the last one will be root!
        for( auto itr = tree.pathTable.rbegin(); itr != tree.pathTable.rend(); ++itr )
        {
            PathEntry * pDir = *itr;
            auto dirOffset = tmpPlanner.offset() + tmpToOut;
            // we can replace the ctor with a factory method - just hide Later::Store() behind the facade method
            Ptr<Burner> dirBurner = New<VectBurner>( blkSz ); // New<TempBurner>(blkSz)
            dirBurner->reserve( blkSz );
            auto writeEntry = [dirBurner, blkSz]( const DirectoryEntry & die, const std::string & enc )
            {
                off64_t cur = dirBurner->offset();
                off64_t pad = dirBurner->padding( cur, blkSz );
                if( die.entrySz > pad ) { dirBurner->append( ZeroExtent( pad ) ); }
                cur = dirBurner->append( TempExtent( &die, sizeof( DirectoryEntry ) - sizeof( die.fileName.data ) ) );
                dirBurner->append( 0, die.textSize(), New<TempMedium>( enc.data() ) );
                return cur;
            };
            // for each folder, sort entries by names, write directories and file extents
            // NOTE - first two entries are this (current) folder and the parent ..folder
            dot.dateTime = pDir->stat.st_mtim;
            dot.fileFlags |= XAttrFlags::Folder;
            dot.extentLba = dirOffset / blkSz;
            dot.fileName.data[0] = 0;
            // specialEntry.length will be written later
            off64_t ownOffset = dirBurner->append( TempExtent<DirectoryEntry>( dot ) );
            Later::Use ownSize = Later::Store<decltype( dot.length )>( DtOf( dirBurner ),
                                                                       ownOffset + offsetof( DirectoryEntry, length ), dot.length,
                                                                       []( decltype( dot.length )& lfield, const Range & range )
            {
                lfield = range.length;
            } ); // for length
            dot.fileName.data[0] = 1;
            off64_t parentOffset = dirBurner->append( TempExtent<DirectoryEntry>( dot ) );
            PathEntry * parent = pDir->parent ? pDir->parent : pDir;
            parents[parent].push_back( Later::Store<DirectoryEntry>( DtOf( dirBurner ),
                                                                     parentOffset, dot,
                                                                     [parent, blkSz]( DirectoryEntry & lfield, const Range & range )
            {
                lfield.dateTime = parent->stat.st_mtim;
                lfield.extentLba = range.offset / blkSz; // as above, tmpPlanner.offset() + tmpToOut
                lfield.length = range.length;
            } ) ); // for length

            NamePool pool; // temporary, while a folder is being read
            std::map<Unicomp, Entry *> entries; // names are unique at this point
            for( Ptr<Entry> pEnt : pDir->entries )
            {
                entries[pool.fitName( pEnt->decoded, pEnt->isFile(), *vol.rule, shuf )] = pEnt.get();
            }
            for( auto & nEnt : entries )
            {
                const Unicomp & name = nEnt.first;
                Entry * pEnt = nEnt.second;
                DirectoryEntry die;
                // Principal component: NAME
                std::string encName;
                vol.pack->compress( encName, name.conv );
                die.fileName.size = encName.size();
                die.entrySz = die.size();
                // Principal component: DATE
                die.dateTime = pEnt->stat.st_mtim;

                if( pEnt->isFile() )
                {
                    // Principal components: FLAG, LBA, LENGTH
                    off64_t length = pEnt->stat.st_size;
                    for( const Extent & xt : tree.layout.at( pEnt ) )
                    {
                        // the offset here is in source dev coordinates; adjust...
                        off64_t offset = srcToTrg.withinDisk( xt );
                        // the offset here is in target dev coordinates
                        die.extentLba = offset / blkSz;
                        if( length <= xt.length )
                        { die.fileFlags &= ~XAttrFlags::TBCont; }
                        else
                        { die.fileFlags |= XAttrFlags::TBCont; }
                        die.length = std::min( length, xt.length );
                        length -= xt.length;
                        writeEntry( die, encName );
                    }
                }
                else
                {
                    // Principal components: FLAG, LBA, LENGTH
                    const Extent & xt = fsFolders[pEnt].extent;
                    die.fileFlags |= XAttrFlags::Folder;
                    // the offset is in target dev coordinates
                    die.extentLba = xt.offset / blkSz;
                    die.length = xt.length;
                    writeEntry( die, encName );
                    // put folder name for path table
                    fsFolders[pEnt].conv = name.conv;
                    fsFolders[pEnt].encName = encName;
                }
            }

            tmpPlanner.append( WrapToGo( dirBurner ) ); // directory extent roundup
            Extent ownExtent = Extent( dirOffset, dirBurner->offset(), dirBurner );
            ownSize( ownExtent );
            fsFolders[pDir].extent = ownExtent;

            // propagate to children
            auto catchup = parents.find( pDir );
            if( catchup != parents.end() )
            {
                for( auto & use : catchup->second ) { use( ownExtent ); }
                //1// parents.erase( catchup ); // MOREINFO will it speed up lookup?
            }
        }
        dot.fileName.data[0] = 0; // root again

        std::map<Unicode, Entry *> pTab;

        wchar_t level = 1; // <= 8
        wchar_t daddy = 1; // the parent directory of the root directory is the root directory
        // traverse from top (Exposure::fsRoot)
        Unicode order;

        PathTableEntryPair pePair;
        Ptr<Burner> ptLsb = New<VectBurner>( blkSz ), ptMsb = New<VectBurner>( blkSz ); // or can be new TempBurner?

        order.resize( 2 );
        order[0] = level;
        order[1] = daddy;
        order.push_back( L'\0' );

        FolDef & rootData = fsFolders[tree.fsRoot.get()];
        // now we can fill in the root directory size...
        vol.vol->rootDirectory.length = rootData.extent.length;
        // ...and proceed to the path tables.
        rootData.encName.resize( 1, '\0' );
        pTab[order] = tree.fsRoot.get();
        auto itd = pTab.begin();

        while( itd != pTab.end() && daddy < CD9660Out::PATHTB_SZ )
        {
            Unicode parentOrder = itd->first;
            order[0] = parentOrder[0] + 1;
            FolDef & parentData = fsFolders[itd->second];
            order[1] = daddy++; // this is assigned by itn after sort
            pePair.set( parentData.encName, parentData.extent.offset / blkSz, parentOrder[1] );
            ptLsb->append( TempExtent( pePair.lsb ) );
            ptMsb->append( TempExtent( pePair.msb ) );
            Extent textExtent( 0, pePair.lsb.textSize(), New<TempMedium>( parentData.encName.c_str() ) );
            ptLsb->append( textExtent );
            ptMsb->append( textExtent );
            //
            PathEntry * pEnt = static_cast<PathEntry *>( itd->second );
            for( Ptr<Entry> pSub : pEnt->entries )
            {
                if( pSub->isDir() )
                {
                    order.resize( 2 );
                    FolDef & childData = fsFolders[pSub.get()];
                    order.append( childData.conv ); // this is going to be our key
                    pTab[order] = pSub.get(); // make sure the addition is after lst!
                }
            }
            ++itd; //++daddy;
        }
        // write two path tables and update their locations
        vol.vol->pTabSz = ptLsb->offset();
        off64_t lsbOff = tmpPlanner.append( WrapToGo( ptLsb ) );
        off64_t msbOff = tmpPlanner.append( WrapToGo( ptMsb ) );
        vol.vol->pTabLsb[0] = ( lsbOff + tmpToOut ) / blkSz;
        vol.vol->pTabMsb[0] = ( msbOff + tmpToOut ) / blkSz;
    } );
    // metadata common to volumes
    // frankly, should contain the serial number
    // setTitles( "LIGHT_OS", "L16_MEDIA" ); // TODO: inject: systemLabel, volumeLabel

    // TODO allow extent conversion if a real file is appended; make it a fallback
    outPlanner.append( tmpPlanner.wrapToGo( innerOff ) );
    outPlanner.autoPad(); // effectively a no-op because dm blocks <= zram blocks

    setSize( outPlanner.offset() );
    // printf( "After fsmeta written: %lu\n", outPlanner.offset() );

    return srcToTrg;
}

void CD9660Out::setSize( off64_t size )
{
    off64_t blk = size / blockSize();
    _pri_vol.blocks = _sec_vol.blocks = blk;
    _map_vol.blocks = size / Blocks::MAPPER_BS;
}

off64_t CD9660Out::planHeaders( IAppend & planner ) const
{
    off64_t cur = planner.offset();
    for( auto & vol : _volumes )
    {
        planner.append( TempExtent( * ( vol.second.vol ) ) );
        planner.padTo( blockSize() );
    }
    // planner.append( TempExtent( _map_vol ) ); // check win!
    // planner.padTo( blockSize() );
    planner.append( TempExtent( _end_vol ) );
    planner.padTo( blockSize() );
    return cur;
}

off64_t CD9660Out::planVolumes( IAppend & planner, std::function<void( FS & )> fsGen )
{
    off64_t cur = planner.offset();
    for( auto & vol : _volumes ) { fsGen( vol.second ); }
    return cur;
}

void CD9660Out::setLabels( const char * system, const char * volume )
{
    _pri_vol.systemId = system;
    _pri_vol.volumeId = volume;
    _sec_vol.CopyMeta( _pri_vol );
}

}//namespace CD
