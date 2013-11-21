QT += opengl
QT -= gui
TARGET = Native
TEMPLATE = lib
CONFIG += staticlib

include(Settings.pri)

!mobile_platform: {
	SOURCES += ../native/ext/glew/glew.c
	HEADERS += ../native/ext/glew/GL/*.h
}

# Backtrace
x86:!mobile_platform: {
	SOURCES += ../native/base/backtrace.cpp
	HEADERS += ../native/base/backtrace.h
}

# RG_ETC1

SOURCES += ../native/ext/rg_etc1/rg_etc1.cpp
HEADERS += ../native/ext/rg_etc1/rg_etc1.h
INCLUDEPATH += ../native/ext/rg_etc1

# JPGE
SOURCES += ../native/ext/jpge/*.cpp
HEADERS += ../native/ext/jpge/*.h
INCLUDEPATH += ../native/ext/jpge

# Stb_image

SOURCES += ../native/ext/stb_image/stb_image.c
HEADERS += ../native/ext/stb_image/stb_image.h
INCLUDEPATH += ../native/ext/stb_image
win32 {
    SOURCES += ../native/ext/stb_image_write/stb_image_write.c
    HEADERS += ../native/ext/stb_image_write/stb_image_writer.h
    INCLUDEPATH += ../native/ext/stb_image_write
}

# Stb_vorbis

SOURCES += ../native/ext/stb_vorbis/stb_vorbis.c
HEADERS += ../native/ext/stb_vorbis/stb_vorbis.h
INCLUDEPATH += ../native/ext/stb_vorbis

# Snappy

SOURCES += ../ext/snappy/*.cpp
HEADERS += ../ext/snappy/*.h
INCLUDEPATH += ../ext/snappy

# Zlib
win32|contains(QT_CONFIG, no-zlib) {
	SOURCES += ../ext/zlib/*.c
	HEADERS += ../ext/zlib/*.h
}

# Native

SOURCES +=  ../native/audio/*.cpp \
	../native/base/buffer.cpp \
	../native/base/colorutil.cpp \
	../native/base/display.cpp \
	../native/base/error_context.cpp \
	../native/base/fastlist_test.cpp \
	../native/base/stringutil.cpp \
	../native/base/timeutil.cpp \
	../native/data/compression.cpp \
	../native/file/*.cpp \
	../native/gfx/gl_debug_log.cpp \
	../native/gfx/gl_lost_manager.cpp \
	../native/gfx/texture.cpp \
	../native/gfx/texture_atlas.cpp \
	../native/gfx/texture_gen.cpp \
	../native/gfx_es2/*.cpp \
	../native/gfx_es2/*.c \
	../native/i18n/*.cpp \
	../native/image/*.cpp \
	../native/input/*.cpp \
	../native/math/curves.cpp \
	../native/math/expression_parser.cpp \
	../native/math/math_util.cpp \
	../native/math/lin/*.cpp \
	../native/net/*.cpp \
	../native/profiler/profiler.cpp \
	../native/thread/*.cpp \
	../native/ui/*.cpp \
	../native/util/bits/*.cpp \
	../native/util/hash/hash.cpp \
	../native/util/random/perlin.cpp \
	../native/util/text/utf8.cpp

HEADERS +=  ../native/audio/*.h \
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
	../native/base/timeutil.h \
	../native/data/compression.h \
	../native/file/*.h \
	../native/gfx/*.h \
	../native/gfx_es2/*.h \
	../native/i18n/*.h \
	../native/image/*.h \
	../native/input/*.h \
	../native/math/*.h \
	../native/math/lin/*.h \
	../native/net/*.h \
	../native/profiler/profiler.h \
	../native/thread/*.h \
	../native/ui/*.h \
	../native/util/bits/*.h \
	../native/util/hash/hash.h \
	../native/util/random/*.h \
	../native/util/text/utf8.h
INCLUDEPATH += ../native

