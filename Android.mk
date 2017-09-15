ifeq ($(TARGET_BOOTLOADER_BOARD_NAME),aries)
	LOCAL_PATH := $(call my-dir)
	include $(call all-makefiles-under,$(LOCAL_PATH))
endif
