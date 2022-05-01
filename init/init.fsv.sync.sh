#!/bin/sh

# Copyright (c) 2022 Light Labs Inc.
# All Rights Reserved
# Released under the MIT license.

/sbin/fsview_fork --unmount=/data --src=userdata --trg=offshoot --err=/dev/kmsg && /system/bin/mount -t ext4 -o nosuid,nodev,barrier=1,noauto_da_alloc,discard $(/sbin/fsview_name offshoot --err=/dev/kmsg) /data &&  /system/bin/setprop light.sync.status 0
