/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef RLIMIT_H
#define RLIMIT_H

#include "wrapper.h"

namespace {}

/// Helper methods used to grab as high an fd number limit as we can get,
/// since the fsview_mkfs daemon needs to hold the fds of all the exposed
/// files to prevent their overwriting during external disk mounted life.

typedef decltype( rlimit64().rlim_cur ) res_limit_t;

res_limit_t FsMaxFiles();
res_limit_t GetFDLimit();
bool SetFDLimit( res_limit_t maxFiles );
bool RaiseFdLimit();

#endif // RLIMIT_H
