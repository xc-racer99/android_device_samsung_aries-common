LOCAL_PATH:= $(call my-dir)

ifeq ($(USE_SUPERSU),true)
include $(CLEAR_VARS)
LOCAL_MODULE       := init.aries.supersu.rc
LOCAL_MODULE_TAGS  := optional eng
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := init.aries.supersu.rc
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT)
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE       := launch_daemonsu.sh
LOCAL_MODULE_TAGS  := optional eng
LOCAL_MODULE_CLASS := ETC
LOCAL_SRC_FILES    := launch_daemonsu.sh
LOCAL_MODULE_PATH  := $(TARGET_ROOT_OUT_SBIN)
# Create /su
LOCAL_POST_INSTALL_CMD := mkdir -p $(addprefix $(TARGET_ROOT_OUT)/, su)
include $(BUILD_PREBUILT)
endif
