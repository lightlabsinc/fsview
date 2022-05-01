/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef STRINGS_H
#define STRINGS_H

#include "wrapper.h"

#include <cwchar>

#ifdef USE_GNU_STDIO_FILEBUF
    #include <iostream>
    #include <iosfwd>
    #include <fstream>
    #include <ext/stdio_filebuf.h>
#endif

// I hate doing it that way but we gotta start with a UTF-8 parser.
// reason:
// 1E+100 made locale support nonpublic / Java only
// 1E+100 made getprop support nonpublic too (Java has to reflect)

/*
 * Some benchmarks (conversions of a short Cyrillic phrase
 * and a short Mandarin phrase repeated 1000 times):
 *
 * root@LFC:/ # time /data/local/tmp/buildfs
    0m0.06s real     0m0.03s user     0m0.03s system // both
root@LFC:/ # time /data/local/tmp/buildfs
    0m0.05s real     0m0.03s user     0m0.01s system // memfd
root@LFC:/ # time /data/local/tmp/buildfs
    0m0.04s real     0m0.02s user     0m0.04s system // utf8
*/

typedef std::wstring Unicode;

/// A few placeholder "un-characters", or "unrecognized characters".
struct UnChar
{
    static constexpr const wchar_t UCS2 = 0xfffd; // black diamond
    static constexpr const wchar_t ANSI = '?';
    static constexpr const wchar_t CDFS = '_';
    static constexpr const wchar_t STOP = '.';
};

/// An abstract C-string decoder into Unicode.
struct IDecoder
{
    virtual ~IDecoder() = default;
    virtual void parse( Unicode & out, const char * in ) = 0;
    Unicode decode( const char * in ); // template method
};

/// A no-dependency implementation of a UTF8 decoder.
struct UTF8Homebrew : public IDecoder
{
    static constexpr const int FORBIDDEN = -1;
    static constexpr const int CONTINUED = -2;

    /// Checks whether a byte is allowed in UTF-8 as an opening byte.
    inline static bool forbidden( char c ) { return ( c & 0xf0 ) == 0xf0 || c == static_cast<char>( 0xc1 ); } // c == 0xc0 begins '\0' in modified UTF-8

    /// Checks whether a byte is a continuation (mid-sequence) byte in UTF-8.
    inline static bool continued( char c ) { return ( c & 0xc0 ) == 0x80; }

    /// Given c is the opening byte, return the number of trailing bytes.
    inline static int trailCount( char & c ); // MOREINFO replace with wchar_t to speed up?

    void parse( Unicode & out, const char * source ) override;
};

/// A trivial ISO-8859-1 -> UTF-8 decoder (simply sets the high byte to 0).
struct ISO88591Flat : public IDecoder
{
    void parse( Unicode & out, const char * source ) override;
};

#ifdef USE_GNU_STDIO_FILEBUF

/// A trick to decode any platform-specific encoding.
/// Very unfortunately, comes under a toxic license.
///
/// See this to reimplement without GNU dependencies:
/// https://www.codeproject.com/Articles/38242/Reading-UTF-8-with-C-streams
struct RawStreamBuf : public IDecoder
{
    RawStreamBuf();
    ~RawStreamBuf();

    void parse( Unicode & out, const char * source ) override;
private:
    int fd;
    __gnu_cxx::stdio_filebuf<wchar_t> fb;
    std::wistream is;
};
#endif

#endif // STRINGS_H
