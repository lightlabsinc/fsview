#!/bin/sh

# Copyright (c) 2022 Light Labs Inc.
# All Rights Reserved
# Released under the MIT license.

TEMP=$(/system/bin/getprop light.sync.handoff)
test -e "$TEMP" && /system/bin/am startservice -n co.light.sync.handoff/.HandOffService "file://$TEMP" && /system/bin/setprop light.sync.handoff ""

# amen
true
