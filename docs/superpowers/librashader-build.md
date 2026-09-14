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
