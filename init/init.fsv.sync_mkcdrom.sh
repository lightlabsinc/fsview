#!/bin/sh

# Copyright (c) 2022 Light Labs Inc.
# All Rights Reserved
# Released under the MIT license.

/sbin/fsview_fork --zero-in=32k --src virtualhd --trg virtualcd --err=/dev/kmsg && /system/bin/setprop light.sync.status 2

# amen
true
