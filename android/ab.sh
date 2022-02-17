mkdir -p assets
cp -r ../assets/flash0 assets/
cp -r ../assets/lang assets/
cp -r ../assets/shaders assets/
cp -r ../assets/themes assets/
cp -r ../assets/debugger assets/
cp ../assets/*.ini assets/
cp ../assets/Roboto-Condensed.ttf assets/Roboto-Condensed.ttf
cp ../assets/*.png assets/
cp ../assets/*.zim assets/
cp ../assets/*.meta assets/
cp ../assets/*.wav assets/
NDK_MODULE_PATH=../ext $NDK/ndk-build -j3 $*
