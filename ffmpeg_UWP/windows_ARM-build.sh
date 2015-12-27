#!/bin/bash

mkdir -p Output/Windows10/ARM

cd Output/Windows10/ARM

../../../configure \
--toolchain=msvc \
--disable-programs \
--disable-d3d11va \
--disable-dxva2 \
--arch=arm \
--as=armasm \
--cpu=armv7 \
--enable-thumb \
--enable-static \
--disable-shared \
--enable-cross-compile \
--target-os=win32 \
--extra-cflags="-MD -DWINAPI_FAMILY=WINAPI_FAMILY_APP -D_WIN32_WINNT=0x0A00 -D__ARM_PCS_VFP" \
--extra-ldflags="-APPCONTAINER WindowsApp.lib" \
--prefix=../../../Build/Windows10/ARM

make

make install