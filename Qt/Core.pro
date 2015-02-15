QT -= gui
TARGET = Core
TEMPLATE = lib
CONFIG += staticlib

include(Settings.pri)

INCLUDEPATH += $$P/ $$P/native $$P/Core/MIPS $$P/ext/xbrz
!contains(DEFINES, USING_GLES2): INCLUDEPATH += $$P/native/ext/glew

arm {
	SOURCES += $$P/Core/MIPS/ARM/*.cpp #CoreARM
	HEADERS += $$P/Core/MIPS/ARM/*.h
}
else:i86 {
	SOURCES += $$P/Core/MIPS/x86/*.cpp
	HEADERS += $$P/Core/MIPS/x86/*.h
}
else {
	SOURCES += $$P/Core/MIPS/fake/*.cpp
	HEADERS += $$P/Core/MIPS/fake/*.h
}
SOURCES += $$P/ext/disarm.cpp

SOURCES += $$P/Core/*.cpp \ # Core
	$$P/Core/Debugger/*.cpp \
	$$P/Core/Dialog/*.cpp \
	$$P/Core/ELF/*.cpp \
	$$P/Core/FileSystems/*.cpp \
	$$P/Core/Font/*.cpp \
	$$P/Core/HLE/*.cpp \
	$$P/Core/HW/*.cpp \
	$$P/Core/MIPS/*.cpp \
	$$P/Core/MIPS/JitCommon/*.cpp \
	$$P/Core/Util/AudioFormat.cpp \
	$$P/Core/Util/BlockAllocator.cpp \
	$$P/Core/Util/GameManager.cpp \
	$$P/Core/Util/ppge_atlas.cpp \
	$$P/Core/Util/PPGeDraw.cpp \
	$$P/ext/libkirk/*.c \ # Kirk
	$$P/ext/sfmt19937/*.c

HEADERS += $$P/Core/*.h \
	$$P/Core/Debugger/*.h \
	$$P/Core/Dialog/*.h \
	$$P/Core/ELF/*.h \
	$$P/Core/FileSystems/*.h \
	$$P/Core/Font/*.h \
	$$P/Core/HLE/*.h \
	$$P/Core/HW/*.h \
	$$P/Core/MIPS/*.h \
	$$P/Core/MIPS/JitCommon/*.h \
	$$P/Core/Util/AudioFormat.h \
	$$P/Core/Util/BlockAllocator.h \
	$$P/Core/Util/GameManager.h \
	$$P/Core/Util/ppge_atlas.h \
	$$P/Core/Util/PPGeDraw.h \
	$$P/ext/libkirk/*.h \
	$$P/ext/sfmt19937/*.h

armv7: SOURCES += $$P/Core/Util/AudioFormatNEON.cpp

win32: INCLUDEPATH += $$P/ffmpeg/WindowsInclude
