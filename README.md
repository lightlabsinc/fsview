# What's this?

*Filesystem View* is a software solution originally written to combine an arbitrary set of files resident on a Linux device partition into a disk image or a virtual block device, generating filesystem metadata on the fly. The resulting device/image can be uploaded for distribution, burned onto a CD/DVD or exposed as a USB storage device in gadget mode.

# Why?

The original intended use of *Filesystem View* was low-latency, host OS friendly exposure of a media collection (raw and processed photos, video recordings, etc.) via a wired USB connection. While the MTP/PTP protocol family is adopted on almost all modern consumer electronic devices, media exposure via a virtual storage device allows the host OS to treat the plugged-in media device as a first class filesystem, enabling features like caching, mapping, file scanning and immediate data availability to user applications. While plugging in an MTP/PTP-visible device with a decent photo collection synchronizes the catalog and displays image thumbnails on the host device within seconds and tens of seconds, plugging in a device running *Filesystem View* displays image thumbnails in a fraction of a second (this time includes filesystem metadata generation on demand). While we anticipate further evolution of multimedia transfer protocols, general purpose filesystems are likely to stay the most common, tightly optimized and extensively supported asset library implementations provided by general purpose operating systems.

# Dependencies and compatibility

## Device environment

The C++ code for virtual block device setup and teardown is intended for a generic Linux environment, has been tested against the Linux kernel version 3.18 and can reasonably be expected to work with newer kernels (unpriviliged userspace functionality seems to be fine with 4.15). While the provided build files (`*.mk`) and integration hints (see below) are Android specific, the required device features are available in the mainline kernel and have no other runtime dependencies. The extent API is not specific to `ext4` but has only been tested against it. Decoupling from Linux is theoretically possible but will require a significant porting effort.

The most important kernel feature/module dependencies include:

* [USB mass storage gadget](https://docs.kernel.org/usb/mass-storage.html)
* [block device mapper](https://docs.kernel.org/admin-guide/device-mapper/index.html)
* [ZRAM (compressed ramdisk)](https://docs.kernel.org/admin-guide/blockdev/zram.html)
* [the extent API](https://docs.kernel.org/filesystems/fiemap.html)
* anonymous files (`man 2 memfd_create`)
* sparse files(`man 2 fallocate`)

All of these dependencies are optional, but you probably won't like the workaround.

## Host environment

*Filesystem View* is capable of exposing HFS+, CDFS and FAT32 virtual filesystems. HFS+ (non-cdrom mode) is recommended for Mac OS hosts; CDFS is recommended for Windows and Linux hosts. Neither NTFS nor exFAT are currently supported.

## Data integrity

When *Filesystem View* "pins" externally exposed files by keeping their `fd`s (file descriptors) open, it relies upon the inode/dirent separation maintained in Unix-family OSes (most famously in Linux). The physical blocks occupied by the file data are owned by the inode; the open file descriptor also points to the inode; the dirent (directory entry) is only used to locate the inode by the file path. As a result, in Linux, one can delete a file (from a directory) and have it (on the disk) at the same time; the physical disk space is only reclaimed when neither a directory entry nor an open file descriptor point to the file inode. For *Filesystem View* purposes, exposed files can be deleted (on the device) while exposed and still stay available to the host until the device is disconnected and the virtual storage device is torn down. In-place modifications to exposed files may or may not be visible to the host applications because of the host filesystem cache, and therefore are not advised. In-place reclamation of space (e.g. by truncating the file, punching a hole, etc.) is STRONGLY DISCOURAGED, as it will lead to unintended exposure of data subsequently written to the reclaimed space.

## Localization

Non-ANSI characters in file names are supported in all exposed virtual file systems. UTF-8 device encoding is assumed. No effort has been made to verify Unicode support in its entirety, though some known corner cases have been addressed.

# How to build it?

* [CMakeLists.txt](CMakeLists.txt) has been provided for portability.
* [Android.mk](Android.mk) and [Application.mk](Application.mk) have been provided to ease integration into an Android firmware build.
* `bfs.sh` is a naive but handy shell script that compiles, links, uploads and runs an `fsview_*` executable named in the first command line parameter. The remaining command line parameters are forwarded to it.

The script is self-contained. To use it, consider tweaking the local paths according to your comfortable environment.

The common implementation library ([conf](conf) and [impl](impl)) is linked into all the end binaries statically. Even though binary footprint may be a concern for a proof-of-concept embedded system, full static linkage is still advised for any code runnable early in the boot sequence.

# How to use it?

## A simple example

Create a burnable DVD image file from the contents of your Movies folder:

    sudo fsview_mkfs --trg=$HOME/mydocs.iso --mkfs=cdfs,label=MYVIDEOS $HOME/Movies

The original files will not be "pinned"; rather, their contents will be written into the DVD image.

## An advanced example

Let's assume that your home directory is encrypted and its decrypted representation resides on `/dev/mapper/userdata`. Since mounted volumes cannot be mirrored by the device mapper directly, you will need to mirror the underlying volume and re-mount the mirror:

    sudo fsview_fork --src=userdata --trg=offshoot --unmount=$HOME \
        --dm-control=/dev/device-mapper --num-catalog=/sys/dev/block && \
    sudo mount -t ext4 -o nosuid,nodev,barrier=1,noauto_da_alloc,discard $(fsview_name offshoot) $HOME

The normal rule is that volumes can be either mounted or mirrored, but not both; however, since regular files cannot be combined into the virtual block device (unless they are declared as loop devices -- and a large number of loop devices can be prohibitively costly to have), we combine the original file extents into the virtual block device instead, and to do that, we need to bypass the rule. The above mirroring and remounting does exactly that: it fools the device-mapper module into thinking that `userdata` (which is still effectively backing a mounted filesystem) can be safely mirrored elsewhere.

Now the following command will create a virtual hybrid (CDFS/HFS+) block device from the contents of your Movies folder (skipping `*.tmp` files):

    sudo fsview_mkfs --mkfs=hfsx,label=MOVIEHD,cdfs,label=MOVIECD --trg=virtualhd --exclude='.*tmp' \
        --dm-control=/dev/device-mapper --num-catalog=/sys/dev/block \
        --tmp=/dev/block/zram1 --zram-control=/sys/block/zram1 \
        --subst=offshoot=userdata --daemonize $HOME/Movies

The `--subst` argument is used to locate the original mirrored volume -- that is, to navigate from the mounted volume back to the original (mirrored and mirrorable) volume.

The original files will be "pinned" (that is, it will be safe to rename or delete them, though still unsafe to modify them in place); the metadata will be placed in ZRAM; and the virtual block device will be registered with `device-mapper` as `virtualhd`. The "pin" will stay in place until the `fsview_mkfs` process is terminated by `SIGTERM`.

Once you no longer need the virtual device, `fsview_down --dm-control=/dev/device-mapper virtualhd` will tear it down.

## Pushing the bounds

* To allow a single process to "pin" a lot (like, A LOT) of files, you may need to raise the respective `rlimit` (e.g. with `ulimit -n`).
* To allow registering large (like, LARGE) virtual CD images with the USB storage gadget, remove the lines in `drivers/usb/gadget/function/storage_common.c` that limit `num_sectors` to `256*60*75`.

To allow smaller CD images, you can also set `min_sectors` to 20 (reserved area + CDFS metadata).

## Android integration

A possible Android integration strategy is described in [ANDROID.md](ANDROID.md).

# Developer notes

A more or less granular class/source file breakdown is provided in [SOURCES](SOURCES).

There are two steps of filesystem creation:

* analysis of the source file set. Directories are traversed recursively, exclude patterns rule out unwanted files, but, most importantly, each file is subjected to extent analysis. If the target representation is a regular file, source files are represented by a single logical extent within themselves; if the target representation is a mapped device, the actual layout of the file body blocks is retrieved from the underlying filesystem. The results are placed in `Original`.
* generation of the output filesystem by the `Volume` object backed by two `Burner` objects: temporary (to accumulate and host the generated filesystem metadata) and eventual (to assemble the full disk image file or virtual block device). The `Volume` object is filesystem-dependent; the `Burner` object is medium-dependent.

It is possible to create an empty file system by passing an empty file set.

A single generated disk image/block device can contain multiple filesystems. The required cooperation between `Volume` objects is accomplished via callbacks specified in the `Hybrid` interface.

It is highly recommended to set the optional `--tolerance` parameter (`MkfsConf::extent_gap`) to nonzero when the output is a mapped block device to reduce the number of individual extents being mirrored. Unless there is a tight limit on the disk size supported by the host OS, it should be set as high as possible, since large (even astronomically large) block numbers come at no cost while extra extents slow down block lookup. Zero tolerance, however, is advised if the original block device contains sensitive information and no blocks other than making up the enumerated files must be exposed.

While virtually any host OS would understand FAT32, the need to explicitly describe each cluster (rather than just every extent) places a practically prohibitive temporal cost on generation of the FAT proper; in a nutshell, we need to write a `uint32_t` value for every cluster (potentially, millions of them) that almost always points to the next index in the series. To (somewhat) speed up this stupid job, `VFatMedium::fill` uses NEON vectorization on ARM and SSE2 on Intel, provided the respective macros have been set by the compiler. The SSE2 version is a later addition for portability. It's been tested, but not as extensively as ARM/NEON.

## Outstanding / unimplemented

FAT32 output is knowingly flaky; not _indeterministically_ flaky, but only borderline compliant. Cluster sizes less than 2K are likely to break reserved sector logic; clusters of 512 bytes are completely unsupported. Nontrivial, nonempty MS-DOS compatible (8.3) file names aren't produced at all (the host is expected to always use the long name); this isn't an issue with Windows and Mac OS hosts, but seems to confuse Linux. If you'd like to troubleshoot that, look at the `SEEDCLS` constant and its uses in `VFat32Out`. Its present value of 2 is an overassumption.

"Laning" is the name of a possible optimization that may be relevant when the source drive block/cluster size is smaller (more granular) than the target's; in this case, instead of starting the next target extent whenever the remainder changes, it may be beneficial to map all the source extents that share a common remainder in one pass. The present logic iterates source extents in the starting block ascending order.

"Inode jamming" stands for a (desirable) strategy to resolve `ino_t` collisions (which are a possibility if source files span multiple filesystems) on a stable basis. Source file inodes are used verbatim as target file identifiers in HFS+ to provide a host-visible file identity that spans multiple virtual device reconstructions and reconnections. However, such stability is not guaranteed when ad-lib `ino_t` numbers are generated in order to resolve a conflict.

In general, certain scenarios uncommon in an Android environment might not have been properly addressed.

# Legal

The code is released under [MIT license](LICENSE).
