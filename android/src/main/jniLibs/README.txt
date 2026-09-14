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
produced by android/build-librashader.sh. Without it, the slang chain falls back to
the in-tree SPIR-V implementation (no push constants, slower).
