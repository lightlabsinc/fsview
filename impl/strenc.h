/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef STRENC_H
#define STRENC_H

#include "wrapper.h"

#include "impl/strdec.h"
#include "impl/unique.h"

/// A zero primitive value for buffer filling/padding.
template<typename T>
struct __attribute( ( packed ) ) Z { const T z = 0; };

/// A zero character for char[] buffer filling/padding.
typedef Z<char> zero;

template <typename C, char F>
void StrPad( C * data, const C * src, size_t count )
{
    size_t len = std::min( count, strlen( src ) );
    strncpy( data, src, len );
    memset( data + len, F, count - len );
}

/// A fixed-size buffer of type C characters pre-filled with the F value.
template <typename C, size_t count, char F>
struct __attribute( ( packed ) ) Fill
{
    C data[count];
    inline Fill() { memset( data, F, count ); }
    inline Fill( const C * src ) { *this = src; }
    inline operator const C * () { return data; }
    inline Fill & operator=( const C * src )
    { StrPad<C, F>( data, src, count ); return *this; }

    /// Copy from another buffer expanding its characters into a WC type.
    /// Used e.g. to populate Joliet UCS-2 entries from ISO-9660 ones.
    template<typename WC>
    void dilute( const Fill & other )
    {
        WC wide[count];
        for( size_t i = 0; i < count; ++i )
        { wide[i] = other.data[i]; }
        memcpy( data, wide, count );
    }
};

/// Text buffers are padded with space.
template <typename C, size_t count> using Text = Fill<C, count, ' '>;

/// Binary buffers are padded with zero bytes.
template <typename C, size_t count> using Data = Fill<C, count, '\0'>;

/// Transform a Unicode string in place into its canonical form.
typedef std::function<void( Unicode & )> Decompo;

/// An API to validate and canonicalize a Unicode string.
struct IUniDeco
{
    /// Check whether a Unicode string, starting at position start, is well-formed.
    virtual bool irregular( const Unicode & decoded, size_t * start = nullptr ) const = 0;
    /// Canonicalize a Unicode string, starting at position start.
    virtual Unicode decompose( const Unicode & decoded, size_t start = 0 ) const = 0;

    void correct( Unicode & decoded );

protected:
    virtual ~IUniDeco() = default;
};

/// A partial implementation to validate and canonicalize a Unicode string
/// on a character by character basis.
struct CharDeco : public IUniDeco
{
    bool irregular( const Unicode & decoded, size_t * start ) const override;
    Unicode decompose( const Unicode & decoded, size_t start = 0 ) const override;

protected:
    /// Check whether a Unicode character is allowed.
    virtual bool irreguchar( wchar_t wc ) const = 0;
    /// Write out (append) a representation of a Unicode character wc.
    virtual void decompochar( Unicode & out, wchar_t wc ) const = 0;
};

/// Correct a Unicode string based on a replacement (char -> char-sequence) map.
struct CMapDeco : public CharDeco
{
protected:
    bool irreguchar( wchar_t wc ) const override;
    void decompochar( Unicode & out, wchar_t wc ) const override;

    std::map<wchar_t, Unicode> illegal;
};

/// Decompose Central European characters in HFS+ (Apple) canonical way.
struct EuroDeco : public CMapDeco { EuroDeco(); };

/// Decompose Korean Hangul characters in HFS+ (Apple) canonical way.
struct HangDeco : public CharDeco
{
protected:
    bool irreguchar( wchar_t wc ) const override;
    void decompochar( Unicode & out, wchar_t wc ) const override;
};

/// An API to encodes a Unicode string into a target representation.
struct ICharPack
{
    // used for upper estimates only in order to pre-allocate buffers
    virtual size_t charSize() const = 0;

    /// @return characters written
    virtual size_t compress( std::string & target, const Unicode & source ) const = 0;

    /// @param[in,out] size     upper bound on bytes to write; bytes written.
    /// @param[in,out] target   target pointer to write and advance.
    /// @param[in]     source   Unicode string to convert
    /// @return characters written
    size_t compress( uint8_t & size, char *& target, const Unicode & source ) const;

    // overload for const/temporary target?
    // inline size_t compress( uint8_t & size, char * target, const Unicode & source ) const
    // { compress( size, target, source ); }

    /// @param[in,out] size upper bound on bytes to write; bytes written.
    /// @param[in,out] target   target pointer to write and advance.
    /// @param[in,out] begin    source string iterator to read from
    /// @param[in]     end      the terminal source string position (exclusive)
    /// @return characters written
    virtual size_t compress( uint8_t & size, char *& target,
                             Unicode::const_iterator & begin,
                             const Unicode::const_iterator & end ) const = 0;

    /// write the byte count, then actual bytes into file (no padding)
    /// @param[in] fd file descriptor to write into
    /// @return characters written
    virtual size_t compress( int fd, const Unicode & source ) const = 0;

    // write it later
    // template<typename WC> void Scatter( WC *& in ) {}
    // template<typename WC, typename... Bufs> Scatter( WC *& in, Bufs && bufs ) { }

    virtual ~ICharPack() = default;
};

/// A partial implementation of ICharPack that encodes on a per-character basis.
struct CharType : public ICharPack
{
    size_t compress( std::string & target, const Unicode & source ) const override;
    size_t compress( uint8_t & size, char *& target,
                     Unicode::const_iterator & begin,
                     const Unicode::const_iterator & end ) const override;
    size_t compress( int fd, const Unicode & source ) const override;

    typedef std::function<void( char )> WriteChar;
    virtual void packChar( wchar_t wc, WriteChar sink ) const = 0;
};

/// Writes a Unicode string as an ANSI string.
struct CharANSI : public CharType
{
    size_t charSize() const override { return 1; }
    void packChar( wchar_t wc, WriteChar sink ) const override;
};

/// Writes a Unicode string as a UCS-2 string (CDFS/Joliet).
struct CharUCS2 : public CharType
{
    size_t charSize() const override { return 2; }
    void packChar( wchar_t wc, WriteChar sink ) const override;
};

/// Writes a Unicode string as a FAT32 long name.
struct CharLFN : public CharType // FIXME use endianness primitives explicitly (UCS2-LE, UCS2-BE)
{
    size_t charSize() const override { return 2; }
    void packChar( wchar_t wc, WriteChar sink ) const override;
};

#endif // STRENC_H
