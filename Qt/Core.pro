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

SOURCES += ../Core/CPU.cpp \ # Core
	../Core/Config.cpp \
	../Core/Core.cpp \
	../Core/CoreTiming.cpp \
	../Core/CwCheat.cpp \
	../Core/Host.cpp \
	../Core/Loaders.cpp \
	../Core/MemMap.cpp \
	../Core/MemMapFunctions.cpp \
	../Core/PSPLoaders.cpp \
	../Core/PSPMixer.cpp \
	../Core/Reporting.cpp \
	../Core/SaveState.cpp \
	../Core/System.cpp \
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

HEADERS += ../Core/CPU.h \
	../Core/Config.h \
	../Core/Core.h \
	../Core/CoreParameter.h \
	../Core/CoreTiming.h \
	../Core/CwCheat.h \
	../Core/Host.h \
	../Core/Loaders.h \
	../Core/MemMap.h \
	../Core/PSPLoaders.h \
	../Core/PSPMixer.h \
	../Core/Reporting.h \
	../Core/SaveState.h \
	../Core/System.h \
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

