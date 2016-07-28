#!/system/bin/sh
#
# Setup /data/data based on whether the phone is encrypted or not
# and migrate the data to the correct location on en/decryption
# Encrypted => leave on /data/data (/datadata cannot be encrypted)
# Unencrypted => symlink to /datadata for performance

PATH=/system/bin/:/system/xbin/

function migrate_datadata {
    # Migrate data from /datadata to /data/data
    if test -d /datadata/com.android.settings ; then
        busybox mv -f /datadata/* /data/data/
        touch /data/data/.nodatadata
        rm -r /data/data/lost+found
        busybox umount /datadata
        erase_image datadata
        busybox mount /datadata
    fi
}

# There are 4 states which this script can be called from.
# They can be detected using vold.decrypt and ro.crypto.state props

CRYPTO_STATE="`getprop ro.crypto.state`"
VOLD_DECRYPT="`getprop vold.decrypt`"

if test -h /data/data ; then
    # Handle pre-CM 10.2 symlink
    rm /data/data
    mkdir /data/data
    chown system.system /data/data
    chmod 0771 /data/data
fi

if test "$CRYPTO_STATE" = "unencrypted" ; then
    if test "$VOLD_DECRYPT" = "" ; then
        # Normal unencrypted boot
        if test -e /datadata/.nodatadata || test -e /data/data/.nodatadata ; then
            migrate_datadata
        else
            mount -o bind /datadata /data/data

            # Remove obsolete Download Link
            if test -h /data/user/0/com.android.providers.downloads/cache; then
                rm -rf /data/user/0/com.android.providers.downloads/cache
            fi
        fi
    else
        # Encrypting, we need to manually unmount /data/data to continue
        busybox umount /data/data
    fi
    # else: Encrypting, do nothing
else
    if test "$VOLD_DECRYPT" = "trigger_post_fs_data" ; then
        # Encrypted boot (after decryption)
        migrate_datadata
    fi
    # else: Encrypted boot (before decryption), do nothing
fi
