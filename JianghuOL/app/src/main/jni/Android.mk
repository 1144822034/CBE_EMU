LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := unicorn
LOCAL_SRC_FILES := ../../../libs/$(TARGET_ARCH_ABI)/libunicorn.so
LOCAL_EXPORT_C_INCLUDES := include/unicorn  # 如果有头文件
include $(PREBUILT_SHARED_LIBRARY)


include $(CLEAR_VARS)
MY_LOCAL_PATH := $(LOCAL_PATH)
LOCAL_SHARED_LIBRARIES := unicorn
LOCAL_MODULE    := mtksim
LOCAL_LDLIBS := -llog -lm
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)/src/cbeEmu
# ANDROID_CBE_PORT -> CBE_CLIENT_ONLY/CBE_PLATFORM_ANDROID in config.h.
# NETWORK_SUPPORT matches desktop client Makefile flags for the remote WT path.
LOCAL_CFLAGS := -DANDROID_CBE_PORT -DNETWORK_SUPPORT -std=gnu11 -Wno-implicit-function-declaration -Wno-implicit-int -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -Wno-int-conversion -Wno-format
LOCAL_SRC_FILES := \
    src/cbe_jni.c \
    src/cbeEmu/cbe_log.c \
    src/cbeEmu/cbeParser.c \
    src/cbeEmu/fileIoEngine.c \
    src/cbeEmu/fontEngine.c \
    src/cbeEmu/gameLauncher.c \
    src/cbeEmu/gifDecode.c \
    src/cbeEmu/lcd.c \
    src/cbeEmu/main.c \
    src/cbeEmu/mystd.c \
    src/cbeEmu/vmMalloc.c
include $(BUILD_SHARED_LIBRARY)
