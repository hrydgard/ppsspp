# librashader Build Instructions

PPSSPP can optionally use [librashader](https://github.com/SnowflakePowered/librashader) as a drop-in replacement for its built-in slang shader stack. librashader is loaded dynamically at runtime and never linked; PPSSPP does not bundle it.

## Library Version

**Pinned tag:** `librashader-v0.12.0`  
**ABI version:** 2  
**API version:** 5

## Building from Source

librashader requires Rust stable ≥ 1.88.

### macOS (verified on arm64)

```bash
git clone --depth 1 --branch librashader-v0.12.0 https://github.com/SnowflakePowered/librashader.git /tmp/librashader
cd /tmp/librashader
cargo build -p librashader-capi --release --no-default-features --features runtime-vulkan,runtime-opengl
```

Build time: ~1.5 minutes on Apple Silicon. Artifacts: `target/release/liblibrashader_capi.dylib` (12 MB) and `liblibrashader_capi.a` (60 MB).

**Post-build:** The cargo output name is `liblibrashader_capi.dylib`, and its install name is an absolute path into `target/release/deps/`. Because the PPSSPP loader resolves the library by the bare name `librashader.dylib`, you must rename the file, fix its install name, and ad-hoc sign it:

```bash
mkdir -p dist
cp target/release/liblibrashader_capi.dylib dist/librashader.dylib
install_name_tool -id librashader.dylib dist/librashader.dylib
codesign -f -s - dist/librashader.dylib
```

Verify:

```bash
otool -D dist/librashader.dylib               # must print: librashader.dylib
nm -gU dist/librashader.dylib | grep -c _libra_   # 44 exported entry points
```

### Linux (expected, not verified)

```bash
git clone --depth 1 --branch librashader-v0.12.0 https://github.com/SnowflakePowered/librashader.git /tmp/librashader
cd /tmp/librashader
RUSTFLAGS="-C link-arg=-Wl,-soname,librashader.so" cargo build -p librashader-capi --release --no-default-features --features runtime-vulkan,runtime-opengl
```

Artifact: `target/release/liblibrashader_capi.so`. Rename to `librashader.so`:

```bash
mkdir -p dist
cp target/release/liblibrashader_capi.so dist/librashader.so
```

If you did not set `RUSTFLAGS` at build time, fix the soname after the fact:

```bash
patchelf --set-soname librashader.so dist/librashader.so
```

### Windows MSVC (expected, not verified)

```bash
git clone --depth 1 --branch librashader-v0.12.0 https://github.com/SnowflakePowered/librashader.git C:\tmp\librashader
cd C:\tmp\librashader
cargo build -p librashader-capi --release --no-default-features --features runtime-vulkan,runtime-opengl
```

Artifact: `target\release\librashader_capi.dll`. Rename to `librashader.dll`:

```bash
mkdir dist
copy target\release\librashader_capi.dll dist\librashader.dll
```

## Using the Library

PPSSPP searches for librashader in the following order:

1. **`$LIBRASHADER_PATH`** — full path to the library file (e.g., `/opt/librashader/librashader.dylib`). If this environment variable is set but the library cannot be loaded, PPSSPP will not fall back to other search locations.
2. **Executable directory** — looks for `librashader.dylib` (macOS), `librashader.so` (Linux), or `librashader.dll` (Windows) next to the PPSSPP binary.
3. **Platform default search path** — relies on the system loader (`dlopen` with a bare name on Unix-like systems; `LoadLibraryW` on Windows).

### CMake Copy Step

During build, you can use the `LIBRASHADER_PREBUILT` CMake cache variable to automatically copy a prebuilt library next to the executable:

```bash
cmake -S . -B build -DLIBRASHADER_PREBUILT=/path/to/librashader.dylib
cmake --build build --target PPSSPPSDL -j12
```

When set, the library will be copied to:
- **macOS bundle:** `PPSSPPSDL.app/Contents/MacOS/librashader.dylib`
- **Other platforms:** Same directory as the executable

This step only runs if `USE_LIBRASHADER=ON` (the default on desktop platforms) and the `LIBRASHADER_PREBUILT` variable is non-empty.

## Enabling at Runtime

librashader support is gated by a runtime toggle:

**Developer Tools → "Use librashader for slang shaders"** (or `SlangUseLibrashader = True` in `ppsspp.ini`)

When enabled, all slang shader presets are rendered through librashader. When disabled (or if librashader is unavailable), PPSSPP falls back to its built-in slang stack.

### Presets that use `OriginalHistoryN`

librashader snapshots the `OriginalHistoryN` ring at the size PPSSPP declares in `libra_image_vk_t`, not at the input image's real extents. PPSSPP normally declares the *native* PSP resolution (so `SourceSize` and `scale_type = source` behave like the built-in stack) while handing over its upscaled render target, which would make history frames a native-sized corner of the frame.

To avoid that, `LibrashaderFilterChain::Load` re-parses the preset with PPSSPP's own parser and scans each pass's `#include`-resolved source for `OriginalHistory1`..`9` / `OriginalHistorySize1`..`9`. Presets that reference them get an extra downscaling blit into a native-sized intermediate, so the declared size is the real size and history frames cover the whole picture (at native resolution). Presets that do not are unaffected and keep sampling the full upscaled framebuffer. The mode is logged once per preset load:

```
INFO  LibrashaderFilterChain: preset parsed: <path> (input mode: upscaled framebuffer with declared native size)
INFO  LibrashaderFilterChain: preset parsed: <path> (input mode: native-sized copy, preset samples OriginalHistoryN)
```

## Backend Notes: OpenGL / GLES

**Requirements.** The GL runtime needs desktop GL **3.3+** or **GLES 3.0+**. That is exactly what
`OpenGLContext::SupportsNativeCallback()` gates on (`gl_extensions.IsGLES ? gl_extensions.GLES3 :
gl_extensions.VersionGEThan(3, 3)`), and the chain selector refuses librashader when it returns
false, so GLES 2 devices never take this path. Sampler objects and VAOs - which the post-callback
state restore relies on - are core at those versions.

Build the library with the GL runtime compiled in (`--features runtime-vulkan,runtime-opengl`, as in
the commands above); a Vulkan-only build reports `librashader loaded` but every GL chain creation
fails.

**librashader GL options used** (`filter_chain_gl_opt_t` in `LibrashaderRuntimeOpenGL.cpp`):

| Option | Value | Why |
|---|---|---|
| `glsl_version` | `0` | Auto-detect from the current context. Verified on macOS's 4.1 core profile (`GLSL version str: 4.10`) - no explicit `330`/`410` override is needed. |
| `use_dsa` | `false` | Direct State Access needs GL 4.5; macOS caps at 4.1 and GLES has no DSA. |
| `force_no_mipmaps` | `false` | Presets that ask for mipmapped passes keep them. |
| `disable_cache` | `false` | librashader turns its own on-disk cache off when DSA is unavailable. |

Framebuffer color textures created by PPSSPP's GL backend use unsized `GL_RGBA`/`GL_UNSIGNED_BYTE`,
so both `libra_image_gl_t`s report `GL_RGBA8` (`0x8058`) as the sized internal format.

**macOS.** SDL hands PPSSPP a **GL 4.1 core** context on top of Metal (`GPU Vendor : Apple ;
renderer: Apple M2 Pro version str: 4.1 Metal - 90.5 ; GLSL version str: 4.10`). Everything works
with `glsl_version = 0`; no DSA, no compute, no `KHR_debug`.

**"Toggle off" on GL renders the raw image.** The in-tree slang chain is Vulkan-only (it is being
removed in Phase 4), so on GL, turning librashader off does not fall back to an equivalent chain -
the preset is refused and PPSSPP presents the unprocessed framebuffer:

```
INFO   Slang chain backend: in-tree
ERROR  Failed to load slang preset '<path>': slang passes require the Vulkan backend in Phase 1 (got a non-Vulkan backend)
```

The same happens if `librashader.dylib`/`.so` is missing (`librashader unavailable: ...`). Neither
case crashes; slang shaders are simply inactive until librashader is available again. Therefore the
A/B reference for a GL capture is the **Vulkan librashader** capture of the same preset, not a GL
"toggle off" capture.

**GL state contract.** Every `libra_gl_*` call runs inside a `GLRStepType::CALLBACK` step, i.e. on
the GL thread with the creating context current; the chain is created and used in the same callback
(unlike Vulkan there is no command-buffer readiness gate), and it is freed through another CALLBACK
step because `libra_gl_filter_chain_free` needs that context. librashader changes GL state freely,
so `GLQueueRunner::RestoreBaselineStateAfterCallback()` puts back everything
`PerformRenderPass`/`PerformBindFramebufferAsRenderTarget` assume: `fbo_unbind()` (which binds the
default FBO and updates both binding caches), the global VAO is rebound, `glUseProgram(0)`,
`GL_ARRAY_BUFFER` 0, `glBindSampler(i, 0)` for every texture slot, `glActiveTexture(GL_TEXTURE0)`,
the `GL_UNPACK_*` pixel-store parameters and `GL_PIXEL_UNPACK_BUFFER` (desktop GL and GLES 3.0+,
selected by a runtime check), full colour/depth/stencil masks, depth/stencil/blend/cull/dither
disabled, scissor test enabled, and - desktop only - logic op, depth clamp, `GL_FRAMEBUFFER_SRGB`
and all eight `GL_CLIP_DISTANCE*` disabled.

**Chain free at teardown.** The free CALLBACK step only runs while the render thread still drains
work. At `DeviceLost` / shutdown it does not - `GLRenderManager::ThreadEnd` deletes queued CALLBACK
functions without running them - so `LibrashaderRuntime::QueueFree` takes a `deviceLost` flag and,
when it is set, drops the state with a single warning
(`LibrashaderRuntimeOpenGL: dropping chain without freeing`) instead of enqueuing a free that would
silently never run. The GL objects die with the context; librashader's Rust-side allocation is
knowingly leaked. `libra_gl_filter_chain_free` itself is therefore only reachable from an in-process
preset change, which has not been exercised on device yet.

**Verified** on 2026-09-14 (macOS 15, Apple M2 Pro, GL 4.1 core over Metal): `stock`, `lut`,
`feedback`, `lcd-psp-matrix`, `twopass` and `srgb` are **bit-identical** to the Vulkan librashader
output of the same frame, and PPSSPP's own overlays (FPS counter, debug statistics, the ImGui
debugger with its framebuffer preview) render correctly over the filtered image. GLES 3 is Phase 3.
At shutdown the GL chain is dropped, not freed (see above), so the drop warning in the log is
expected, not a failure.

## Confirming the Load

Check the PPSSPP log for one of these lines at startup:

```
INFO  librashader loaded (ABI 2, API 5)
```

or, if loading failed (logged at INFO, not WARN - an absent library is a supported configuration):

```
INFO  librashader unavailable: <reason>
```

If the library was found and preloaded from a path but librashader's own bare-name lookup still failed, the reason names the likely cause:

```
INFO  librashader unavailable: librashader preloaded from /path/to/librashader.dylib but bare-name load failed - check the library's install name/soname is librashader.dylib (see docs/superpowers/librashader-build.md)
```

When a slang preset is loaded, the chosen backend is logged:

```
INFO  Slang chain backend: librashader
```

or

```
INFO  Slang chain backend: in-tree
```

## Licensing

- **librashader library:** MPL-2.0 OR GPL-3.0
- **librashader headers:** MIT
- **PPSSPP integration:** PPSSPP loads librashader dynamically and does not link it. No GPL requirements flow to PPSSPP.

## Android

### Prerequisites

- Rust stable ≥ 1.88 with Android targets:
  ```bash
  rustup target add aarch64-linux-android armv7-linux-androideabi x86_64-linux-android
  ```
- `cargo-ndk` 4.x:
  ```bash
  cargo install cargo-ndk
  ```
- Android NDK 29 (matches `ndkVersion` in `android/build.gradle.kts`). Set `ANDROID_NDK_HOME` or
  install at one of the default locations (`$ANDROID_HOME/ndk/<ver>`,
  `$HOME/Library/Android/sdk/ndk/<ver>`, or `/opt/homebrew/share/android-commandlinetools/ndk/<ver>`).

### Building

```bash
./android/build-librashader.sh [ABI ...]
```

Defaults to `arm64-v8a armeabi-v7a x86_64` (all three ABIs). Optionally override with:

- `LIBRASHADER_TAG` — git tag (default: `librashader-v0.12.0`)
- `LIBRASHADER_SRC` — source directory (default: `<repo>/build/librashader-src`, gitignored)
- `ANDROID_NDK_HOME` — path to the NDK (auto-detected if unset)

The script clones librashader (if needed), cross-compiles for each ABI with `cargo ndk`, and copies
the output to `android/src/main/jniLibs/<abi>/librashader.so`. Build time: ~45 seconds per ABI after
the first (incremental). Outputs are **14 MB** (arm64-v8a), **11 MB** (armeabi-v7a), **13 MB**
(x86_64).

Example:

```bash
ANDROID_NDK_HOME=/opt/homebrew/share/android-commandlinetools/ndk/29.0.14206865 \
  ./android/build-librashader.sh arm64-v8a
```

### APK Packaging

Gradle automatically packages everything under `android/src/main/jniLibs/<abi>/` into the APK.
The loader finds `librashader.so` with a bare-name `dlopen` (`Librashader::Load` in
`Common/GPU/Librashader/LibrashaderLoader.cpp` → `librashader_load_instance()`) because the APK's
`lib/<abi>` directory is in the app's linker namespace. `useLegacyPackaging = true` here means the
library is both compressed in the APK and extracted at install; it would work with
`extractNativeLibs=false` too. No path prefix is needed.

The `jniLibs/` directory is gitignored; run the script after each checkout or librashader version
bump, then rebuild the APK with Gradle.

**Note:** Building with an `ANDROID_NDK_HOME` different from `ndkVersion` in `build.gradle.kts` can
produce a `libc++_shared.so` symbol-version mismatch at load.

The script produces all three ABIs and Gradle packages every `jniLibs/<abi>` regardless of
`-Pandroid.injected.build.abi` (that flag filters only the CMake output), so the dev APK carries all
three `librashader.so` (~29 MB uncompressed); release flavors prune by `ndk.abiFilters` (`normal`/`gold`
keep all three, `legacy` two, `vr` one). To build a single ABI, pass it to the script
(`android/build-librashader.sh arm64-v8a`) and delete the other `jniLibs/<abi>/librashader.so`.
Gradle-side pruning is a Phase 4 item.

### Confirming on Device

Enable slang shader logging:

```
Device ini: set G3DLevel = 4 and SYSTEMLevel = 4 in BOTH the [Log] and [LogDebug] sections
(or Developer Tools → Logging channels)
```

Then load a slang preset and check `adb logcat`:

```bash
adb logcat | grep -E "librashader loaded|Slang chain backend"
```

Expected:

```
INFO  librashader loaded (ABI 2, API 5)
INFO  Slang chain backend: librashader
```

If `librashader.so` is missing from the APK, the chain falls back to the in-tree implementation (no
push constants):

```
INFO  librashader unavailable: <reason>
INFO  Slang chain backend: in-tree
```

### Vulkan Validation Layers on Android (Debug APK)

To enable Vulkan validation layers during development:

1. Download the [Khronos Vulkan ValidationLayers release](https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases) (e.g., `android-binaries-1.4.357.0.zip`).
2. Extract `libVkLayer_khronos_validation.so` from the archive's `<abi>/` directories.
3. Drop the `.so` files into `android/src/main/jniLibs/<abi>/` (same location as `librashader.so`).
4. Rebuild the APK.

The PPSSPP debug build enables validation at compile time via `g_Validate` in
`GPU/Vulkan/VulkanUtil.cpp` (gated by `_DEBUG`). PPSSPP prefixes layer messages with `VKDEBUG:` (grep
for that, not `VUID`/`VALIDATION`), and the debug callback reports core validation only (synchronization
validation requires `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`, which
`Common/GPU/Vulkan/VulkanContext.cpp` never sets). Release builds ignore the layers even if present.

### CI Deferred

The fork's Android CI jobs use `android/ab.sh` (ndk-build via `Android.mk`), which does not list the
`GPU/Common/Slang` sources (so a `cargo ndk` step would be pointless until that build is fixed;
`FramebufferManagerCommon.cpp` references the Slang sources unconditionally, so the ndk-build path is
not expected to build at all). The Gradle/CMake path is the one that works; Android librashader
integration is tested manually on device. `.github/workflows/manual_generate_apk.yml` is the
Gradle-based job that can host a `cargo ndk` step.
