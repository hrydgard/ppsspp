QT += opengl
QT -= gui
TARGET = Common
TEMPLATE = lib
CONFIG += staticlib

include(Settings.pri)

arm {
	SOURCES += ../Common/ArmCPUDetect.cpp \
		../Common/ArmEmitter.cpp \
		../Common/ArmThunk.cpp
	HEADERS += ../Common/ArmEmitter.h
}
x86 {
	SOURCES += ../Common/ABI.cpp \
		../Common/CPUDetect.cpp \
		../Common/Thunk.cpp \
		../Common/x64Analyzer.cpp \
		../Common/x64Emitter.cpp
	HEADERS +=  ../Common/ABI.h \
		../Common/CPUDetect.h \
		../Common/Thunk.h \
		../Common/x64Analyzer.h \
		../Common/x64Emitter.h
}
win32 {
	SOURCES += ../Common/stdafx.cpp
	HEADERS += ../Common/stdafx.h
}

SOURCES += ../Common/ColorUtil.cpp \
	../Common/ConsoleListener.cpp \
	../Common/ExtendedTrace.cpp \
	../Common/FPURoundModeGeneric.cpp \
	../Common/FileSearch.cpp \
	../Common/FileUtil.cpp \
	../Common/Hash.cpp \
	../Common/LogManager.cpp \
	../Common/MathUtil.cpp \
	../Common/MemArena.cpp \
	../Common/MemoryUtil.cpp \
	../Common/Misc.cpp \
	../Common/MsgHandler.cpp \
	../Common/StringUtils.cpp \
	../Common/Thread.cpp \
	../Common/ThreadPools.cpp \
	../Common/Timer.cpp \
	../Common/Version.cpp \
	../Common/Crypto/*.cpp
HEADERS += ../Common/ChunkFile.h \
	../Common/ColorUtil.h \
	../Common/ConsoleListener.h \
	../Common/ExtendedTrace.h \
	../Common/FileSearch.h \
	../Common/FileUtil.h \
	../Common/Hash.h \
	../Common/LogManager.h \
	../Common/MathUtil.h \
	../Common/MemArena.h \
	../Common/MemoryUtil.h \
	../Common/MsgHandler.h \
	../Common/StringUtils.h \
	../Common/Thread.h \
	../Common/ThreadPools.h \
	../Common/Timer.h \
	../Common/Crypto/*.h

INCLUDEPATH += ../native

