#!/bin/bash

BUILD_TYPE=Release

if type "arm-unknown-nto-qnx8.0.0eabi-cpp" >/dev/null 2>&1; then
    BB_OS=10.0.9
    echo "Building for Blackberry 10.0"
elif type "arm-unknown-nto-qnx6.5.0eabi-cpp" >/dev/null 2>&1; then
    BB_OS=2.1.0
    echo "Building for Blackberry 2.1"
else
    echo "Could not find your Blackberry NDK. Please source bbndk-env.sh"
    exit
fi


if [ -z "$PPSSPP_ROOT" ]; then
	PPSSPP_ROOT=${PWD}/..
fi
if [ -z "$PROJECT_ROOT" ]; then
	PROJECT_ROOT=${PPSSPP_ROOT}/..
fi
PKG_CONFIG_PATH=${PROJECT_ROOT}/install/lib/pkgconfig
PKG_CONFIG_LIBDIR=${PROJECT_ROOT}/install/lib/pkgconfig

SDL_PROJECT=${PROJECT_ROOT}/SDL
SDLIMAGE_PROJECT=${PROJECT_ROOT}/SDL_image
SDLMIXER_PROJECT=${PROJECT_ROOT}/SDL_mixer

while true; do
	case "$1" in
	-h | --help ) 
		echo "Build script for BlackBerry PlayBook"
		echo "For normal usage, please use the NDK to build."
		echo
		echo "Options: "
		echo "  -d, --debug             Create a debug build. (default is release)"
		echo "  -h, --help              Show this help message."
		echo "  -r, --root PATH         Specify the root directory of PPSSPP. (default is PWD parent)"
		echo "  -p, --project-root PATH Specify the root directory containing all projects. (default is root dirs parent)"
		echo "                          If specific projects are in different directories, you can specify them below."
		echo "  --pkg-config PATH       Specify the pkgconfig directory. (default is PPSSPP_ROOT/../install/lib/pkgconfig)"
		echo
		echo "Dependency Paths (defaults are under project root): "
		echo "  --sdl PATH                   SDL 1.2 project directory (default is SDL)"
		echo "  --tco PATH                   TouchControlOverlay project directory (default is TouchControlOverlay)"
		echo "  --sdl_image PATH             SDL_image project directory (default is SDL_image)"
		echo "  --sdl_mixer PATH             SDL_mixer project directory (default is SDL_mixer)"
		echo "  --ogg PATH                   ogg project directory (default is ogg)"
		echo "  --vorbis PATH                vorbis project directory (default is vorbis)"
		exit 0
		;;
	-d | --debug ) BUILD_TYPE=Debug; shift ;;
	-r | --root ) PPSSPP_ROOT="$2"; shift 2 ;;
	-p | --project-root ) PROJECT_ROOT="$2"; shift 2 ;;
	--pkg-config ) PKG_CONFIG_PATH="$2"; PKG_CONFIG_LIBDIR="$2"; shift 2 ;;
	--sdl ) SDL_PROJECT="$2"; shift 2 ;;
	--sdl_image ) SDLIMAGE_PROJECT="$2"; shift 2 ;;
	--sdl_mixer ) SDLMIXER_PROJECT="$2"; shift 2 ;;
	--tco ) TCO_PROJECT="$2"; shift 2 ;;
	--ogg ) OGG_PROJECT="$2"; shift 2 ;;
	--vorbis ) VORBIS_PROJECT="$2"; shift 2 ;;
	-- ) shift; break ;;
	* ) break ;;
  esac
done

if [ -z "$SDL_PROJECT" ]; then
	SDL_PROJECT="$PROJECT_ROOT/SDL"
fi
if [ -z "$SDLIMAGE_PROJECT" ]; then
	SDLIMAGE_PROJECT="$PROJECT_ROOT/SDL_image"
fi
if [ -z "$SDLMIXER_PROJECT" ]; then
	SDLMIXER_PROJECT="$PROJECT_ROOT/SDL_mixer"
fi
if [ -z "$TCO_PROJECT" ]; then
	TCO_PROJECT="$PROJECT_ROOT/TouchControlOverlay"
fi
if [ -z "$OGG_PROJECT" ]; then
	OGG_PROJECT="$PROJECT_ROOT/ogg"
fi
if [ -z "$VORBIS_PROJECT" ]; then
	VORBIS_PROJECT="$PROJECT_ROOT/vorbis"
fi

export PKG_CONFIG_PATH
export PKG_CONFIG_LIBDIR

echo "Build type: ${BUILD_TYPE}"

cmake \
-DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
-DCMAKE_SYSTEM_NAME=QNX \
-DCMAKE_C_COMPILER="${QNX_HOST}/usr/bin/ntoarmv7-gcc" \
-DCMAKE_CXX_COMPILER="${QNX_HOST}/usr/bin/ntoarmv7-g++" \
-DTHREADS_PTHREAD_ARG="" \
-DLIBINTL_INCLUDE_DIR="${QNX_TARGET}/usr/include" \
-DLIBINTL_LIB_FOUND=TRUE \
-DLIBINTL_LIBRARIES="${QNX_TARGET}/armle-v7/usr/lib/libintl.so" \
-DLIBINTL_LIBC_HAS_DGETTEXT=FALSE \
-DSDL_INCLUDE_DIR="${SDL_PROJECT}/include" \
-DSDL_LIBRARY="${SDL_PROJECT}/Device-${BUILD_TYPE}/libSDL12.so;${TCO_PROJECT}/Device-${BUILD_TYPE}/libTouchControlOverlay.so" \
-DSDL_FOUND=ON \
-DSDLIMAGE_INCLUDE_DIR="${SDLIMAGE_PROJECT}" \
-DSDLIMAGE_LIBRARY="${SDLIMAGE_PROJECT}/Device-${BUILD_TYPE}/libSDL_image.so" \
-DSDLIMAGE_FOUND=ON \
-DSDLMIXER_INCLUDE_DIR="${SDLMIXER_PROJECT}" \
-DSDLMIXER_LIBRARY="${SDLMIXER_PROJECT}/Device-${BUILD_TYPE}/libSDL_mixer.so;${OGG_PROJECT}/Device-${BUILD_TYPE}/libogg.so;${VORBIS_PROJECT}/Device-${BUILD_TYPE}/libvorbis.so" \
-DSDLMIXER_FOUND=ON \
-DPNG_LIBRARY="${QNX_TARGET}/armle-v7/usr/lib/libpng.so" \
-DPNG_PNG_INCLUDE_DIR="${QNX_TARGET}/usr/include" \
-DBLACKBERRY=${BB_OS} \
-DARM=7 \
${PWD}

make -j4
