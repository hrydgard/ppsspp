# librashader Integration — Phase 3 (Android: packaging, Vulkan + GLES verification) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `librashader.so` inside the Android APK so the Phase 1/2 librashader chain actually runs on Android, and verify it on the AYN Thor (Adreno 740) over Vulkan and GLES 3, including the first Vulkan validation-layer run of the CALLBACK step.

**Architecture:** No new runtime code is expected. A build script cross-compiles librashader's C API crate for the three APK ABIs with `cargo ndk`, renames the artifact to `librashader.so` (soname set at link time), and drops it into the module's existing gitignored `android/src/main/jniLibs/<abi>/` directory, which the Gradle build already packages (`jniLibs.useLegacyPackaging = true` extracts it so the loader's bare-name `dlopen("librashader.so")` resolves inside the app's linker namespace). Verification is on-device via `adb`: the device config (ini on the SD card) is writable over `adb shell`, `adb exec-out screencap` gives exact framebuffer captures for pixel A/B, and the debug APK enables Vulkan validation when the Khronos layer library is present in the same jniLibs directory. GLES verification may require requesting an ES 3 context in `NativeGLSurfaceView`.

**Tech Stack:** Rust stable (cargo 1.98), `cargo-ndk` 4.1.2, Android NDK 29.0.14206865, Gradle (AGP, CMake native build), `adb`; librashader 0.12.0 (`runtime-vulkan,runtime-opengl`); Khronos `Vulkan-ValidationLayers` Android binaries.

**Spec:** `docs/superpowers/specs/2026-09-14-librashader-integration-design.md` §6.7 (build/distribution), §10 row 3, §12 (risks: Android context loss leak, frames in flight). Phase 1/2 plans for reference: `2026-09-14-librashader-phase1-vulkan-core.md`, `2026-09-14-librashader-phase2-opengl.md`.

## Global Constraints

- **No binaries in git.** `librashader.so` and any validation layer `.so` live only in `android/src/main/jniLibs/<abi>/` (already ignored by `android/src/main/.gitignore` and `android/src/main/jniLibs/.gitignore`). The script may clone librashader into `build/librashader-src` (ignored by the root `build*/` pattern) — never into `ext/`.
- **Pinned version:** tag `librashader-v0.12.0` (C ABI 2 / API 5), the same the vendored headers came from. Features `runtime-vulkan,runtime-opengl`, `--no-default-features`, `--platform 21` (matches `-DANDROID_PLATFORM=android-21` in `android/build.gradle.kts`). Soname `librashader.so` via `RUSTFLAGS="-C link-arg=-Wl,-soname,librashader.so"`. Verified facts from the controller's trial build: artifact `target/aarch64-linux-android/release/liblibrashader_capi.so` (14 MB), `NEEDED libc++_shared.so libdl.so libm.so libc.so` (the APK already ships `libc++_shared.so`), 44 `libra_*` exports.
- **cargo-ndk syntax:** `cargo ndk -t <abi> --platform 21 build -p librashader-capi --release --no-default-features --features runtime-vulkan,runtime-opengl` (`-p` is the *package* flag in cargo-ndk 4.x; the API level is `--platform`). NDK via `ANDROID_NDK_HOME=/opt/homebrew/share/android-commandlinetools/ndk/29.0.14206865` (or `$ANDROID_HOME/ndk/<ndkVersion from build.gradle.kts>`).
- **No behavior change on desktop or in the Phase 1/2 runtime code**, except the GLES context-version change in Task 3 if the device turns out to hand PPSSPP an ES 2 context (see that task's contingency, which must keep ES 2 as the fallback).
- **APK build/install:** `export ANDROID_HOME=/opt/homebrew/share/android-commandlinetools; ./gradlew -p android assembleNormalDebug -Pandroid.injected.build.abi=arm64-v8a -PANDROID_VERSION_CODE=999999999 -PANDROID_VERSION_NAME=librashader-p3 --console=plain`; APK at `android/build/intermediates/apk/normal/debug/android-normal-debug.apk`; `adb install -t -r <apk>`. The Gradle daemon must be stopped at the end (`./gradlew -p android --stop`).
- **Device facts (AYN Thor, serial 64dc3c35, Adreno 740):** memstick is on the SD card, config `/storage/9C33-6BBD/ROMs/psp/PSP/SYSTEM/ppsspp.ini` (writable via `adb shell`, edit with `adb shell sed -i` or pull/edit/push); user slang dir `/storage/emulated/0/Android/data/org.ppsspp.ppsspp/files/slang/` with the full libretro pack (`presets/crt-royale-downsample.slangp`, `presets/handheld-plus-color-mod/lcd-grid-v2-psp-color.slangp`, `crt/crt-maximus-royale-fast-mode.slangp`, ...). Current keys: `GraphicsBackend = 3 (VULKAN)`, `InternalResolution = 4`, `G3DLevel = 2` (set `G3DLevel = 4` and `SystemLevel = 4` so INFO lines such as `librashader loaded` and `Slang chain backend:` reach logcat; restore afterwards). Launch a game: `adb shell am start -n org.ppsspp.ppsspp/.PpssppActivity --es org.ppsspp.ppsspp.Shortcuts 'content://com.android.externalstorage.documents/tree/9C33%2D6BBD%3AROMs%2Fpsp/document/9C33%2D6BBD%3AROMs%2Fpsp%2F3rd%20Birthday%2Eiso'`. Wake/unlock first (`adb shell input keyevent KEYCODE_WAKEUP; adb shell input keyevent 82`); the device sleeps quickly. Screenshots: `adb exec-out screencap -p > file.png` (exact framebuffer; compare with `/tmp/ppsspp-t8/cmp.py`). Logs: `adb logcat -c` before a run, `adb logcat -d | grep -E "PPSSPP|librashader|Fatal|DEBUG"` after. The device's GLES driver is ANGLE-over-Vulkan (`OpenGL ES 3.1.0 (ANGLE ...)`).
- **Commit trailer:** end every commit message with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- **Desktop tests still gate every task:** `cmake --build build-unittest --target PPSSPPUnitTest -j12 && ./build-unittest/PPSSPPUnitTest all` → `76 tests passed.`

## File structure

| File | Responsibility |
|---|---|
| `android/build-librashader.sh` (new) | Clone/pin librashader, cross-build per ABI with cargo-ndk, rename + verify, place into `android/src/main/jniLibs/<abi>/librashader.so` |
| `android/src/main/jniLibs/README.txt` | Mention librashader alongside the validation-layer note |
| `docs/superpowers/librashader-build.md` | Android section: prerequisites, script, where the library goes, how to confirm, validation-layer recipe, CI note |
| `android/src/org/ppsspp/ppsspp/NativeGLSurfaceView.java` / `PpssppActivity.java` | Only if Task 3's contingency triggers: request ES 3 with ES 2 fallback |
| `docs/superpowers/specs/...design.md` §10 row 3, this plan's tables | Results |

---

### Task 1: `android/build-librashader.sh` and docs

**Files:**
- Create: `android/build-librashader.sh`
- Modify: `android/src/main/jniLibs/README.txt`, `docs/superpowers/librashader-build.md`

**Interfaces:**
- Produces: `android/build-librashader.sh [ABI ...]` (default `arm64-v8a armeabi-v7a x86_64`); env overrides `LIBRASHADER_TAG` (default `librashader-v0.12.0`), `LIBRASHADER_SRC` (default `<repo>/build/librashader-src`), `ANDROID_NDK_HOME`; output `android/src/main/jniLibs/<abi>/librashader.so`; exit non-zero with a clear message when cargo, cargo-ndk, a rustup target or the NDK is missing.

- [ ] **Step 1: Script**

```bash
#!/usr/bin/env bash
# Cross-builds librashader's C API for Android and drops librashader.so into the APK's jniLibs.
# Usage: android/build-librashader.sh [ABI ...]   (default: arm64-v8a armeabi-v7a x86_64)
# Env:   LIBRASHADER_TAG (default librashader-v0.12.0), LIBRASHADER_SRC, ANDROID_NDK_HOME
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
TAG=${LIBRASHADER_TAG:-librashader-v0.12.0}
SRC=${LIBRASHADER_SRC:-$REPO/build/librashader-src}
OUT=$REPO/android/src/main/jniLibs
ABIS=("$@"); [ ${#ABIS[@]} -eq 0 ] && ABIS=(arm64-v8a armeabi-v7a x86_64)
PLATFORM=21   # matches -DANDROID_PLATFORM=android-21 in android/build.gradle.kts

need() { command -v "$1" >/dev/null 2>&1 || { echo "error: '$1' not found ($2)" >&2; exit 1; }; }
need cargo "install Rust stable >= 1.88 via rustup"
need rustup "install Rust via rustup"
cargo ndk --version >/dev/null 2>&1 || { echo "error: cargo-ndk not found (cargo install cargo-ndk)" >&2; exit 1; }

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
	NDK_VER=$(sed -n 's/.*ndkVersion = "\([^"]*\)".*/\1/p' "$REPO/android/build.gradle.kts" | head -1)
	for base in "${ANDROID_HOME:-}" "$HOME/Library/Android/sdk" /opt/homebrew/share/android-commandlinetools; do
		[ -n "$base" ] && [ -d "$base/ndk/$NDK_VER" ] && export ANDROID_NDK_HOME="$base/ndk/$NDK_VER" && break
	done
fi
[ -d "${ANDROID_NDK_HOME:-}" ] || { echo "error: set ANDROID_NDK_HOME (NDK not found)" >&2; exit 1; }
echo "NDK: $ANDROID_NDK_HOME"

target_for() { case "$1" in arm64-v8a) echo aarch64-linux-android;; armeabi-v7a) echo armv7-linux-androideabi;; x86_64) echo x86_64-linux-android;; x86) echo i686-linux-android;; *) echo "error: unknown ABI $1" >&2; exit 1;; esac; }
for abi in "${ABIS[@]}"; do
	t=$(target_for "$abi")
	rustup target list --installed | grep -qx "$t" || { echo "error: rustup target $t missing (rustup target add $t)" >&2; exit 1; }
done

if [ ! -d "$SRC/.git" ]; then
	git clone --depth 1 --branch "$TAG" https://github.com/SnowflakePowered/librashader.git "$SRC"
else
	(cd "$SRC" && git fetch --depth 1 origin "refs/tags/$TAG:refs/tags/$TAG" && git checkout -q "$TAG")
fi

HOST=$(uname -s | tr '[:upper:]' '[:lower:]')-x86_64
READELF="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST/bin/llvm-readelf"
NM="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST/bin/llvm-nm"

for abi in "${ABIS[@]}"; do
	t=$(target_for "$abi")
	echo "== $abi ($t)"
	( cd "$SRC" && RUSTFLAGS="-C link-arg=-Wl,-soname,librashader.so" \
	  cargo ndk -t "$abi" --platform "$PLATFORM" build -p librashader-capi --release \
	    --no-default-features --features runtime-vulkan,runtime-opengl )
	mkdir -p "$OUT/$abi"
	cp "$SRC/target/$t/release/liblibrashader_capi.so" "$OUT/$abi/librashader.so"
	if [ -x "$READELF" ]; then
		"$READELF" -d "$OUT/$abi/librashader.so" | grep -q 'SONAME.*\[librashader.so\]' || { echo "error: soname not set for $abi" >&2; exit 1; }
		n=$("$NM" -gD --defined-only "$OUT/$abi/librashader.so" | grep -c ' libra_' || true)
		[ "$n" -ge 40 ] || { echo "error: only $n libra_* exports for $abi" >&2; exit 1; }
		echo "   ok: soname librashader.so, $n exports, $(du -h "$OUT/$abi/librashader.so" | cut -f1)"
	fi
done
echo "Done. Rebuild the APK (gradle packages android/src/main/jniLibs automatically)."
```

`chmod +x android/build-librashader.sh`.

- [ ] **Step 2: Run it for the device ABI and verify**

Run: `ANDROID_NDK_HOME=/opt/homebrew/share/android-commandlinetools/ndk/29.0.14206865 android/build-librashader.sh arm64-v8a`
Expected: `ok: soname librashader.so, 44 exports, 14M`; file at `android/src/main/jniLibs/arm64-v8a/librashader.so`; `git status` shows nothing new under `android/src/main/jniLibs`. Then run the other two ABIs (`armeabi-v7a x86_64`) once — the rustup targets are installed; each build is ~1 min. If an ABI fails to build, record the error in the report and keep the script defaulting to all three only if all three built; otherwise default to `arm64-v8a` and document why.

- [ ] **Step 3: Docs**

`android/src/main/jniLibs/README.txt`: add a paragraph — librashader for slang shaders also goes here as `<abi>/librashader.so`, produced by `android/build-librashader.sh`; without it the slang chain falls back to the in-tree implementation. `docs/superpowers/librashader-build.md`: new "Android" section — prerequisites (`rustup target add aarch64-linux-android armv7-linux-androideabi x86_64-linux-android`, `cargo install cargo-ndk`, NDK 29), the script, the jniLibs location and why the bare-name `dlopen` works (app linker namespace, legacy packaging extracts `lib*.so`), how to confirm on device (`adb logcat | grep -E "librashader loaded|Slang chain backend"` after setting `G3DLevel = 4`), the validation-layer recipe (Task 2 Step 3), and a "CI" paragraph: the fork's Android CI jobs use `android/ab.sh` (ndk-build via `Android.mk`), which does not list the `GPU/Common/Slang` sources at all (pre-existing), so a `cargo ndk` step would be pointless until that build is fixed — the Gradle/CMake path is the one that works; record this as deferred.

- [ ] **Step 4: Desktop tests unaffected**

Run: `./build-unittest/PPSSPPUnitTest all` → `76 tests passed.` (no C++ changed).

- [ ] **Step 5: Commit**

```bash
git add android/build-librashader.sh android/src/main/jniLibs/README.txt docs/superpowers/librashader-build.md
git commit -m "android: build-librashader.sh cross-builds librashader.so into jniLibs

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: APK with librashader, Vulkan verification on the AYN Thor, validation-layer run

**Files:**
- Possibly modify (fixes only, if a defect is found): `GPU/Common/Slang/LibrashaderRuntimeVulkan.cpp`, `GPU/Common/Slang/LibrashaderFilterChain.cpp`, `Common/GPU/Vulkan/VulkanQueueRunner.cpp`, `Common/GPU/Librashader/LibrashaderLoader.cpp`
- Modify: this plan (results table below), `docs/superpowers/librashader-build.md` if the recipe needed adjusting

- [ ] **Step 1: Build, inspect, install**

Build the APK (Global Constraints command). Verify: `unzip -l <apk> | grep -E "lib/arm64-v8a/(librashader|libppsspp_jni|libc\+\+_shared)"` lists all three; `adb install -t -r <apk>` → `Success`; `adb shell dumpsys package org.ppsspp.ppsspp | grep versionName` → `librashader-p3`.

- [ ] **Step 2: Vulkan load + rendering**

Device ini: `G3DLevel = 4`, `SystemLevel = 4`, `GraphicsBackend = 3 (VULKAN)`, `SlangUseLibrashader = True`, `SlangShaderPreset = /storage/emulated/0/Android/data/org.ppsspp.ppsspp/files/slang/presets/handheld-plus-color-mod/lcd-grid-v2-psp-color.slangp`. Launch the game; after ~25 s: logcat must show `librashader loaded (ABI 2, API 5)`, `Slang chain backend: librashader`, `LibrashaderFilterChain: preset parsed`, an `input mode:` line, no `LibrashaderFilterChain:`/`LibrashaderRuntimeVulkan:` errors, no `Fatal signal`; `adb exec-out screencap -p` shows the game with the LCD grid. Then `SlangUseLibrashader = False`, relaunch, screencap: in-tree output for A/B. Compare with `cmp.py` (same frame is unlikely in a live game — capture the static "This game saves data automatically" dialog that appears right after boot, which Phase 1's device run showed; it is static for ~10 s).

- [ ] **Step 3: Vulkan validation layers (first ever run of the CALLBACK step under validation)**

The debug APK sets `VulkanInitFlags::VALIDATE` at compile time (`GPU/Vulkan/VulkanUtil.cpp:29-33`, `_DEBUG` builds) and `VulkanContext` enables `VK_LAYER_KHRONOS_validation` if it is loadable; PPSSPP's own `android/src/main/jniLibs/README.txt` documents dropping the Khronos Android layer there. Do it: `curl -sfL -o /tmp/vvl.zip https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases/download/vulkan-sdk-1.4.357.0/android-binaries-1.4.357.0.zip` (30 MB), unzip, copy `arm64-v8a/libVkLayer_khronos_validation.so` into `android/src/main/jniLibs/arm64-v8a/`, rebuild + reinstall the APK, confirm `unzip -l` lists the layer, launch with `lcd-grid-v2-psp-color` and then `presets/crt-royale-downsample.slangp`; logcat filtered for `VALIDATION|VUID|vkCmd|SYNC-HAZARD|Validation Error` (PPSSPP logs layer messages through its debug callback at ERROR/WARN). Expected: zero messages attributable to the CALLBACK step or librashader (messages that also appear with `SlangUseLibrashader = False` are pre-existing PPSSPP noise — capture a baseline run with the toggle off and diff the message sets). Also confirm the layer actually loaded (`Enabling Vulkan validation`/layer-found log line, or the message volume being non-zero on the baseline). Remove the layer `.so` from jniLibs afterwards and rebuild so the final installed APK is layer-free (record both APK sizes). If the layer refuses to load on this device (e.g. Vulkan 1.3 driver vs layer version), record NOT RUN with the exact log.

- [ ] **Step 4: Heavy preset, performance, lifecycle**

`SlangShaderPreset = .../crt/crt-maximus-royale-fast-mode.slangp` (heaviest available; falls back to `presets/crt-royale-downsample.slangp` if it fails to compile — record the error): loads (`preset parsed`, first frames after the MAX_INFLIGHT_FRAMES gate), renders, no errors; note chain creation time from the log timestamps (`preset parsed` → first frame without the unfiltered fallback is not logged; use the timestamp of the first `input mode:` line vs the time the game booted). Performance: enable the FPS counter (ini key ``, see its ConfigSetting line in `Core/Config.cpp`; value 1 = speed, 2 = FPS, 3 = both) and read it off two screencaps ~10 s apart for librashader vs in-tree on `crt-royale-downsample` (both chains render the same preset); record the numbers. Lifecycle: with librashader active, sleep/wake the device in-game (`KEYCODE_SLEEP`, 3 s, `KEYCODE_WAKEUP`, `82`): process survives, the chain is recreated (a second `preset parsed` + `input mode:` pair), image still filtered. Preset switch: edit `SlangShaderPreset` in the ini and trigger a reload without relaunching if possible (open and close the in-game pause menu with `KEYCODE_BACK`/`KEYCODE_ESCAPE` — `NotifyConfigChanged` fires when settings close); otherwise relaunch and record that in-process switching was done by ini + relaunch only.

- [ ] **Step 5: Record**

| Check | Result | Notes |
|---|---|---|
| APK contains librashader.so (arm64) | | |
| Vulkan: librashader loaded + backend selected | | |
| lcd-grid-v2-psp-color renders (librashader vs in-tree) | | |
| Validation layers: layer loaded | | |
| Validation layers: CALLBACK/librashader messages | | |
| crt-maximus-royale-fast-mode loads + renders | | |
| Performance crt-royale-downsample (FPS librashader / in-tree) | | |
| Sleep/wake with librashader | | |
| Preset switch | | |

Commit any fix plus the table: `git commit -m "librashader: Phase 3 Android Vulkan verification results ..."` with the trailer.

---

### Task 3: GLES 3 verification on the device (with ES 3 context contingency)

**Files:**
- Possibly modify: `android/src/org/ppsspp/ppsspp/NativeGLSurfaceView.java`, `android/src/org/ppsspp/ppsspp/PpssppActivity.java` (~line 715 `mGLSurfaceView.setEGLContextClientVersion(isVRDevice() ? 3 : 2)`), and only if a GL-specific defect appears: `GPU/Common/Slang/LibrashaderRuntimeOpenGL.cpp`, `Common/GPU/OpenGL/GLQueueRunner.cpp`, `Common/GPU/OpenGL/thin3d_gl.cpp`
- Modify: this plan (table), spec §10 row 3

- [ ] **Step 1: Switch the device to OpenGL and read the context version**

Ini: `GraphicsBackend = 0 (OPENGL)`, `SlangUseLibrashader = True`, preset `lcd-grid-v2-psp-color`. Launch; logcat: PPSSPP logs the GL vendor/version string at startup (`GL_VERSION`/`OpenGL ES 3.x`). Record whether the context is ES 2 or ES 3.x and whether `Slang chain backend:` says `librashader` or `in-tree`.

- [ ] **Step 2: Contingency — ES 2 context**

If the log shows an ES 2.0 context (and therefore `in-tree`), PPSSPP's Java GL path (`javaGL = true`, `PpssppActivity.java:712-715`) requested client version 2. Change it to try 3 first: in `PpssppActivity.java` replace `setEGLContextClientVersion(isVRDevice() ? 3 : 2)` with a call that sets 3 when `android.opengl.GLES30` is usable and the device reports `reqGlEsVersion >= 0x30000` (`ActivityManager.getDeviceConfigurationInfo().reqGlEsVersion`), else 2 — mirror the existing VR branch's style, keep ES 2 as the fallback, and add a one-line INFO log of the chosen version. Rebuild the APK, reinstall, repeat Step 1. Document the change in the build doc's Android section and in this table. If the context is already ES 3.x, skip this step and say so.

- [ ] **Step 3: GLES rendering and A/B vs Vulkan**

With `librashader` selected on GL: no `LibrashaderRuntimeOpenGL:`/`LibrashaderFilterChain:` errors (a `create:` error here means ES 3.x shader compilation failed for the preset — capture the full string; try `presets/crt-royale-downsample.slangp` and `lcd-grid-v2-psp-color` both); screencap the static boot dialog on GL and on Vulkan (`GraphicsBackend = 3`) with librashader, `cmp.py` them; expect visually identical (ANGLE vs native Vulkan rounding may differ — report the numbers and view the images). Also `SlangUseLibrashader = False` on GL → raw image (in-tree is Vulkan-only), no crash.

- [ ] **Step 4: GL lifecycle (the path Phase 2 could not exercise)**

Sleep/wake in-game on GL: expect the drop warning `LibrashaderRuntimeOpenGL: dropping chain without freeing (no GL context)` at context loss (or no warning if Android kept the context), then a new `preset parsed` + `input mode:` after wake and a filtered image. Preset switch via pause-menu close (see Task 2 Step 4): on GL this is the first time `libra_gl_filter_chain_free` runs on any device — add nothing to the code; just confirm no crash and that the new preset renders. Record whether the free path was reached (no log line exists for it; infer from a successful switch without leak-related crash and from the absence of the drop warning).

- [ ] **Step 5: Record**

| Check | Result | Notes |
|---|---|---|
| GL context version reported | | |
| ES 3 contingency applied? | | |
| GL: librashader loaded + backend selected | | |
| lcd-grid-v2-psp-color GL vs Vulkan (librashader) | | |
| crt-royale-downsample on GLES | | |
| Toggle off on GL → raw, no crash | | |
| Sleep/wake on GL (drop warning, recreation) | | |
| Preset switch on GL (free path) | | |

Restore the device ini afterwards: `GraphicsBackend = 3 (VULKAN)`, `G3DLevel = 2`, `SystemLevel` as before, and the user's original `SlangShaderPreset` (`presets/crt-royale-downsample.slangp` at the start of Phase 3). Update spec §10 row 3 with the outcome. Commit with the trailer.

---

## Out of scope (Phase 4)

Remove the in-tree chain, revert the thin3d slot/descriptor bumps and sRGB render-pass keying, delete `bSlangUseLibrashader`, D3D11 adapter, CI integration once the Android ndk-build path lists the Slang sources.
