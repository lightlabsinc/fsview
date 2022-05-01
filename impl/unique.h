/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef UNIQUE_H
#define UNIQUE_H

#include "wrapper.h"

#include <random>

#include "impl/strdec.h"

// string jamming is used to compact the device serial number preserving variability.
uint32_t Crc32( const char * str ); // Equivalent to cksum -o 3

/* The requirements to the name pool is as follows:
 * - there is a length limit that needs to be enforced; (30 for ISO 9660 Level 3, let's say 120 for Joliet);
 * - there is a character set that needs to be enforced ('d-characters' for 9660, all -specials for Joliet);
 * - we don't check for lossiness, at least for now. All supported encodings are present in the output.
 * - the std::wstring representation of file names is common and authoritative.
 * - the correlated part of a file/folder name is anything before the last dot or the entire name if no dot;
 * - the variant is inserted at the end of the correlated part.
 * - if a size limit is met, the correlated part is contracted to accommodate the variant.
 * -
 * ...
 * - there is a suffix that needs to be appended.
 */

/// A data structure to represent delimiting positions in a file name, such as '.' dots.
typedef std::vector<size_t> Delimit;

/// A delimitation-aware Unicode string wrapper. Implements hierarchy-aware
///  lexicographic comparison (i.e. ab.k < abc.0 < abc.01 < abc.1 < abc$ < ac < b)
struct Unicomp
{
    friend struct Unicomp;

    Unicode conv; // trimmed, varied, combined
    Delimit seps; // separators + EOL (aka \0)

    inline bool operator<( const Unicomp & other ) const { return cmp( other ) < 0; }
    inline bool operator>( const Unicomp & other ) const { return cmp( other ) > 0; }
    inline bool operator<=( const Unicomp & other ) const { return cmp( other ) <= 0; }
    inline bool operator>=( const Unicomp & other ) const { return cmp( other ) >= 0; }
    inline bool operator==( const Unicomp & other ) const { return cmp( other ) == 0; }
    inline bool operator!=( const Unicomp & other ) const { return cmp( other ) != 0; }

    inline size_t sep( size_t sepId ) const { return sepId < seps.size() ? seps[sepId] : conv.size(); }

private:
    /// Compares with another delimited Unicode string. Uses cmpp().
    int cmp( const Unicomp & other ) const;
    /// Compares respective string fragments with known boundaries.
    int cmpp( const Unicode & left, size_t lastLeft, size_t nextLeft,
              const Unicode & right, size_t lastRight, size_t nextRight ) const;
};

/// A container that holds variations of a file name until they are both target-compliant and unique.
struct UniqName : public Unicomp
{
    UniqName( const Unicode & original, bool file ) : tran( original ), link( tran ), isFile( file ) {}

    Unicode tran; /// <transliterated
    Unicode link; /// <dot-extracted but not transliterated or trimmed
    const bool isFile;
};

/// Produces numeric suffices to append to a file name at a given retry attempt.
struct IVariant
{
    virtual int variant( int attempt ) = 0;
    virtual ~IVariant() = default;
};

/// Implements IVariant with a legacy random number generator.
struct StdRand : public IVariant
{
    inline int variant( int attempt ) override { return attempt ? attempt + ( std::rand() % ( attempt * attempt ) ) : 0; }
};

/// Implements IVariant with an MT19937 random number generator.
struct Mt19937 : public IVariant
{
    // TODO compare performance with std::rand();
    inline Mt19937() : engine( 1 ) {}
    int variant( int attempt ) override;
    std::mt19937 engine;
};

/// A name variation engine. Stateless; does not own or cache the name in any way, only defines the rules.
struct INameRule
{
    /// Transliterate a name into the target character set (Ignobel => IGNOBEL).
    virtual void translit( UniqName & name ) const = 0;
    /// Append a variant mix-in number to the name, trimming the base name if necessary (IGNOBEL + 1997 => IGNO1997)
    virtual void mixInVar( UniqName & name, int variant ) const = 0;
    /// Reinsert the file extension and other filesystem-required parts, e.g. "version" (IGNO1997 => IGNO1997.TXT;1)
    virtual void decorate( UniqName & name ) const = 0;
    virtual ~INameRule() = default;
};

/// A compact way to define file name segment separators (e.g. '.', ';')
template<wchar_t SEP1, wchar_t SEP2, int16_t VERS> struct NameComp
{
    static constexpr const wchar_t mSEP1 = SEP1;
    static constexpr const wchar_t mSEP2 = SEP2;
    static constexpr const int16_t mVERS = VERS;
};

/// A compact way to define file name segment sizes (e.g. MS-DOS 8+3)
template<size_t sBASE, size_t sEXT, size_t sTOTAL> struct NameTrim
{
    static constexpr const size_t mEXT = sEXT;
    static constexpr const size_t mBASE =
        sBASE < sTOTAL - sEXT - 1 ?
        sBASE : sTOTAL - sEXT - 1;
};

using VFatChar = NameComp<'.', '\0', 0>;
using CDFSChar = NameComp<'.', ';', 1>;
using CDFSLvl1 = NameTrim<8, 3, 12>;
using CDFSLvl2 = NameTrim<24, 5, 30>;
using CDJoliet = NameTrim<54, 5, 60>;

/// CDFS-compliant unique name generator. Encompasses MS-DOS, ISO-9660 and Joliet.
template<typename Comp, typename Trim, void( *Spay )( wchar_t & ), bool forceDOT>
struct CDFSRule : public INameRule
{
    void translit( UniqName & name ) const override
    {
        auto origSz = name.tran.size();
        auto dotPos = name.isFile ? name.tran.rfind( UnChar::STOP ) : Unicode::npos;
        if( dotPos == Unicode::npos || dotPos < origSz - Trim::mEXT - 1 ) { dotPos = origSz; }
        name.link.resize( dotPos );
        // until here, the code is common across rules and should be moved to Name?
        for( auto dit = name.tran.begin(); dit != name.tran.end(); Spay( *dit++ ) );
    }

    void mixInVar( UniqName & name, int variant ) const override
    {
        // likely: faster than printf()
        constexpr const int radix = 10;
        std::vector<char> itoa; // reverse order; don't \0
        while( variant ) { itoa.push_back( '0' + ( variant % radix ) ); variant /= radix; }
        int basePart = std::min( name.link.size(), Trim::mBASE - itoa.size() );
        name.conv = name.tran.substr( 0, basePart );
        for( auto dit = itoa.rbegin(); dit != itoa.rend(); name.conv.push_back( *dit++ ) );
    }

    void decorate( UniqName & name ) const override
    {
        const bool hasExt = name.tran.size() > name.link.size();
        const bool addDot = ( forceDOT && name.isFile ) || hasExt;
        if( addDot ) { name.seps.push_back( name.conv.size() ); name.conv.push_back( Comp::mSEP1 ); }
        if( hasExt ) { name.conv.append( name.tran.begin() + name.link.size() + 1, name.tran.end() ); }
        if( Comp::mVERS && name.isFile )
        {
            // as above, needs to be faster
            constexpr const int radix = 10;
            name.seps.push_back( name.conv.size() ); name.conv.push_back( Comp::mSEP2 ); // make fun!
            auto pos = name.conv.length();
            int version = Comp::mVERS;
            while( version ) { name.conv.insert( name.conv.begin() + pos, '0' + ( version % radix ) ); version /= radix; }
        }
    }
};

/// Force an 'A-character' as defined by ISO-9660.
template<typename WC>
void EnsureD( WC & wc )
{
    if( 'a' <= wc && wc <= 'z' )
    { wc -= 0x20; } // lowercase
    else if( 'A' <= wc && wc <= 'Z' ) {}
    else if( '0' <= wc && wc <= '9' ) {}
    else { wc = UnChar::CDFS; }
}

/// Force a 'D-character' as defined by ISO-9660.
template<typename WC>
void EnsureD1( WC & wc )
{
    if( wc < 0x20 || wc == '*' || wc == '/' || wc == '\\' || wc == ':' || wc == ';' || wc == '?' )
    { wc = UnChar::UCS2; }
}

//using EnsureDChar = EnsureD<char>;
//using EnsureD1Char = EnsureD1<char>;
//using EnsureDWChar = EnsureD<wchar_t>;
//using EnsureD1WChar = EnsureD1<wchar_t>;

using DosVolRule = CDFSRule<CDFSChar, CDFSLvl1, &EnsureD<wchar_t>, true>;
using PriVolRule = CDFSRule<CDFSChar, CDFSLvl2, &EnsureD<wchar_t>, true>;
using SecVolRule = CDFSRule<CDFSChar, CDJoliet, &EnsureD1<wchar_t>, false>;

using FatVolRule = CDFSRule<VFatChar, CDFSLvl1, &EnsureD<wchar_t>, false>;

// A-chars are:
// Boot System Identifier (BP 8 to 39
// Boot Identifier (BP 40 to 71)
// System Identifier (PVD)
// Publisher Identifier
// Data Preparer Identifier
// Application Identifier

// D-chars (allowed for file names) are: A-Z0-9_

/// This structure is pertinent to a source (or virtual source) directory.
/// It ensures that distinct source file names are canonicalized into unique compliant target names.
struct NamePool
{
    /// The facade method.
    /// @param[in] origName     name in the source file tree
    /// @param[in] isFile       whether the name is a regular file (not a folder)
    /// @param[in] rule         the canonicalization rule
    /// @param[in] shuf         the numeric suffix generator, such as StdRand or Mt19937
    /// @returns    a properly transliterated, trimmed, "uniqualized" and delimited output file name.
    Unicomp fitName( const Unicode & origName, bool isFile, const INameRule & rule, IVariant & shuf );

private:
    void tryExisting( UniqName & name ) const;  ///< attempts to use an existing name mapping
    bool tryBrandNew( UniqName & name );        ///< attempts to register a new name mapping

    // FIXME Map<Unicode, Unicode> to allow sharing
    std::map<Unicode, Unicode> convToLink;
    std::map<Unicode, Unicode> linkToConv;
};

#endif // UNIQUE_H
