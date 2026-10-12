LOCAL_PATH := $(call my-dir)

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
	COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

COREFLAGS  := -DSTACK_LINE_READER_BUFFER_SIZE=1024 -DHAVE_DLFCN_H
CORE_DIR   := ../..
WITH_DYNAREC := 1

ifeq ($(TARGET_ARCH),arm64)
  COREFLAGS += -D_ARCH_64
  HAVE_NEON := 1
endif

ifeq ($(TARGET_ARCH),arm)
  COREFLAGS += -D__arm__ -DARM_ASM -D_ARCH_32 -mfpu=neon
  HAVE_NEON := 1
endif

ifeq ($(TARGET_ARCH),x86)
  COREFLAGS += -D_ARCH_32 -D_M_IX86 -fomit-frame-pointer -mtune=atom -mfpmath=sse -mssse3 -mstackrealign
endif

ifeq ($(TARGET_ARCH),x86_64)
  COREFLAGS += -D_ARCH_64 -D_M_X64 -fomit-frame-pointer -mtune=atom -mfpmath=sse -mssse3 -mstackrealign
endif

include $(CLEAR_VARS)

PLATFORM_EXT   := android
platform := android
GLES = 1

LOCAL_MODULE := retro

include $(CORE_DIR)/libretro/Makefile.common

COREFLAGS += -DINLINE="inline" -DPPSSPP -DMOBILE_DEVICE -DBAKE_IN_GIT -DDYNAREC -DUSING_GLES2 -D__STDC_CONSTANT_MACROS -DGLEW_NO_GLU  $(INCFLAGS)
LOCAL_SRC_FILES = $(SOURCES_CXX) $(SOURCES_C) $(ASMFILES)
LOCAL_CPPFLAGS := -Wall -std=c++17 $(COREFLAGS) -DSPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS $(DEFINES)
LOCAL_CFLAGS := -O2 -DANDROID $(COREFLAGS) $(DEFINES)
LOCAL_LDLIBS += -lz -landroid -lGLESv2 -lOpenSLES -lEGL -ldl -llog -latomic

include $(BUILD_SHARED_LIBRARY)
