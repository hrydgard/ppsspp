LOCAL_PATH := $(call my-dir)

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
	COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

COREFLAGS  := -DSTACK_LINE_READER_BUFFER_SIZE=1024 -DHAVE_DLFCN_H
CORE_DIR   := ../..
FFMPEGDIR  := $(CORE_DIR)/ext/ffmpeg
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

# FFmpeg, from the sources in ext/ffmpeg. See its README.md.
include $(CLEAR_VARS)
include $(CORE_DIR)/ext/ffmpeg-build/sources.mk
LOCAL_MODULE := libffmpeg
LOCAL_SRC_FILES := $(addprefix $(FFMPEGDIR)/,$(FFMPEG_SOURCES))
LOCAL_CFLAGS := -O2 -I$(CORE_DIR)/ext/ffmpeg-build -I$(FFMPEGDIR) -DHAVE_AV_CONFIG_H -D_ISOC99_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -w -Werror=implicit-function-declaration
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

PLATFORM_EXT   := android
platform := android
GLES = 1

LOCAL_MODULE := retro

include $(CORE_DIR)/libretro/Makefile.common

COREFLAGS += -DINLINE="inline" -DPPSSPP -DUSE_FFMPEG -DWITH_UPNP -DMOBILE_DEVICE -DBAKE_IN_GIT -DDYNAREC -D__LIBRETRO__ -DUSING_GLES2 -D__STDC_CONSTANT_MACROS -DGLEW_NO_GLU -DMINIUPNP_STATICLIB $(INCFLAGS)
LOCAL_SRC_FILES = $(SOURCES_CXX) $(SOURCES_C) $(ASMFILES)
LOCAL_CPPFLAGS := -Wall -std=c++17 $(COREFLAGS) -DSPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS $(DEFINES)
LOCAL_CFLAGS := -O2 -DANDROID $(COREFLAGS) $(DEFINES)
LOCAL_LDLIBS += -lz -landroid -lGLESv2 -lOpenSLES -lEGL -ldl -llog -latomic
LOCAL_STATIC_LIBRARIES += libffmpeg

include $(BUILD_SHARED_LIBRARY)
