TARGET = ppsspp

# Main Qt modules
QT += core gui opengl

# PPSSPP Modules
symbian: LIBS += -lCore.lib -lGPU.lib -lCommon.lib -lNative.lib
else: LIBS += -lCore -lGPU -lCommon -lNative

include(Settings.pri)

# To support Sailfish which is stuck on GCC 4.6
linux-g++:system($$QMAKE_CXX --version | grep "4.6."): DEFINES+=override

lessThan(QT_MAJOR_VERSION, 5) {
	macx: error(PPSSPP requires Qt5 for OS X but $$[QT_VERSION] was detected.)
	else:lessThan(QT_MINOR_VERSION, 7): error(PPSSPP requires Qt 4.7 or newer but Qt $$[QT_VERSION] was detected.)
}

# Extra Qt modules
greaterThan(QT_MAJOR_VERSION,4) {
	QT += widgets
	# Most platforms don't have this pre-installed. Especially hard to get on OSX.
	exists($$[QT_INSTALL_HEADERS]/QtSystemInfo) {
		QT += systeminfo
		DEFINES += QT_HAS_SYSTEMINFO
	}
	mobile_platform: QT += sensors
} else:!maemo5:mobile_platform {
	CONFIG += mobility
	MOBILITY += sensors
	symbian: MOBILITY += systeminfo feedback
}

# External (platform-dependant) libs

macx|equals(PLATFORM_NAME, "linux") {
	PRE_TARGETDEPS += $$CONFIG_DIR/libCommon.a $$CONFIG_DIR/libCore.a $$CONFIG_DIR/libGPU.a $$CONFIG_DIR/libNative.a
	CONFIG += link_pkgconfig
	packagesExist(sdl2) {
		DEFINES += SDL
		SOURCES += $$P/SDL/SDLJoystick.cpp
		HEADERS += $$P/SDL/SDLJoystick.h
		PKGCONFIG += sdl2
		macx {
			LIBS += -F/Library/Frameworks -framework SDL
			INCLUDEPATH += /Library/Frameworks/SDL.framework/Versions/A/Headers
		}
	}
}

unix:contains(QT_CONFIG, system-zlib) {
	LIBS += -lz
}

# Qt Multimedia (if SDL is not found)
!contains(DEFINES, SDL) {
	lessThan(QT_MAJOR_VERSION,5):!exists($$[QT_INSTALL_HEADERS]/QtMultimedia) {
		# Fallback to mobility audio
		CONFIG += mobility
		MOBILITY += multimedia
	}
	else: QT += multimedia
}

# Main
SOURCES += $$P/native/base/QtMain.cpp
HEADERS += $$P/native/base/QtMain.h
symbian {
	SOURCES += $$P/native/base/SymbianMediaKeys.cpp
	HEADERS += $$P/native/base/SymbianMediaKeys.h
}

# UI
SOURCES += $$P/UI/*.cpp \
	$$P/android/jni/TestRunner.cpp
arm:android: SOURCES += $$P/android/jni/ArmEmitterTest.cpp
HEADERS += $$P/UI/*.h

INCLUDEPATH += $$P $$P/Common $$P/native $$P/native/ext $$P/native/ext/glew

mobile_platform {
	!no_assets: RESOURCES += $$P/Qt/assets.qrc
} else {
	# TODO: Rewrite Debugger with same backend as Windows version
	# Do not use .ui forms. Use Qt5 + C++11 features to minimise code
	SOURCES += $$P/Qt/*.cpp $$P/Qt/Debugger/*.cpp
	HEADERS += $$P/Qt/*.h $$P/Qt/Debugger/*.h
	FORMS += $$P/Qt/Debugger/*.ui
	!no_assets: RESOURCES += $$P/Qt/desktop_assets.qrc
	INCLUDEPATH += $$P/Qt $$P/Qt/Debugger
	
	# Creating translations should be done by Qt, really
	isEmpty(LREL_TOOL): LREL_TOOL = lrelease
	# Grab all possible directories (win32/unix)
	win32: PATHS = $$split($$(PATH), ;)
	else: PATHS = $$split($$(PATH), :)
	# Either -qt4 or -qt5 will work.
	for(bin, PATHS): exists($${bin}/$${LREL_TOOL}-qt4): LREL_TOOL=$${bin}/$${LREL_TOOL}-qt4
	for(bin, PATHS): exists($${bin}/$${LREL_TOOL}-qt5): LREL_TOOL=$${bin}/$${LREL_TOOL}-qt5

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


