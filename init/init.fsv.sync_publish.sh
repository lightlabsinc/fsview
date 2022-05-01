#!/bin/sh

# Copyright (c) 2022 Light Labs Inc.
# All Rights Reserved
# Released under the MIT license.

echo -n $(/sbin/fsview_name virtualhd) >  /sys/class/android_usb/android0/f_mass_storage/lun/file

echo -n $(/sbin/fsview_name virtualcd) >  /sys/class/android_usb/android0/f_mass_storage/lun0/file

echo -n $(/system/bin/getprop light.sync.handoff) > /sys/class/android_usb/android0/f_mass_storage/lun1/file

# amen
true
