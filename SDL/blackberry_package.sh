#!/bin/bash

TYPE=Device-Release
PPSSPP_ROOT=${PWD}/..
WORKSPACE=${PPSSPP_ROOT}/..
blackberry-nativepackager -package PPSSPP.bar bar-descriptor.xml ppsspp \
-e icon-114.png icon-114.png \
-devMode -debugToken ~/debugtoken.bar \
-e ../android/assets assets \
-e ${WORKSPACE}/SDL12/${TYPE}/libSDL12.so lib/libSDL12.so \
-e ${WORKSPACE}/TouchControlOverlay/${TYPE}/libTouchControlOverlay.so lib/libTouchControlOverlay.so \
-e ${WORKSPACE}/SDL_image/${TYPE}/libSDL_image.so lib/libSDL_image.so \
-e ${WORKSPACE}/SDL_mixer/${TYPE}/libSDL_mixer.so lib/libSDL_mixer.so
