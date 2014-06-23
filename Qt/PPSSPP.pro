TARGET = PPSSPPQt

# Main Qt modules
QT += core gui opengl
include(Settings.pri)

lessThan(QT_MAJOR_VERSION, 5):lessThan(QT_MINOR_VERSION, 7) {
	error(PPSSPP requires Qt 4.7 or newer but Qt $$[QT_VERSION] was detected.)
}

# Extra Qt modules
linux:lessThan(QT_MAJOR_VERSION,5):!exists($$[QT_INSTALL_HEADERS]/QtMultimedia) {
	# Ubuntu et al workaround. They forgot QtMultimedia
	CONFIG += mobility
	MOBILITY += multimedia
}
else: QT += multimedia

greaterThan(QT_MAJOR_VERSION,4) {
	QT += widgets
	mobile_platform: QT += sensors
} else:!maemo5:mobile_platform {
	CONFIG += mobility
	MOBILITY += sensors
	symbian: MOBILITY += systeminfo feedback
}

# PPSSPP Libs
QMAKE_LIBDIR += $$CONFIG_DIR
symbian: LIBS += -lCore.lib -lGPU.lib -lCommon.lib -lNative.lib
else: LIBS += -lCore -lGPU -lCommon -lNative

# FFMPEG Path
win32:	QMAKE_LIBDIR += $$P/ffmpeg/Windows/$${QMAKE_TARGET.arch}/lib/
linux {
	arm: QMAKE_LIBDIR += $$P/ffmpeg/linux/armv7/lib/
	else:QMAKE_LIBDIR += $$P/ffmpeg/linux/$${QMAKE_TARGET.arch}/lib/
}
macx:	QMAKE_LIBDIR += $$P/ffmpeg/macosx/x86_64/lib/
ios:	QMAKE_LIBDIR += $$P/ffmpeg/ios/universal/lib/
qnx:	QMAKE_LIBDIR += $$P/ffmpeg/blackberry/armv7/lib/
symbian:QMAKE_LIBDIR += $$P/ffmpeg/symbian/armv6/lib/
android:QMAKE_LIBDIR += $$P/ffmpeg/android/armv7/lib/

contains(DEFINES, USE_FFMPEG): LIBS += -lavformat -lavcodec -lavutil -lswresample -lswscale

# External (platform-dependant) libs

win32 {
	#Use a fixed base-address under windows
	QMAKE_LFLAGS += /FIXED /BASE:"0x00400000" /DYNAMICBASE:NO
	LIBS += -lwinmm -lws2_32 -lShell32 -lAdvapi32
	contains(QMAKE_TARGET.arch, x86_64): LIBS += $$files($$P/dx9sdk/Lib/x64/*.lib)
	else: LIBS += $$files($$P/dx9sdk/Lib/x86/*.lib)
}

macx|linux {
	PRE_TARGETDEPS += $$CONFIG_DIR/libCommon.a $$CONFIG_DIR/libCore.a $$CONFIG_DIR/libGPU.a $$CONFIG_DIR/libNative.a
	CONFIG += link_pkgconfig
	packagesExist(sdl) {
		DEFINES += QT_HAS_SDL
		SOURCES += $$P/SDL/SDLJoystick.cpp
		HEADERS += $$P/SDL/SDLJoystick.h
		PKGCONFIG += sdl
		macx {
			LIBS += -F/Library/Frameworks -framework SDL
			INCLUDEPATH += /Library/Frameworks/SDL.framework/Versions/A/Headers
		}
	}
}
linux:!android: LIBS += -ldl -lrt
macx: LIBS += -liconv
qnx: LIBS += -lscreen
symbian: LIBS += -lremconcoreapi -lremconinterfacebase
linux:arm|android: LIBS += -lEGL
unix:contains(QT_CONFIG, system-zlib) {
	LIBS += -lz
}

# Main
SOURCES += $$P/native/base/QtMain.cpp
HEADERS += $$P/native/base/QtMain.h
symbian {
	SOURCES += $$P/native/base/SymbianMediaKeys.cpp
	HEADERS += $$P/native/base/SymbianMediaKeys.h
}

# UI
SOURCES += $$P/UI/*Screen.cpp \
	$$P/UI/*Screens.cpp \
	$$P/UI/BackgroundAudio.cpp \
	$$P/UI/Store.cpp \
	$$P/UI/GamepadEmu.cpp \
	$$P/UI/GameInfoCache.cpp \
	$$P/UI/NativeApp.cpp \
	$$P/UI/OnScreenDisplay.cpp \
	$$P/UI/TiltEventProcessor.cpp \
	$$P/UI/UIShader.cpp \
	$$P/UI/ui_atlas_lowmem.cpp \
	$$P/android/jni/TestRunner.cpp

arm:android: SOURCES += $$P/android/jni/ArmEmitterTest.cpp

HEADERS += $$P/UI/*.h
INCLUDEPATH += $$P $$P/Common $$P/native $$P/native/ext

mobile_platform: RESOURCES += $$P/Qt/assets.qrc
else {
	# TODO: Rewrite Debugger with same backend as Windows version
	# Don't use .ui forms. Use Qt5 + C++11 features to minimise code
	SOURCES += $$P/Qt/*.cpp $$P/Qt/Debugger/*.cpp
	HEADERS += $$P/Qt/*.h $$P/Qt/Debugger/*.h
	FORMS += $$P/Qt/Debugger/*.ui
	RESOURCES += $$P/Qt/desktop_assets.qrc
	INCLUDEPATH += $$P/Qt $$P/Qt/Debugger
	
	# Creating translations should be done by Qt, really
	LREL_TOOL = lrelease
	# Grab all possible directories (win32/unix) and search from last to first
	win32: PATHS = $$reverse($$split($$(PATH), ;))
	else: PATHS = $$reverse($$split($$(PATH), :))
	greaterThan(QT_MAJOR_VERSION, 4) {
		for(bin, PATHS): exists($${bin}/$${LREL_TOOL}-qt5): LREL_TOOL=$${bin}/$${LREL_TOOL}-qt5
	} else {
		for(bin, PATHS): exists($${bin}/$${LREL_TOOL}-qt4): LREL_TOOL=$${bin}/$${LREL_TOOL}-qt4
	}

	# Translations
	TRANSLATIONS = $$files($$P/Qt/languages/ppsspp_*.ts)

	lang.name = $$LREL_TOOL ${QMAKE_FILE_IN}
	lang.input = TRANSLATIONS
	lang.output = ${QMAKE_FILE_PATH}/${QMAKE_FILE_BASE}.qm
	lang.commands = $$LREL_TOOL ${QMAKE_FILE_IN}
	lang.CONFIG = no_link
	QMAKE_EXTRA_COMPILERS += lang
	PRE_TARGETDEPS += compiler_lang_make_all
}

# Packaging
win32: ICON = $$P/Windows/ppsspp.rc

symbian {
	TARGET.UID3 = 0xE0095B1D
	DEPLOYMENT.display_name = PPSSPP
	vendor_deploy.pkg_prerules = "%{\"Qtness\"}" ":\"Qtness\""
	ICON = $$P/assets/icon.svg

	DEPLOYMENT += vendor_deploy
	MMP_RULES += "DEBUGGABLE"

	# 268 MB maximum
	TARGET.EPOCHEAPSIZE = 0x40000 0x10000000
	TARGET.EPOCSTACKSIZE = 0x10000
}

linux {
        icon.files = $$P/assets/icon-114.png
        icon.path = /usr/share/icons/hicolor/114x114/apps
        INSTALLS += icon
}

maemo {
	target.path = /opt/PPSSPP/bin
	desktopfile.files = PPSSPP.desktop
	desktopfile.path = /usr/share/applications
	INSTALLS += target desktopfile
	# Booster
	QMAKE_CXXFLAGS += -fPIC -fvisibility=hidden -fvisibility-inlines-hidden
	QMAKE_LFLAGS += -pie -rdynamic
	CONFIG += qt-boostable
}

ANDROID_PACKAGE_SOURCE_DIR = $$PWD/android

