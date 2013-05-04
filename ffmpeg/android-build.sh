#!/bin/bash
#Change NDK to your Android NDK location
NDK=/home/cv/ndk
PLATFORM=$NDK/platforms/android-8/arch-arm/
PREBUILT=$NDK/toolchains/arm-linux-androideabi-4.4.3/prebuilt/linux-x86

function build_ARMv6
{
./configure --target-os=linux \
    --prefix=./android/armv6 \
    --enable-cross-compile \
    --extra-libs="-lgcc" \
    --arch=arm \
    --cc=$PREBUILT/bin/arm-linux-androideabi-gcc \
    --cross-prefix=$PREBUILT/bin/arm-linux-androideabi- \
    --nm=$PREBUILT/bin/arm-linux-androideabi-nm \
    --sysroot=$PLATFORM \
    --extra-cflags=" -O3 -fpic -DANDROID -DHAVE_SYS_UIO_H=1 -Dipv6mr_interface=ipv6mr_ifindex -fasm -Wno-psabi -fno-short-enums -fno-strict-aliasing -finline-limit=300 -DCMP_HAVE_VFP -mfloat-abi=softfp -mfpu=vfp -marm -march=armv6" \
    --disable-shared \
    --enable-static \
    --extra-ldflags="-Wl,-rpath-link=$PLATFORM/usr/lib -L$PLATFORM/usr/lib -nostdlib -lc -lm -ldl -llog" \
    --enable-zlib \
    --disable-ffplay \
    --disable-ffprobe \
    --disable-ffserver \
    --disable-neon \

make clean
make  -j4 install
}

function build_ARMv7
{
./configure --target-os=linux \
    --prefix=./android/armv7 \
    --enable-cross-compile \
    --extra-libs="-lgcc" \
    --arch=arm \
    --cc=$PREBUILT/bin/arm-linux-androideabi-gcc \
    --cross-prefix=$PREBUILT/bin/arm-linux-androideabi- \
    --nm=$PREBUILT/bin/arm-linux-androideabi-nm \
    --sysroot=$PLATFORM \
    --extra-cflags=" -O3 -fpic -DANDROID -DHAVE_SYS_UIO_H=1 -Dipv6mr_interface=ipv6mr_ifindex -fasm -Wno-psabi -fno-short-enums -fno-strict-aliasing -finline-limit=300 -mfloat-abi=softfp -mfpu=vfp -marm -march=armv7-a" \
    --disable-shared \
    --enable-static \
    --extra-ldflags="-Wl,-rpath-link=$PLATFORM/usr/lib -L$PLATFORM/usr/lib -nostdlib -lc -lm -ldl -llog" \
    --enable-zlib \
    --disable-ffplay \
    --disable-ffprobe \
    --disable-ffserver \
    --disable-neon \

make clean
make  -j4 install
}
build_ARMv6
build_ARMv7