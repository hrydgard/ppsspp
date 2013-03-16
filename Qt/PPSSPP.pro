TARGET = PPSSPPQt

QT += core gui opengl
CONFIG += mobility
MOBILITY += multimedia
symbian: MOBILITY += sensors
win32: QT += multimedia

include(Settings.pri)

# Libs
symbian: LIBS += -lCore.lib -lCommon.lib -lNative.lib -lcone -leikcore -lavkon -lezlib
blackberry: LIBS += -L. -lCore -lCommon -lNative -lscreen -lsocket -lstdc++
win32 {
	CONFIG(release, debug|release) {
		LIBS += -L$$OUT_PWD/release
	} else {
		LIBS += -L$$OUT_PWD/debug
    }
	LIBS += -lCore -lCommon -lNative -lwinmm -lws2_32 -lkernel32 -luser32 -lgdi32 -lshell32 -lcomctl32 -ldsound -lxinput
}
linux: LIBS += -L. -lCore -lCommon -lNative

linux:!mobile_platform {
	PRE_TARGETDEPS += ./libCommon.a ./libCore.a ./libNative.a
	CONFIG += link_pkgconfig
	packagesExist(sdl) {
		DEFINES += QT_HAS_SDL
		PKGCONFIG += sdl
	}
}

# Main
SOURCES += ../native/base/QtMain.cpp
HEADERS += ../native/base/QtMain.h

# Native
SOURCES += ../android/jni/EmuScreen.cpp \
	../android/jni/MenuScreens.cpp \
	../android/jni/GamepadEmu.cpp \
	../android/jni/TestRunner.cpp \
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
	INCLUDEPATH += ../Qt
}

# Translations
TRANSLATIONS = $$files(languages/ppsspp_*.ts)

lang.name = lrelease ${QMAKE_FILE_IN}
lang.input = TRANSLATIONS
lang.output = ${QMAKE_FILE_PATH}/${QMAKE_FILE_BASE}.qm
lang.commands = $$[QT_INSTALL_BINS]/lrelease ${QMAKE_FILE_IN}
lang.CONFIG = no_link
QMAKE_EXTRA_COMPILERS += lang
PRE_TARGETDEPS += compiler_lang_make_all

# Packaging
symbian {
	deploy.pkg_prerules = "$${LITERAL_HASH}{\"PPSSPP\"}, (0xE0095B1D), 0, 7, 0, TYPE=SA" "%{\"Qtness\"}" ":\"Qtness\""
	assets.sources = ../android/assets/ui_atlas.zim ../assets/ppge_atlas.zim ../assets/flash
	assets.path = E:/PPSSPP
	DEPLOYMENT += deploy assets
	ICON = ../assets/icon.svg
	# 268MB maximum
	TARGET.EPOCHEAPSIZE = 0x40000 0x10000000
	TARGET.EPOCSTACKSIZE = 0x10000
}
