QT -= gui
TARGET = Common
TEMPLATE = lib
CONFIG += staticlib

include(Settings.pri)

# CPU
arm {
	SOURCES += $$P/Common/ArmCPUDetect.cpp \
		$$P/Common/ArmEmitter.cpp \
		$$P/Common/ArmThunk.cpp
	HEADERS += $$P/Common/ArmEmitter.h
}
else {
	SOURCES += $$P/Common/ABI.cpp \
		$$P/Common/CPUDetect.cpp \
		$$P/Common/Thunk.cpp \
		$$P/Common/x64Analyzer.cpp \
		$$P/Common/x64Emitter.cpp
	HEADERS +=  $$P/Common/ABI.h \
		$$P/Common/Thunk.h \
		$$P/Common/x64Analyzer.h \
		$$P/Common/x64Emitter.h
}
HEADERS += $$P/Common/CPUDetect.h

win32 {
	SOURCES += $$P/Common/stdafx.cpp
	HEADERS += $$P/Common/stdafx.h
}

!symbian {
	SOURCES += $$P/Common/MemArena.cpp
	HEADERS += $$P/Common/MemArena.h
}

SOURCES += $$P/Common/ChunkFile.cpp \
	$$P/Common/ConsoleListener.cpp \
	$$P/Common/FileUtil.cpp \
	$$P/Common/LogManager.cpp \
	$$P/Common/KeyMap.cpp \
	$$P/Common/MemoryUtil.cpp \
	$$P/Common/Misc.cpp \
	$$P/Common/MsgHandler.cpp \
	$$P/Common/StringUtils.cpp \
	$$P/Common/ThreadPools.cpp \
	$$P/Common/Timer.cpp \
	$$P/Common/Crypto/*.cpp
HEADERS += $$P/Common/ChunkFile.h \
	$$P/Common/ConsoleListener.h \
	$$P/Common/FileUtil.h \
	$$P/Common/LogManager.h \
	$$P/Common/KeyMap.h \
	$$P/Common/MemoryUtil.h \
	$$P/Common/MsgHandler.h \
	$$P/Common/StringUtils.h \
	$$P/Common/ThreadPools.h \
	$$P/Common/Timer.h \
	$$P/Common/Crypto/*.h

INCLUDEPATH += $$P/native

