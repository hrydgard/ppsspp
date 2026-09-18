NDK_MODULE_PATH=../ext $NDK/ndk-build -j$(nproc 2>/dev/null || echo 4) $*
