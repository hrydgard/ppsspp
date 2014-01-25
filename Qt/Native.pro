QT += opengl
QT -= gui
TARGET = Native
TEMPLATE = lib
CONFIG += staticlib

include(Settings.pri)

!mobile_platform: {
	SOURCES += $$P/native/ext/glew/glew.c
	HEADERS += $$P/native/ext/glew/GL/*.h
}

# RG_ETC1

SOURCES += $$P/native/ext/rg_etc1/rg_etc1.cpp
HEADERS += $$P/native/ext/rg_etc1/rg_etc1.h
INCLUDEPATH += $$P/native/ext/rg_etc1

# Cityhash

SOURCES += ../native/ext/cityhash/city.cpp
HEADERS += ../native/ext/cityhash/*.h
INCLUDEPATH += ../native/ext/cityhash

# JPGE
SOURCES += $$P/native/ext/jpge/*.cpp
HEADERS += $$P/native/ext/jpge/*.h
INCLUDEPATH += $$P/native/ext/jpge

# Stb_vorbis

SOURCES += $$P/native/ext/stb_vorbis/stb_vorbis.c
HEADERS += $$P/native/ext/stb_vorbis/stb_vorbis.h
INCLUDEPATH += $$P/native/ext/stb_vorbis

# Snappy

SOURCES += $$P/ext/snappy/*.cpp
HEADERS += $$P/ext/snappy/*.h
INCLUDEPATH += $$P/ext/snappy

# VJSON

SOURCES += $$P/native/ext/vjson/json.cpp \
	$$P/native/ext/vjson/block_allocator.cpp
HEADERS += $$P/native/ext/vjson/json.h \
	$$P/native/ext/vjson/block_allocator.h
INCLUDEPATH += $$P/native/ext/vjson

# Zlib
win32|contains(QT_CONFIG, no-zlib) {
	SOURCES += $$P/ext/zlib/*.c
	HEADERS += $$P/ext/zlib/*.h
}

# Libzip
SOURCES += $$P/native/ext/libzip/*.c
HEADERS += $$P/native/ext/libzip/*.h

# Libpng
SOURCES += $$P/native/ext/libpng16/*.c
HEADERS += $$P/native/ext/libpng16/*.h
INCLUDEPATH += $$P/native/ext


# Native

SOURCES +=  $$P/native/audio/*.cpp \
	$$P/native/base/backtrace.cpp \
	$$P/native/base/buffer.cpp \
	$$P/native/base/colorutil.cpp \
	$$P/native/base/display.cpp \
	$$P/native/base/error_context.cpp \
	$$P/native/base/fastlist_test.cpp \
	$$P/native/base/stringutil.cpp \
	$$P/native/base/timeutil.cpp \
	$$P/native/data/compression.cpp \
	$$P/native/file/*.cpp \
	$$P/native/gfx/gl_debug_log.cpp \
	$$P/native/gfx/gl_lost_manager.cpp \
	$$P/native/gfx/texture.cpp \
	$$P/native/gfx/texture_atlas.cpp \
	$$P/native/gfx/texture_gen.cpp \
	$$P/native/gfx_es2/*.cpp \
	$$P/native/gfx_es2/*.c \
	$$P/native/i18n/*.cpp \
	$$P/native/image/*.cpp \
	$$P/native/input/*.cpp \
	$$P/native/math/curves.cpp \
	$$P/native/math/expression_parser.cpp \
	$$P/native/math/math_util.cpp \
	$$P/native/math/lin/*.cpp \
	$$P/native/net/*.cpp \
	$$P/native/profiler/profiler.cpp \
	$$P/native/thread/*.cpp \
	$$P/native/ui/*.cpp \
	$$P/native/util/bits/*.cpp \
	$$P/native/util/hash/hash.cpp \
	$$P/native/util/random/perlin.cpp \
	$$P/native/util/text/utf8.cpp \
	$$P/native/util/text/parsers.cpp

HEADERS +=  $$P/native/audio/*.h \
	$$P/native/base/backtrace.h \
	$$P/native/base/basictypes.h \
	$$P/native/base/buffer.h \
	$$P/native/base/color.h \
	$$P/native/base/colorutil.h \
	$$P/native/base/display.h \
	$$P/native/base/error_context.h \
	$$P/native/base/fastlist.h \
	$$P/native/base/linked_ptr.h \
	$$P/native/base/logging.h \
	$$P/native/base/mutex.h \
	$$P/native/base/scoped_ptr.h \
	$$P/native/base/stats.h \
	$$P/native/base/stringutil.h \
	$$P/native/base/timeutil.h \
	$$P/native/data/compression.h \
	$$P/native/file/*.h \
	$$P/native/gfx/*.h \
	$$P/native/gfx_es2/*.h \
	$$P/native/i18n/*.h \
	$$P/native/image/*.h \
	$$P/native/input/*.h \
	$$P/native/math/*.h \
	$$P/native/math/lin/*.h \
	$$P/native/net/*.h \
	$$P/native/profiler/profiler.h \
	$$P/native/thread/*.h \
	$$P/native/ui/*.h \
	$$P/native/util/bits/*.h \
	$$P/native/util/hash/hash.h \
	$$P/native/util/random/*.h \
	$$P/native/util/text/utf8.h \
	$$P/native/util/text/parsers.h
INCLUDEPATH += $$P/native

