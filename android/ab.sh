cp -r ../flash0 assets
cp -r ../lang assets
NDK_MODULE_PATH=..:../native/ext $NDK/ndk-build -j3 TARGET_PLATFORM=android-9 $*
