# librashader Integration — Phase 4 (Perf gate, carried-over fixes, in-tree chain removal, Windows/D3D11) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make librashader the only slang rendering core: measure its cost properly, run synchronization validation once, land the Android fixes carried over from Phase 3, delete the in-tree chain and the thin3d changes that existed only for it (so the fork's delta versus upstream shrinks to librashader glue plus the kept preset/UI subsystems), bring up the Windows build with a D3D11 adapter on the available Windows machine, and finish the packaging items.

**Architecture:** Tasks 1–3 are measurement and small fixes that gate and de-risk the removal. Task 4 removes `SlangFilterChain`/`SlangPassCompiler`/`SlangReflection`/`SlangResolution`, the factory's fallback branch and the `SlangUseLibrashader` toggle, and reverts the texture-slot/descriptor caps, the GL `sampler3..7` lookups and the sRGB/float render-pass keying to upstream values; the selector becomes "librashader if loaded and the backend supports native callbacks, else no chain". Tasks 5–6 run on the Windows machine over SSH: add the new sources to the `.vcxproj` files, build `PPSSPP.sln`, build `librashader.dll`, verify Vulkan/GL on Windows, then add `LibrashaderRuntimeD3D11.cpp` plus `D3D11DrawContext::RunNativeCallback` (immediate mode: SRV/RTV handed straight to librashader, thin3d's cached state re-applied afterwards). Task 7 finishes packaging (jniLibs ABI pruning, CI step) and the docs.

**Tech Stack:** C++17; librashader 0.12.0 C API (Vulkan, GL, D3D11 runtimes); PPSSPP thin3d/VulkanQueueRunner/GLQueueRunner/D3D11; CMake + Ninja (macOS), MSBuild + `Windows/PPSSPP.sln` (Windows), Gradle (Android); Rust/cargo (macOS: cargo-ndk 4.1.2; Windows: MSVC target); adb; ssh to the Windows host `pcsx2-win`.

**Spec:** `docs/superpowers/specs/2026-09-14-librashader-integration-design.md` §5 (what is made redundant), §6.6, §10 row 4, §12 (risks), §13 Q2 (answered: remove now). Phase 1–3 plans for reference.

## Global Constraints

- **Perf gate before removal:** Task 4 may start only if Task 1's averaged GPU-time ratio (librashader CALLBACK ÷ in-tree passes, same preset, same scene, ≥ 200 frames each) is ≤ 1.5 on the Android device. If it is higher, stop after Task 3 and report; do not delete the in-tree chain.
- **Reverts are to upstream values:** `MAX_TEXTURE_SLOTS = 3` (`Common/GPU/thin3d.h`; upstream `hrydgard/ppsspp` master), `MAX_DESC_SET_BINDINGS = 5` (`Common/GPU/Vulkan/VulkanRenderManager.h`), D3D11 `MAX_BOUND_TEXTURES = 8`, GL sampler-name queries back to `sampler0..2` only, `FramebufferDesc::colorFormat` and `RPKey::colorFormat`/`_padding` removed, `VKRFramebuffer` back to a fixed `VK_FORMAT_R8G8B8A8_UNORM`, `DataFormat::R8G8B8A8_UNORM_SRGB` plumbing removed **only if** nothing outside the in-tree chain uses it. Verify each with `git diff upstream/master -- <file>` after the change: the remaining hunks must be the native-callback additions only.
- **Kept subsystems (do not touch):** `SlangpParser`, `SlangPreset.h`, `Core/Slang/*` (preset library, importer, paths, `GetPresetParameters`), `UI/SlangShaderScreen.*`, config `sSlangShaderPreset`/`sSlangBuildbotUrl`/`mSlangParams`, all `Librashader*` files, the CALLBACK steps, thin3d `RunNativeCallback`/`SupportsNativeCallback`.
- **Behavior after removal:** with librashader loaded, output identical to before on every backend (re-verify `stock`/`lcd-psp-matrix` on macOS Vulkan + GL against Phase 2 screenshots); without it, `Slang chain backend: none` is logged once per reload and the raw image is presented (no crash, no black screen). The `bSlangUseLibrashader` setting and Developer Tools checkbox are removed; an existing ini key is ignored.
- **Unit tests:** tests that exercise deleted code are deleted with it; every remaining test passes. Record the new total (expected 76 minus the reflection/resolution/semantics/push-constant/compiler tests).
- **Windows work runs over SSH** to host alias `pcsx2-win` (Windows 11, user `Ilya`, VS 18 Community with MSVC 14.51, MSBuild at `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`, VS-bundled CMake, Windows SDK 10.0.26100.0, Rust `x86_64-pc-windows-msvc`, git, 16 cores, RTX 4090 + Radeon iGPU, 906 GB free, no PPSSPP checkout yet). Run PowerShell scripts by piping them: `ssh pcsx2-win 'powershell -NoProfile -Command -' < script.ps1` (avoids cmd quoting). Long builds: start with `Start-Process`/`nohup`-equivalent and poll a log file; SSH sessions time out at 10 min per command. Clone with `git clone --recursive https://github.com/ilya-slalom/ppsspp.git C:\Users\Ilya\source\ppsspp` and check out this branch. The exe reads `librashader.dll` from the executable directory (`Windows\x64\Release\`).
- **Android device facts, launch/ini/screenshot recipes:** as in the Phase 3 plan's Global Constraints (`docs/superpowers/plans/2026-09-14-librashader-phase3-android.md`); the installed APK is `librashader-p3` and `android/src/main/jniLibs/<abi>/librashader.so` exist on disk. Stop the Gradle daemon after building (`./gradlew -p android --stop`).
- **Commit trailer:** end every commit message with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- **Desktop gate every task:** `cmake --build build-unittest --target PPSSPPUnitTest PPSSPPSDL -j12 && ./build-unittest/PPSSPPUnitTest all`.

## File structure

| File | Responsibility |
|---|---|
| `Core/Config.h/.cpp`, `UI/DeveloperToolsScreen.cpp` | T1: `bLogGpuProfile` dev toggle; T2: `bVulkanSyncValidation`; T4: remove `bSlangUseLibrashader` |
| `UI/DebugOverlay.cpp` | T1: log the GPU profile string at 1 Hz when enabled |
| `Common/GPU/Vulkan/VulkanContext.h/.cpp`, `GPU/Vulkan/VulkanUtil.cpp` | T2: `VulkanInitFlags::SYNC_VALIDATE` → `VkValidationFeaturesEXT` |
| `android/jni/app-android.cpp`, `Common/GPU/OpenGL/OpenGLGraphicsContext.h`, `Common/GPU/OpenGL/GLRenderManager.h` | T3: skip GL calls before the restart drain |
| `android/src/org/ppsspp/ppsspp/PpssppActivity.java` | T3: ES 3 request with ES 2 fallback |
| `GPU/Common/Slang/LibrashaderFilterChain.cpp` | T3: native-size WARN → INFO |
| `GPU/Common/Slang/{SlangFilterChain,SlangPassCompiler,SlangReflection}.{h,cpp}`, `SlangResolution.h` | T4: deleted |
| `GPU/Common/Slang/SlangChainFactory.cpp`, `ISlangFilterChain.h`, `GPU/Common/FramebufferManagerCommon.cpp` | T4: selector without fallback |
| `Common/GPU/thin3d.h`, `Common/GPU/Vulkan/{VulkanRenderManager,VulkanQueueRunner,VulkanFramebuffer,thin3d_vulkan}.*`, `Common/GPU/OpenGL/thin3d_gl.cpp`, `Common/GPU/D3D11/thin3d_d3d11.cpp` | T4: reverts |
| `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`, `unittest/TestLibrashader.cpp` | T4: drop tests of deleted code; selector truth table |
| `GPU/GPU.vcxproj(.filters)`, `Common/Common.vcxproj(.filters)`, `Windows/PPSSPP.vcxproj` as needed | T5: Windows build of the Slang/Librashader sources |
| `Common/GPU/thin3d.h`, `Common/GPU/D3D11/thin3d_d3d11.cpp`, `GPU/Common/Slang/LibrashaderRuntimeD3D11.cpp`, `LibrashaderRuntime.{h,cpp}`, `Common/GPU/Librashader/LibrashaderLoader.h` | T6: D3D11 adapter |
| `android/build.gradle.kts`, `.github/workflows/manual_generate_apk.yml`, docs, spec | T7 |

---

### Task 1: Averaged GPU-time comparison on the device (the perf gate)

**Files:**
- Modify: `Core/Config.h` (after `int iShowStatusFlags;`), `Core/Config.cpp` (next to the `iShowStatusFlags` setting, ~line 680), `UI/DebugOverlay.cpp` (the `DebugOverlay::GPU_PROFILE` case, ~line 259), `UI/DeveloperToolsScreen.cpp` (General tab)
- Modify: this plan (results table)

**Interfaces:**
- Produces: `bool g_Config.bLogGpuProfile` (ini `LogGpuProfile`, default false); when true and the GPU_PROFILE overlay is active, `DrawGPUProfilerVis` path logs `INFO_LOG(Log::G3D, "GPUPROFILE %s", gpu->GetDrawContext()->GetGpuProfileString().c_str())` at most once per second (static `double lastLog` with `time_now_d()`).

- [x] **Step 1: Dev toggle**

`Core/Config.h`: `bool bLogGpuProfile;  // Log the GPU profiler string once per second while the GPU_PROFILE overlay is shown.`
`Core/Config.cpp`: `ConfigSetting("LogGpuProfile", SETTING(g_Config, bLogGpuProfile), false, CfgFlag::DEFAULT),`
`UI/DebugOverlay.cpp`, inside `case DebugOverlay::GPU_PROFILE:` after `DrawGPUProfilerVis(ctx, gpu);`:

```cpp
			if (g_Config.bLogGpuProfile) {
				static double lastLog = 0.0;
				double now = time_now_d();
				if (now - lastLog >= 1.0) {
					lastLog = now;
					std::string s = gpu->GetDrawContext()->GetGpuProfileString();
					// One log line per profiler line so logcat keeps them intact.
					for (const auto &line : SplitString(s, '\n'))
						if (!line.empty()) INFO_LOG(Log::G3D, "GPUPROFILE %s", line.c_str());
				}
			}
```
Check the helper names in this file (`time_now_d` from `Common/TimeUtil.h`, `SplitString` from `Common/StringUtils.h`; include if missing; `gpu->GetDrawContext()` — confirm the accessor name used elsewhere in `DebugOverlay.cpp` for the profile string). Developer Tools General tab: `list->Add(new CheckBox(&g_Config.bLogGpuProfile, dev->T("Log GPU profile")));`.

- [x] **Step 2: Build APK, measure**

Build + install the arm64 debug APK (version name `librashader-p4a`). Device ini: `GraphicsBackend = 3 (VULKAN)`, `SlangShaderPreset = .../slang/presets/crt-royale-downsample.slangp`, `DebugOverlay = 6` (GPU_PROFILE, value confirmed in Phase 3), `LogGpuProfile = True`, `G3DLevel = 4` in both `[Log]` and `[LogDebug]`. Launch the game; after the boot dialog appears, `adb logcat -c`, wait 60 s, `adb logcat -d | grep GPUPROFILE > /tmp/ppsspp-t8/p4-prof-libra.log`. Repeat with `SlangUseLibrashader = False` → `p4-prof-intree.log`. Parse with a small Python script: for librashader sum lines matching `CALLBACK librashader` per sample; for in-tree sum `RENDER slang-pass*` + `BLIT ... slang` lines per sample (use the exact step tags seen in the Phase 3 GPU-profile screenshots: `RENDER slang-pass` ×N and one `BLIT`); report mean, median, p95 and sample count for each; ratio = mean_libra / mean_intree. Also record the total frame GPU time both ways. Restore ini keys afterwards (`DebugOverlay = 0`, `LogGpuProfile = False`, `SlangUseLibrashader = True`, log levels back to 2).

- [x] **Step 3: Record and gate**

**Results** (AYN Thor, Adreno 740, Vulkan, `presets/crt-royale-downsample.slangp`,
internal resolution 4x → 1920x1088, APK `librashader-p4a` arm64 debug; scene: 3rd Birthday's
static "memory stick" boot dialog, which waits for input, so every sample in every run rendered
the same frame — confirmed by `BACKBUF BackBuffer (draws: 3, 1920x1080/1080x1920)` in 100% of
samples). Post-process = `CALLBACK librashader ...` for librashader; 14 × `RENDER slang-pass
slang-pass ...` + 1 × `BLIT 'slang-feedback' slang-pass -> slang-feedback ...` for in-tree
(15 steps in every sample). Logs `/tmp/ppsspp-t8/p4-prof-{libra,intree,libra-dynrender}.log`,
parser `/tmp/ppsspp-t8/p4-parse-prof.py`.

| Metric | librashader | in-tree | ratio |
|---|---|---|---|
| samples (1 Hz) | 215 | 214 | — |
| post-process GPU ms mean | 10.569 | 8.661 | **1.220** |
| median / p95 | 10.564 / 10.604 | 8.661 / 8.704 | 1.219 / 1.218 |
| whole-frame GPU ms mean | 13.880 | 11.325 | 1.226 |

`use_dynamic_rendering = true` (214 samples): post-process mean **10.569 ms** (median 10.567,
p95 10.597), whole-frame mean 13.976 ms — no measurable difference in the CALLBACK cost and
whole-frame marginally worse (within run-to-run noise). Output was pixel-identical to the
`false` build (`cmp.py p4-dynrender-false.png p4-dynrender-true.png` → 0 differing pixels of
2073600), but since it is not faster the flag was **reverted to `false`** per the rule.

Gate: ratio ≤ 1.5 → **PASS (1.220)**, Task 4 may proceed. Also try once with `opts.use_dynamic_rendering = true` in `LibrashaderRuntimeVulkan.cpp` if the device reports Vulkan 1.3 (Adreno 740 driver 512.676 does) and record whether it changes the number; keep whichever is faster only if identical output (`cmp.py` the boot dialog) — otherwise leave `false`.

- [x] **Step 4: Desktop gate + commit**

`./build-unittest/PPSSPPUnitTest all` → 76. Commit `perf: LogGpuProfile dev toggle + Phase 4 GPU-time comparison results` with the trailer.

---

### Task 2: Synchronization validation run (Android, debug APK + Khronos layer)

**Files:**
- Modify: `Common/GPU/Vulkan/VulkanContext.h` (`VulkanInitFlags`: add `SYNC_VALIDATE = (1 << 1)`), `Common/GPU/Vulkan/VulkanContext.cpp` (instance creation ~line 229–244), `GPU/Vulkan/VulkanUtil.cpp` (`VulkanInitFlagsFromConfig` ~line 65), `Core/Config.h/.cpp` (`bool bVulkanSyncValidation`, ini `VulkanSyncValidation`, default false, next to `VulkanDisableImplicitLayers` ~line 368)
- Modify: this plan (results)

- [x] **Step 1: Wire the feature**

In `VulkanContext::CreateInstance` (the block shown around `VkInstanceCreateInfo inst_info`): when `(createInfo_.flags & VulkanInitFlags::VALIDATE) && (createInfo_.flags & VulkanInitFlags::SYNC_VALIDATE)` and the instance extension `VK_EXT_validation_features` is available (`IsInstanceExtensionAvailable(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME)` — enable it via the same path other instance extensions use, ~line 200–210), chain:

```cpp
	VkValidationFeatureEnableEXT syncEnables[] = { VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT };
	VkValidationFeaturesEXT validationFeatures{ VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
	validationFeatures.enabledValidationFeatureCount = 1;
	validationFeatures.pEnabledValidationFeatures = syncEnables;
	if (syncValidation) {
		validationFeatures.pNext = inst_info.pNext;
		inst_info.pNext = &validationFeatures;
		INFO_LOG(Log::G3D, "Vulkan synchronization validation enabled");
	}
```
`VulkanUtil.cpp`: `if (g_Validate && g_Config.bVulkanSyncValidation) flags |= VulkanInitFlags::SYNC_VALIDATE;`. Verify the enum values in `ext/vulkan/vulkan.h` (`VK_EXT_validation_features` is core-promoted-free; the struct exists since 1.1.106).

- [x] **Step 2: Run on device**

Drop `arm64-v8a/libVkLayer_khronos_validation.so` (from `/tmp/vvl` extracted in Phase 3, or re-download `android-binaries-1.4.357.0.zip`) into `android/src/main/jniLibs/arm64-v8a/`, build `librashader-p4b`, install. Ini: `VulkanSyncValidation = True`, log levels 4, preset `lcd-grid-v2-psp-color`, then `crt-royale-downsample`. Baseline with `SlangUseLibrashader = False` for each. Collect `adb logcat -d | grep -E "VKDEBUG|SYNC-HAZARD|Vulkan synchronization validation enabled"` per run into `/tmp/ppsspp-t8/p4-sync-*.log`. Report the set difference (librashader − baseline) of message IDs, with the first occurrence text of each new ID. Expected: none attributable to the CALLBACK step; if a `SYNC-HAZARD` names the callback's images, fix the barrier in `VulkanQueueRunner::PerformCallback` (allowed file) and re-run. Remove the layer, rebuild `librashader-p4c`, reinstall. Restore ini.

- [x] **Step 3: Record + commit**

Table: layer loaded (yes/no), sync validation enabled log line, baseline IDs, librashader IDs, difference, fix applied (if any). Desktop gate 76. Commit `Vulkan: optional synchronization validation (VulkanSyncValidation) + Phase 4 results`.

**Results (2026-09-14, AYN Thor `64dc3c35`, Adreno 740, APK `librashader-p4b`, VVL 1.4.357.0, game `3rd Birthday`, ~40 s at the boot dialog, `InternalResolution = 4`):**

| Run | Preset | `SlangUseLibrashader` | Layer loaded | `Vulkan synchronization validation enabled` | `SYNC-HAZARD` | Distinct message-ID set |
|---|---|---|---|---|---|---|
| `p4-sync-lcd-baseline.log` | `lcd-grid-v2-psp-color` | False (in-tree) | yes | yes | 0 | `{WARNING(perf:-937765618) vkCreateGraphicsPipelines()}` (6 lines) |
| `p4-sync-lcd-libra.log` | `lcd-grid-v2-psp-color` | True (librashader) | yes | yes | 0 | ∅ |
| `p4-sync-royale-baseline.log` | `presets/crt-royale-downsample` | False (in-tree) | yes | yes | 0 | `{WARNING(perf:-937765618) vkCreateGraphicsPipelines()}` (9 lines) |
| `p4-sync-royale-libra.log` | `presets/crt-royale-downsample` | True (librashader) | yes | yes | 0 | ∅ |

**Set difference (librashader − baseline) = ∅ for both presets.** No `SYNC-HAZARD` of any kind in
any run, so nothing to attribute to the CALLBACK step / `VulkanQueueRunner::PerformCallback`, and
**no barrier fix was needed**. The only baseline ID is the pre-existing in-tree-slang pipeline
warning already recorded in Phase 3 (`Vertex attribute at location 2 not consumed by vertex
shader`), which disappears when librashader replaces those pipelines. Screencaps are byte-identical
in size to the Phase 3 validation runs (109,967 / 118,408 / 459,206 / 358,896 B), i.e. rendering was
unaffected.

**Positive control (the chained `VkValidationFeaturesEXT` is really consumed):** a throwaway build
`librashader-p4b-control` added `VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT` as a second entry
of the same `syncEnables[]` array; that run produced 43 best-practices messages
(`perf:1147161417 vkBindImageMemory()`, `perf:280337739 vkBindBufferMemory()`,
`perf:2075859757` LAZILY_ALLOCATED, `validation:1734198062 vkCreateDevice()`, …) that appear in no
other run. The extra entry was reverted before the final build.

Cleanup: layer `.so` removed from `jniLibs`, `librashader-p4c` rebuilt and installed
(`unzip -l | grep -c VkLayer` → `0`; the Android loader no longer logs `added global layer
'VK_LAYER_KHRONOS_validation'`). Ini restored: `VulkanSyncValidation = False`, `G3DLevel = 2` and
`SYSTEMLevel = 2` in both `[Log]` and `[LogDebug]`. Gradle daemon stopped. Desktop gate: 76 tests
passed. No UI checkbox was added — `VulkanSyncValidation` is an ini-only dev knob (it costs a lot of
performance and only does anything in a validation-enabled build).

---

### Task 3: Carried-over Android fixes (GL restart drain, ES 3 request, WARN → INFO)

**Files:**
- Modify: `Common/GPU/OpenGL/GLRenderManager.h` (~line 851: where `skipGLCalls_ = true` is set in `NotifyEmuThreadExit`), `Common/GPU/OpenGL/OpenGLGraphicsContext.h` (~line 37), `android/jni/app-android.cpp` (`displayInit` second-time branch ~line 967 before `EmuThread_Join`), `android/src/org/ppsspp/ppsspp/PpssppActivity.java` (~line 715), `GPU/Common/Slang/LibrashaderFilterChain.cpp` (~line 257 native-size `WARN_LOG` → `INFO_LOG`)

- [ ] **Step 1: GL restart drain**

Root cause (Phase 3 final review): on wake, `GLSurfaceView` creates a new EGL context and `displayInit` (second-time branch) calls `EmuThread_Join`, which drains already-queued frames on the new context while `skipGLCalls_` is still false, so stale texture names reach PPSSPP's steps and the CALLBACK. Add `void GLRenderManager::SetSkipGLCalls() { skipGLCalls_ = true; }` (public; `NotifyEmuThreadExit` already sets the same flag — reuse it internally), `OpenGLGraphicsContext::NotifyContextLost() { renderManager_->SetSkipGLCalls(); }` (add a virtual `NotifyContextLost()` no-op to the `GraphicsContext` base if there is none — check `Common/GraphicsContext.h`), and in `app-android.cpp`'s `displayInit` second-time branch call `graphicsContext->NotifyContextLost();` immediately before `EmuThread_Join(...)`. `ThreadStart` resets `skipGLCalls_` for the new context (`GLRenderManager.cpp:55` — verify). Guard the call so the first-time branch is untouched.

- [ ] **Step 2: ES 3 request**

`PpssppActivity.java` ~line 715: replace `mGLSurfaceView.setEGLContextClientVersion(isVRDevice() ? 3 : 2);` with

```java
			int glesVersion = 2;
			if (isVRDevice()) {
				glesVersion = 3;
			} else {
				ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
				ConfigurationInfo ci = am != null ? am.getDeviceConfigurationInfo() : null;
				if (ci != null && ci.reqGlEsVersion >= 0x30000) {
					glesVersion = 3;
				}
			}
			Log.i(TAG, "Requesting OpenGL ES client version " + glesVersion);
			mGLSurfaceView.setEGLContextClientVersion(glesVersion);
```
Add the imports (`android.app.ActivityManager`, `android.content.pm.ConfigurationInfo`) if missing. PPSSPP's own GL backend already requires ES 3 features, so this only makes the request explicit; ES 2 remains the fallback.

- [ ] **Step 3: WARN → INFO** in `LibrashaderFilterChain.cpp` for the "source is WxH but reported as wxh" message (keep the once-per-chain flag).

- [ ] **Step 4: Verify on device (GL) and commit**

Build `librashader-p4d`, install. Ini `GraphicsBackend = 0 (OPENGL)`, preset `lcd-grid-v2-psp-color`, log levels 4. Launch: log shows `Requesting OpenGL ES client version 3`, GL version 3.2, `Slang chain backend: librashader`. Sleep/wake twice: NO `disabled after librashader error` line, the drop warning still appears at context loss, chain recreated, image filtered. Then Vulkan sleep/wake once to confirm no regression. Restore ini (`GraphicsBackend = 3`). Desktop gate 76 (GL desktop unaffected: run `stock.slangp` on macOS GL once). Commit `android: skip GL calls before the restart drain; request ES 3; demote native-size log`.

---

### Task 4: Remove the in-tree chain and revert the thin3d changes it needed (gated on Task 1)

**Files:**
- Delete: `GPU/Common/Slang/SlangFilterChain.h/.cpp`, `SlangPassCompiler.h/.cpp`, `SlangReflection.h/.cpp`, `SlangResolution.h`
- Modify: `GPU/Common/Slang/ISlangFilterChain.h` (`SlangChainBackend { None, Librashader }`), `GPU/Common/Slang/SlangChainFactory.cpp` (no fallback; `CreateSlangFilterChain` returns nullptr for `None`), `GPU/Common/FramebufferManagerCommon.cpp` (`UpdateSlangChain`: no `bSlangUseLibrashader`, handle nullptr chain, log `Slang chain backend: none` once), `Core/Config.h/.cpp` (remove `bSlangUseLibrashader`), `UI/DeveloperToolsScreen.cpp` (remove the checkbox + header), `GPU/CMakeLists.txt`, `unittest/TestSlangParser.cpp` + `unittest/UnitTest.cpp` (drop tests of deleted code: `SlangResolution`, `SlangSemantics`, `SlangReflection`, `SlangSemanticsPhase2`, `SlangPushConstant`, `SlangPushConstantBraceInComment`, `SlangReflectionIndexOverflow`, and `SlangParamOverride` if it only tested `SlangFilterChain::ResolveParamValue` — otherwise move that helper next to `SlangParamDesc` in `SlangPreset.h` as a free function and keep the test), `unittest/TestLibrashader.cpp` (truth table: `Librashader` iff loaded && supportsNativeCallback && backend ∈ {VULKAN, OPENGL}; else `None`)
- Revert to upstream: `Common/GPU/thin3d.h` (`MAX_TEXTURE_SLOTS = 3`, remove `FramebufferDesc::colorFormat`, restore the `BindFramebufferAsTexture` comment), `Common/GPU/Vulkan/VulkanRenderManager.h` (`MAX_DESC_SET_BINDINGS = 5`), `VulkanRenderManager.cpp`/`VulkanQueueRunner.cpp`/`VulkanFramebuffer.h/.cpp`/`thin3d_vulkan.cpp` (colorFormat parameter, `RPKey::colorFormat`/`_padding`, sRGB render-pass keying, `PushDescriptorSet` sizing back to upstream), `Common/GPU/OpenGL/thin3d_gl.cpp` (drop `sampler3..7` queries), `Common/GPU/D3D11/thin3d_d3d11.cpp` (`MAX_BOUND_TEXTURES = 8`), and any `R8G8B8A8_UNORM_SRGB` `DataFormat` plumbing that no remaining code uses (grep first; `Common/GPU/DataFormat.h` and the per-backend format tables)

- [ ] **Step 1: Delete and re-wire (commit A)**

Remove the files and CMake entries; rewrite `SlangChainFactory.cpp`:

```cpp
SlangChainBackend ChooseSlangChainBackend(bool librashaderLoaded, GPUBackend gpuBackend, bool drawSupportsNativeCallback) {
	if (!librashaderLoaded || !drawSupportsNativeCallback)
		return SlangChainBackend::None;
	if (gpuBackend != GPUBackend::VULKAN && gpuBackend != GPUBackend::OPENGL)
		return SlangChainBackend::None;
	return SlangChainBackend::Librashader;
}
ISlangFilterChain *CreateSlangFilterChain(Draw::DrawContext *draw, SlangChainBackend backend) {
#if USE_LIBRASHADER
	if (backend == SlangChainBackend::Librashader) return new LibrashaderFilterChain(draw);
#endif
	return nullptr;
}
```
(the `userPrefersLibrashader` parameter goes away; update `ISlangFilterChain.h`, the test, and `UpdateSlangChain`, which must treat a nullptr chain as "no chain": clear `slangChainPresetPath_`, log `Slang chain backend: none (librashader not loaded or backend unsupported)` once per reload, and skip `Load`). Update the tests, build, run all — record the new count. Commit `slang: remove the in-tree filter chain; librashader is the only rendering core`.

- [ ] **Step 2: Revert thin3d/Vulkan/GL/D3D11 (commit B)**

Apply the reverts listed above. Method: for each file, `git diff upstream/master -- <file>` and remove every hunk that is not part of the native-callback work (CALLBACK step, `RunNativeCallback`, `SupportsNativeCallback`, `NativeCallbackInfo`, `VULKAN_GET_INSTANCE_PROC_ADDR`, `GL_GET_PROC_ADDRESS`, the GL restore function, Task 3's `SetSkipGLCalls`). After the change, `git diff upstream/master --stat -- Common/GPU` must list only those additions. Build all targets; `LibrashaderFilterChain` creates its framebuffers without `colorFormat` already, so nothing else should break. Run the unit tests. Commit `thin3d: revert the texture-slot/descriptor caps and sRGB render-pass keying added for the in-tree slang chain`.

- [ ] **Step 3: Verify on macOS (Vulkan + GL) and Android**

macOS: `stock.slangp` and `lcd-psp-matrix.slangp` on Vulkan and GL with librashader → screenshots `cmp.py`-identical to the Phase 2 captures (`/tmp/ppsspp-t8/shot-p2-*`); with the dylib renamed away → `Slang chain backend: none`, raw image, no crash. Android: build `librashader-p4e`, install, `lcd-grid-v2-psp-color` on Vulkan renders; toggle key no longer exists (ini key ignored). Record in a table. Restore ini.

- [ ] **Step 4: Docs + commit C**

Spec §5 "made redundant" → "removed in Phase 4"; §6.6 (selector without toggle); §10 row 4 progress; `docs/superpowers/librashader-build.md` remove toggle references ("without the library, slang shaders are off"). Commit.

---

### Task 5: Windows build over SSH (vcxproj coverage, PPSSPP.sln, librashader.dll, Vulkan/GL smoke)

**Files:**
- Modify: `GPU/GPU.vcxproj` + `.filters` (add `Common\Slang\*.cpp/.h` that remain: `ISlangFilterChain.h`, `SlangChainFactory.cpp`, `SlangpParser.cpp/.h`, `SlangPreset.h`, `LibrashaderFilterChain.cpp/.h`, `LibrashaderRuntime.cpp/.h`, `LibrashaderRuntimeVulkan.cpp`, `LibrashaderRuntimeOpenGL.cpp`), `Common/Common.vcxproj` + `.filters` (`GPU\Librashader\LibrashaderLoader.cpp/.h`), and add `USE_LIBRASHADER=1` to `PreprocessorDefinitions` plus `..\ext\librashader\include` to `AdditionalIncludeDirectories` for every configuration of `Common`, `GPU`, `UI` (`UI/UI.vcxproj`, for `DeveloperToolsScreen.cpp`/`SlangShaderScreen.cpp`) and `Windows/PPSSPP.vcxproj` if it compiles any file including `LibrashaderLoader.h`. Follow the existing entries' style (e.g. `Core/Core.vcxproj:922-924`). Also check `UWP/*.vcxproj` compile the same sources; if they do, add the files with `USE_LIBRASHADER=0` there (UWP is OFF per spec).
- Create on the Windows host: checkout at `C:\Users\Ilya\source\ppsspp`, `librashader.dll` built with cargo.

- [ ] **Step 1: Project files (edit locally, commit, push the branch; the Windows host pulls)**

Edit the `.vcxproj`/`.filters` files as XML (keep CRLF line endings and the existing indentation; verify with `git diff --stat` that only the intended lines changed and `file GPU/GPU.vcxproj` still reports CRLF). Commit `windows: add Slang/Librashader sources and USE_LIBRASHADER to the VS projects`, `git push -u origin feature/librashader-phase4-removal` (this push is required for the Windows host to build; it is a feature branch, not master).

- [ ] **Step 2: Clone + build on Windows**

PowerShell over SSH (pipe scripts): `git clone --recursive https://github.com/ilya-slalom/ppsspp.git C:\Users\Ilya\source\ppsspp; cd ...; git checkout feature/librashader-phase4-removal`. Build: `& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" Windows\PPSSPP.sln /m /p:Configuration=Release /p:Platform=x64 /v:m > C:\Users\Ilya\source\ppsspp-build.log 2>&1` started detached (`Start-Process -NoNewWindow -RedirectStandardOutput ...`), polled every ~4 min via `Get-Content -Tail 5`. Expected artifact `Windows\x64\Release\PPSSPPWindows64.exe` (check the exact name in `Windows/PPSSPP.vcxproj` `<TargetName>`). Fix compile errors that are ours (Slang/Librashader sources under MSVC: e.g. missing `<cstring>` noted in Phase 1, `strlen`, `ssize_t`, designated initializers); pre-existing upstream MSVC breakage is out of scope — report it.

- [ ] **Step 3: librashader.dll**

On Windows: `git clone --depth 1 --branch librashader-v0.12.0 https://github.com/SnowflakePowered/librashader.git C:\Users\Ilya\source\librashader; cargo build -p librashader-capi --release --no-default-features --features runtime-vulkan,runtime-opengl,runtime-d3d11` → `target\release\librashader_capi.dll` → copy to `Windows\x64\Release\librashader.dll`. Verify exports with `dumpbin /exports` (from the VS developer prompt: `& "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\dumpbin.exe" /exports librashader.dll | Select-String libra_ | Measure-Object`). Document in the build doc (Windows section: cargo command, rename, placement next to the exe, `LIBRASHADER_PATH` env).

- [ ] **Step 4: Windows Vulkan + GL smoke**

The Windows host has a display session (RTX 4090). Config lives in `%USERPROFILE%\Documents\PPSSPP\PSP\SYSTEM\ppsspp.ini` (or `memstick` beside the exe — check `Windows/main.cpp` `GetExeDirectory` handling; a `memstick\` folder beside the exe makes it portable — create one to avoid touching the user's profile). Copy `/tmp/ppsspp-t8/locoroco.ppdmp` and `assets/shaders/slang_test/*.slangp/*.slang/*.png` over `scp`. Launch `PPSSPPWindows64.exe <dump>` via `Start-Process` in the interactive session (`schtasks /create ... /it` or `psexec -i` may be needed for a GUI session from SSH; if no GUI launch path works, record NOT RUN with the reason). Log file: enable `FileLogging = True` in the ini so `ppsspp.log` (or `PSP\SYSTEM\ppsspp.log`) captures `librashader loaded`, `Slang chain backend: librashader`. Screenshot: PPSSPP's own screenshot key or `Windows.Graphics.Capture` via a small PowerShell using `System.Drawing` (`[System.Windows.Forms.Screen]::PrimaryScreen` + `Graphics.CopyFromScreen`). Vulkan and GL runs with `stock.slangp` and `lcd-psp-matrix.slangp`; compare visually with the macOS captures.

- [ ] **Step 5: Record + commit**

Table (vcxproj build, dll exports, Vulkan load/render, GL load/render, notes). Commit docs.

---

### Task 6: D3D11 adapter (Windows)

**Files:**
- Modify: `Common/GPU/thin3d.h` (`NativeCallbackInfo`: add `uint64_t srcView = 0;  // D3D11: ID3D11ShaderResourceView*` and `uint64_t dstView = 0;  // D3D11: ID3D11RenderTargetView*`; add `NativeObject::D3D11_DEVICE_CONTEXT` only if `NativeObject::CONTEXT` on D3D11 does not already return the `ID3D11DeviceContext*` — it does per `thin3d_d3d11.cpp:1927`, so probably nothing), `Common/GPU/D3D11/thin3d_d3d11.cpp` (`SupportsNativeCallback() const override { return true; }`, `RunNativeCallback` implementation), `Common/GPU/Librashader/LibrashaderLoader.h` (`#if PPSSPP_PLATFORM(WINDOWS) #define LIBRA_RUNTIME_D3D11 #endif` before the include — the header's D3D11 section is guarded by `_WIN32 && LIBRA_RUNTIME_D3D11` and includes `<d3d11.h>`), `GPU/Common/Slang/LibrashaderRuntime.h/.cpp` (declare/select `CreateLibrashaderRuntimeD3D11()` under `PPSSPP_PLATFORM(WINDOWS)`), create `GPU/Common/Slang/LibrashaderRuntimeD3D11.cpp`, `GPU/Common/Slang/SlangChainFactory.cpp` + `unittest/TestLibrashader.cpp` (admit `DIRECT3D11`), `GPU/GPU.vcxproj(.filters)`, `GPU/CMakeLists.txt` (compile the D3D11 adapter only on Windows, matching how other D3D11 files are gated)

**Interfaces:**
- librashader D3D11 C API (vendored header, lines ~1066–1128): `libra_d3d11_filter_chain_create(libra_shader_preset_t *preset, ID3D11Device *device, const filter_chain_d3d11_opt_t *options, libra_d3d11_filter_chain_t *out)`; `libra_d3d11_filter_chain_frame(libra_d3d11_filter_chain_t *chain, ID3D11DeviceContext *device_context, size_t frame_count, ID3D11ShaderResourceView *image, ID3D11RenderTargetView *out, const libra_viewport_t *viewport, const float *mvp, const frame_d3d11_opt_t *opt)`; `_set_param`, `_free`. `filter_chain_d3d11_opt_t { version; force_no_mipmaps; disable_cache; }`. Preset runtime hint `LIBRA_PRESET_CTX_RUNTIME_D3D11`.

- [ ] **Step 1: thin3d D3D11 `RunNativeCallback` (immediate mode)**

`D3D11DrawContext::RunNativeCallback(src, dst, fn, tag)`: `D3D11Framebuffer *s = (D3D11Framebuffer *)src, *d = ...;` fill `NativeCallbackInfo` with `srcView = (uint64_t)s->colorSRView.Get()`, `dstView = (uint64_t)d->colorRTView.Get()`, `srcFormat/dstFormat = DXGI_FORMAT_R8G8B8A8_UNORM` (28), widths/heights (`D3D11Framebuffer` has `Width()/Height()` or public members — check ~line 65–85), `cmdBuffer = (uint64_t)context_`; unbind the framebuffer's SRV from any pixel-shader slot it may be bound to and unbind the current RTV (`context_->OMSetRenderTargets(0, nullptr, nullptr)`) before calling `fn` to avoid read/write hazards; call `fn(info)` synchronously; afterwards restore thin3d's assumptions: re-issue `OMSetRenderTargets` with `curRenderTargetView_`/`curDepthStencilView_`, and invalidate the cached pipeline state so `ApplyCurrentState` re-sends everything (`curBlend_ = nullptr; curDepthStencil_ = nullptr; curRaster_ = nullptr; curInputLayout_ = nullptr; curVS_ = nullptr; curPS_ = nullptr; curGS_ = nullptr; curTopology_ = ...invalid; curPipeline_ = nullptr;` — use the exact member names at ~lines 225–245 and mirror what `BindPipeline`/`ApplyCurrentState` compare against), plus clear the sampler/texture slot caches if any exist (`nextTextures_`/`nextSamplers_` are applied per draw — confirm), reset viewport/scissor caches (`RSSetViewports` is re-sent per `SetViewport`? check) — the goal: the next thin3d draw is correct regardless of what librashader changed. Return true.

- [ ] **Step 2: Adapter**

`LibrashaderRuntimeD3D11.cpp` mirroring the GL adapter: `Init` grabs `ID3D11Device *` via `NativeObject::DEVICE`; `MakeFrameCallback` lambda: create on first call `libra_d3d11_filter_chain_create(&rs->preset, device, &opts, &rs->d3d11Chain)` (`opts.version = LIBRASHADER_CURRENT_VERSION; force_no_mipmaps = false; disable_cache = false;`), then `set_param` loop and `libra_d3d11_filter_chain_frame(&rs->d3d11Chain, (ID3D11DeviceContext *)info.cmdBuffer, frameCount, (ID3D11ShaderResourceView *)info.srcView, (ID3D11RenderTargetView *)info.dstView, &vp, nullptr, &fopts)` with the same frame options as the other adapters; `QueueFree`: D3D11 is immediate-mode and thread-agnostic (the device is free-threaded) → call `libra_d3d11_filter_chain_free` directly on the emu thread (documented in spec §8), `preset_free` if non-null. `LibrashaderRenderState` gains `libra_d3d11_filter_chain_t d3d11Chain` under `#if PPSSPP_PLATFORM(WINDOWS)`. Selector admits `DIRECT3D11` (test row: `(true, DIRECT3D11, true) == Librashader`; `DIRECT3D9 → None`). Native input blit for history presets uses thin3d `BlitFramebuffer`, which D3D11 implements — no change.

- [ ] **Step 3: Build on Windows, verify**

Push the branch; on the Windows host pull, rebuild (incremental), run with `GraphicsBackend = 1 (DIRECT3D11)`: `librashader loaded`, `Slang chain backend: librashader`, `stock.slangp` and `lcd-psp-matrix.slangp` render (screenshots), compare with the Vulkan run from Task 5; toggle nothing (no toggle exists) — rename the dll → raw image. PPSSPP's own UI/OSD must be intact after the callback (state restore). Record a table. If the Windows GUI cannot be driven from SSH, record NOT RUN with the reason and keep the code compile-verified only.

- [ ] **Step 4: Commit**

`slang: D3D11 runtime adapter for LibrashaderFilterChain (Windows)` + `thin3d: D3D11 RunNativeCallback` + docs (spec §6.2 D3D11 paragraph, §10 row 4, build doc Windows section).

---

### Task 7: Packaging leftovers and final docs

**Files:**
- Modify: `android/build.gradle.kts` (prune `jniLibs` per flavor: the existing `ndk.abiFilters` already restrict release flavors; for dev builds add a Gradle property `-PlibrashaderAbi=<abi>` that sets `packaging.jniLibs.excludes` for the other ABIs — or document that the script's single-ABI mode is the dev path; pick the smaller change and document it), `.github/workflows/manual_generate_apk.yml` (a step before the Gradle build: install Rust stable + `cargo-ndk` + the three targets, run `android/build-librashader.sh`; mark the job as unverified locally), `docs/superpowers/librashader-build.md`, spec §10 (Phase 4 row: result), `README`-level note in `android/src/main/jniLibs/README.txt` if wording changed
- Optional check: load-test `armeabi-v7a`/`x86_64` `librashader.so` in the macOS Android emulator (an AVD `dolphin_test` exists at `~/.android/avd`; `/opt/homebrew/share/android-commandlinetools/emulator/emulator -avd dolphin_test`) — install the x86_64 APK (`-Pandroid.injected.build.abi=x86_64`), launch, confirm `librashader loaded` in logcat; record NOT RUN if the AVD lacks GPU/Vulkan or does not boot in ~3 minutes.

- [ ] Steps: implement, build the APK once to confirm Gradle still packages correctly, run the emulator check, update docs/spec, commit `android/ci: jniLibs ABI handling, cargo ndk step in manual_generate_apk.yml, Phase 4 docs`.

---

## Out of scope

Upstreaming to hrydgard/ppsspp; iOS/UWP/libretro librashader packaging; replacing the in-tree `.slangp` parser used for parameter enumeration with librashader's parameter API.
