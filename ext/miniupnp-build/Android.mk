# Basic Android.mk for miniupnpc in PPSSPP

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := libminiupnp-build
LOCAL_ARM_MODE := arm
LOCAL_SRC_FILES := \
    ../miniupnp/miniupnpc/addr_is_reserved.c \
    ../miniupnp/miniupnpc/connecthostport.c \
    ../miniupnp/miniupnpc/igd_desc_parse.c \
    ../miniupnp/miniupnpc/minisoap.c \
    ../miniupnp/miniupnpc/minissdpc.c \
    ../miniupnp/miniupnpc/miniupnpc.c \
    ../miniupnp/miniupnpc/miniwget.c \
    ../miniupnp/miniupnpc/minixml.c \
    ../miniupnp/miniupnpc/minixmlvalid.c \
    ../miniupnp/miniupnpc/portlistingparse.c \
    ../miniupnp/miniupnpc/receivedata.c \
    ../miniupnp/miniupnpc/upnpcommands.c \
    ../miniupnp/miniupnpc/upnpdev.c \
    ../miniupnp/miniupnpc/upnperrors.c \
    ../miniupnp/miniupnpc/upnpreplyparse.c

LOCAL_CFLAGS := -O3 -fsigned-char -fno-strict-aliasing -Wall -Wno-multichar -D__STDC_CONSTANT_MACROS
LOCAL_CPPFLAGS := -fno-exceptions -std=gnu++11 -fno-rtti -Wno-reorder
# Note: LOCAL_PATH is the directory this file is in.
LOCAL_C_INCLUDES := $(LOCAL_PATH)/.. $(LOCAL_PATH)/../miniupnp ..

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
