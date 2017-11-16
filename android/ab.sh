cp -r ../assets/flash0 assets/
cp -r ../assets/lang assets/
cp -r ../assets/shaders assets/
cp ../assets/langregion.ini assets/langregion.ini
cp ../assets/compat.ini assets/compat.ini
cp ../assets/Roboto-Condensed.ttf assets/Roboto-Condensed.ttf
cp ../assets/*.png assets/
NDK_MODULE_PATH=../ext:../ext/native/ext $NDK/ndk-build -j3 $*
