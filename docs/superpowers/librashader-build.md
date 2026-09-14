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

### Windows MSVC (verified on x64)

Build from a VS x64 developer environment - cargo shells out to MSVC's linker:

```bat
git clone --depth 1 --branch librashader-v0.12.0 https://github.com/SnowflakePowered/librashader.git C:\Users\Ilya\source\librashader
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d C:\Users\Ilya\source\librashader
cargo build -p librashader-capi --release --no-default-features --features runtime-vulkan,runtime-opengl,runtime-d3d11
```

`runtime-d3d11` is included so a single DLL also serves PPSSPP's D3D11 backend. Build time: ~1m50s
on a 16-core host. Artifact: `target\release\librashader_capi.dll` (13.8 MB).

There is no install-name/soname step on Windows (the import name lives in the importing module and
PPSSPP calls `LoadLibraryW`), so copying under the bare name PPSSPP looks for is all that is needed:

```bat
copy target\release\librashader_capi.dll C:\Users\Ilya\source\ppsspp\librashader.dll
```

Verify the exports - 52 `libra_` entry points with these three runtimes compiled in:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\<ver>\bin\Hostx64\x64\dumpbin.exe" /exports librashader.dll | Select-String ' libra_' | Measure-Object
```

`libra_vk_filter_chain_create`, `libra_gl_filter_chain_create` and `libra_d3d11_filter_chain_create`
must all be present; a DLL missing a runtime still reports `librashader loaded` but every chain
creation on that backend fails.

**rustup shims over SSH.** `%USERPROFILE%\.cargo\bin\cargo.exe` is a symlink to the active
toolchain. Under an SSH session that can fail (`os error 448: untrusted mount point`, or "No
application is associated with this file"); call the real binaries in the toolchain directory
instead and put them first on `PATH`:

```bat
set TC=%USERPROFILE%\.rustup\toolchains\stable-x86_64-pc-windows-msvc\bin
set PATH=%TC%;%PATH%
set RUSTC=%TC%\rustc.exe
set RUSTDOC=%TC%\rustdoc.exe
"%TC%\cargo.exe" build -p librashader-capi --release --no-default-features --features runtime-vulkan,runtime-opengl,runtime-d3d11
```

### Building PPSSPP itself on Windows

`Windows/PPSSPP.sln` compiles the Slang/librashader sources; `USE_LIBRASHADER=1` and
`../ext/librashader/include` are set for every configuration of `Common/Common.vcxproj`,
`GPU/GPU.vcxproj` and `unittest/UnitTests.vcxproj` (the UWP projects list the same sources but
deliberately without the define, so librashader stays off there, exactly like the CMake build).

```bat
cd /d C:\Users\Ilya\source\ppsspp
git config --global --add safe.directory "*"
git submodule update --init --recursive --depth 1 --jobs 6
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Windows\PPSSPP.sln /m /p:Configuration=Release /p:Platform=x64 /v:m
```

**The `safe.directory` line is not optional** when the checkout is not owned by the account running
git (a repo cloned by an elevated shell ends up owned by `Administrators`). Without it every git
command fails with `detected dubious ownership` - including the `fetch`/`checkout` in a build script,
which then happily builds whatever was already checked out. Always confirm `git rev-parse HEAD` on
the host before trusting a Windows build result.

Release|x64 links `PPSSPPWindows64.exe` into the **repository root** (not `Windows\x64\Release\`,
which only holds the static libs), so `librashader.dll` belongs in the repository root too - or
point `LIBRASHADER_PATH` at it from anywhere.

**Verified** on 2026-09-14 (Windows 11, RTX 4090, driver 596.49, Vulkan 1.4.329 and GL 4.6):
`stock.slangp` and `lcd-psp-matrix.slangp` render through librashader on both the Vulkan and the
OpenGL backend (`librashader loaded (ABI 2, API 5)`, `Slang chain backend: librashader`), and on
2026-09-15 on the **Direct3D 11** backend as well (see below).

## Backend Notes: Direct3D 11 (Windows only)

Select the backend with `GraphicsBackend = DIRECT3D11` in `ppsspp.ini` (the strings come from
`GPUBackendToString`: `OPENGL`, `DIRECT3D11`, `VULKAN`). No other setting is involved - the chain
selector admits D3D11 as soon as `librashader.dll` loads.

The DLL must be built with `runtime-d3d11` compiled in (the command above already does that;
`dumpbin /exports librashader.dll` should list `libra_d3d11_filter_chain_create`).

**What the adapter does** (`GPU/Common/Slang/LibrashaderRuntimeD3D11.cpp`, whole file inside
`#if USE_LIBRASHADER && PPSSPP_PLATFORM(WINDOWS)`): takes the `ID3D11Device *` from
`NativeObject::DEVICE`, creates the filter chain on the first frame callback, pushes the preset
parameter overrides, then calls `libra_d3d11_filter_chain_frame` with the immediate context, the
source framebuffer's shader resource view and the destination framebuffer's render target view.
Frame options are the same as on GL/Vulkan (SDR, `aspect_ratio = 0`, `frametime_delta = 16`).

**Immediate mode, and what that means for state.** D3D11 has no render thread and no command buffer,
so `D3D11DrawContext::RunNativeCallback` runs the callback synchronously on the calling thread. It
unbinds all pixel-shader resource slots and the render target first (the destination is normally
still bound as the current RTV, the source is often still bound as an SRV), and afterwards it
restores `curRenderTargetView_`/`curDepthStencilView_`, invalidates every cached comparison
`ApplyCurrentState()` makes (via `Invalidate(CACHED_RENDER_STATE)` plus the blend-factor and stencil
dirty flags), clears the SRV/sampler slots it dirtied, and fires the draw engine's
`RENDER_PASS_STATE` invalidation callback so viewport/scissor and texture state are re-sent. Because
everything is synchronous and D3D11 devices are free-threaded, `QueueFree` needs no deletion queue:
`libra_d3d11_filter_chain_free` (and `libra_preset_free`) run directly, even on device loss.

**`SourceSize` on D3D11: native-sized input.** `libra_d3d11_filter_chain_frame` takes only an
`ID3D11ShaderResourceView`, with no width/height fields - unlike `libra_image_gl_t`/`libra_image_vk_t`,
which is how the other adapters report the PSP's *native* 480x272 size while handing over the
upscaled framebuffer. librashader therefore reads the size off the resource on D3D11, so a preset
whose result depends on the input size would otherwise run its math at the render resolution
(CRT/LCD masks tiling at 3x instead of on the native grid). The D3D11 adapter therefore returns
`true` from `LibrashaderRuntime::RequiresNativeSizedInput()`, and `LibrashaderFilterChain::Load()`
routes the input through the native-sized copy it already had for `OriginalHistoryN` presets whenever
the preset reads `SourceSize`/`OriginalSize` or has an intermediate `scale_type = source` pass. D3D11's
`thin3d` has no `BlitFramebuffer`, so that copy is a linear-filtered `BlitUsingRaster`
(`DRAW2D_COPY_COLOR`) quad draw the framebuffer manager hands the chain; Vulkan/GL keep using
`BlitFramebuffer`. All
three backends then behave the same; the residual difference is input texel detail (D3D11 samples a
downscaled 480x272 copy where GL/Vulkan sample the upscaled framebuffer with native-sized
semantics), not geometry. Presets that do not depend on the source size (a single viewport-scaled
pass, e.g. `stock.slangp`) keep the upscaled input on D3D11 too. The chosen mode is in the
`input mode:` INFO line at preset load: `native-sized copy (history)`,
`native-sized copy (D3D11: preset depends on SourceSize)` or
`upscaled framebuffer with declared native size`.

**Shaders must survive FXC.** librashader cross-compiles the preset to HLSL and compiles it with
FXC, which is stricter than glslang: a dynamically indexed vector component is not a valid l-value
(`error X3500: array reference cannot be used as an l-value; not natively addressable`). This is why
`lcd-psp-matrix-pass3.slang` builds its subpixel mask with selects instead of `mask[sub] = 1.0`.
A preset that only ever ran on GL/Vulkan may need the same treatment; the failure is reported as
`LibrashaderFilterChain: disabled after librashader error (create: D3D11FilterError(D3DCompileError(...)))`
and PPSSPP keeps presenting the unfiltered image.

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

There is no toggle. librashader is the only slang rendering core: whenever a preset is selected
(`SlangShaderPreset` / Settings → Graphics → Slang shaders), PPSSPP loads the library and renders
the preset through it. Supported backends are VULKAN, OPENGL (3.3+/GLES 3.0+) and DIRECT3D11 (Windows).
Without the library — or on any other backend — slang shaders are off: the log shows

```
INFO  Slang chain backend: none (librashader not loaded or backend unsupported)
```

once per preset (re)load and the unfiltered image is presented. (Older builds had a
`SlangUseLibrashader` ini key / Developer Tools checkbox; the key is now ignored.)

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

**Without the library, slang shaders are off.** If `librashader.dylib`/`.so` is missing, or the GL
context is below 3.3 / GLES 3.0, no chain is created and PPSSPP presents the unprocessed
framebuffer:

```
INFO  librashader unavailable: <reason>
INFO  Slang chain backend: none (librashader not loaded or backend unsupported)
```

This does not crash; slang shaders are simply inactive until librashader is available again.
Therefore the A/B reference for a GL capture is the **Vulkan librashader** capture of the same
preset, not a "shaders off" capture.

**GL state contract.** Every `libra_gl_*` call runs inside a `GLRStepType::NATIVE_CALLBACK` step, i.e. on
the GL thread with the creating context current; the chain is created and used in the same callback
(unlike Vulkan there is no command-buffer readiness gate), and it is freed through another NATIVE_CALLBACK
step because `libra_gl_filter_chain_free` needs that context. librashader changes GL state freely,
so `GLQueueRunner::RestoreBaselineStateAfterCallback()` puts back everything
`PerformRenderPass`/`PerformBindFramebufferAsRenderTarget` assume: `fbo_unbind()` (which binds the
default FBO and updates both binding caches), the global VAO is rebound, `glUseProgram(0)`,
`GL_ARRAY_BUFFER` 0, `glBindSampler(i, 0)` for every texture slot, `glActiveTexture(GL_TEXTURE0)`,
the `GL_UNPACK_*` pixel-store parameters and `GL_PIXEL_UNPACK_BUFFER` (desktop GL and GLES 3.0+,
selected by a runtime check), full colour/depth/stencil masks, depth/stencil/blend/cull/dither
disabled, scissor test enabled, and - desktop only - logic op, depth clamp, `GL_FRAMEBUFFER_SRGB`
and all eight `GL_CLIP_DISTANCE*` disabled.

**Chain free at teardown.** The free NATIVE_CALLBACK step only runs while the render thread still drains
work. At `DeviceLost` / shutdown it does not - `GLRenderManager::ThreadEnd` deletes queued NATIVE_CALLBACK
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

or, when the library is unavailable or the backend cannot run native callbacks:

```
INFO  Slang chain backend: none (librashader not loaded or backend unsupported)
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
`-Pandroid.injected.build.abi` (that flag filters only the CMake output), so dev APKs carry all
three `librashader.so` (~29 MB uncompressed) by default. Release flavors are already pruned by
`ndk.abiFilters` (`normal`/`gold` keep all three, `legacy` two, `vr` one).

**Dev builds with a single ABI:** Use the `-PlibrashaderAbi=<abi>` Gradle property to exclude the
other ABIs from packaging:

```bash
ANDROID_HOME=/opt/homebrew/share/android-commandlinetools \
  ./gradlew assembleNormalDebug \
    -Pandroid.injected.build.abi=arm64-v8a \
    -PlibrashaderAbi=arm64-v8a \
    -PANDROID_VERSION_CODE=999999999 \
    -PANDROID_VERSION_NAME=dev --console=plain
```

The resulting APK contains only `lib/arm64-v8a/librashader.so` (~14 MB uncompressed), reducing the
dev APK size by ~15 MB. When `-PlibrashaderAbi` is absent, behavior is unchanged (all present ABIs
are packaged); an unknown ABI value fails the build (`require(keepAbi in allAbis)` validation).
Alternatively, build a single ABI with the script (`android/build-librashader.sh arm64-v8a`) and
manually delete the other `jniLibs/<abi>/librashader.so` directories.

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

If `librashader.so` is missing from the APK, slang shaders are off and the raw image is presented:

```
INFO  librashader unavailable: <reason>
INFO  Slang chain backend: none (librashader not loaded or backend unsupported)
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
validation is opt-in: set `VulkanSyncValidation = True` in the ini; `Common/GPU/Vulkan/VulkanContext.cpp`
then chains `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT` — Phase 4 ran it with zero hazards). Release builds ignore the layers even if present.

### CI Integration

`.github/workflows/manual_generate_apk.yml` now includes a `cargo ndk` step before the Gradle build:

1. Install NDK 29.0.14206865 via `sdkmanager` (Gradle would otherwise install it only later)
2. Install Rust stable with Android targets (`aarch64-linux-android`, `armv7-linux-androideabi`,
   `x86_64-linux-android`); cache `~/.cargo` (registry, git, the `cargo-ndk` binary)
3. Install `cargo-ndk`
4. Build librashader: `ANDROID_NDK_HOME=$ANDROID_HOME/ndk/29.0.14206865 android/build-librashader.sh`

The Gradle build then packages the resulting `jniLibs/<abi>/librashader.so` files into the APK.
The workflow is unverified locally (added in Phase 4 without a local CI runner); `ANDROID_HOME` is
preset on the GitHub Actions `ubuntu-latest` runner image.

The fork's other Android CI jobs (`android/ab.sh` via ndk-build) do not list the `GPU/Common/Slang`
sources and are not expected to build. Android librashader integration is primarily tested manually
on device.
