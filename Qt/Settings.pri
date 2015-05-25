VERSION = 1.0.1.0
DEFINES += USING_QT_UI USE_FFMPEG

# Global specific
win32:CONFIG(release, debug|release): CONFIG_DIR = $$join(OUT_PWD,,,/release)
else:win32:CONFIG(debug, debug|release): CONFIG_DIR = $$join(OUT_PWD,,,/debug)
else:CONFIG_DIR=$$OUT_PWD
OBJECTS_DIR = $$CONFIG_DIR/.obj/$$TARGET
MOC_DIR = $$CONFIG_DIR/.moc/$$TARGET
UI_DIR = $$CONFIG_DIR/.ui/$$TARGET
RCC_DIR = $$CONFIG_DIR/.rcc/$$TARGET
QMAKE_CLEAN += -r $$OBJECTS_DIR $$MOC_DIR $$UI_DIR $$RCC_DIR

P = $$_PRO_FILE_PWD_/..
INCLUDEPATH += $$P/Common
win32|contains(QT_CONFIG, no-zlib): INCLUDEPATH += $$P/ext/zlib

# Work out arch name
include(Platform/ArchDetection.pri)
# Work out platform name
include(Platform/OSDetection.pri)
# OS dependent paths
!system_ffmpeg: INCLUDEPATH += $$P/ffmpeg/$${PLATFORM_NAME}/$${PLATFORM_ARCH}/include

!contains(CONFIG, staticlib) {
	QMAKE_LIBDIR += $$CONFIG_DIR
	!system_ffmpeg: QMAKE_LIBDIR += $$P/ffmpeg/$${PLATFORM_NAME}/$${PLATFORM_ARCH}/lib/
	contains(DEFINES, USE_FFMPEG): LIBS+=  -lavformat -lavcodec -lavutil -lswresample -lswscale
	equals(PLATFORM_NAME, "linux"):arm|android: LIBS += -lEGL
}

# Work out the git version in a way that works on every QMake
symbian {
	exists($$P/.git): GIT_VERSION = $$system(git describe --always)
	isEmpty(GIT_VERSION): GIT_VERSION = $$VERSION
} else {
	# QMake seems to change how it handles quotes with every version. This works for most systems:
	exists($$P/.git): GIT_VERSION = '\\"$$system(git describe --always)\\"'
	isEmpty(GIT_VERSION): GIT_VERSION = '\\"$$VERSION\\"'
}
DEFINES += PPSSPP_GIT_VERSION=\"$$GIT_VERSION\"

# Optimisations
win32-msvc* {
	DEFINES += _MBCS GLEW_STATIC _CRT_SECURE_NO_WARNINGS "_VARIADIC_MAX=10"
	contains(DEFINES, UNICODE): DEFINES += _UNICODE
	QMAKE_ALLFLAGS_RELEASE += /O2 /fp:fast
} else {
	DEFINES += __STDC_CONSTANT_MACROS
	QMAKE_CXXFLAGS += -Wno-unused-function -Wno-unused-variable -Wno-strict-aliasing -fno-strict-aliasing -Wno-unused-parameter -Wno-multichar -Wno-uninitialized -Wno-ignored-qualifiers -Wno-missing-field-initializers
	greaterThan(QT_MAJOR_VERSION,4): CONFIG+=c++11
	else: QMAKE_CXXFLAGS += -std=c++11
	QMAKE_CFLAGS_RELEASE ~= s/-O.*/
	QMAKE_CXXFLAGS_RELEASE ~= s/-O.*/
	QMAKE_ALLFLAGS_RELEASE += -O3 -ffast-math
}

contains(QT_CONFIG, opengles.) {
	DEFINES += USING_GLES2
	# How else do we know if the environment prefers windows?
	!equals(PLATFORM_NAME, "linux")|android|maemo {
		CONFIG += mobile_platform
	}
}
mobile_platform: DEFINES += MOBILE_DEVICE

# Handle flags for both C and C++
QMAKE_CFLAGS += $$QMAKE_ALLFLAGS
QMAKE_CXXFLAGS += $$QMAKE_ALLFLAGS
QMAKE_CFLAGS_DEBUG += $$QMAKE_ALLFLAGS_DEBUG
QMAKE_CXXFLAGS_DEBUG += $$QMAKE_ALLFLAGS_DEBUG
QMAKE_CFLAGS_RELEASE += $$QMAKE_ALLFLAGS_RELEASE
QMAKE_CXXFLAGS_RELEASE += $$QMAKE_ALLFLAGS_RELEASE
