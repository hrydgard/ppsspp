TARGET = PPSSPPQt

QT += core gui opengl
CONFIG += mobility
MOBILITY += multimedia
win32: QT += multimedia
VERSION = 0.8.0

include(Settings.pri)
mobile_platform: MOBILITY += sensors
symbian: MOBILITY += systeminfo

# Libs
symbian {
	LIBS += -lCore.lib -lCommon.lib -lNative.lib
	# For now you have to copy these to the Symbian lib dir using ffmpeg/symbian-install.sh
	LIBS += -lavformat.lib -lavcodec.lib -lavutil.lib -lswresample.lib -lswscale.lib
}
qnx: LIBS += -L. -lCore -lCommon -lNative -lscreen -lz
win32 {
	CONFIG(release, debug|release) {
		LIBS += -L$$OUT_PWD/release
	} else {
		LIBS += -L$$OUT_PWD/debug
	}
	LIBS += -lCore -lCommon -lNative -lwinmm -lws2_32
}
linux {
	LIBS += -L. -lCore -lCommon -lNative -ldl
	PRE_TARGETDEPS += ./libCommon.a ./libCore.a ./libNative.a
	!mobile_platform {
		CONFIG += link_pkgconfig
		packagesExist(sdl) {
			DEFINES += QT_HAS_SDL
			PKGCONFIG += sdl
		}
	}
}

# Main
SOURCES += ../native/base/QtMain.cpp
HEADERS += ../native/base/QtMain.h

# Native
SOURCES += ../UI/EmuScreen.cpp \
	../UI/MenuScreens.cpp \
	../UI/GamepadEmu.cpp \
	../UI/GameInfoCache.cpp \
	../UI/OnScreenDisplay.cpp \
	../UI/PluginScreen.cpp \
	../android/jni/TestRunner.cpp \
	../UI/UIShader.cpp \
	../UI/ui_atlas.cpp

INCLUDEPATH += .. ../Common ../native

# Temporarily only use new UI for Linux desktop
linux:!mobile_platform {
	MOC_DIR = moc
	UI_DIR = ui
	RCC_DIR = rcc
	SOURCES += *.cpp
	HEADERS += *.h
	FORMS += *.ui
	RESOURCES += resources.qrc
	INCLUDEPATH += ../Qt
} else {
	SOURCES += ../UI/NativeApp.cpp
}
RESOURCES += assets.qrc

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
	deploy.pkg_prerules = "$${LITERAL_HASH}{\"PPSSPP\"}, (0xE0095B1D), 0, 8, 0, TYPE=SA" "%{\"Qtness\"}" ":\"Qtness\""
	assets.sources = ../assets/flash ../lang
	assets.path = E:/PPSSPP
	DEPLOYMENT += deploy assets
	ICON = ../assets/icon.svg
	# 268MB maximum
	TARGET.EPOCHEAPSIZE = 0x40000 0x10000000
	TARGET.EPOCSTACKSIZE = 0x10000
}

contains(MEEGO_EDITION,harmattan) {
	target.path = /opt/PPSSPP/bin
	assets.files = ../assets/flash ../lang
	assets.path = /opt/PPSSPP
	desktopfile.files = PPSSPP.desktop
	desktopfile.path = /usr/share/applications
	icon.files = ../assets/icon-114.png
	icon.path = /usr/share/icons/hicolor/114x114/apps
	INSTALLS += target assets desktopfile icon
	# Booster
	QMAKE_CXXFLAGS += -fPIC -fvisibility=hidden -fvisibility-inlines-hidden
	QMAKE_LFLAGS += -pie -rdynamic
	CONFIG += qt-boostable
}

