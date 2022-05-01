/*
 * Copyright (c) 2022 Light Labs Inc.
 * All Rights Reserved
 * Licensed under the MIT license.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "conf/cmdarg.h"

/// Common configuration of all the utilities in the bundle.
struct StdArgs : public CmdArgs
{
    /// Specify a file path to redirect stdout.
    void rerouteOut( const char * path );

    /// Specify a file path to redirect stderr.
    void rerouteErr( const char * path );

    /// A shorthand to redirect a standard stream.
    static inline void redirect( FILE * stream, const char * path )
    { if( path ) { freopen( path, "w", stream ); } }

    /// Fill in the operating system information ("uname").
    const char * familiarize();

    const char * outPath = nullptr;
    const char * errPath = nullptr; // "/dev/kmsg" within init.rc

    struct utsname whoami;

    StdArgs();
};

// We could have parallel hierarchies of *Conf and *Args,
// rendering configuration definitions completely unaware
// of the argc/argv parsing mechanism. However, that would
// have introduced "ladder inheritance" (repeated diamond)
// and I don't want that many virtual bases. Therefore, we
// are keeping it simple: all configurations inherit from
// StdArgs and burden their parent with their expectations
// (pretty much what happens in a human family).
// So this is where *Args (generic) becomes *Conf (domain).

#define DEFAULT_SYSTEM "LIGHT_OS"

/// A common configuration that includes device and
/// control node paths, specifyng safe default values.
struct CtrlConf : public StdArgs
{
    const char * system     = DEFAULT_SYSTEM;
    const char * dev_cat    = "/dev/block";
    const char * num_cat    = "/sys/dev/block";
    const char * dmControl  = "/dev/device-mapper";

    CtrlConf();
};

/// The configuration of the main utility in the bundle: fsview_mkfs.
struct MkfsConf : public CtrlConf
{
    /// Input files (list)
    // --root [optional; default is in[0]]
    std::vector<const char *> entries; // (folder) (folder|file)...

    // --exclude (pattern...) ** [ use std::regex ]
    // in fact, we want std::wregex, or e'en a custom bool predicate on RawDirEnt=
    std::vector<const char *> ex;
    void exclude( char * pattern );

    /// Volume to expose (flag set)
    enum FSType
    {
        FS_Files   = 1 << 0, // dm-linear only, no metadata
        FS_Fat32   = 1 << 1,
        FS_CDFS    = 1 << 2,
        FS_HFSX    = 1 << 3,
        FS_CDHF    = FS_CDFS | FS_HFSX,
    };
    // --mkfs=(fat32|cdfs|hfsx|cdfs,hfsx) # can tokenize with getsubopt
    int fsType = 0, lastFs = 0;
    inline bool hybridAllowed( FSType type ) const { return !fsType || ( type | fsType ) == FS_CDHF; }
    void mkfs( FSType fs );

    /// Volume labels: values
    std::map<int, std::string> labels;

    /// Output (values, required); control nodes (values, optional)
    // --out=(disk name ("virtualcd") or file path ("/...")) # gso-tokenized!
    // --out=virtualhd,label=LHD,lun=lun # with suboptions
    inline bool hasValidTarget() const { return target; }
    inline bool isTargetMapped() const { return target && target[0] != '/'; }
    inline bool isTargetCopied() const { return target && target[0] == '/'; }

    const char * target = nullptr;

    // --temp=(path to temporary file or device)
    const char * buffer = nullptr;
    // --zram-control=/sys/block/zram1 (fallback: temporary file) *
    const char * zrControl = nullptr;
    inline bool isBufferRamdsk() const { return zrControl; }

    /// Surface substitutions:
    // --subst=/dev/block/dm-1=/dev/block/dm-0
    std::list<std::pair<const char *, const char *>> subst;
    void substitute( const char * found, const char * used );
    void mapDevices( std::function<dev_t ( const char * )> locate,
                     std::function<void( dev_t, dev_t )> put ) const;

    /// Inode translation: flag
    bool inode_jam = false;
    // --jam-inodes (obfuscate or allocate sequentially) **
    // [ default: 1:1 w/ conflict resolutions ]
    // [ multiple drives are leaking in lazily: use top bits ]

    /// Cost analysis and optimization: value, flag
    // --gap=1G extent merging
    off64_t extent_gap = -1;
    off64_t tolerance();

    // --lanes=2 # laning (FAT32) **
    // --lanes=4 # extreme laning
    blkcnt_t lanes = 1;
    void setLanes( int laneCnt );

    // --wipe-dust # pack small extents together **
    bool star_dust = false;

    /// Miscellaneous:
    // --crawl - close FDs asap (& not raise the limit) **
    bool crawl_fds = false;

    // --memfd - use memfds for temp files instead of std::vectors *
    bool use_memfd = false;

    // --wait-term - wait until SIGTERM (hold the file descriptors)
    bool daemonize = false;

    // --setprop - set properties when done (makes sense w/daemonize)
    typedef std::pair<const char *, const char *> Assignment;
    std::list<Assignment> setOnDone;

    MkfsConf();

private:
    SubOpt ds_opt;
    SubOpt fs_opt;
    SubOpt in_opt;
    SubOpt ex_opt;
    SubOpt ok_opt;
};

/// Configuration of fsview_fork, the utility that mirrors another device
/// via the device mapper, optionally zeroing a few initial sectors.
struct ForkConf : public CtrlConf
{
    const char * unmount = nullptr;
    long retries = 16;

    const char * forkSrc = nullptr; // "userdata" or "virtualhd"
    const char * forkTrg = nullptr; // "offshoot" or "virtualcd"

    long zoffset = 0;

    ForkConf();
};

/// Configuration of fsview_temp, the creator of a temporary FAT drive.
struct TempConf : public CtrlConf
{
    const char * target = nullptr;
    const char * vLabel = nullptr;
    const char * root = nullptr;

    bool sparse = false;
    off64_t size = 3 << 17; // ~400k

    TempConf();
};

/// Configuration of fsview_name, the reader of a device-mapper internal drive name.
struct NameConf : public CtrlConf
{
    const char * setprop = nullptr; // light.sync.path.<name>
    const char * oneprop = nullptr; // light.sync.path
    const char * tmp_cat = nullptr; // (/any/tmpfs)

    NameConf();
};

// fsview_down uses CtrlConf w/o extension

#endif // CONFIG_H
