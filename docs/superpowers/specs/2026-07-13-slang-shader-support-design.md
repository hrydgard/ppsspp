# Design: RetroArch-compatible (slang) Shader Support for PPSSPP

- **Status:** Draft for review
- **Date:** 2026-07-13
- **Branch:** `feature/slang-shader-support`
- **Author:** design doc (Claude-assisted)

## 1. Summary

Add a new, RetroArch-compatible **slang** post-processing sub-system to PPSSPP, sitting
alongside the existing post-shader feature rather than replacing it. The feature lets a user:

1. Download the zipped slang-shaders package from the libretro buildbot.
2. Unpack and import it into the user's runtime shader directory.
3. Browse the imported shaders by **category** (crt, handheld, presets, …) and select a preset.
4. Render that preset through a **fully slang-compatible filter-chain pipeline** (multi-pass,
   reflection-driven uniform binding, history/feedback/LUT textures, per-pass scale/format).
5. Do all of the above through UI available on every platform PPSSPP supports.

The design leans on the fact that PPSSPP **already vendors and initializes the exact toolchain
RetroArch uses** — `glslang` (GLSL→SPIR-V) and `SPIRV-Cross` (SPIR-V→GLSL/GLES/HLSL) live in
[`ext/glslang/`](../../../ext/glslang/) and [`ext/SPIRV-Cross/`](../../../ext/SPIRV-Cross/), wired
up in [`Common/GPU/ShaderTranslation.cpp`](../../../Common/GPU/ShaderTranslation.cpp) and
initialized at startup in [`UI/NativeApp.cpp`](../../../UI/NativeApp.cpp). The hard, risky part of
the port (portable multi-backend shader cross-compilation) is therefore already solved in-tree.

## 2. Goals & Non-Goals

### Goals
- Consume real, unmodified `.slang` / `.slangp` files as shipped by libretro.
- Parse `.slangp` presets and the full slang uniform/semantic model via SPIR-V reflection.
- Support multi-pass chains with per-pass scale types, sRGB/float framebuffers, mipmaps, wrap
  modes, `OriginalHistory#`, `PassFeedback#`, and PNG LUT textures.
- Download + unpack + import the buildbot archive with progress UI, no app rebuild.
- Category-based browsing scalable to thousands of shaders, on all platforms.
- Zero regression to the existing post-shader system; both coexist and are independently
  selectable.

### Non-Goals
- Not replacing or deprecating the existing `.fsh`/`.vsh` post-shaders or `Type=Texture`
  compute upscalers.
- Not supporting legacy libretro `.cg`/`.cgp` or `.glsl`/`.glslp` formats (slang only).
- Not implementing RetroArch runtime-parameter presets-of-presets nuances beyond the documented
  `.slangp` keyword set (see §7 for the explicit supported subset).
- Not shipping the shader archive inside the binary (it is downloaded at runtime).

## 3. Key Decisions (locked)

| Decision | Choice | Rationale |
|---|---|---|
| Pipeline architecture | **New parallel subsystem** (`SlangFilterChain`) that emits `Draw::Pipeline`s into the existing present flow | Slang semantics diverge too far from `ShaderInfo` to overload cleanly; isolates risk. |
| Storage target | **User data dir at runtime** — a `slang/` subtree under `DIRECTORY_CUSTOM_SHADERS` | No rebuild, all platforms, matches how custom shaders already load. |
| Backend scope (v1) | **Vulkan-first**, others phased | Slang is native Vulkan GLSL; cleanest path and earliest working conformance target. |
| Delivery | **Full vision, phased roadmap** | One design, incremental milestones (see §9). |
| Download source | **Buildbot nightly zip** (`shaders_slang.zip`) | One HTTP GET + unzip; matches the request; yields the full categorized tree. |
| UI model | **Two-level: category → shader**, with search | Scales to thousands; preserves the archive's category organization. |

## 4. Existing Infrastructure We Build On

| Concern | Existing PPSSPP asset | Location |
|---|---|---|
| GLSL→SPIR-V | vendored `glslang` | [`ext/glslang/`](../../../ext/glslang/), init in [`UI/NativeApp.cpp`](../../../UI/NativeApp.cpp) via `ShaderTranslationInit()` |
| SPIR-V→GLSL/GLES/HLSL + reflection | vendored `SPIRV-Cross` | [`ext/SPIRV-Cross/`](../../../ext/SPIRV-Cross/), used in [`Common/GPU/ShaderTranslation.cpp`](../../../Common/GPU/ShaderTranslation.cpp) |
| Multi-backend GPU abstraction | `thin3d` (Draw::) | [`Common/GPU/thin3d.h`](../../../Common/GPU/thin3d.h) |
| Multi-pass present + FBO ping-pong | `PresentationCommon` | [`GPU/Common/PresentationCommon.cpp`](../../../GPU/Common/PresentationCommon.cpp) |
| Present entry point | `FramebufferManagerCommon::CopyDisplayToOutput` | [`GPU/Common/FramebufferManagerCommon.cpp`](../../../GPU/Common/FramebufferManagerCommon.cpp) |
| HTTP download + progress | `g_DownloadManager.StartDownload` | [`Common/Net/HTTPRequest.h`](../../../Common/Net/HTTPRequest.h) |
| Zip download-and-install precedent | `GameManager::DownloadAndInstall`, `InstallZipScreen` | [`Core/Util/GameManager.cpp`](../../../Core/Util/GameManager.cpp), [`UI/InstallZipScreen.cpp`](../../../UI/InstallZipScreen.cpp) |
| Zip reading | `ZipFileReader` (libzip) | [`Common/File/VFS/ZipFileReader.cpp`](../../../Common/File/VFS/ZipFileReader.cpp) |
| Store-style remote content UI | `Store.cpp` | [`UI/Store.cpp`](../../../UI/Store.cpp) |
| PNG decode (for LUTs) | existing image loader | `Common/Data/Format` (PNG) |

## 5. Architecture Overview

Five cooperating components. Only the rendering core touches the GPU; the rest are file/UI/network.

```
                 ┌─────────────────────────────────────────────────────┐
                 │                      UI layer                         │
                 │  SlangShaderScreen (download / category → shader)     │
                 └───────────────┬──────────────────────┬───────────────┘
                                 │ selects preset        │ triggers import
                                 ▼                        ▼
        ┌────────────────────────────────┐   ┌───────────────────────────────┐
        │  SlangPresetLibrary            │   │  SlangPackageImporter          │
        │  - scans slang/ dir tree       │   │  - download buildbot zip       │
        │  - category + preset index     │   │  - unpack into slang/          │
        │  - .slangp discovery/metadata  │   │  - progress + integrity        │
        └───────────────┬────────────────┘   └───────────────────────────────┘
                        │ preset path
                        ▼
        ┌────────────────────────────────────────────────────────────────┐
        │  SlangFilterChain  (rendering core, new parallel subsystem)      │
        │  ┌──────────────┐  ┌───────────────┐  ┌──────────────────────┐  │
        │  │ SlangpParser │→ │ SlangPassCompiler│→│ FilterChainRuntime  │  │
        │  │ (.slangp)    │  │ glslang+SPIRV-  │  │ per-pass FBOs, scale │  │
        │  │              │  │ Cross+reflection │  │ history/feedback/LUT │  │
        │  └──────────────┘  └───────────────┘  └──────────────────────┘  │
        └────────────────────────────┬─────────────────────────────────────┘
                                     │ Draw::Pipeline[] + resource bindings
                                     ▼
             FramebufferManagerCommon::CopyDisplayToOutput  (unchanged entry point,
             extended to delegate to SlangFilterChain when a slang preset is active)
```

### 5.1 Integration seam with the existing present path
`FramebufferManagerCommon` owns `PresentationCommon`. We add an owned, optional
`SlangFilterChain *slangChain_`. At present time:
- if a slang preset is active and its chain compiled successfully → the source framebuffer is fed
  to `slangChain_`, which runs its passes and produces a final texture that is then blitted to the
  backbuffer using the same output-rect math already in `PresentationCommon` (aspect, rotation,
  cardboard, insets are reused, not reimplemented);
- otherwise → the existing `PresentationCommon` post-shader path runs unchanged.

The two systems are mutually exclusive per-frame and never both process the image. This keeps the
existing feature a zero-risk fallback.

## 6. Component 1 — SlangFilterChain (rendering core)

New directory: `GPU/Common/Slang/`.

### 6.1 SlangpParser (`SlangpParser.{h,cpp}`)
- Parses the INI-style `.slangp` (reuse PPSSPP's `IniFile`). Zero-based `shaderN` indexing.
- Resolves relative `shaderN` / LUT / `#include` paths against the preset's directory.
- Produces a `SlangPreset` struct: ordered list of `SlangPassDesc` + LUT declarations + parameter
  overrides + global `feedback_pass`.
- **Supported keyword set (v1):** `shaders`, `shaderN`, `filter_linearN`, `scale_typeN` /
  `scale_type_xN` / `scale_type_yN`, `scaleN` / `scale_xN` / `scale_yN`, `srgb_framebufferN`,
  `float_framebufferN`, `aliasN`, `mipmap_inputN`, `wrap_modeN`, `frame_count_modN`,
  `feedback_pass`, `textures` + per-LUT `_linear`/`_mipmap`/`_wrap_mode`, `parameters` + per-param
  overrides. Unknown keys are logged and ignored (forward-compat).

### 6.2 SlangPassCompiler (`SlangPassCompiler.{h,cpp}`)
- Splits a `.slang` source on `#pragma stage vertex` / `#pragma stage fragment`; resolves
  `#include` / `#pragma include_optional`; extracts `#pragma name`, `#pragma format`,
  `#pragma parameter`.
- Compiles each stage with glslang using **Vulkan rules** (slang is real Vulkan GLSL — it bypasses
  the string-rewriting `ConvertToVulkanGLSL` path entirely).
- Reflects the SPIR-V with SPIRV-Cross to build a `PassReflection`:
  - UBO + push_constant member names, offsets, sizes;
  - every `sampler2D` and its binding;
  - maps each member name to a **semantic** (built-in / pass-alias / LUT / user-parameter).
- Cross-compiles the SPIR-V to the active backend dialect via SPIRV-Cross (Vulkan consumes SPIR-V
  directly; GL/GLES/D3D11 reuse the existing SPIRV-Cross options from `ShaderTranslation.cpp`).
- Emits a `Draw::ShaderModule` pair and a `Draw::Pipeline` per pass.

### 6.3 Semantic binding (the core new mechanism)
A `SemanticMap` resolves slang member names to values/resources each frame. Two families:

- **By-name value semantics** packed into the per-pass UBO/push block by reflected offset:
  `MVP`, `FrameCount` (+ `frame_count_mod`), `FrameDirection`, `Rotation`, and every `*Size`
  companion (`SourceSize`, `OriginalSize`, `OutputSize`, `FinalViewportSize`,
  `OriginalHistorySize#`, `PassOutputSize#`, `PassFeedbackSize#`, `<Alias>Size`, `<LUT>Size`),
  each laid out as `(w, h, 1/w, 1/h)`. Plus user `#pragma parameter` floats.
- **By-name texture semantics** bound to sampler slots: `Original`, `Source`,
  `OriginalHistory#`, `PassOutput#` / `<Alias>`, `PassFeedback#` / `<Alias>Feedback`, LUT names.

Because binding is by name, the per-pass code packs values into a scratch buffer at reflected
offsets — no fixed `PostShaderUniforms` struct is used for slang.

### 6.4 FilterChainRuntime (`SlangFilterChain.{h,cpp}`)
Owns and executes the chain each frame:
- **Resolution resolution:** compute each pass's output size from `scale_type`/`scale`
  (source = ×input, viewport = ×final display rect, absolute = pixels), independently per axis.
- **Framebuffer pool:** one `Draw::Framebuffer` per pass, with format chosen from
  `srgb_framebufferN` / `float_framebufferN` / `#pragma format` (R8G8B8A8_UNORM default,
  R8G8B8A8_SRGB, R16G16B16A16_SFLOAT, etc.). Mipmap generation when `mipmap_input`. Sampler
  wrap/filter per pass from `wrap_modeN` / `filter_linearN`.
- **History ring:** `OriginalHistory#` depth = max index referenced by reflection across all
  passes; N input frames retained (frames before start = transparent black).
- **Feedback buffers:** for the pass named by `feedback_pass` (and any alias-feedback references),
  keep the previous frame's output in a dedicated buffer, ping-ponged with the live output.
- **LUTs:** decode declared PNGs once at compile time into `Draw::Texture`s.
- **Execution:** for each pass, resolve `SemanticMap`, bind textures + UBO/push, draw a full-screen
  quad into the pass FBO; the final pass output is handed back for blit-to-screen.

### 6.5 DeviceLost / DeviceRestore
Mirror `PresentationCommon`'s handling: release all `Draw::` objects on device loss and rebuild
from the parsed `SlangPreset` on restore. Compilation is cached to disk-independent state; a
recompile on restore is acceptable.

## 7. Feature coverage matrix (slang spec vs. this design)

| Slang feature | v1 support | Notes |
|---|---|---|
| Multi-pass chain | ✅ | Arbitrary N passes. |
| `#pragma stage` single-file source | ✅ | Split in SlangPassCompiler. |
| `#include` / `include_optional` | ✅ | Resolved pre-compile. |
| Reflection name-binding | ✅ | Core mechanism (§6.3). |
| `#pragma parameter` (unlimited) | ✅ | UI exposes them (§10). |
| `Source`, `Original` | ✅ | |
| `OriginalHistory#` | ✅ | Depth from reflection. |
| `PassOutput#` / aliases | ✅ | Causal check enforced. |
| `PassFeedback#` / feedback_pass | ✅ | One-frame feedback buffers. |
| LUT PNG textures | ✅ | Decoded at compile. |
| scale types (source/viewport/absolute, per-axis) | ✅ | |
| srgb / float framebuffers, `#pragma format` | ✅ | Subject to backend format availability. |
| mipmap_input, wrap_mode, filter_linear | ✅ | |
| `frame_count_mod`, `FrameDirection`, `Rotation` | ✅ | FrameDirection = 1 (no rewind in PPSSPP). |
| Runtime-adjustable params persisted per preset | ✅ | Stored in config (see §10). |
| `.cg`/`.glsl` legacy formats | ❌ | Out of scope. |

## 8. Component 2 — SlangPackageImporter (download + unpack + import)

New: `Core/Slang/SlangPackageImporter.{h,cpp}` (network/file only, no GPU).

- **Download:** `g_DownloadManager.StartDownload(buildbotUrl, destZipPath, RequestFlags::ProgressBar…)`
  exactly as `Store.cpp` / `GameManager` already do; default URL is the buildbot
  `shaders_slang.zip`, overridable in config for mirrors.
- **Unpack:** open the downloaded zip with `ZipFileReader`; extract the shader tree into
  `DIRECTORY_CUSTOM_SHADERS/slang/`. Reuse the extraction/whitelist pattern from
  `GameManager`/`InstallZipScreen` (path sanitization against zip-slip; only extract
  `.slang`/`.slangp`/`.inc`/`.png` and directories).
- **Atomicity & versioning:** extract into a temp dir, then swap into place; write a small
  `slang/manifest.json` recording source URL, timestamp, and file count so the UI can show
  "installed / update available" and support re-import.
- **Integrity:** verify HTTP success + zip open; on partial failure, discard the temp dir and
  surface an error (same error-surfacing path as Store downloads).
- **Progress:** the importer exposes progress state; the UI polls it (mirrors `GameManager`).

## 9. Phased delivery / milestones

Each phase ends at a demonstrable, mergeable milestone.

- **Phase 1 — Rendering core (Vulkan):** SlangpParser + SlangPassCompiler + FilterChainRuntime;
  wire into `FramebufferManagerCommon`; load a preset from a **manually-placed** `slang/` folder
  (no download UI yet). Milestone: run crt-lottes and a simple multi-pass chain on Vulkan.
- **Phase 2 — Full slang conformance (Vulkan):** history, feedback, LUTs, sRGB/float, per-axis
  scale, mipmaps. Milestone: **crt-royale renders correctly** (12 passes, 6 LUTs) — the
  conformance target.
- **Phase 3 — Import pipeline:** SlangPackageImporter (download + unpack + import) + import UI.
  Milestone: one-click download from buildbot and browse installed tree.
- **Phase 4 — Category browsing UI + parameters:** SlangPresetLibrary + SlangShaderScreen (§10).
  Milestone: category→preset selection with search and per-preset parameter sliders.
- **Phase 5 — Backend expansion:** D3D11, then OpenGL/GLES, via SPIRV-Cross cross-compile;
  handle per-backend framebuffer-format capability fallbacks. Milestone: feature parity on all
  desktop + mobile backends.

## 10. Component 3+4 — UI (all platforms)

PPSSPP's UI is a single cross-platform layer (`UI/` on top of `Common/UI`), so one implementation
covers Windows/macOS/Linux/Android/iOS/etc. No per-platform native UI is required.

### 10.1 SlangPresetLibrary (`Core/Slang/SlangPresetLibrary.{h,cpp}`)
- Scans `slang/` recursively; the **first path segment under `slang/` is the category** (crt,
  handheld, presets, bezel, …). Indexes `.slangp` presets per category with display name (from
  filename / preset metadata).
- Provides: `GetCategories()`, `GetPresets(category)`, `FindPreset(id)`, and a fuzzy `Search(text)`
  across all categories. Cheap to rebuild; rebuilt on import completion.

### 10.2 SlangShaderScreen (`UI/SlangShaderScreen.{h,cpp}`)
Two-level browser reached from Graphics settings ("RetroArch (slang) shaders…"):
- **Top:** Import/Update button (progress bar; drives SlangPackageImporter), a search box, and a
  category list. Empty state prompts the first import.
- **Category view:** presets in that category as a selectable list; selecting one activates it and
  shows its `#pragma parameter` sliders (unlimited, unlike the legacy 4-slider cap).
- **Active-shader indicator** and a "None / disable" entry. Selecting a slang preset clears any
  active legacy post-shader and vice-versa (mutual exclusivity from §5.1).

### 10.3 Config (`Core/Config`)
New keys: `sSlangShaderPreset` (active preset id, empty = none), `sSlangBuildbotUrl` (override),
and a per-preset parameter map `mSlangParams` (preset id → name→value) so parameter tweaks persist.
Legacy `vPostShaderNames` is untouched.

### 10.4 Settings entry point
Add one row in `UI/GameSettingsScreen.cpp` Graphics section that pushes `SlangShaderScreen`,
adjacent to the existing post-shader chooser, clearly labeled as the RetroArch-compatible system.

## 11. Data flow (end to end)

1. User opens **SlangShaderScreen** → taps **Import** → `SlangPackageImporter` downloads
   `shaders_slang.zip`, verifies, extracts into `slang/`, writes `manifest.json`.
2. `SlangPresetLibrary` rebuilds its category/preset index from the tree.
3. User picks a **category**, then a **preset** → `sSlangShaderPreset` is set; parameter sliders
   populate from the preset's `#pragma parameter` list.
4. On next frame, `FramebufferManagerCommon` sees an active slang preset and (lazily, once) asks
   `SlangFilterChain` to parse+compile it; on success it becomes `slangChain_`.
5. Each frame: source framebuffer → `SlangFilterChain` runs its passes (semantic binding, history,
   feedback, LUTs) → final texture → blitted to backbuffer via the reused output-rect math.

## 12. Error handling

- **Download/unpack failure:** temp dir discarded, existing install untouched, error shown in UI
  (reuse Store/GameManager error surfacing). Retryable.
- **Preset parse failure / unsupported keyword:** unknown keys ignored+logged; fatal parse errors
  disable the preset and show a one-line error (reuse `PresentationCommon::ShowPostShaderError`
  style), falling back to no post-processing.
- **Shader compile/reflection failure:** the failing pass's error (glslang/SPIRV-Cross message) is
  logged; the whole preset is disabled with a UI notice rather than rendering garbage.
- **Unsupported framebuffer format on a backend:** fall back to the nearest supported format and
  log a warning (e.g. float→unorm), so the preset still renders (possibly with reduced fidelity)
  rather than failing hard.
- **Device lost:** release + rebuild (§6.5).

## 13. Testing strategy

- **Unit — SlangpParser:** table-driven tests over hand-written `.slangp` snippets covering every
  keyword in §6.1, per-axis scale, LUT declarations, alias resolution, unknown-key tolerance.
- **Unit — SlangPassCompiler reflection:** compile small `.slang` fixtures and assert the derived
  `PassReflection` (member→semantic map, sampler bindings, history depth deduction).
- **Unit — resolution resolution:** given scale descs + input/viewport sizes, assert per-pass
  output dimensions (source/viewport/absolute, per-axis).
- **Unit — SlangPackageImporter:** extract a small crafted zip into a temp dir; assert tree layout,
  zip-slip rejection, temp→final swap, manifest contents. Network mocked.
- **Unit — SlangPresetLibrary:** build a fake `slang/` tree; assert category grouping and search.
- **Integration (Vulkan, headless where possible):** render a 1-pass and a multi-pass preset to an
  offscreen target and checksum/compare against a reference; crt-royale as the conformance target
  (Phase 2 gate).
- **Regression:** existing post-shader tests/paths must be untouched and continue to pass.

Follow TDD per phase: failing test → minimal implementation → green, per the repo workflow.

## 14. Risks & mitigations

| Risk | Mitigation |
|---|---|
| glslang/SPIRV-Cross version in-tree lacks a feature real shaders use | Assess against a shader corpus in Phase 1; bump the vendored ext if needed (isolated change). |
| Backend format gaps (sRGB/float RTs on GLES) | Capability query + graceful fallback (§12); Vulkan-first de-risks. |
| Archive size / slow buildbot | Progress UI + resumable/retryable download; allow mirror URL override. |
| Shader corpus performance (heavy CRT chains) | Per-preset it's the user's choice; expose pass count / cost in UI later if needed. |
| Scope creep into legacy `.cgp`/`.glslp` | Explicit non-goal; slang only. |

## 15. Open questions (to resolve during planning)

- Exact buildbot URL/path to pin as the default (nightly vs. stable snapshot).
- Whether to cache compiled SPIR-V/pipelines to disk to speed up re-activation (defer to a later
  optimization pass; not required for correctness).
- iOS download/storage sandbox specifics for the extracted tree (validate in Phase 3).
