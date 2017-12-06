LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
	secril-shim.c

LOCAL_SHARED_LIBRARIES := \
	liblog \
	libril \
	libcutils

LOCAL_CFLAGS := -Wall -Werror

LOCAL_MODULE := libsecril-shim

LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR_SHARED_LIBRARIES)

include $(BUILD_SHARED_LIBRARY)
