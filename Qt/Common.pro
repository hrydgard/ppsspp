QT += opengl
QT -= gui
TARGET = Common
TEMPLATE = lib
CONFIG += staticlib

blackberry: {
	QMAKE_CC = ntoarmv7-gcc
	QMAKE_CXX = ntoarmv7-g++
	DEFINES += "_QNX_SOURCE=1" "_C99=1"
}

SOURCES +=		../Common/ArmABI.cpp \
				../Common/ArmEmitter.cpp \
				../Common/ThunkARM.cpp \
				../Common/Action.cpp \
				../Common/ColorUtil.cpp \
				../Common/ConsoleListener.cpp \
				../Common/Crypto/aes_cbc.cpp \
				../Common/Crypto/aes_core.cpp \
				../Common/Crypto/bn.cpp \
				../Common/Crypto/ec.cpp \
				../Common/Crypto/md5.cpp \
				../Common/Crypto/sha1.cpp \
				../Common/ExtendedTrace.cpp \
				../Common/FPURoundModeGeneric.cpp \
				../Common/FileSearch.cpp \
				../Common/FileUtil.cpp \
				../Common/Hash.cpp \
				../Common/IniFile.cpp \
				../Common/LogManager.cpp \
				../Common/MemArena.cpp \
				../Common/MemoryUtil.cpp \
				../Common/Misc.cpp \
				../Common/MathUtil.cpp \
				../Common/MsgHandler.cpp \
				../Common/StringUtil.cpp \
				../Common/Thread.cpp \
				../Common/Timer.cpp \
				../Common/Version.cpp
HEADERS +=		../Common/ArmABI.h \
				../Common/ArmEmitter.h \
				../Common/Action.h \
				../Common/ColorUtil.h \
				../Common/ConsoleListener.h \
				../Common/Crypto/md5.h \
				../Common/Crypto/sha1.h \
				../Common/ExtendedTrace.h \
				../Common/FileSearch.h \
				../Common/FileUtil.h \
				../Common/Hash.h \
				../Common/IniFile.h \
				../Common/LogManager.h \
				../Common/MemArena.h \
				../Common/MemoryUtil.h \
				../Common/MathUtil.h \
				../Common/MsgHandler.h \
				../Common/StringUtil.h \
				../Common/Thread.h \
				../Common/Timer.h

QMAKE_CXXFLAGS += -std=c++0x -Wno-unused-function -Wno-unused-variable -Wno-multichar -Wno-uninitialized -Wno-ignored-qualifiers -Wno-missing-field-initializers -Wno-unused-parameter
DEFINES += ARM USING_GLES2
blackberry: DEFINES += BLACKBERRY BLACKBERRY10
symbian: {
	QMAKE_CXXFLAGS += -march=armv6 -mfpu=vfp -mfloat-abi=softfp -marm -Wno-parentheses -Wno-comment
	DEFINES += SYMBIAN
}
