#!/bin/bash
# Check Blackberry NDK
BB_OS=`cat ${QNX_TARGET}/etc/qversion 2>/dev/null`
if [ ! -z "$BB_OS" ]; then
	CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=Blackberry/bb.toolchain.cmake -DBLACKBERRY=${BB_OS} ${CMAKE_ARGS}"
	BUILD_EXT="-bb"
	DEBUG_ARGS="-devMode -debugToken ${QNX_CONFIGURATION}/../debugtoken.bar"
	BB_PACKAGE=1
fi

# Check arguments
while test $# -gt 0
do
	case "$1" in
		--headless) echo "Headless mode enabled"
			CMAKE_ARGS="-DHEADLESS=ON ${CMAKE_ARGS}"
			;;
		--ios) CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=ios/ios.toolchain.cmake -GXcode ${CMAKE_ARGS}"
			BUILD_EXT="-ios"
			;;
		--android) CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=android/android.toolchain.cmake ${CMAKE_ARGS}"
			BUILD_EXT="-android"
			;;
		--no-package) echo "Blackberry packaging disabled"
			BB_PACKAGE=0
			;;
		--release-package) echo "Blackberry release package enabled"
			if [ ! -f "Blackberry/build.txt" ]; then
				echo "1" > "Blackberry/build.txt"
			fi
			DEBUG_ARGS="-buildId ../Blackberry/build.txt"
			;;
		--simulator) echo "Simulator mode enabled"
			CMAKE_ARGS="-DSIMULATOR=ON ${CMAKE_ARGS}"
			;;
		--*) echo "Bad option: $1"
			exit 1
			;;
		*) MAKE_OPT="$1 ${MAKE_OPT}"
			;;
	esac
	shift
done

if [ "$BUILD_EXT" == "-ios" ]; then
	echo "Building for iOS"
elif [ "$BUILD_EXT" == "-android" ]; then
	echo "Building for Android"
elif [ "$BUILD_EXT" == "-bb" ]; then
	echo "Building for Blackberry ${BB_OS}"
else
	echo "Building for native host."
fi

# Strict errors. Any non-zero return exits this script
set -e

mkdir -p build${BUILD_EXT}
pushd build${BUILD_EXT}
cmake $HEADLESS $CMAKE_ARGS .. | (grep -v "^-- " || true)
make -j4 $MAKE_OPT
if [ "$BB_PACKAGE" == "1" ]; then
	cp ../Blackberry/bar-descriptor.xml .
	blackberry-nativepackager -package PPSSPP.bar bar-descriptor.xml $DEBUG_ARGS
fi
popd
