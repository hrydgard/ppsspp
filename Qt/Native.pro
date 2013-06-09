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

# Cityhash

SOURCES += ../native/ext/cityhash/city.cpp
HEADERS += ../native/ext/cityhash/*.h
INCLUDEPATH += ../native/ext/cityhash

# VJSON

SOURCES += ../native/ext/vjson/json.cpp \
	../native/ext/vjson/block_allocator.cpp
HEADERS += ../native/ext/vjson/json.h \
	../native/ext/vjson/block_allocator.h
INCLUDEPATH += ../native/ext/vjson

# Stb_image

SOURCES += ../native/ext/stb_image/stb_image.c
HEADERS += ../native/ext/stb_image/stb_image.h
INCLUDEPATH += ../native/ext/stb_image

# Stb_vorbis

SOURCES += ../native/ext/stb_vorbis/stb_vorbis.c
HEADERS += ../native/ext/stb_vorbis/stb_vorbis.h
INCLUDEPATH += ../native/ext/stb_vorbis

# Snappy

SOURCES += ../ext/snappy/*.cpp
HEADERS += ../ext/snappy/*.h
INCLUDEPATH += ../ext/snappy

# Zlib
!symbian: {
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
	../native/i18n/*.cpp \
	../native/image/*.cpp \
	../native/input/gesture_detector.cpp \
	../native/json/json_writer.cpp \
	../native/math/curves.cpp \
	../native/math/math_util.cpp \
	../native/math/lin/*.cpp \
	../native/midi/midi_input.cpp \
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
	../native/gfx/gl_debug_log.h \
	../native/gfx/gl_lost_manager.h \
	../native/gfx/texture.h \
	../native/gfx/texture_atlas.h \
	../native/gfx/texture_gen.h \
	../native/gfx_es2/*.h \
	../native/i18n/*.h \
	../native/image/*.h \
	../native/input/gesture_detector.h \
	../native/input/input_state.h \
	../native/json/json_writer.h \
	../native/math/curves.h \
	../native/math/lin/*.h \
	../native/midi/midi_input.h \
	../native/net/*.h \
	../native/profiler/profiler.h \
	../native/thread/*.h \
	../native/ui/ui.h \
	../native/ui/screen.h \
	../native/ui/virtual_input.h \
	../native/util/bits/*.h \
	../native/util/hash/hash.h \
	../native/util/random/*.h \
	../native/util/text/utf8.h \
	../native/ext/rapidxml/*.hpp
INCLUDEPATH += ../native

