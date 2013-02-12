TARGET = PPSSPPQt

QT += core gui opengl
CONFIG += mobility
MOBILITY += multimedia

include(Settings.pri)

# Libs
symbian: LIBS += -lCore.lib -lCommon.lib -lNative.lib -lcone -leikcore -lavkon -lezlib
blackberry: LIBS += -L. -lCore -lCommon -lNative -lscreen -lsocket -lstdc++
win32: LIBS += -L. -lCore -lCommon -lNative -lwinmm -lws2_32 -lkernel32 -luser32 -lgdi32 -lshell32 -lcomctl32 -ldsound -lxinput
linux: LIBS += -L. -lCore -lCommon -lNative

!mobile_platform {
	PRE_TARGETDEPS += ./libCommon.a ./libCore.a ./libNative.a
	CONFIG += link_pkgconfig
	packagesExist(sdl) {
		DEFINES += QT_HAS_SDL
		PKGCONFIG += sdl
	}
}

TRANSLATIONS = languages/ppsspp_en.ts \
		languages/ppsspp_pl.ts \
		languages/ppsspp_fr.ts \
		languages/ppsspp_de.ts \
		languages/ppsspp_cn.ts

# Main
SOURCES += ../native/base/QtMain.cpp
HEADERS += ../native/base/QtMain.h

# Native
SOURCES += ../android/jni/EmuScreen.cpp \
	../android/jni/MenuScreens.cpp \
	../android/jni/GamepadEmu.cpp \
	../android/jni/UIShader.cpp \
	../android/jni/ui_atlas.cpp

INCLUDEPATH += .. ../Common ../native

# Temporarily only use new UI for Linux desktop
mobile_platform {
	SOURCES += ../android/jni/NativeApp.cpp
} else {
	MOC_DIR = moc
	UI_DIR = ui
	RCC_DIR = rcc
	SOURCES += *.cpp
	HEADERS += *.h
	FORMS += *.ui
	RESOURCES += resources.qrc
}

# Packaging
symbian {
	deploy.pkg_prerules = "$${LITERAL_HASH}{\"PPSSPP\"}, (0xE0095B1D), 0, 6, 1, TYPE=SA" "%{\"Qtness\"}" ":\"Qtness\""
	assets.sources = ../android/assets/ui_atlas.zim ../assets/ppge_atlas.zim
	assets.path = E:/PPSSPP
	DEPLOYMENT += deploy assets
	ICON = ../assets/icon.svg
	# 268MB maximum
	TARGET.EPOCHEAPSIZE = 0x40000 0x10000000
	TARGET.EPOCSTACKSIZE = 0x10000
}
