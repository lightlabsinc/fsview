#!/bin/sh

# Copyright (c) 2022 Light Labs Inc.
# All Rights Reserved
# Released under the MIT license.

listluns () {
	echo 'LUN*/FILE'
	cat /sys/class/android_usb/android0/f_mass_storage/lun*/file
}

shutdown () {
	echo > /sys/class/android_usb/android0/f_mass_storage/lun1/file && echo > /sys/class/android_usb/android0/f_mass_storage/lun0/file &&  echo > /sys/class/android_usb/android0/f_mass_storage/lun/file && /system/bin/setprop light.sync.status 3
}

listluns > /dev/kmsg # original
if ! shutdown # try
then
	listluns > /dev/kmsg # leftover
	echo -n 0 > /sys/class/android_usb/android0/enable
	shutdown # retry
	echo -n 1 > /sys/class/android_usb/android0/enable
	if getprop sys.usb.state | grep adb
	then /system/bin/setprop light.sync.needadbd $RANDOM
	fi
fi
listluns > /dev/kmsg # eventual

# amen
true
