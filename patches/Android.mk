LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
$(info $(shell ($(LOCAL_PATH)/apply_patches.sh)))
