/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef CMDARG_H
#define CMDARG_H

#include "wrapper.h"

/// A wrapper for an ordered map key backed by a C-string constant.
/// Avoids redundant allocations and string copying by value.
struct StrKey
{
    const char * value;

    inline bool operator ==( const StrKey & other ) const
    { return strcmp( value, other.value ) == 0; }
    inline bool operator <( const StrKey & other ) const
    { return strcmp( value, other.value ) < 0; }
    inline operator const char * () const { return value; }
    inline StrKey() : value( "" ) {}
    inline StrKey( const StrKey & other ) = default;
    inline StrKey( const char * val ) : value( val ) {}
};

/// An abstract configurable interface. Reusable(tm).
struct ConfCb
{
    typedef std::function<void()> OnFlag;                       /// Handler a parameterless modifier (flag).
    typedef std::function<void( int )> OnAtoi;                  ///< Handler of an integer value.
    typedef std::function<void( long )> OnAtol;                 ///< Handler of a long integer value.
    typedef std::function<void( char * )> OnAttr;               ///< Handler of a C-string. May delimit the passed value.
    typedef std::function<void( int, char ** )> OnArgs;         ///< Handler of the entire command line (argc, argv)
    typedef std::map<StrKey, bool> MustHave;                    ///< A map of required options.

    void expectFlag( const char * name, bool & lvFlag );        ///< Handle a flag by setting the provided boolean l-value.
    void expectFlag( const char * name, OnFlag onFlag );        ///< Handle a flag by calling the provided handler.

    void expectAtoi( const char * name, int & lIntPtr );        ///< Handle an int by setting the provided integer l-value.
    void expectAtoi( const char * name, OnAtoi onAtoi );        ///< Handle an int by calling the provided handler.

    void expectAtol( const char * name, long & lIntPtr );       ///< Handle a long int by setting the provided long l-value.
    void expectAtol( const char * name, OnAtol onAtol );        ///< Handle a long int calling the provided handler.

    void expectAttr( const char * name, const char *& lAttr );  ///< Handle a string by setting the provided pointer.
    void expectAttr( const char * name, OnAttr onAttr );        ///< Handle a string by calling the provided handler.

    /// Convenience macros for explicit binding by object pointer and method pointer.
#define BIND(fname, atype) \
    [ptr, func]( atype val ) { (*ptr.*func)( val ); }

#define BIND_EXPECT(fname, atype) \
    template<typename T> \
    void fname( const char * name, T * ptr, void( T::*func )( atype ) ) \
    { fname( name, BIND(fname, atype) ); }

    BIND_EXPECT( expectAttr, char * )
    BIND_EXPECT( expectAtoi, int )
    BIND_EXPECT( expectAtol, long )

protected:
    /// Store the attribute callback.
    /// "validation" is either no_argument, required_argument or optional_argument.
    virtual void expectImpl( const char * name, OnAttr onAv, int validation ) = 0;

protected:
    /// Display an error message if a required attribute is not provided.
    void raiseMissing( const char * mustHave );
    /// Display an error message if any of the required attributes is not provided.
    void raiseMissing( MustHave & mustHave );

    virtual ~ConfCb() = default;
};

/// A specialized configuration sink that processes sub-options (-i a,b=c,d=e)
struct SubOpt : public ConfCb
{
    /// Suboption name -> handler callback.
    std::map<StrKey, OnAttr> callbacks;
    /// A callback to handle unrecognized (or free-form) options. No-op by default.
    std::function<void( const char *, char * )> onOther = []( const char *, char * ) {};
    /// A minimal (required) set of options.
    MustHave minimal;

    void expectImpl( const char * name, OnAttr onAttr, int validation ) override;

    /// Expect a free-form string, such as a file name or path.
    void expectName( const char *& name );

    /// Expect a free-form name-value pair.
    void expectName( const char *& name, const char *& value );

    /// Populate the configuration from a provided option string.
    /// Call this method once all the expectations have been defined.
    void parse( char * options );

private:
    /// Skip any characters that are member of expr.
    static void skipAnyOf( char *& input, const char * expr );
    /// Skip any characters that are NOT members of expr.
    static void seekUntil( char *& input, const char * expr );
};

/// A specialized configuration sink that processes command line options.
struct CmdArgs : public ConfCb
{
    std::vector<struct option> options;
    std::vector<OnAttr> callbacks;
    std::string shortcuts;
    OnArgs main = []( int, char ** ) {};

    /// Set handler of remaining unparsed arguments.
    void expectArgs( OnArgs onArgs ) { main = onArgs; }

    /// Populate the configuration from a provided command line.
    /// Call this method once all the expectations have been defined.
    void parse( int & argc, char ** & argv );

    /// Check if the provided string "looks like" an absolute path.
    static inline bool isAbsPath( const char * path )
    { return path && ( '/' == *path ); }

protected:
    void expectImpl( const char * name, OnAttr onAttr, int validation ) override;

private:
    int nextIndex() const { return options.size() + 1; }
};

#endif // CMDARG_H
