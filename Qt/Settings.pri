blackberry|symbian: CONFIG += mobile_platform
unix:!blackberry:!macx: CONFIG += linux

# Global specific
QMAKE_CXXFLAGS += -std=c++0x -Wno-unused-function -Wno-unused-variable -Wno-multichar -Wno-uninitialized -Wno-ignored-qualifiers -Wno-missing-field-initializers -Wno-unused-parameter

# Arch specific
contains(QT_ARCH, i686)|contains(QT_ARCH, x86)|contains(QT_ARCH, x86_64): {
	QMAKE_CXXFLAGS += -msse2
	CONFIG += x86
}
else { # Assume ARM
	DEFINES += ARM
	CONFIG += arm
}
mobile_platform: DEFINES += USING_GLES2


# Platform specific
blackberry: {
# They try to force QCC with all mkspecs
# QCC is 4.4.1, we need 4.6.3
	QMAKE_CC = ntoarmv7-gcc
	QMAKE_CXX = ntoarmv7-g++
	DEFINES += BLACKBERRY BLACKBERRY10 "_QNX_SOURCE=1" "_C99=1"
}
symbian: {
	QMAKE_CXXFLAGS += -march=armv6 -mfpu=vfp -mfloat-abi=softfp -marm -Wno-parentheses -Wno-comment
	DEFINES += SYMBIAN
	CONFIG += 4.6.3
}
