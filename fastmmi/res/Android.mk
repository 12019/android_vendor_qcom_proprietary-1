LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := pass.png
LOCAL_SRC_FILES := $(LOCAL_MODULE)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)/mmi
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := fail.png
LOCAL_SRC_FILES := $(LOCAL_MODULE)
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)/mmi/
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := strings.xml
LOCAL_CFLAGS := -Wall
LOCAL_MODULE := strings.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)/mmi
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := strings-zh-rCN.xml
LOCAL_CFLAGS := -Wall
LOCAL_MODULE := strings-zh-rCN.xml
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)/mmi
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := DroidSansFallback.ttf
LOCAL_CFLAGS := -Wall
LOCAL_MODULE := fonts.ttf
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)/mmi
include $(BUILD_PREBUILT)
