#!/bin/bash

TYPE=Device-Release
DEBUG="-devMode -debugToken ${HOME}/debugtoken.bar"
PPSSPP_ROOT=${PWD}/..
WORKSPACE=${PPSSPP_ROOT}/..
blackberry-nativepackager -package PPSSPP.bar bar-descriptor.xml PPSSPPBlackberry \
-e icon-114.png icon-114.png $DEBUG \
-e ../android/assets assets \
-e ${WORKSPACE}/SDL12/${TYPE}/libSDL12.so lib/libSDL12.so \
-e ${WORKSPACE}/TouchControlOverlay/${TYPE}/libTouchControlOverlay.so lib/libTouchControlOverlay.so 
