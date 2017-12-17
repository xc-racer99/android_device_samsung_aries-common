# Copyright (C) 2007 The Android Open Source Project
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

# BoardConfigCommon.mk
#
# Product-specific compile-time definitions.
#

TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_ARCH_VARIANT_CPU := cortex-a8
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a8

TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true

TARGET_BOARD_PLATFORM := s5pc110
TARGET_BOOTLOADER_BOARD_NAME := aries

BOARD_NAND_PAGE_SIZE := 4096
BOARD_NAND_SPARE_SIZE := 128
BOARD_KERNEL_BASE := 0x32000000
BOARD_KERNEL_PAGESIZE := 4096

BOARD_BOOTIMAGE_PARTITION_SIZE := 7864320
BOARD_CACHEIMAGE_PARTITION_SIZE := 18350080
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := yaffs2
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 943718400
BOARD_USERDATAIMAGE_PARTITION_SIZE := 1379926016
BOARD_FLASH_BLOCK_SIZE := 4096
TARGET_USERIMAGES_USE_YAFFS := true

# Art
ifeq ($(HOST_OS),linux)
  ifneq ($(TARGET_BUILD_VARIANT),eng)
    ifeq ($(WITH_DEXPREOPT),)
      WITH_DEXPREOPT := true
    endif
  endif
endif

# Bionic stuff
BOARD_USES_LEGACY_MMAP := true
MALLOC_SVELTE := true
TARGET_ENABLE_NON_PIE_SUPPORT := true
TARGET_NEEDS_PLATFORM_TEXTRELS := true

# Bluetooth
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_BCM := true
BOARD_CUSTOM_BT_CONFIG := device/samsung/aries-common/config/libbt_vndcfg.txt

# Camera
BOARD_CAMERA_DEVICE := /dev/video0
BOARD_CAMERA_HAVE_ISO := true
BOARD_V4L2_DEVICE := /dev/video1

# Charger
BOARD_CHARGER_ENABLE_SUSPEND := true

# Connectivity - Wi-Fi
BOARD_HOSTAPD_DRIVER        := NL80211
BOARD_HOSTAPD_PRIVATE_LIB   := lib_driver_cmd_bcmdhd
BOARD_WLAN_DEVICE           := bcmdhd
BOARD_WPA_SUPPLICANT_DRIVER := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_bcmdhd
WIFI_DRIVER_FW_PATH_PARAM   := "/sys/module/bcmdhd/parameters/firmware_path"
WIFI_DRIVER_FW_PATH_STA     := "/vendor/firmware/fw_bcmdhd.bin"
WIFI_DRIVER_FW_PATH_AP      := "/vendor/firmware/fw_bcmdhd_apsta.bin"
WPA_SUPPLICANT_VERSION      := VER_0_8_X

# Dex-preoptimization to speed up first boot sequence
ifeq ($(HOST_OS),linux)
    WITH_DEXPREOPT := true
    WITH_DEXPREOPT_BOOT_IMG_ONLY := true
endif

# Kernel source
TARGET_KERNEL_SOURCE := kernel/samsung/aries

# OpenGL
BOARD_ALLOW_EGL_HIBERNATION := true
BOARD_CUSTOM_VSYNC_IOCTL := true
BOARD_EGL_WORKAROUND_BUG_10194508 := true
TARGET_RUNNING_WITHOUT_SYNC_FRAMEWORK := true

# OMX
BOARD_CANT_REALLOCATE_OMX_BUFFERS := true
BOARD_SCREENRECORD_LANDSCAPE_ONLY := true

# Releasetools
TARGET_RELEASETOOLS_EXTENSIONS := device/samsung/aries-common

# RIL
TARGET_NEEDS_ROOT_RIL_INIT := true
TARGET_SPECIFIC_HEADER_PATH := device/samsung/aries-common/include

# Recovery
BOARD_USES_BML_OVER_MTD := true
BOARD_CUSTOM_MKBOOTIMG := mksgsbootimg
RECOVERY_FSTAB_VERSION := 2
TARGET_RECOVERY_FSTAB := device/samsung/aries-common/rootdir/fstab.aries
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true
TARGET_USE_CUSTOM_LUN_FILE_PATH := "/sys/devices/platform/s3c-usbgadget/gadget/lun%d/file"

# SELinux
BOARD_SEPOLICY_DIRS += device/samsung/aries-common/sepolicy
ifneq (galaxys4gmtd, $(TARGET_DEVICE))
    BOARD_SEPOLICY_DIRS += device/samsung/aries-common/sepolicy-lvm
endif

# Include aries specific stuff
-include device/samsung/aries-common/Android.mk

# TWRP Flags
TW_THEME := portrait_mdpi
TW_NO_REBOOT_BOOTLOADER := true
TW_INTERNAL_STORAGE_PATH := "/sdcard"
TW_INTERNAL_STORAGE_MOUNT_POINT := "sdcard"
TW_EXTERNAL_STORAGE_PATH := "/external_sd"
TW_EXTERNAL_STORAGE_MOUNT_POINT := "external_sd"
TW_INCLUDE_FB2PNG := true
TW_INCLUDE_CRYPTO := true
TW_MAX_BRIGHTNESS := 255
TW_BRIGHTNESS_PATH := "/sys/class/backlight/s5p_bl/brightness"
