# Basic Android.mk for libnative

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := libnative
LOCAL_ARM_MODE := arm
LOCAL_SRC_FILES :=\
    base/backtrace.cpp \
    base/buffer.cpp \
    base/display.cpp \
    base/timeutil.cpp \
    base/colorutil.cpp \
    base/stringutil.cpp \
    data/compression.cpp \
    ext/rg_etc1/rg_etc1.cpp \
    ext/cityhash/city.cpp \
    ext/libpng17/png.c \
    ext/libpng17/pngerror.c \
    ext/libpng17/pngget.c \
    ext/libpng17/pngmem.c \
    ext/libpng17/pngpread.c \
    ext/libpng17/pngread.c \
    ext/libpng17/pngrio.c \
    ext/libpng17/pngrtran.c \
    ext/libpng17/pngrutil.c \
    ext/libpng17/pngset.c \
    ext/libpng17/pngtest.c \
    ext/libpng17/pngtrans.c \
    ext/libpng17/pngwio.c \
    ext/libpng17/pngwrite.c \
    ext/libpng17/pngwtran.c \
    ext/libpng17/pngwutil.c \
    ext/jpge/jpgd.cpp \
    ext/jpge/jpge.cpp \
    ext/sha1/sha1.cpp \
    ext/vjson/json.cpp \
    ext/vjson/block_allocator.cpp \
    file/fd_util.cpp \
    file/chunk_file.cpp \
    file/file_util.cpp \
    file/free.cpp \
    file/path.cpp \
    file/ini_file.cpp \
    file/zip_read.cpp \
    json/json_writer.cpp \
    i18n/i18n.cpp \
    input/gesture_detector.cpp \
    input/input_state.cpp \
    math/fast/fast_math.c \
    math/fast/fast_matrix.c \
    math/dataconv.cpp \
    math/math_util.cpp \
    math/curves.cpp \
    math/expression_parser.cpp \
    math/lin/plane.cpp.arm \
    math/lin/quat.cpp.arm \
    math/lin/vec3.cpp.arm \
    math/lin/matrix4x4.cpp.arm \
    net/http_client.cpp \
    net/http_server.cpp \
    net/http_headers.cpp \
    net/resolve.cpp \
    net/sinks.cpp \
    net/url.cpp \
    profiler/profiler.cpp \
    thread/executor.cpp \
    thread/threadutil.cpp \
    thread/prioritizedworkqueue.cpp \
    thread/threadpool.cpp \
    gfx_es2/glsl_program.cpp \
    gfx_es2/gpu_features.cpp \
    gfx_es2/gl3stub.c \
    gfx_es2/draw_buffer.cpp.arm \
    gfx_es2/draw_text.cpp.arm \
    gfx_es2/draw_text_android.cpp.arm \
    gfx/gl_debug_log.cpp \
    gfx/texture_atlas.cpp \
    image/zim_load.cpp \
    image/zim_save.cpp \
    image/png_load.cpp \
    thin3d/thin3d.cpp \
    thin3d/thin3d_gl.cpp \
    thin3d/thin3d_vulkan.cpp \
    thin3d/VulkanRenderManager.cpp \
    thin3d/VulkanQueueRunner.cpp \
    ui/view.cpp \
    ui/viewgroup.cpp \
    ui/ui.cpp \
    ui/ui_screen.cpp \
    ui/ui_tween.cpp \
    ui/ui_context.cpp \
    ui/screen.cpp \
    util/text/utf8.cpp \
    util/text/parsers.cpp \
    util/text/wrap_text.cpp \
    util/hash/hash.cpp

LOCAL_CFLAGS := -O3 -DUSING_GLES2 -fsigned-char -fno-strict-aliasing -Wall -Wno-multichar -D__STDC_CONSTANT_MACROS
LOCAL_CPPFLAGS := -fno-exceptions -std=gnu++11 -fno-rtti -Wno-reorder
LOCAL_C_INCLUDES := $(LOCAL_PATH)/ext $(LOCAL_PATH)/ext/libzip ..

#Portable native and separate code on android in future is easy you needs add files 
#by ($(target_arch_ABI),arquitecture (armeabi-v7a , armeabi , x86 , MIPS)
# ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
ifeq ($(findstring armeabi-v7a,$(TARGET_ARCH_ABI)),armeabi-v7a)
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -DARM -DARMEABI_V7A
LOCAL_SRC_FILES := $(LOCAL_SRC_FILES) \
    math/fast/fast_matrix_neon.S.neon \
    ext/libpng17/arm/arm_init.c \
    ext/libpng17/arm/filter_neon_intrinsics.c \
    ext/libpng17/arm/filter_neon.S.neon

else ifeq ($(TARGET_ARCH_ABI),armeabi)
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -DARM -DARMEABI -march=armv6
else ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -D_ARCH_64 -DARM64
else ifeq ($(TARGET_ARCH_ABI),x86)
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -D_M_IX86
LOCAL_SRC_FILES := $(LOCAL_SRC_FILES) \
    math/fast/fast_matrix_sse.c
else ifeq ($(TARGET_ARCH_ABI),x86_64)
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -D_M_X64
LOCAL_SRC_FILES := $(LOCAL_SRC_FILES) \
    math/fast/fast_matrix_sse.c
endif

include $(BUILD_STATIC_LIBRARY)
