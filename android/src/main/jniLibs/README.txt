Put the arm64-v8a and the other folders here, downloaded from https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases

src/main/jniLibs/
  arm64-v8a/
    libVkLayer_khronos_validation.so
  armeabi-v7a/
    libVkLayer_khronos_validation.so
  x86/
    libVkLayer_khronos_validation.so
  x86-64/
    libVkLayer_khronos_validation.so
librashader (for slang shader preset support) also goes here as <abi>/librashader.so,
produced by android/build-librashader.sh. Without it, slang shaders are off.

Gradle packages every <abi>/librashader.so present in jniLibs regardless of
-Pandroid.injected.build.abi (that flag filters only the CMake output). For dev builds
with a single ABI, use -PlibrashaderAbi=<abi> to exclude the other ABIs from the APK:

  ./gradlew assembleNormalDebug -Pandroid.injected.build.abi=arm64-v8a \
      -PlibrashaderAbi=arm64-v8a -PANDROID_VERSION_CODE=999999999 \
      -PANDROID_VERSION_NAME=dev --console=plain

Release flavors are already pruned by ndk.abiFilters in build.gradle.kts.
