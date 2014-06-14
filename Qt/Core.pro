QT -= gui
TARGET = Core
TEMPLATE = lib
CONFIG += staticlib

include(Settings.pri)

INCLUDEPATH += $$P/ $$P/native $$P/Core/MIPS $$P/ext/xbrz

arm {
	SOURCES += $$P/Core/MIPS/ARM/*.cpp \ #CoreARM
		$$P/ext/disarm.cpp
	HEADERS += $$P/Core/MIPS/ARM/*.h
}
else {
	SOURCES += $$P/Core/MIPS/x86/*.cpp
	HEADERS += $$P/Core/MIPS/x86/*.h
}

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
	$$P/Core/Util/*.cpp \
	$$P/ext/libkirk/*.c # Kirk

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
	$$P/Core/Util/*.h \
	$$P/ext/libkirk/*.h

win32: INCLUDEPATH += $$P/ffmpeg/WindowsInclude
