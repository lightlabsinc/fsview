/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#include "rlimit.h"

#include <iostream>
#include <fstream>

res_limit_t FsMaxFiles()
{
    std::ifstream fm( "/proc/sys/fs/file-max", std::ifstream::in );
    res_limit_t maxFiles;
    return fm >> maxFiles, maxFiles ? maxFiles : RLIM64_INFINITY;
}

res_limit_t GetFDLimit()
{
    struct rlimit64 out;
    getrlimit64( RLIMIT_NOFILE, &out );
    return out.rlim_cur;
}

bool SetFDLimit( res_limit_t maxFiles )
{
    struct rlimit64 set, out;
    set.rlim_max = maxFiles;
    set.rlim_cur = maxFiles;

    return ( prlimit64( getpid(), RLIMIT_NOFILE, &set, &out ) >= 0 )
           || ( setrlimit64( RLIMIT_NOFILE, &set ) >= 0 );
}

bool RaiseFdLimit()
{
    res_limit_t maxFiles = FsMaxFiles();
    return ( maxFiles > GetFDLimit() )
           && SetFDLimit( maxFiles );
}
