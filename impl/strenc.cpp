/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "strenc.h"
#include "strenc.inc"


void IUniDeco::correct( Unicode & decoded )
{
    size_t start;
    if( irregular( decoded, &start ) ) { decoded = decompose( decoded, start ); }
}

bool CharDeco::irregular( const Unicode & decoded, size_t * start ) const
{
    for( size_t pos = 0; pos < decoded.size(); ++pos )
    {
        if( irreguchar( decoded[pos] ) )
        {
            if( start ) { *start = pos; }
            return true;
        }
    }
    return false;
}

Unicode CharDeco::decompose( const Unicode & decoded, size_t start ) const
{
    Unicode canonical;
    if( start )
    {
        canonical.append( decoded.begin(), decoded.begin() + start );
    }
    for( size_t pos = start; pos < decoded.size(); ++pos )
    {
        decompochar( canonical, decoded[pos] );
    }
    return canonical;
}

bool CMapDeco::irreguchar( wchar_t wc ) const { return illegal.find( wc ) != illegal.end(); }

void CMapDeco::decompochar( Unicode & out, wchar_t wc ) const
{
    auto correction = illegal.find( wc );
    if( correction != illegal.end() )
    { out.append( correction->second ); }
    else
    { out.push_back( correction->first ); }
}

size_t CharType::compress( std::string & target, const Unicode & source ) const
{
    int maxSize = source.size();
    target.clear();
    target.reserve( maxSize * charSize() );
    //
    int index = 0;
    while( index < maxSize ) { packChar( source[index++], [&target]( char c ) { target.push_back( c ); } ); }
    //
    return index;
}

size_t CharType::compress( uint8_t & size, char *& target,
                           Unicode::const_iterator & begin,
                           const Unicode::const_iterator & end ) const
{
    int maxSize = std::min<ptrdiff_t>( end - begin, size / charSize() );
    //
    char * start = target;
    int index = 0;
    while( index < maxSize ) { index++, packChar( *begin++, [&target]( char c ) { *target++ = c; } ); }
    //
    size = target - start;
    return index;
}

size_t CharType::compress( int fd, const Unicode & source ) const
{
    std::string target;
    size_t chars = compress( target, source );
    off_t start = lseek( fd, 0, SEEK_CUR );
    size_t written = write( fd, target.data(), target.size() );
    if( written == target.size() ) { return chars; }
    else { lseek( fd, start, SEEK_SET ); return 0; }
}

void CharANSI::packChar( wchar_t wc, CharType::WriteChar sink ) const
{
    sink( ( char ) wc );
}

void CharUCS2::packChar( wchar_t wc, CharType::WriteChar sink ) const
{
    sink( ( char )( wc >> 8 ) );
    sink( ( char ) wc );
}

void CharLFN::packChar( wchar_t wc, CharType::WriteChar sink ) const
{
    sink( ( char ) wc );
    sink( ( char )( wc >> 8 ) );
}

size_t ICharPack::compress( uint8_t & size, char *& target, const Unicode & source ) const
{
    Unicode::const_iterator cb = source.cbegin(), ce = source.cend();
    return compress( size, target, cb, ce );
}
