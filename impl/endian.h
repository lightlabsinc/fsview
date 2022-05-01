/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef ENDIAN_H
#define ENDIAN_H

#include <byteswap.h>

// Endian-aware integral types.

/// The endian detection bait.
const int Unity = 1;
/// Whether the current architecture is little-endian (least significant bytes first).
const bool IsLE = *( const char * ) & Unity;
/// Whether the current architecture is big-endian (least significant bytes last).
const bool IsBE = !IsLE;

template<typename I> inline I Swap( const I & src );

// Definitions of byte swap operations for various primitive types.
#define SWAP(issigned, bitcount) \
    template<> \
    inline issigned##bitcount##_t Swap<issigned##bitcount##_t>( const issigned##bitcount##_t & src ) \
    { return bswap_##bitcount( src ); }

#define SWAP_WORD(bitcount) \
    SWAP(int, bitcount) SWAP(uint, bitcount)

SWAP_WORD( 16 )
SWAP_WORD( 32 )
SWAP_WORD( 64 )

SWAP( char, 16 )
//SWAP( char, 32 )

/// A generic endian-aware primitive type container.
/// I = primitive type
/// L = "is little endian"
/// L reflects the bit representation of the stored value.
template <typename I, bool L>
struct __attribute( ( packed ) ) SB
{
    I val;

    /// Assign a raw default-endian value and represent in the proper endianness.
    /// (Or vice versa.)
    inline static I Swin( I other ) { return ( L ^ IsLE ) ? Swap( other ) : other; }

    /// Return the current value in platform endianness.
    inline operator I() const { return Swin( val ); }

    inline SB & operator=( I other ) { val = Swin( other ); return *this; }
    inline SB( I other ) { ( *this ) = other; }

    SB() : val( 0 ) {} // works with GNU STL, requires a default ctor w/ C++ STL
    // SB() = default; // have to sanitize for uninitialized memory in all structures
    SB( const SB & other ) = default;
    SB & operator=( const SB & other ) = default;

    inline bool operator==( const SB & other ) const { return val == other.val; }

    /// Compare values in platform endianness.
    inline bool operator< ( const SB & other ) const { return I( *this ) < I( other ); }
};

template <typename I> using LSB = SB<I, true>;
template <typename I> using MSB = SB<I, false>;

/// A "both-endian" CDFS-specific type that stores a primitive value in both byte orders.
template <typename I>
struct __attribute( ( packed ) ) Bilateral
{
    LSB<I> lsb;
    MSB<I> msb;

    inline I toInt() const { return IsBE ? msb.val : lsb.val; }
    inline operator I() const { return toInt(); }
    inline Bilateral & operator=( I other ) { msb = other; lsb = other; return *this; }
    inline Bilateral( I other ) { ( *this ) = other; }

    inline Bilateral() = default;
    inline Bilateral( const Bilateral & other ) = default;
    inline Bilateral & operator =( const Bilateral & other ) = default;
};

#endif // ENDIAN_H
