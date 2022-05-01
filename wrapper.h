/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef WRAPPER_H
#define WRAPPER_H

#include "allsys.h" // GLibC

// Includes from the standard library + useful container and smart pointer definitions.

#include <algorithm>
#include <functional>
#include <utility>

#include <memory>   // shared pointers
#include <list>     // linked lists
#include <map>      // sorted maps

#include <string>
#include <vector>
#include <set>

template<typename T> using Ptr = std::shared_ptr<T>;
template<typename T> using Index = std::list<T *>;
template<typename T> using List = std::list<Ptr<T>>;

/// Shorthand for a Java-style "newInstance()" call without parameters.
template<typename T> inline Ptr<T> New() { return std::shared_ptr<T>( new T() ); }

/// Shorthand for a Java-style "newInstance()" call with constructor parameters.
template<typename T, typename... Args> inline Ptr<T> New( Args && ... args ) { return std::make_shared<T>( std::forward<Args>( args )... ); }

/// Shorthand for a map with value-semantic keys and pointer-semantic values.
template<typename K, typename T> using Map = std::map<K, Ptr<T>>;

/// A no-op "deleter" for a "shared pointer" effectively referencing memory managed elsewhere.
template<typename T> void Leave( T * ) {}
template<typename T> inline Ptr<T> Temp( T * ptr ) { return std::shared_ptr<T>( ptr, &Leave<T> ); }

#endif // WRAPPER_H
