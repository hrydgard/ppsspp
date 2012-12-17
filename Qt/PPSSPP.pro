TARGET = PPSSPPQt
QT += core gui opengl multimedia

symbian: {
	LIBS += -lCore.lib -lCommon.lib -lNative.lib -lcone -leikcore -lavkon -lezlib
	CONFIG += 4.6.3
}
# They try to force QCC with all mkspecs
# QCC is 4.4.1, we need 4.6.3
blackberry: {
	QMAKE_CC = ntoarmv7-gcc
	QMAKE_CXX = ntoarmv7-g++
	DEFINES += "_QNX_SOURCE=1" "_C99=1"
	LIBS += -L. -lCore -lCommon -lNative -lscreen -lsocket -lstdc++
}

# Main
SOURCES += ../native/base/QtMain.cpp
HEADERS += ../native/base/QtMain.h

# Native

SOURCES += ../android/jni/NativeApp.cpp \
			../android/jni/EmuScreen.cpp \
			../android/jni/MenuScreens.cpp \
			../android/jni/GamepadEmu.cpp \
			../android/jni/UIShader.cpp \
			../android/jni/ui_atlas.cpp

INCLUDEPATH += .. ../Common ../native

QMAKE_CXXFLAGS += -std=c++0x -Wno-unused-function -Wno-unused-variable -Wno-multichar -Wno-uninitialized -Wno-ignored-qualifiers -Wno-missing-field-initializers -Wno-unused-parameter
DEFINES += ARM USING_GLES2
blackberry: DEFINES += BLACKBERRY BLACKBERRY10
symbian: {
	QMAKE_CXXFLAGS += -march=armv6 -mfpu=vfp -mfloat-abi=softfp -marm -Wno-parentheses -Wno-comment
	DEFINES += SYMBIAN

	vendorinfo = "%{\"Qtness\"}" ":\"Qtness\""
	packageheader = "$${LITERAL_HASH}{\"PPSSPP\"}, (0xE0095B1D), 0, 0, 4, TYPE=SA"
	my_deployment.pkg_prerules = packageheader vendorinfo
	assets.sources = ../android/assets/ui_atlas.zim ../android/assets/ppge_atlas.zim
	assets.path = E:/PPSSPP
	DEPLOYMENT += my_deployment assets
        ICON = ../assets/icon.svg
# 268MB maximum
	TARGET.EPOCHEAPSIZE = 0x40000 0x10000000
	TARGET.EPOCSTACKSIZE = 0x10000
}
