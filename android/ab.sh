cp -r ../flash0 assets
NDK_MODULE_PATH=..:../native/ext $NDK/ndk-build -j3 TARGET_PLATFORM=android-9 $*
