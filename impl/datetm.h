/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef DATETM_H
#define DATETM_H

#include <functional>

namespace {}

// POSIX time helpers.

/// Time value sink.
typedef std::function<void( struct tm &, int )> SetTm;

/// Assign the provided time to a provided value sink.
void SetTime( const struct timespec & ts, SetTm setTm );

/// Assign the current time to a provided value sink.
void SetNow( SetTm setTm );

template<typename O> // replacement for std::bind(this)
SetTm TmSet( O * o ) { return [o]( struct tm & t, int centis ) { o->setTm( t, centis ); }; }

#endif // DATETM_H
