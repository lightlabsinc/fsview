/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef _ALL_LINUX_PCH
#define _ALL_LINUX_PCH

namespace {}

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <malloc.h>

#include <uchar.h>
#include <wchar.h>
#include <wctype.h>
#include <time.h>
#include <byteswap.h>

#include <fcntl.h>
#include <errno.h>
#include <error.h>

#include <dirent.h>

#include <signal.h>

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <sys/capability.h>
#include <sys/resource.h>

#include <sys/ioctl.h>

#include <linux/blkpg.h>
#include <linux/blktrace_api.h>

#include <linux/fiemap.h>
#include <linux/dm-ioctl.h>

#include <sys/mount.h>
#include <sys/klog.h>

// must follow sys/mount.h (kernel and glibc header coordination issue)
// see: https://bugzilla.redhat.com/show_bug.cgi?id=1497501
#include <linux/fs.h>

#include <getopt.h>
#include <sys/utsname.h>

#include <sys/sendfile.h>
#include <sys/syscall.h>
#include <sys/mman.h>

/// Open a temporary memory-resident file accessible by its integer fd.
/// The file exists until it's closed by all the processes that use it.
static inline int memfd_open( const char * name, unsigned int flags ) { return syscall( SYS_memfd_create, name, flags ); }

#if defined(ANDROID) || defined(__ANDROID__)
#include <sys/system_properties.h>
#else
inline int __system_property_set(const char*, const char*) { return EINVAL; }
#endif

#endif
