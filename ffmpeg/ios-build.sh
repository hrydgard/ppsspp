#!/bin/bash
#
export DEVELOPER_ROOT_PATH=/Applications/Xcode.app/Contents/Developer
export CC=${DEVELOPER_ROOT_PATH}/Platforms/iPhoneOS.platform/Developer/usr/bin/arm-apple-darwin10-llvm-gcc-4.2
export SYS_ROOT=${DEVELOPER_ROOT_PATH}/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS6.1.sdk
function build_ffmpeg
{
./configure --target-os=darwin \
	--prefix=./ios/armv7/ \
	--enable-cross-compile \
	--arch=arm \
	--cpu=cortex-a8 \
	--cc=${CC} \
	--as="gas-preprocessor.pl ${CC}" \
  	--disable-armv5te \
  	--disable-armv6  \
  	--disable-armv6t2 \
	--disable-ffplay \
    --disable-ffprobe \
    --disable-ffserver \
	--enable-neon \
	--extra-cflags="-march=armv7-a -mfpu=neon -mfloat-abi=softfp -DUSE_HFC_LOG  -isysroot ${SYS_ROOT}" \
	--extra-ldflags="-isysroot ${SYS_ROOT}" \
	--disable-yasm \
    --disable-asm

sed  -i '.ori' 's/CONFIGURATION.*$/CONFIGURATION \" \"/g' config.h
make clean
make  -j4 install
}
build_ffmpeg