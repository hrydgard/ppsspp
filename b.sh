#!/bin/bash
CMAKE=1

# Check arguments
while test $# -gt 0
do
	case "$1" in
		--qt) echo "Qt enabled"
			QT=1
			CMAKE_ARGS="-DUSING_QT_UI=ON ${CMAKE_ARGS}"
			;;
		--qtbrew) echo "Qt enabled (homebrew)"
			QT=1
			CMAKE_ARGS="-DUSING_QT_UI=ON -DCMAKE_PREFIX_PATH=$(brew --prefix qt5) ${CMAKE_ARGS}"
			;;
		--ios) CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/ios.cmake ${CMAKE_ARGS}"
			TARGET_OS=iOS
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
		--libretro) echo "Build Libretro core"
			CMAKE_ARGS="-DLIBRETRO=ON ${CMAKE_ARGS}"
			;;
		--unittest) echo "Build unittest"
			CMAKE_ARGS="-DUNITTEST=ON ${CMAKE_ARGS}"
			;;
		--no-package) echo "Packaging disabled"
			PACKAGE=0
			;;
		--clang) echo "Clang enabled"
			export CC=/usr/bin/clang
			export CXX=/usr/bin/clang++
			;;
		--sanitize) echo "Enabling address-sanitizer if available"
			CMAKE_ARGS="-DUSE_ADDRESS_SANITIZER=ON ${CMAKE_ARGS}"
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
	BUILD_DIR="build"
fi

# Strict errors. Any non-zero return exits this script
set -e

mkdir -p ${BUILD_DIR}
pushd ${BUILD_DIR}

cmake $CMAKE_ARGS ..

make -j4 $MAKE_OPT
popd
