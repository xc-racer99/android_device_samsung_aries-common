#!/sbin/busybox sh

# Check if UBI already exist
if /sbin/busybox test -e /dev/ubi0_0 ; then
	exit 0;
fi

/sbin/ubiformat /dev/mtd/mtd3 -y
/sbin/ubiattach -p /dev/mtd/mtd3

/sbin/ubimkvol /dev/ubi0 -s 19712KiB -N radio
/sbin/ubimkvol /dev/ubi0 -s 5888KiB -N efs
/sbin/ubimkvol /dev/ubi0 -s 40960KiB -N cache
/sbin/ubimkvol /dev/ubi0 -s 4096KiB -N uboot-scripts
/sbin/ubimkvol /dev/ubi0 -m -N system

# mount and unmount /radio to create UBIFS filesystem
/sbin/busybox mkdir -p /radio
/sbin/busybox mount -t ubifs ubi0:radio /radio
/sbin/busybox umount /radio

# mount efs regardless, copy from /sdcard/efs.tar if present
/sbin/busybox mkdir /efs
/sbin/busybox mount -t ubifs ubi0:efs /efs

/sbin/busybox mkdir /mnt-tmp
/sbin/busybox mount /dev/block/mmcblk0p1 /mnt-tmp

# try SGS4G backup method first
if /sbin/busybox test -e /mnt-tmp/backup/efs.tar ; then
	/sbin/busybox tar xf /mnt-tmp/backup/efs.tar
fi

# now aries-common backup method
if /sbin/busybox test -d /mnt-tmp/backup/efs ; then
	busybox cp -r /mnt-tmp/backup/efs /efs
fi

/sbin/busybox sync
/sbin/busybox umount /mnt-tmp
/sbin/busybox umount /efs
