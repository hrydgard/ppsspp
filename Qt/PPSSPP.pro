TARGET = PPSSPPQt
VERSION = 0.9.5

# Main Qt modules
QT += core gui opengl
include(Settings.pri)

# Extra Qt modules
linux: CONFIG += link_pkgconfig
win32|greaterThan(QT_MAJOR_VERSION,4) {
	QT += multimedia
} else {
	linux:packagesExist(QtMultimedia) {
		QT += multimedia
	} else {
		CONFIG += mobility
		MOBILITY += multimedia
	}
}
greaterThan(QT_MAJOR_VERSION,4): QT += widgets

mobile_platform: MOBILITY += sensors
symbian: MOBILITY += systeminfo feedback

# PPSSPP Libs
symbian: XT=".lib"
else: LIBS += -L$$CONFIG_DIR
LIBS += -lCore$${XT} -lCommon$${XT} -lNative$${XT}

# FFMPEG Path
win32:  FFMPEG_DIR = ../ffmpeg/Windows/$${QMAKE_TARGET.arch}/lib/
linux:  FFMPEG_DIR = ../ffmpeg/linux/$${QMAKE_TARGET.arch}/lib/
macx:!mobile_platform:   FFMPEG_DIR = ../ffmpeg/macosx/x86_64/lib/
qnx:    FFMPEG_DIR = ../ffmpeg/blackberry/armv7/lib/
symbian:FFMPEG_DIR = -l

# External (platform-dependant) libs
win32|symbian: LIBS += $${FFMPEG_DIR}avformat.lib $${FFMPEG_DIR}avcodec.lib $${FFMPEG_DIR}avutil.lib $${FFMPEG_DIR}swresample.lib $${FFMPEG_DIR}swscale.lib
else:!contains(MEEGO_EDITION,harmattan): LIBS += $${FFMPEG_DIR}libavformat.a $${FFMPEG_DIR}libavcodec.a $${FFMPEG_DIR}libavutil.a $${FFMPEG_DIR}libswresample.a $${FFMPEG_DIR}libswscale.a

win32 {
	#Use a fixed base-address under windows
	QMAKE_LFLAGS += /FIXED /BASE:"0x00400000"
	QMAKE_LFLAGS += /DYNAMICBASE:NO
	LIBS += -lwinmm -lws2_32 -lShell32 -lAdvapi32
	contains(QMAKE_TARGET.arch, x86_64): LIBS += $$files(../dx9sdk/Lib/x64/*.lib)
	else: LIBS += $$files(../dx9sdk/Lib/x86/*.lib)
}
linux {
	LIBS += -ldl
	PRE_TARGETDEPS += ./libCommon.a ./libCore.a ./libNative.a
	packagesExist(sdl) {
		DEFINES += QT_HAS_SDL
		PKGCONFIG += sdl
	}
}
qnx: LIBS += -lscreen
symbian: LIBS += -lRemConCoreApi -lRemConInterfaceBase
contains(QT_CONFIG, system-zlib) {
	unix: LIBS += -lz
}

# Main
SOURCES += ../native/base/QtMain.cpp
HEADERS += ../native/base/QtMain.h
symbian {
	SOURCES += ../native/base/SymbianMediaKeys.cpp
	HEADERS += ../native/base/SymbianMediaKeys.h
}

# UI
SOURCES += ../UI/*Screen.cpp \
	../UI/*Screens.cpp \
	../UI/GamepadEmu.cpp \
	../UI/GameInfoCache.cpp \
	../UI/OnScreenDisplay.cpp \
	../UI/UIShader.cpp \
	../android/jni/TestRunner.cpp

HEADERS += ../UI/*.h
INCLUDEPATH += .. ../Common ../native

# Use forms UI for desktop platforms
!mobile_platform {
	SOURCES += *.cpp
	HEADERS += *.h
	FORMS += *.ui
	RESOURCES += resources.qrc
	INCLUDEPATH += ../Qt

	# Translations
	TRANSLATIONS = $$files(languages/ppsspp_*.ts)

	lang.name = lrelease ${QMAKE_FILE_IN}
	lang.input = TRANSLATIONS
	lang.output = ${QMAKE_FILE_PATH}/${QMAKE_FILE_BASE}.qm
	lang.commands = $$[QT_INSTALL_BINS]/lrelease ${QMAKE_FILE_IN}
	lang.CONFIG = no_link
	QMAKE_EXTRA_COMPILERS += lang
	PRE_TARGETDEPS += compiler_lang_make_all
} else {
	# Desktop handles the Init separately
	SOURCES += ../UI/NativeApp.cpp
}
RESOURCES += assets.qrc
SOURCES += ../UI/ui_atlas_lowmem.cpp

# Packaging
symbian {
	TARGET.UID3 = 0xE0095B1D
	DEPLOYMENT.display_name = PPSSPP
	vendor_deploy.pkg_prerules = "%{\"Qtness\"}" ":\"Qtness\""
	ICON = ../assets/icon.svg

	# Folders:
	shaders.sources = ../assets/shaders
	shaders.path = E:/PPSSPP/PSP
	lang.sources = $$files(../lang/*.ini)
	lang.path = E:/PPSSPP/lang

	DEPLOYMENT += vendor_deploy assets shaders lang

	# 268 MB maximum
	TARGET.EPOCHEAPSIZE = 0x40000 0x10000000
	TARGET.EPOCSTACKSIZE = 0x10000
}

contains(MEEGO_EDITION,harmattan) {
	target.path = /opt/PPSSPP/bin
	shaders.files = ../assets/shaders
	shaders.path = /opt/PPSSPP/PSP
	lang.files = $$files(../lang/*.ini)
	lang.path = /opt/PPSSPP/lang
	desktopfile.files = PPSSPP.desktop
	desktopfile.path = /usr/share/applications
	icon.files = ../assets/icon-114.png
	icon.path = /usr/share/icons/hicolor/114x114/apps
	INSTALLS += target assets shaders lang desktopfile icon
	# Booster
	QMAKE_CXXFLAGS += -fPIC -fvisibility=hidden -fvisibility-inlines-hidden
	QMAKE_LFLAGS += -pie -rdynamic
	CONFIG += qt-boostable
}

