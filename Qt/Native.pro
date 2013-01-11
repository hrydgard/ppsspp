QT += opengl
QT -= gui
TARGET = Native
TEMPLATE = lib
CONFIG += staticlib

include(Settings.pri)

!mobile_platform: {
	SOURCES += ../native/ext/glew/glew.c
	HEADERS += ../native/ext/glew/GL/glew.h \
		../native/ext/glew/GL/glxew.h \
		../native/ext/glew/GL/wglew.h
	INCLUDEPATH += ../native/ext/glew
}

# Backtrace
x86:!mobile_platform: {
	SOURCES += ../native/base/backtrace.cpp
	HEADERS += ../native/base/backtrace.h
}

# EtcPack

SOURCES += ../native/ext/etcpack/etcdec.cpp \
	../native/ext/etcpack/etcpack.cpp \
	../native/ext/etcpack/image.cpp
HEADERS += ../native/ext/etcpack/etcdec.h \
	../native/ext/etcpack/etcpack.h \
	../native/ext/etcpack/image.h
INCLUDEPATH += ../native/ext/etcpack

# Stb_image

SOURCES += ../native/ext/stb_image/stb_image.c
HEADERS += ../native/ext/stb_image/stb_image.h
INCLUDEPATH += ../native/ext/stb_image

# Stb_vorbis

SOURCES += ../native/ext/stb_vorbis/stb_vorbis.c
HEADERS += ../native/ext/stb_vorbis/stb_vorbis.h
INCLUDEPATH += ../native/ext/stb_vorbis

# Snappy

SOURCES += ../ext/snappy/snappy-c.cpp ../ext/snappy/snappy.cpp
HEADERS += ../ext/snappy/snappy-internal.h \
		../ext/snappy/snappy-sinksource.h \
		../ext/snappy/snappy-stubs-internal.h \
		../ext/snappy/snappy-stubs-public.h \
		../ext/snappy/snappy.h
INCLUDEPATH += ../ext/snappy

# Zlib
!symbian: {
	SOURCES += ../ext/zlib/adler32.c \
		../ext/zlib/compress.c \
		../ext/zlib/crc32.c \
		../ext/zlib/deflate.c \
		../ext/zlib/gzclose.c \
		../ext/zlib/gzlib.c \
		../ext/zlib/gzread.c \
		../ext/zlib/gzwrite.c \
		../ext/zlib/infback.c \
		../ext/zlib/inffast.c \
		../ext/zlib/inflate.c \
		../ext/zlib/inflate.h \
		../ext/zlib/inftrees.c \
		../ext/zlib/trees.c \
		../ext/zlib/uncompr.c \
		../ext/zlib/zutil.c
	HEADERS += ../ext/zlib/crc32.h \
		../ext/zlib/deflate.h \
		../ext/zlib/gzguts.h \
		../ext/zlib/inffast.h \
		../ext/zlib/inffixed.h \
		../ext/zlib/inftrees.h \
		../ext/zlib/trees.h \
		../ext/zlib/zconf.h \
		../ext/zlib/zlib.h \
		../ext/zlib/zutil.h
	INCLUDEPATH += ../ext/zlib
}


# Native

SOURCES +=  ../native/audio/mixer.cpp \
	../native/audio/wav_read.cpp \
	../native/base/buffer.cpp \
	../native/base/colorutil.cpp \
	../native/base/display.cpp \
	../native/base/error_context.cpp \
	../native/base/fastlist_test.cpp \
	../native/base/stringutil.cpp \
	../native/base/threadutil.cpp \
	../native/base/timeutil.cpp \
	../native/file/chunk_file.cpp \
	../native/file/dialog.cpp \
	../native/file/easy_file.cpp \
	../native/file/fd_util.cpp \
	../native/file/file_util.cpp \
	../native/file/zip_read.cpp \
	../native/gfx/gl_debug_log.cpp \
	../native/gfx/gl_lost_manager.cpp \
	../native/gfx/texture.cpp \
	../native/gfx/texture_atlas.cpp \
	../native/gfx/texture_gen.cpp \
	../native/gfx_es2/draw_buffer.cpp \
	../native/gfx_es2/fbo.cpp \
	../native/gfx_es2/gl_state.cpp \
	../native/gfx_es2/glsl_program.cpp \
	../native/gfx_es2/vertex_format.cpp \
	../native/image/png_load.cpp \
	../native/image/zim_load.cpp \
	../native/image/zim_save.cpp \
	../native/input/gesture_detector.cpp \
	../native/json/json_writer.cpp \
	../native/math/curves.cpp \
	../native/math/lin/aabb.cpp \
	../native/math/lin/matrix4x4.cpp \
	../native/math/lin/plane.cpp \
	../native/math/lin/quat.cpp \
	../native/math/lin/vec3.cpp \
	../native/math/math_util.cpp \
	../native/midi/midi_input.cpp \
	../native/net/http_client.cpp \
	../native/net/resolve.cpp \
	../native/profiler/profiler.cpp \
	../native/ui/screen.cpp \
	../native/ui/ui.cpp \
	../native/ui/virtual_input.cpp \
	../native/util/bits/bits.cpp \
	../native/util/bits/varint.cpp \
	../native/util/hash/hash.cpp \
	../native/util/random/perlin.cpp \
	../native/util/text/utf8.cpp

HEADERS +=  ../native/audio/mixer.h \
	../native/audio/wav_read.h \
	../native/base/basictypes.h \
	../native/base/buffer.h \
	../native/base/color.h \
	../native/base/colorutil.h \
	../native/base/display.h \
	../native/base/error_context.h \
	../native/base/fastlist.h \
	../native/base/linked_ptr.h \
	../native/base/logging.h \
	../native/base/mutex.h \
	../native/base/scoped_ptr.h \
	../native/base/stats.h \
	../native/base/stringutil.h \
	../native/base/threadutil.h \
	../native/base/timeutil.h \
	../native/file/chunk_file.h \
	../native/file/dialog.h \
	../native/file/easy_file.h \
	../native/file/fd_util.h \
	../native/file/file_util.h \
	../native/file/vfs.h \
	../native/file/zip_read.h \
	../native/gfx/gl_debug_log.h \
	../native/gfx/gl_lost_manager.h \
	../native/gfx/texture.h \
	../native/gfx/texture_atlas.h \
	../native/gfx/texture_gen.h \
	../native/gfx_es2/fbo.h \
	../native/gfx_es2/gl_state.h \
	../native/gfx_es2/glsl_program.h \
	../native/gfx_es2/vertex_format.h \
	../native/gfx_es2/draw_buffer.h \
	../native/image/png_load.h \
	../native/image/zim_load.h \
	../native/image/zim_save.h \
	../native/input/gesture_detector.h \
	../native/input/input_state.h \
	../native/json/json_writer.h \
	../native/math/compression.h \
	../native/math/curves.h \
	../native/math/lin/aabb.h \
	../native/math/lin/matrix4x4.h \
	../native/math/lin/plane.h \
	../native/math/lin/quat.h \
	../native/math/lin/ray.h \
	../native/math/lin/vec3.h \
	../native/math/math_util.h \
	../native/midi/midi_input.h \
	../native/net/http_client.h \
	../native/net/resolve.h \
	../native/ui/ui.h \
	../native/profiler/profiler.h \
	../native/ui/screen.h \
	../native/ui/virtual_input.h \
	../native/util/bits/bits.h \
	../native/util/bits/hamming.h \
	../native/util/bits/varint.h \
	../native/util/hash/hash.h \
	../native/util/random/perlin.h \
	../native/util/random/rng.h \
	../native/util/text/utf8.h \
	../native/ext/rapidxml/rapidxml.hpp \
	../native/ext/rapidxml/rapidxml_iterators.hpp \
	../native/ext/rapidxml/rapidxml_print.hpp \
	../native/ext/rapidxml/rapidxml_utils.hpp
INCLUDEPATH += ../native

