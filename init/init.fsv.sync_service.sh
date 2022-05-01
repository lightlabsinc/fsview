#!/bin/sh

# Copyright (c) 2022 Light Labs Inc.
# All Rights Reserved
# Released under the MIT license.

HASH=$(/sbin/fsview_hash $(/system/bin/getprop ro.serialno))
PATH=$(/system/bin/getprop light.sync.folders)

# TODO allow customization of the virtual filesystem (Mac/Win/both/FAT)
/sbin/fsview_mkfs --tmp=/dev/block/zram1 --zram-control=/sys/block/zram1 --subst=offshoot=userdata --mkfs=hfsx,label=CAMHD$HASH,cdfs,label=CAMCD$HASH --trg=virtualhd "$PATH" --exclude='.*tmp' --daemonize --setprop=light.sync.status=1 --err=/dev/kmsg
