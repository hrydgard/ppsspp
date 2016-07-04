QT += opengl
QT -= gui
TARGET = Native
TEMPLATE = lib
CONFIG += staticlib

include(Settings.pri)

# To support Sailfish which is stuck on GCC 4.6
linux-g++:system($$QMAKE_CXX --version | grep \"4\.6\.\"): DEFINES+=override

INCLUDEPATH += $$P/ext/native

!exists( /usr/include/GL/glew.h ) {
	!contains(DEFINES,USING_GLES2) {
		SOURCES += $$P/ext/native/ext/glew/glew.c
		HEADERS += $$P/ext/native/ext/glew/GL/*.h
		INCLUDEPATH += $$P/ext/native/ext/glew
	}
}

# RG_ETC1

SOURCES += $$P/ext/native/ext/rg_etc1/rg_etc1.cpp
HEADERS += $$P/ext/native/ext/rg_etc1/rg_etc1.h
INCLUDEPATH += $$P/ext/native/ext/rg_etc1

# Cityhash

SOURCES += ../ext/native/ext/cityhash/city.cpp
HEADERS += ../ext/native/ext/cityhash/*.h
INCLUDEPATH += ../ext/native/ext/cityhash

# JPGE
SOURCES += $$P/ext/native/ext/jpge/*.cpp
HEADERS += $$P/ext/native/ext/jpge/*.h
INCLUDEPATH += $$P/ext/native/ext/jpge

# Snappy
!exists( /usr/include/snappy-c.h ) {
	SOURCES += $$P/ext/snappy/*.cpp
	HEADERS += $$P/ext/snappy/*.h
	INCLUDEPATH += $$P/ext/snappy
}

# udis86

SOURCES += $$P/ext/udis86/*.c
HEADERS += $$P/ext/udis86/*.h
INCLUDEPATH += $$P/ext/udis86

# VJSON

SOURCES += $$P/ext/native/ext/vjson/json.cpp \
	$$P/ext/native/ext/vjson/block_allocator.cpp
HEADERS += $$P/ext/native/ext/vjson/json.h \
	$$P/ext/native/ext/vjson/block_allocator.h
INCLUDEPATH += $$P/ext/native/ext/vjson

# Zlib
win32|contains(QT_CONFIG, no-zlib) {
	SOURCES += $$P/ext/zlib/*.c
	HEADERS += $$P/ext/zlib/*.h
}

# Libzip
!exists( /usr/include/zip.h ) {
	SOURCES += $$P/ext/native/ext/libzip/*.c
	HEADERS += $$P/ext/native/ext/libzip/*.h
}

# Native

SOURCES += \
	$$P/ext/native/base/backtrace.cpp \
	$$P/ext/native/base/buffer.cpp \
	$$P/ext/native/base/colorutil.cpp \
	$$P/ext/native/base/compat.cpp \
	$$P/ext/native/base/display.cpp \
	$$P/ext/native/base/stringutil.cpp \
	$$P/ext/native/base/timeutil.cpp \
	$$P/ext/native/data/compression.cpp \
	$$P/ext/native/file/*.cpp \
	$$P/ext/native/gfx/gl_debug_log.cpp \
	$$P/ext/native/gfx/gl_lost_manager.cpp \
	$$P/ext/native/gfx/texture_atlas.cpp \
	$$P/ext/native/gfx_es2/*.cpp \
	$$P/ext/native/gfx_es2/*.c \
	$$P/ext/native/i18n/*.cpp \
	$$P/ext/native/image/*.cpp \
	$$P/ext/native/input/*.cpp \
	$$P/ext/native/math/curves.cpp \
	$$P/ext/native/math/expression_parser.cpp \
	$$P/ext/native/math/math_util.cpp \
	$$P/ext/native/math/lin/*.cpp \
	$$P/ext/native/math/fast/*.c \
	$$P/ext/native/net/*.cpp \
	$$P/ext/native/profiler/profiler.cpp \
	$$P/ext/native/thin3d/thin3d.cpp \
	$$P/ext/native/thin3d/thin3d_gl.cpp \
	$$P/ext/native/thread/*.cpp \
	$$P/ext/native/ui/*.cpp \
	$$P/ext/native/util/hash/hash.cpp \
	$$P/ext/native/util/text/wrap_text.cpp \
	$$P/ext/native/util/text/utf8.cpp \
	$$P/ext/native/util/text/parsers.cpp

armv7: SOURCES += $$files($$P/ext/native/math/fast/fast_matrix_neon.S)


HEADERS += \
	$$P/ext/native/base/backtrace.h \
	$$P/ext/native/base/basictypes.h \
	$$P/ext/native/base/buffer.h \
	$$P/ext/native/base/colorutil.h \
	$$P/ext/native/base/display.h \
	$$P/ext/native/base/linked_ptr.h \
	$$P/ext/native/base/logging.h \
	$$P/ext/native/base/mutex.h \
	$$P/ext/native/base/stringutil.h \
	$$P/ext/native/base/timeutil.h \
	$$P/ext/native/data/compression.h \
	$$P/ext/native/file/*.h \
	$$P/ext/native/gfx/*.h \
	$$P/ext/native/gfx_es2/*.h \
	$$P/ext/native/i18n/*.h \
	$$P/ext/native/image/*.h \
	$$P/ext/native/input/*.h \
	$$P/ext/native/math/*.h \
	$$P/ext/native/math/lin/*.h \
	$$P/ext/native/math/fast/*.h \
	$$P/ext/native/net/*.h \
	$$P/ext/native/profiler/profiler.h \
	$$P/ext/native/thread/*.h \
	$$P/ext/native/ui/*.h \
	$$P/ext/native/util/hash/hash.h \
	$$P/ext/native/util/random/*.h \
	$$P/ext/native/util/text/wrap_text.h \
	$$P/ext/native/util/text/utf8.h \
	$$P/ext/native/util/text/parsers.h
