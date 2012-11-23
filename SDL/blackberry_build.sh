#!/bin/bash

BB_OS=`cat ${QNX_TARGET}/etc/qversion 2>/dev/null`
if [ -z "$BB_OS" ]; then
    echo "Could not find your Blackberry NDK. Please source bbndk-env.sh"
    exit 1
fi
echo "Building for Blackberry ${BB_OS}"

PPSSPP_ROOT=${PWD}/..
if [ -z "$PROJECT_ROOT" ]; then
	PROJECT_ROOT=${PPSSPP_ROOT}/..
fi
if [ -z "$SDL_PROJECT" ]; then
	SDL_PROJECT="$PROJECT_ROOT/SDL"
fi
if [ -z "$TCO_PROJECT" ]; then
	TCO_PROJECT="$PROJECT_ROOT/TouchControlOverlay"
fi

while true; do
	case "$1" in
	-h | --help ) 
		echo "Build script for BlackBerry"
		echo
		echo "Options: "
		echo "  -h, --help              Show this help message."
		echo "  -p, --project-root PATH Specify the root directory containing all projects. (default is PPSSPP's parent)"
		echo "Dependency Paths (defaults are under project root): "
		echo "  --sdl PATH              SDL 1.2 project directory (default is SDL)"
		echo "  --tco PATH              TouchControlOverlay project directory (default is TouchControlOverlay)"
		exit 0
		;;
	-p | --project-root ) PROJECT_ROOT="$2"; shift 2 ;;
	--sdl ) SDL_PROJECT="$2"; shift 2 ;;
	--tco ) TCO_PROJECT="$2"; shift 2 ;;
	-- ) shift; break ;;
	* ) break ;;
  esac
done

cmake \
-DCMAKE_C_COMPILER="${QNX_HOST}/usr/bin/ntoarmv7-gcc" \
-DCMAKE_CXX_COMPILER="${QNX_HOST}/usr/bin/ntoarmv7-g++" \
-DSDL_INCLUDE_DIR="${SDL_PROJECT}/include" \
-DSDL_LIBRARY="${SDL_PROJECT}/Device-Release/libSDL12.so;${TCO_PROJECT}/Device-Release/libTouchControlOverlay.so" \
-DBLACKBERRY=${BB_OS} \
${PPSSPP_ROOT}

# Compile and create unsigned PPSSPP.bar with debugtoken
DEBUG="-devMode -debugToken ${HOME}/debugtoken.bar"
make -j4 && blackberry-nativepackager -package PPSSPP.bar bar-descriptor.xml $DEBUG
