QT += opengl
QT -= gui
TARGET = Core
TEMPLATE = lib
CONFIG += staticlib

version.target = ../git-version.cpp
!contains(MEEGO_EDITION,harmattan):contains(QMAKE_HOST.os, "Windows") { version.commands = $$PWD/../Windows/git-version-gen.cmd }
else { version.commands = $$PWD/git-version-gen.sh }
version.depends = ../.git

QMAKE_EXTRA_TARGETS += version
PRE_TARGETDEPS += ../git-version.cpp
SOURCES += ../git-version.cpp

include(Settings.pri)

INCLUDEPATH += ../native ../Core/MIPS ../ ../ext/xbrz

arm {
	SOURCES += ../Core/MIPS/ARM/*.cpp \ #CoreARM
		../ext/disarm.cpp
	HEADERS += ../Core/MIPS/ARM/*.h
}
x86 {
	SOURCES += ../Core/MIPS/x86/*.cpp
	HEADERS += ../Core/MIPS/x86/*.h
}

win32 {
	SOURCES += ../Windows/OpenGLBase.cpp
	HEADERS += ../Windows/OpenGLBase.h
}

SOURCES += ../Core/*.cpp \ # Core
	../Core/Debugger/*.cpp \
	../Core/Dialog/*.cpp \
	../Core/ELF/*.cpp \
	../Core/FileSystems/*.cpp \
	../Core/Font/*.cpp \
	../Core/HLE/*.cpp \
	../Core/HW/*.cpp \
	../Core/MIPS/*.cpp \
	../Core/MIPS/JitCommon/*.cpp \
	../Core/Util/*.cpp \
	../GPU/GeDisasm.cpp \ # GPU
	../GPU/GPUCommon.cpp \
	../GPU/GPUState.cpp \
	../GPU/Math3D.cpp \
	../GPU/Null/NullGpu.cpp \
	../GPU/GLES/*.cpp \
	../ext/libkirk/*.c \ # Kirk
	../ext/xbrz/*.cpp # XBRZ

HEADERS += ../Core/*.h \
	../Core/Debugger/*.h \
	../Core/Dialog/*.h \
	../Core/ELF/*.h \
	../Core/FileSystems/*.h \
	../Core/Font/*.h \
	../Core/HLE/*.h \
	../Core/HW/*.h \
	../Core/MIPS/*.h \
	../Core/MIPS/JitCommon/*.h \
	../Core/Util/*.h \
	../GPU/GLES/*.h \
	../GPU/*.h \
	../ext/libkirk/*.h \
	../ext/xbrz/*.h

