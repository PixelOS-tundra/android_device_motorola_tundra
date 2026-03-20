#! /vendor/bin/sh
#=============================================================================
# Copyright (c) 2020-21 Qualcomm Technologies, Inc.
# All Rights Reserved.
# Confidential and Proprietary - Qualcomm Technologies, Inc.
#=============================================================================

soc_id=`cat /sys/devices/soc0/soc_id` 2> /dev/null

# Store soc_id in ro.vendor.qti.soc_id
setprop ro.vendor.qti.soc_id $soc_id

# For chipsets in QCV family, convert soc_id to soc_name
# and store it in ro.vendor.qti.soc_name.
setprop ro.vendor.qti.soc_name lahaina
setprop ro.vendor.qti.soc_model SM8350
fi
