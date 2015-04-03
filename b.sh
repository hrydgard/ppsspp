#!/bin/bash
CMAKE=1
# Check Blackberry NDK
BB_OS=`cat ${QNX_TARGET}/etc/qversion 2>/dev/null`
if [ ! -z "$BB_OS" ]; then
	CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=Blackberry/bb.toolchain.cmake -DBLACKBERRY=${BB_OS} ${CMAKE_ARGS}"
	DEBUG_ARGS="-devMode -debugToken ${QNX_CONFIGURATION}/../debugtoken.bar"
	PACKAGE=1
	TARGET_OS=Blackberry
fi

# Check Symbian NDK
if [ ! -z "$EPOCROOT" ]; then
	QMAKE_ARGS="-spec symbian-sbsv2 ${QMAKE_ARGS}"
	CMAKE=0
	PACKAGE=1
	MAKE_OPT="release-gcce ${MAKE_OPT}"
	TARGET_OS=Symbian
fi

# Check arguments
while test $# -gt 0
do
	case "$1" in
		--qt) echo "Qt enabled"
			CMAKE=0
			;;
		--ios) CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=ios/ios.toolchain.cmake -GXcode ${CMAKE_ARGS}"
			TARGET_OS=iOS
			PACKAGE=1
			;;
		--android) CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=android/android.toolchain.cmake ${CMAKE_ARGS}"
			TARGET_OS=Android
			PACKAGE=1
			;;
		--simulator) echo "Simulator mode enabled"
			CMAKE_ARGS="-DSIMULATOR=ON ${CMAKE_ARGS}"
			;;
		--release)
			CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release ${CMAKE_ARGS}"
			QMAKE_ARGS="CONFIG+=release ${QMAKE_ARGS}"
			;;
		--debug)
			CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Debug ${CMAKE_ARGS}"
			QMAKE_ARGS="CONFIG+=debug ${QMAKE_ARGS}"
			;;
		--system-ffmpeg)
			QMAKE_ARGS="CONFIG+=system_ffmpeg ${QMAKE_ARGS}"
			;;
		--headless) echo "Headless mode enabled"
			CMAKE_ARGS="-DHEADLESS=ON ${CMAKE_ARGS}"
			;;
		--unittest) echo "Build unittest"
			CMAKE_ARGS="-DUNITTEST=ON ${CMAKE_ARGS}"
			;;
		--no-package) echo "Packaging disabled"
			PACKAGE=0
			;;
		--release-package) echo "Blackberry release package enabled"
			if [ ! -f "Blackberry/build.txt" ]; then
				echo "1" > "Blackberry/build.txt"
			fi
			DEBUG_ARGS="-buildId ../Blackberry/build.txt"
			;;
		--*) echo "Bad option: $1"
			exit 1
			;;
		*) MAKE_OPT="$1 ${MAKE_OPT}"
			;;
	esac
	shift
done

if [ ! -z "$TARGET_OS" ]; then
	echo "Building for $TARGET_OS"
	BUILD_DIR="$(tr [A-Z] [a-z] <<< build-"$TARGET_OS")"
	# HACK (doesn't like shadowed dir)
	if [ "$TARGET_OS" == "Symbian" ]; then
		BUILD_DIR="Qt"
	fi
else
	echo "Building for native host."
	if [ "$CMAKE" == "0" ]; then
		BUILD_DIR="build-qt"
	else
		BUILD_DIR="build"
	fi
fi

# Strict errors. Any non-zero return exits this script
set -e

mkdir -p ${BUILD_DIR}
pushd ${BUILD_DIR}

if [ "$CMAKE" == "1" ]; then
	cmake $HEADLESS $CMAKE_ARGS .. | (grep -v "^-- " || true)
else
	qmake $QMAKE_ARGS ../Qt/PPSSPPQt.pro
fi

make -j4 $MAKE_OPT

if [ "$PACKAGE" == "1" ]; then
	if [ "$TARGET_OS" == "Blackberry" ]; then
		cp ../Blackberry/bar-descriptor.xml .
		blackberry-nativepackager -package PPSSPP.bar bar-descriptor.xml $DEBUG_ARGS
	elif [ "$TARGET_OS" == "Symbian" ]; then
		make sis
	elif [ "$TARGET_OS" == "iOS" ]; then
		xcodebuild -configuration Release
	fi
fi
popd
