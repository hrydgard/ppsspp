# Basic Android.mk for libnative

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := libnative
LOCAL_ARM_MODE := arm
LOCAL_SRC_FILES :=\
    android/native_audio.cpp \
    audio/wav_read.cpp \
    audio/mixer.cpp.arm \
    base/buffer.cpp \
    base/display.cpp \
    base/timeutil.cpp \
    base/colorutil.cpp \
    base/error_context.cpp \
    base/stringutil.cpp \
    data/compression.cpp \
    ext/rg_etc1/rg_etc1.cpp \
    ext/cityhash/city.cpp \
    ext/jpge/jpge.cpp \
    ext/sha1/sha1.cpp \
    ext/stb_image/stb_image.c \
    ext/stb_vorbis/stb_vorbis.c.arm \
    ext/vjson/json.cpp \
    ext/vjson/block_allocator.cpp \
    file/dialog.cpp \
    file/fd_util.cpp \
    file/easy_file.cpp \
    file/chunk_file.cpp \
    file/file_util.cpp \
    file/ini_file.cpp \
    file/zip_read.cpp \
    json/json_writer.cpp \
    i18n/i18n.cpp \
	input/gesture_detector.cpp \
    math/math_util.cpp \
    math/curves.cpp \
    math/lin/aabb.cpp.arm \
    math/lin/plane.cpp.arm \
    math/lin/quat.cpp.arm \
    math/lin/vec3.cpp.arm \
    math/lin/matrix4x4.cpp.arm \
    midi/midi_input.cpp \
    net/http_client.cpp \
    net/resolve.cpp \
    net/url.cpp \
    profiler/profiler.cpp \
    thread/threadutil.cpp \
    thread/prioritizedworkqueue.cpp \
    thread/threadpool.cpp \
    gfx_es2/glsl_program.cpp \
    gfx_es2/gl_state.cpp \
    gfx_es2/draw_buffer.cpp.arm \
    gfx_es2/vertex_format.cpp \
    gfx_es2/fbo.cpp \
    gfx/gl_debug_log.cpp \
    gfx/gl_lost_manager.cpp \
    gfx/texture.cpp \
    gfx/texture_atlas.cpp \
    gfx/texture_gen.cpp \
    image/zim_load.cpp \
    image/zim_save.cpp \
    image/png_load.cpp \
    ui/view.cpp \
    ui/viewgroup.cpp \
    ui/ui.cpp \
    ui/ui_screen.cpp \
    ui/ui_context.cpp \
    ui/screen.cpp \
    ui/virtual_input.cpp \
    util/random/perlin.cpp \
    util/text/utf8.cpp

LOCAL_CFLAGS := -O3 -DGL_GLEXT_PROTOTYPES -DUSING_GLES2 -fsigned-char -fno-strict-aliasing
LOCAL_CPPFLAGS := -fno-exceptions -std=gnu++11 -frtti
LOCAL_LDLIBS := -lz
LOCAL_C_INCLUDES := $(LOCAL_PATH)/ext/libzip

#Portable native and separate code on android in future is easy you needs add files 
#by ($(target_arch_ABI),arquitecture (armeabi-v7a , armeabi , x86 , MIPS)

ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
LOCAL_SRC_FILES += math/math_util.cpp 
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -DARM
else ifeq ($(TARGET_ARCH_ABI),armeabi)
LOCAL_SRC_FILES += math/math_utilarmv6.cpp 
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -DARM
else ifeq ($(TARGET_ARCH_ABI),x86)
LOCAL_SRC_FILES += math/math_util.cpp 
LOCAL_CFLAGS := $(LOCAL_CFLAGS) -D_M_IX86
endif

include $(BUILD_STATIC_LIBRARY)
