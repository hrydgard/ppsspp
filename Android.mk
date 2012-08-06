# Basic Android.mk for libnative

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := libnative
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
    ext/sha1/sha1.cpp \
    ext/stb_vorbis/stb_vorbis.c.arm \
    ext/vjson/json.cpp \
    ext/vjson/block_allocator.cpp \
    file/dialog.cpp \
    file/fd_util.cpp \
    file/easy_file.cpp \
    file/chunk_file.cpp \
    file/file_util.cpp \
    file/zip_read.cpp \
    json/json_writer.cpp \
    math/math_util.cpp.arm \
    math/lin/aabb.cpp.arm \
    math/lin/plane.cpp.arm \
    math/lin/quat.cpp.arm \
    math/lin/vec3.cpp.arm \
    math/lin/matrix4x4.cpp.arm \
    midi/midi_input.cpp \
    net/http_client.cpp \
    net/resolve.cpp \
    profiler/profiler.cpp \
    gfx_es2/glsl_program.cpp \
    gfx_es2/draw_buffer.cpp.arm \
    gfx_es2/vertex_format.cpp \
    gfx/gl_debug_log.cpp \
    gfx/gl_lost_manager.cpp \
    gfx/texture.cpp \
    gfx/texture_atlas.cpp \
    gfx/texture_gen.cpp \
    image/zim_load.cpp \
	ui/ui.cpp \
	ui/screen.cpp \
    util/random/perlin.cpp


LOCAL_CFLAGS := -O2
LOCAL_CPPFLAGS := -fno-exceptions -fno-rtti -std=gnu++0x
LOCAL_LDLIBS := -lz
LOCAL_C_INCLUDES := $(LOCAL_PATH)/ext/libzip


include $(BUILD_STATIC_LIBRARY)
