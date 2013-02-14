DEFINES += USING_QT_UI
blackberry|symbian|contains(MEEGO_EDITION,harmattan): CONFIG += mobile_platform
unix:!blackberry:!symbian:!macx: CONFIG += linux

# Global specific
DEFINES -= UNICODE
INCLUDEPATH += ../ext/zlib ../native/ext/glew ../Common

win32-msvc* {
	QMAKE_CXXFLAGS_RELEASE += /O2 /arch:SSE2 /fp:fast
	DEFINES += _MBCS GLEW_STATIC NOMINMAX
	PRECOMPILED_HEADER = ../Windows/stdafx.h
	PRECOMPILED_SOURCE = ../Windows/stdafx.cpp
} else {
	QMAKE_CXXFLAGS += -Wno-unused-function -Wno-unused-variable -Wno-multichar -Wno-uninitialized -Wno-ignored-qualifiers -Wno-missing-field-initializers -Wno-unused-parameter
	QMAKE_CXXFLAGS += -std=c++0x -ffast-math -fno-strict-aliasing
}

# Arch specific
contains(QT_ARCH, i686)|contains(QT_ARCH, x86)|contains(QT_ARCH, x86_64)|contains(QT_ARCH, windows): {
	!win32-msvc*: QMAKE_CXXFLAGS += -msse2
	CONFIG += x86
}
else { # Assume ARM
	DEFINES += ARM
	CONFIG += arm
}
mobile_platform: DEFINES += USING_GLES2


# Platform specific
contains(MEEGO_EDITION,harmattan): DEFINES += MEEGO_EDITION_HARMATTAN "_SYS_UCONTEXT_H=1"
blackberry: {
# They try to force QCC with all mkspecs
# QCC is 4.4.1, we need 4.6.3
	QMAKE_CC = ntoarmv7-gcc
	QMAKE_CXX = ntoarmv7-g++
	DEFINES += BLACKBERRY BLACKBERRY10 "_QNX_SOURCE=1" "_C99=1"
}
symbian: {
# Does not seem to be a way to change to armv6 compile so just override in variants.xml (see README)
	MMP_RULES -= "ARMFPU softvfp+vfpv2"
	MMP_RULES += "ARMFPU vfpv2"
	QMAKE_CXXFLAGS += -marm -Wno-parentheses -Wno-comment
	INCLUDEPATH += $$EPOCROOT/epoc32/include/stdapis/glib-2.0
	DEFINES += __MARM_ARMV6__
	CONFIG += 4.6.3
}
