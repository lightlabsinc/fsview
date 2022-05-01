#!/bin/sh

# Copyright (c) 2022 Light Labs Inc.
# All Rights Reserved
# Released under the MIT license.

HASH=$(/sbin/fsview_hash $(/system/bin/getprop ro.serialno))
PATH=$(/system/bin/getprop light.sync.scratch)
TMPD=$(/system/bin/getprop light.sync.tempdir)
TMPN=$(/system/bin/date +%s).$RANDOM.bag
SIZE=$(/system/bin/getprop light.sync.reserve)
TEMP="$TMPD/$TMPN"

# The alternative is to use newfs_msdos here;
# that would lean to larger clusters, though.
/sbin/fsview_temp --size="$SIZE" --sparse --label=CAMSD$HASH --trg="$TEMP" --root="$PATH" --err=/dev/kmsg && /system/bin/chmod 0660 "$TEMP" && /system/bin/chown system:system "$TEMP" && /system/bin/setprop light.sync.handoff "$TEMP"

# amen
true
