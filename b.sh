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
		--ios-xcode) CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/ios.cmake -DIOS_PLATFORM=OS -GXcode ${CMAKE_ARGS}"
			TARGET_OS=iOS-xcode
			;;
		--fat) CMAKE_ARGS="-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64 ${CMAKE_ARGS}"
			;;
		--no-png) CMAKE_ARGS="-DUSE_SYSTEM_LIBPNG=OFF ${CMAKE_ARGS}"
			;;
		--no-sdl2) CMAKE_ARGS="-DUSE_SYSTEM_LIBSDL2=OFF ${CMAKE_ARGS}"
			;;
		--rpi-armv6)
			CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/raspberry.armv6.cmake ${CMAKE_ARGS}"
			;;
		--rpi)
			CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/raspberry.armv7.cmake ${CMAKE_ARGS}"
			;;
		--rpi64)
			CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=cmake/Toolchains/raspberry.armv8.cmake ${CMAKE_ARGS}"
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
			;;
		--debug)
			CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Debug ${CMAKE_ARGS}"
			;;
		--reldebug)
			CMAKE_ARGS="-DCMAKE_BUILD_TYPE=RelWithDebInfo ${CMAKE_ARGS}"
			;;
		--headless) echo "Headless mode enabled"
			CMAKE_ARGS="-DHEADLESS=ON ${CMAKE_ARGS}"
			;;
		--libretro) echo "Build Libretro core"
			CMAKE_ARGS="-DLIBRETRO=ON ${CMAKE_ARGS}"
			;;
		--libretro_android) echo "Build Libretro Android core"
		        CMAKE_ARGS="-DLIBRETRO=ON -DCMAKE_TOOLCHAIN_FILE=${NDK}/build/cmake/android.toolchain.cmake -DANDROID_ABI=${APP_ABI} ${CMAKE_ARGS}"
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
			CMAKE_ARGS="-DUSE_ASAN=ON ${CMAKE_ARGS}"
			;;
		--sanitizeub) echo "Enabling ub-sanitizer if available"
			CMAKE_ARGS="-DUSE_UBSAN=ON ${CMAKE_ARGS}"
			;;
		--gold) echo "Gold build enabled"
			CMAKE_ARGS="-DGOLD=ON ${CMAKE_ARGS}"
			;;
		--alderlake) echo "Alderlake opt"
			CMAKE_ARGS="-DCMAKE_C_FLAGS=\"-march=alderlake\" -DCMAKE_CPP_FLAGS=\"-march=alderlake\""
			;;
		--no_mmap) echo "Disable mmap"
			CMAKE_ARGS="-DUSE_NO_MMAP=ON ${CMAKE_ARGS}"
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

CORES_COUNT=4
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        CORES_COUNT="$(nproc)"
elif [[ "$OSTYPE" == "darwin"* ]]; then
        CORES_COUNT="$(sysctl -n hw.physicalcpu)"
fi

# Strict errors. Any non-zero return exits this script
set -e

mkdir -p ${BUILD_DIR}
pushd ${BUILD_DIR}

cmake $CMAKE_ARGS ..
make -j$CORES_COUNT $MAKE_OPT
popd
