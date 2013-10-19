DEFINES += USING_QT_UI USE_FFMPEG
unix:!qnx:!symbian:!macx: CONFIG += linux

# Global specific
INCLUDEPATH += ../ext/zlib ../native/ext/glew ../Common

win32-msvc* {
	QMAKE_CXXFLAGS_RELEASE += /O2 /arch:SSE2 /fp:fast
	DEFINES += _MBCS GLEW_STATIC _CRT_SECURE_NO_WARNINGS
	PRECOMPILED_HEADER = ../Windows/stdafx.h
	PRECOMPILED_SOURCE = ../Windows/stdafx.cpp
	INCLUDEPATH += .. ../ffmpeg/Windows/$${QMAKE_TARGET.arch}/include
} else {
	QMAKE_CXXFLAGS += -Wno-unused-function -Wno-unused-variable -Wno-multichar -Wno-uninitialized -Wno-ignored-qualifiers -Wno-missing-field-initializers -Wno-unused-parameter
	QMAKE_CXXFLAGS += -std=c++0x -ffast-math -fno-strict-aliasing
	QMAKE_CFLAGS_RELEASE -= -O2
	QMAKE_CFLAGS_RELEASE += -O3
	QMAKE_CXXFLAGS_RELEASE -= -O2
	QMAKE_CXXFLAGS_RELEASE += -O3
}
# Arch specific
xarch = $$find(QT_ARCH, "86")
contains(QT_ARCH, windows)|count(xarch, 1) {
	!win32-msvc*: QMAKE_CXXFLAGS += -msse2
	CONFIG += x86
}
else { # Assume ARM
	DEFINES += ARM
	CONFIG += arm
}

gleslib = $$lower($$QMAKE_LIBS_OPENGL)
gleslib = $$find(gleslib, "gles")
contains(MEEGO_EDITION,harmattan)|!count(gleslib,0) {
	DEFINES += USING_GLES2
	CONFIG += mobile_platform
}

# Platform specific
contains(MEEGO_EDITION,harmattan): {
	# Does not yet support FFMPEG
	DEFINES -= USE_FFMPEG
	DEFINES += MEEGO_EDITION_HARMATTAN "_SYS_UCONTEXT_H=1"
}

linux:!mobile_platform: {
	DEFINES += __STDC_CONSTANT_MACROS
	contains(QT_ARCH, x86_64) {
		QMAKE_TARGET.arch = x86_64
	} else {
		QMAKE_TARGET.arch = x86
	}
	INCLUDEPATH += ../ffmpeg/linux/$${QMAKE_TARGET.arch}/include
}
qnx {
	# Use mkspec: unsupported/qws/qnx-armv7-g++
	DEFINES += BLACKBERRY "_QNX_SOURCE=1" "_C99=1"
}
symbian {
	# Does not seem to be a way to change to armv6 compile so just override in variants.xml (see README)
	MMP_RULES -= "ARMFPU softvfp+vfpv2"
	MMP_RULES += "ARMFPU vfpv2"
	DEFINES += __STDC_CONSTANT_MACROS
#"BOOST_COMPILER_CONFIG=<boost/mpl/aux_/config/gcc.hpp>"
	QMAKE_CXXFLAGS += -marm -Wno-parentheses -Wno-comment
	INCLUDEPATH += $$EPOCROOT/epoc32/include/stdapis $$EPOCROOT/epoc32/include/stdapis/glib-2.0
	INCLUDEPATH += ../ffmpeg/symbian/armv6/include
}
