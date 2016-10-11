#!/bin/bash
CMAKE=1

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
			echo !!!!!!!!!!!!!!! The error below is expected. Go into build-ios and open the XCodeProj.
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
	if [ "$TARGET_OS" == "iOS" ]; then
		xcodebuild -configuration Release
	fi
fi
popd
