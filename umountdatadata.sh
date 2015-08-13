#!/system/bin/sh
#
# FIX-ME
# Dirty hack to unmount /data/data so the phone doesn't hang when encrypting

PATH=/system/bin/:/system/xbin/

busybox umount /data/data
