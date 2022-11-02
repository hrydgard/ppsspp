To test-build CI for MacOS on ARM, use the following bag of tricks:

TARGET_ARCH=arm LIBRETRO_APPLE_PLATFORM=arm64-apple-macos10.15 CROSS_COMPILE=1 LIBRETRO_APPLE_ISYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk  make -C . -f Makefile

