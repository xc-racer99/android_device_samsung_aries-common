ifeq ($(TARGET_BOOTLOADER_BOARD_NAME),aries)
    include $(all-subdir-makefiles)
# Don't allow building for non-galaxys4g devices for now
ifeq ($(filter fascinatemtd galaxys4gmtd telusgalaxys4gmtd,$(TARGET_DEVICE)),)
    $(warning ****************************************************************************)
    $(warning * This repo hasn't been updated for 6.0 save changes for fascinate orgalaxys4g devices.)
    $(warning * Please update this repo to work before trying to build for other devices.)
    $(warning * Of course, you'll also need to remove this message.)
    $(warning ****************************************************************************)
    $(error stopping)
endif
endif
