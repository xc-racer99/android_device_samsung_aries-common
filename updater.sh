#!/tmp/busybox sh
#
# Copyright (C) 2008 The Android Open-Source Project
# Copyright (C) 2011 by Teamhacksung
# Copyright (C) 2013 OmniROM Project
#
# Modified by Humberto Borba <humberos@gmail.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Universal Updater Script for Samsung Galaxy S Phones
# Combined GSM & CDMA version
#

set -x
export PATH=/:/sbin:/system/xbin:/system/bin:/tmp:${PATH}

# 600MB
SYSTEM_SIZE='629145600';

# write logs to /tmp
set_log() {
    /tmp/busybox mkdir -p /tmp/omni;
    /tmp/busybox rm -rf /tmp/omni/"${1}";
    exec >> /tmp/omni/"${1}" 2>&1;
}

# restore logs from /tmp
restore_log() {
    local omni_log_path="${2}"/omni/log;
    if /tmp/busybox test -e /tmp/omni/"${1}" ; then
        mkdir -p "${omni_log_path}";
        /tmp/busybox cp /tmp/omni/"${1}" "${omni_log_path}"/"${1}";
    fi
}

# ui_print
OUTFD=$(\
    /tmp/busybox ps | \
    /tmp/busybox grep -v "grep" | \
    /tmp/busybox grep -o -E "/tmp/updater .*" | \
    /tmp/busybox cut -d " " -f 3\
);

if /tmp/busybox test -e /tmp/update_binary ; then
    OUTFD=$(\
        /tmp/busybox ps | \
        /tmp/busybox grep -v "grep" | \
        /tmp/busybox grep -o -E "update_binary(.*)" | \
        /tmp/busybox cut -d " " -f 3\
    );
fi

ui_print() {
    if [ "${OUTFD}" != "" ]; then
        echo "ui_print ${1} " 1>&"${OUTFD}";
        echo "ui_print " 1>&"${OUTFD}";
    else
        echo "${1}";
    fi
}

# Check if we're in CDMA or GSM mode
if /tmp/busybox test "${1}" = cdma ; then
    # CDMA mode
    IS_GSM='/tmp/busybox false';
    SD_PART='/dev/block/mmcblk1p1';
    MMC_PART1='/dev/block/mmcblk0p1';
    MMC_PART2='/dev/block/mmcblk0p2';
    MTD_SIZE='490733568';
else
    # GSM mode
    IS_GSM='/tmp/busybox true';
    SD_PART='/dev/block/mmcblk0p1';
    MMC_PART1='/dev/block/mmcblk0p1';
    MMC_PART2='/dev/block/mmcblk0p2';
    EFS_PART="$(/tmp/busybox grep efs /proc/mtd | /tmp/busybox awk '{print $1}' | /tmp/busybox sed 's/://g' | /tmp/busybox sed 's/mtd/mtdblock/g')";
    RADIO_PART="$(/tmp/busybox grep radio /proc/mtd | /tmp/busybox awk '{print $1}' | /tmp/busybox sed 's/://g' | /tmp/busybox sed 's/mtd/mtdblock/g')";
    MTD_SIZE='442499072';
fi

# fix package name
fix_package_location() {
    local PACKAGE_LOCATION=${1};
    # Remove leading /mnt for Samsung recovery
    PACKAGE_LOCATION=${PACKAGE_LOCATION#/mnt};
    # Convert to modern sdcard path
    PACKAGE_LOCATION=$(echo "${PACKAGE_LOCATION}" | /tmp/busybox sed -e "s|^/sdcard/||");
    PACKAGE_LOCATION=$(echo "${PACKAGE_LOCATION}" | /tmp/busybox sed -e "s|^/emmc/||");
    PACKAGE_LOCATION=$(echo "${PACKAGE_LOCATION}" | /tmp/busybox sed -e "s|^/external_sd/||");
    PACKAGE_LOCATION=$(echo "${PACKAGE_LOCATION}" | /tmp/busybox sed -e "s|^/storage/sdcard0/||");
    PACKAGE_LOCATION=$(echo "${PACKAGE_LOCATION}" | /tmp/busybox sed -e "s|^/storage/sdcard1/||");
    echo "${PACKAGE_LOCATION}";
}

# check for update package name
if ! /tmp/busybox test -n "${UPDATE_PACKAGE}" ; then
    # scrape last install package reference from /tmp/recovery.log
    UPDATE_PACKAGE=$(\
        /tmp/busybox cat /tmp/recovery.log | \
        /tmp/busybox grep Installing | \
        /tmp/busybox head -n1\
    );
    UPDATE_PACKAGE=$(\
        echo "$UPDATE_PACKAGE" | \
        /tmp/busybox sed s/Installing\ \'//g | \
        /tmp/busybox sed s/\'\.\.\.//g\
    );
fi

UPDATE_PACKAGE=$(fix_package_location "${UPDATE_PACKAGE}");
ui_print "Package:${UPDATE_PACKAGE}"

# check mounts
check_mount() {
    local MOUNT_POINT=$(/tmp/busybox readlink "${1}");
    if ! /tmp/busybox test -n "${MOUNT_POINT}" ; then
        # readlink does not work on older recoveries for some reason
        # doesn't matter since the path is already correct in that case
        echo "Using non-readlink mount point ${1}";
        MOUNT_POINT="${1}";
    fi
    if ! /tmp/busybox grep -q "${MOUNT_POINT}" /proc/mounts ; then
        /tmp/busybox mkdir -p "${MOUNT_POINT}";
        /tmp/busybox umount -l "${2}";
        if ! /tmp/busybox mount -t "${3}" "${2}" "${MOUNT_POINT}" ; then
            echo "Cannot mount ${1} (${MOUNT_POINT}).";
            exit 1;
        fi
    fi
}

# warning repartitions
warn_repartition() {
    if ! /tmp/busybox test -e /tmp/.accept_wipe ; then
        /tmp/busybox touch /tmp/.accept_wipe;
        ui_print ""
        ui_print "========================================"
        ui_print "ATTENTION"
        ui_print ""
        ui_print "This VERSION uses an incompatible"
        ui_print "partition layout"
        ui_print "Your /data will be wiped completely"
        ui_print "So, make your backups then just"
        ui_print "run this update.zip"
        ui_print "again to confirm install"
        ui_print ""
        ui_print "ATTENTION"
        ui_print "========================================"
        ui_print ""
        exit 9
    fi
    /tmp/busybox rm -fr /tmp/.accept_wipe;
}

# format partitions
format_partitions() {
    # create lvm partitions
    /lvm/sbin/lvm lvcreate -L ${SYSTEM_SIZE}B -n system lvpool;
    /lvm/sbin/lvm lvcreate -l 100%FREE -n userdata lvpool;

    # format partitions
    /tmp/make_ext4fs -b 4096 -g 32768 -i 7680 -I 256 -a /system /dev/lvpool/system;
    /tmp/make_ext4fs -b 4096 -g 32768 -i 8192 -I 256 -a /data /dev/lvpool/userdata;
    /tmp/busybox umount -l /datadata
    /tmp/erase_image datadata
}

# setup lvm partitions
setup_lvm_partitions() {
    # umount
    /tmp/busybox umount -l /system;
    /tmp/busybox umount -l /data;
    /tmp/busybox umount -f -l ${MMC_PART2};

    # remove any lvm reference
    /lvm/sbin/lvm lvremove -f lvpool;
    /lvm/sbin/lvm vgremove -f lvpool;
    /lvm/sbin/lvm pvremove -ffy ${MMC_PART2};

    # force clean up
    dd if=/dev/zero of=${MMC_PART2} bs=512 count=1;

    # create lvm phisical volumes and lvpool group
    /lvm/sbin/lvm pvcreate ${MMC_PART2};
    /lvm/sbin/lvm vgcreate lvpool ${MMC_PART2};
}

# backup /efs partition
backup_efs() {
    if ${IS_GSM} ; then
        # make sure efs is mounted
        check_mount /efs "${1}" "${2}";

        # create a backup of efs
        if /tmp/busybox test -e "${3}"/omni/backup/efs ; then
            /tmp/busybox mv "${3}"/omni/backup/efs "${3}"/omni/backup/efs-$$;
        fi
        /tmp/busybox rm -rf "${3}"/omni/backup/efs;

        /tmp/busybox mkdir -p "${3}"/omni/backup/efs;
        /tmp/busybox cp -R /efs/ "${3}"/omni/backup;
    fi
}

# restore /efs partition
restore_efs() {
    if ${IS_GSM} ; then
        if /tmp/busybox test -e /sdcard/omni/backup/efs/nv_data.bin ; then
            /tmp/busybox umount -l /efs;
            /tmp/erase_image efs;
            /tmp/busybox mkdir -p /efs;

            if ! /tmp/busybox grep -q /efs /proc/mounts ; then
                if ! /tmp/busybox mount -t yaffs2 /dev/block/"${EFS_PART}" /efs ; then
                    echo "Cannot mount efs.";
                    exit 6;
                fi
            fi

            /tmp/busybox cp -R /sdcard/omni/backup/efs /;
            /tmp/busybox umount -l /efs;
        else
            echo "nv_data.bin not found";
        fi
    fi
}

# restore /modem partition
restore_modem() {
    if ${IS_GSM} ; then
        # create mountpoint for radio partition
        /tmp/busybox mkdir -p /radio;

        # make sure radio partition is mounted
        if ! /tmp/busybox grep -q /radio /proc/mounts ; then
            /tmp/busybox umount -l /dev/block/"${RADIO_PART}";
            if ! /tmp/busybox mount -t yaffs2 /dev/block/"${RADIO_PART}" /radio ; then
                echo "Cannot mount radio partition.";
                exit 5;
            fi
        fi
        # if modem.bin doesn't exist on radio partition, format the partition and copy it
        if ! /tmp/busybox test -e /radio/modem.bin ; then
            /tmp/busybox umount -l /dev/block/"${RADIO_PART}";
            /tmp/erase_image radio;
            if ! /tmp/busybox mount -t yaffs2 /dev/block/"${RADIO_PART}" /radio ; then
                echo "Cannot copy modem.bin to radio partition.";
                exit 5;
            else
                /tmp/busybox cp /tmp/modem.bin /radio/modem.bin;
            fi
        fi
        # unmount radio partition
        /tmp/busybox umount -l /radio;
    fi
}

if /tmp/busybox test -e /dev/block/bml7 ; then
################################################################################
################################################################################
# Install process from stock
# check if we're running on a bml

    # we're running on a bml device
    # everything is logged into /tmp/omni/bml.log
    set_log bml.log;

    # send a warning to user
    warn_repartition;

    # make sure sdcard is mounted
    check_mount /mnt/sdcard "${SD_PART}" vfat;

    # backup efs
    backup_efs /dev/block/stl3 rfs /mnt/sdcard;

    # write the package path to sdcard omni.cfg
    echo "${UPDATE_PACKAGE}" > /mnt/sdcard/omni.cfg;

    # write new kernel to boot partition
    /tmp/flash_image boot /tmp/boot.img;
    if [ "$?" != "0" ] ; then
        exit 3;
    fi

    restore_log bml.log /mnt/sdcard;
    /tmp/busybox sync;
    /sbin/reboot now;
    exit 0;

elif [ "$(/tmp/busybox cat /sys/class/mtd/mtd2/size)" != "${MTD_SIZE}" ] || \
     [ "$(/tmp/busybox cat /sys/class/mtd/mtd2/name)" != "datadata" ] ; then
################################################################################
################################################################################
# Install process
# we're running on a mtd (old) device

    # everything is logged /tmp/omni_mtd_old.log
    set_log mtd_old.log;

    # send a warning to user
    warn_repartition;

    # make sure sdcard is mounted
    check_mount /sdcard $SD_PART vfat

    # write the package path to sdcard omni.cfg
    echo "${UPDATE_PACKAGE}" > /sdcard/omni.cfg;

    # clear datadata
    /tmp/busybox umount -l /datadata
    /tmp/erase_image datadata

    # backup efs
    backup_efs /dev/block/"${EFS_PART}" yaffs2 /sdcard;

    # write new kernel to boot partition
    /tmp/bml_over_mtd.sh boot 72 reservoir 2004 /tmp/boot.img;

    # restore logs and reboot
    restore_log bml_over_mtd.log /sdcard;
    restore_log mtd_old.log /sdcard;
    /tmp/busybox sync;
    /sbin/reboot now;
    exit 0

elif /tmp/busybox test -e /dev/block/mtdblock0 ; then
################################################################################
################################################################################
# Install process
# we're running on a mtd (current) device

    # everything is logged into /tmp/omni/mtd.log
    set_log mtd.log;

    # restore modem.bin
    restore_modem;

    # check lvm resize
    if /tmp/busybox test -e /dev/lvpool/system ; then
        if [ "$(/tmp/busybox blockdev --getsize64 /dev/mapper/lvpool-system)" != "${SYSTEM_SIZE}" ] || \
           /tmp/busybox test -e /dev/lvpool/swap; then
            warn_repartition;
            setup_lvm_partitions;
            format_partitions;
        fi
    fi

    # check sdcard
    check_mount /mnt/sdcard "${SD_PART}" vfat;

    if ! /tmp/busybox test -e /sdcard/omni.cfg ; then
        # unmount system and data (recovery seems to expect system to be unmounted)
        /tmp/busybox umount -l /system;
        /tmp/busybox umount -l /data;

        # update install - flash boot image then skip back to updater-script
        # (boot image is already flashed for first time install or old mtd upgrade)
        # flash boot image
        /tmp/bml_over_mtd.sh boot 72 reservoir 2004 /tmp/boot.img;

        if ! ${IS_GSM} ; then
            /tmp/bml_over_mtd.sh recovery 102 reservoir 2004 /tmp/recovery_kernel;
        fi

        restore_log bml_over_mtd.log /sdcard;
        restore_log mtd.log /sdcard;
        exit 0;
    fi

    # prevent loops
    /tmp/busybox rm -fr /sdcard/omni.cfg;

    # efs
    restore_efs;

    # lvm setup
    setup_lvm_partitions;
    format_partitions;

    # restart into recovery so the user can install further packages before booting
    /tmp/busybox touch /cache/.startrecovery;
    restore_log mtd.log /sdcard;
    exit 0;
fi
