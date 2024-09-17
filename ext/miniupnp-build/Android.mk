# Basic Android.mk for miniupnpc in PPSSPP

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := libminiupnp-build
LOCAL_ARM_MODE := arm
LOCAL_SRC_FILES := \
    ../miniupnp/miniupnpc/src/addr_is_reserved.c \
    ../miniupnp/miniupnpc/src/connecthostport.c \
    ../miniupnp/miniupnpc/src/igd_desc_parse.c \
    ../miniupnp/miniupnpc/src/minisoap.c \
    ../miniupnp/miniupnpc/src/minissdpc.c \
    ../miniupnp/miniupnpc/src/miniupnpc.c \
    ../miniupnp/miniupnpc/src/miniwget.c \
    ../miniupnp/miniupnpc/src/minixml.c \
    ../miniupnp/miniupnpc/src/minixmlvalid.c \
    ../miniupnp/miniupnpc/src/portlistingparse.c \
    ../miniupnp/miniupnpc/src/receivedata.c \
    ../miniupnp/miniupnpc/src/upnpcommands.c \
    ../miniupnp/miniupnpc/src/upnpdev.c \
    ../miniupnp/miniupnpc/src/upnperrors.c \
    ../miniupnp/miniupnpc/src/upnpreplyparse.c

LOCAL_CFLAGS := -O3 -fsigned-char -fno-strict-aliasing -Wall -Wno-multichar -D__STDC_CONSTANT_MACROS
LOCAL_CPPFLAGS := -fno-exceptions -std=gnu++11 -fno-rtti -Wno-reorder
# Note: LOCAL_PATH is the directory this file is in.
LOCAL_C_INCLUDES := $(LOCAL_PATH)/.. $(LOCAL_PATH)/../miniupnp/miniupnpc/src $(LOCAL_PATH)/../miniupnp/miniupnpc/include ..

ifeq ($(findstring armeabi-v7a,$(TARGET_ARCH_ABI)),armeabi-v7a)
LOCAL_CFLAGS := $(LOCAL_CFLAGS)
else ifeq ($(TARGET_ARCH_ABI),armeabi)
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -march=armv6
else ifeq ($(TARGET_ARCH_ABI),x86)
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -D_M_IX86
else ifeq ($(TARGET_ARCH_ABI),x86_64)
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -D_M_X64
endif

include $(BUILD_STATIC_LIBRARY)
