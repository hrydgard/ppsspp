# Design: librashader-backed slang shader rendering for PPSSPP

- **Status:** Draft for review
- **Date:** 2026-09-14
- **Branch:** `feature/librashader-integration` (based on `chore/merge-upstream-2026-09-14`)
- **Author:** design doc (Claude-assisted)
- **Supersedes (rendering core only):** `2026-07-13-slang-shader-support-design.md` §4–§5.
  The parser, preset library, importer, UI and config from that design stay.

## 1. Summary

Replace the in-tree slang *rendering core* (`SlangFilterChain` + `SlangPassCompiler`,
~1400 LOC, Vulkan-only) with [librashader](https://github.com/SnowflakePowered/librashader),
the reference implementation of the RetroArch slang shader pipeline, loaded at runtime
through its MIT-licensed C loader header. Everything above the rendering core is kept:
preset discovery, package import, parameter browsing UI and config persistence.

librashader records its whole filter chain into a caller-supplied command buffer using raw
Vulkan (or GL/D3D11) handles. PPSSPP's `thin3d` abstraction does not expose those handles
per frame, and both the Vulkan and OpenGL backends execute on a deferred render thread from a
recorded step list. The central piece of this design is therefore a new **native callback
step** in the render managers, surfaced through one new `thin3d` entry point, that runs a
caller's function on the render thread with the current command buffer and the source and
destination images already in the layouts librashader requires.

The in-tree chain is kept as a fallback during the transition and is removed in a later phase
once librashader ships on every platform PPSSPP supports.

## 2. Why

| In-tree chain today | With librashader |
|---|---|
| Vulkan only; GL/D3D11 cross-compile plan at 0/34 tasks, GL black-render unsolved | Vulkan, GL 3.3+/GLES 3.0+, D3D11 (and D3D9/12/Metal) already implemented |
| Push-constant → UBO textual rewrite of shader source | Compiles the untouched shader through the same glslang + SPIRV-Cross path RetroArch uses |
| Needed `MAX_TEXTURE_SLOTS` 3→12 and `MAX_DESC_SET_BINDINGS` 6→14 in thin3d, which upstream just tightened to 5 | librashader owns its pipelines, descriptors and intermediates; the thin3d bumps can be reverted |
| No shader cache; synchronous compile on the main thread | Parallel compile plus a persistent on-disk cache |
| Spec conformance maintained by us | Maintained upstream; tested against the full libretro shader repository |

## 3. Goals and non-goals

### Goals
- Render any preset the current chain renders, pixel-comparable, on desktop Vulkan first.
- Bring OpenGL/GLES and D3D11 to parity without a per-backend cross-compile layer in PPSSPP.
- Keep the existing user-facing feature set: preset browser, import, parameter sliders,
  per-game preset, parameter persistence.
- Degrade gracefully: if the librashader library is not present or fails to load, PPSSPP
  behaves exactly as before this change (in-tree chain during transition; "shader off" after
  the in-tree chain is removed).
- Keep the change surface in `thin3d` and the render managers small and reviewable.

### Non-goals
- Replacing PPSSPP's legacy `.ini`/GLSL post-shader system. It stays untouched.
- Metal or wgpu runtimes. PPSSPP has no Metal backend (macOS/iOS use MoltenVK).
- Statically linking librashader. Upstream marks static linking unsupported.
- Rewriting the parameter UI on top of librashader's parameter API in this pass. The in-tree
  `.slangp` parser keeps enumerating parameters for the sliders; it is already tested and
  device-free.

## 4. Assumptions and decisions taken without an interactive review

These were decided to keep the work moving. Each is cheap to reverse before Phase 2 starts.

1. **Runtime dynamic loading, never linking.** The only librashader code compiled into PPSSPP
   is the MIT `librashader.h` / `librashader_ld.h` pair. The MPL-2.0 shared library is
   loaded with `dlopen` / `LoadLibrary`. This keeps PPSSPP's GPL-2.0-or-later licensing
   simple (MPL-2.0 permits distribution alongside GPL code) and lets a build without the
   library still run.
2. **Order of backends: desktop Vulkan → desktop OpenGL → Android Vulkan/GLES → D3D11.**
   Desktop Vulkan is where the in-tree chain is verified today, so it is the regression
   baseline. Android needs a Rust cross-build and is deferred to its own phase.
3. **Keep the in-tree chain as a fallback until Phase 4.** Both implementations sit behind
   one interface; a developer setting chooses between them for A/B comparison. Removal is an
   explicit phase, not a side effect.
4. **Chain creation happens on the render thread, deferred.** librashader's non-deferred
   create submits to the queue and waits idle, which would race the render thread's
   submissions. The deferred variant records LUT uploads into the command buffer we hand it.
   Shader compilation stalls the render thread for the duration of one preset switch, which
   is the behavior users have today (the in-tree chain compiles synchronously too).
5. **Output goes into a PPSSPP-owned `Draw::Framebuffer`**, sized to the display rect,
   exactly as the in-tree chain does today, and is handed to `PresentationCommon` unchanged.
   Rendering librashader's last pass straight into the swapchain image is a possible later
   optimization, not part of this design.
6. **Building librashader is a developer/CI step, not part of the default CMake build.**
   A documented `cargo` command and a CMake option that copies a prebuilt library next to
   the executable are sufficient for Phases 1–2. CI integration and Android packaging come
   with Phase 3.

## 5. Current state (what is being replaced and what is kept)

Reference: `GPU/Common/Slang/`, `Core/Slang/`, `UI/SlangShaderScreen.*`.

**Kept unchanged (backend-agnostic, ~1500 LOC):**
`SlangpParser`, `SlangPreset.h`, `SlangResolution.h`, `SlangReflection` (parameter
enumeration only), `SlangPresetLibrary`, `SlangPackageImporter`, `SlangPaths`,
`SlangShaderScreen`, config fields `sSlangShaderPreset`, `sSlangBuildbotUrl`,
`mSlangParams`, and `FramebufferManagerCommon::UpdateSlangChain` scaffolding.

**Replaced (rendering core), removed in Phase 4:** `SlangFilterChain`, `SlangPassCompiler`,
`SlangReflection`, `SlangResolution.h`. Their public contract is the interface in §6.4, which
`LibrashaderFilterChain` is now the only implementation of. (Parameter enumeration for the UI
lives in `SlangpParser`/`Core/Slang/SlangPresetLibrary.cpp`, which are kept.)

**Removed in Phase 4** (reverted to upstream values, since only the in-tree chain needed them):
`MAX_TEXTURE_SLOTS` 12→3 (`Common/GPU/thin3d.h`), `MAX_DESC_SET_BINDINGS` 14→5
(`VulkanRenderManager.h`), D3D11 `MAX_BOUND_TEXTURES` 12→8, the GL `sampler3..7` uniform
queries, `FramebufferDesc::colorFormat`, `RPKey::colorFormat`/`_padding` and the sRGB/float
render-pass keying in `VulkanFramebuffer.*` / `VulkanRenderManager.cpp` / `VulkanQueueRunner.cpp`.
`git diff upstream/master -- Common/GPU` is now pure additions (the native-callback plumbing).

**Integration point (unchanged location):**
`FramebufferManagerCommon::PrepareCopyDisplayToOutput`, which today calls
`slangChain_->Run(vfb->fbo, nativeW, nativeH, displayRectW, displayRectH, frameCount)` and
feeds the result to `presentation_->SourceFramebuffer(...)`.

## 6. Architecture

```
FramebufferManagerCommon (emu thread)
   │  ISlangFilterChain::Run(src fbo, native size, display rect, frameCount)
   ▼
LibrashaderFilterChain (emu thread side)          SlangFilterChain (in-tree, fallback)
   │  ensures output Draw::Framebuffer
   │  draw->RunNativeCallback(src, dst, fn)
   ▼
thin3d VKContext::RunNativeCallback
   │  renderManager_.RunNativeCallback(VKRFramebuffer *src, *dst, fn)
   ▼
VulkanRenderManager: EndCurRenderStep(); push VKRStep{CALLBACK}
   ▼  (render thread)
VulkanQueueRunner::PerformCallback(step, cmd, curFrame)
   │  transition src → SHADER_READ_ONLY_OPTIMAL, dst → COLOR_ATTACHMENT_OPTIMAL, flush barriers
   │  fn(NativeCallbackInfo{cmd, srcImage, srcFormat, dstImage, dstFormat, curFrame})
   │  record dst layout as COLOR_ATTACHMENT_OPTIMAL (librashader leaves it there)
   ▼
LibrashaderFilterChain render-thread side
   │  first call: libra_vk_filter_chain_create_deferred(preset, device, cmd, opts)
   │  every call:  set params; libra_vk_filter_chain_frame(chain, cmd, frameCount, in, out, viewport, NULL, opts)
```

### 6.1 Librashader loader — `Common/GPU/Librashader/LibrashaderLoader.{h,cpp}`

- Vendors `ext/librashader/include/librashader.h` and `librashader_ld.h` pinned to a
  release tag (0.12.0, C ABI 2 / API 5) with the upstream MIT notice.
- Defines `LIBRA_RUNTIME_VULKAN` (and later `LIBRA_RUNTIME_OPENGL`, `LIBRA_RUNTIME_D3D11`)
  before including the loader header so only the needed runtime bindings are compiled.
- API:
  ```cpp
  namespace Librashader {
  // Loads once, thread-safe, idempotent. Returns true if the library and ABI matched.
  bool Load(std::string *error);
  bool IsLoaded();
  const libra_instance_t &Instance();  // valid only when IsLoaded()
  void Unload();                        // at shutdown only
  }
  ```
- Search order: `LIBRASHADER_PATH` env var, the executable directory
  (`GetExeDirectory()`), then the platform default name (`librashader.dll`,
  `librashader.dylib`, `librashader.so`) through the normal loader search path. On Android
  the app's native library directory is already on that path.
- Never asserts on absence; absence is a normal state and is logged once at INFO.

### 6.2 Native callback step in the render managers

**Vulkan.** New `VKRStepType::CALLBACK`. `VKRStep` gains:
```cpp
struct {
    VKRFramebuffer *src;   // color read by the callback (may be null)
    VKRFramebuffer *dst;   // color written by the callback (may be null)
    NativeCallbackFn *fn;  // heap-allocated std::function, deleted after the step runs
} callback;
```
`VulkanRenderManager::RunNativeCallback(VKRFramebuffer *src, VKRFramebuffer *dst,
NativeCallbackFn fn, const char *tag)`:
- `EndCurRenderStep()`; bumps `numReads` on the last RENDER step targeting `src` (same as
  `BlitFramebuffer`); inserts `src` and `dst` into `dependencies`; pushes the step.

`VulkanQueueRunner::PerformCallback(const VKRStep &step, VkCommandBuffer cmd, int curFrame)`:
- `recordBarrier_.TransitionColorImageAuto(&src->color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)`;
  `TransitionColorImageAuto(&dst->color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)`;
  `recordBarrier_.Flush(cmd)`.
- Calls `fn` with a `Draw::NativeCallbackInfo` (see §6.3).
- Sets `dst->color.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` afterwards. librashader
  documents that it leaves the output in that layout and emits no final barrier. `src`
  stays in `SHADER_READ_ONLY_OPTIMAL`, which the tracker already records.
- The step optimizer passes (`ApplyMGSHack`, `ApplySonicHack`, render-pass merging,
  `RENDER_SKIP` conversion) must treat CALLBACK like COPY: an opaque barrier that reads
  `src` and writes `dst`. The `switch` statements in `RunSteps`, `LogSteps`, and the
  step-type dumps gain a CALLBACK case; `default: UNREACHABLE()` must never be hit.

**OpenGL (Phase 2 — implemented and verified on macOS, 2026-09-14).** Mirror:
`GLRStepType::CALLBACK`, `GLRStep::callback{src, dst, fn}`, `GLQueueRunner::PerformCallback` runs
`fn` with `src->color_texture.texture` and `dst->color_texture.texture`, then calls
`RestoreBaselineStateAfterCallback()` because librashader changes GL state freely. The step is
gated on `OpenGLContext::SupportsNativeCallback()` (desktop GL 3.3+ / GLES 3.0+); GLES 2 never
selects librashader. Unlike Vulkan there is no readiness gate: the GL adapter creates the chain and
renders in the same callback, and frees it through a second CALLBACK step (the creating context must
be current for `libra_gl_filter_chain_free`).

That second CALLBACK step only helps while the render thread still drains work. At `DeviceLost` /
shutdown it does not: `GLRenderManager::ThreadEnd` deletes queued CALLBACK functions without running
them, so `QueueFree` takes a `deviceLost` flag and, when it is set, drops the state with one warning
instead of enqueuing a free that would never run. The GL objects die with the context and
librashader's Rust-side allocation is knowingly leaked (see §8). `libra_gl_filter_chain_free` has
therefore never been exercised on device; only the drop path has.

Under VR multi-pass (`keepSteps`) the same CALLBACK step is replayed once per pass with the same
`frame_count`, so a preset's history/feedback ring advances twice per frame. Acceptable for now; to
revisit if Quest ever comes into scope.

The restored baseline is:

- `fbo_unbind()`, which binds the default FBO and sets both binding caches to it, so the next
  `fbo_bind_fb_target` for a real framebuffer really rebinds
- `glBindVertexArray(globalVAO_)` when VAOs are in use (also the only thing that restores
  vertex-attribute enable state), `glUseProgram(0)`, `glBindBuffer(GL_ARRAY_BUFFER, 0)`
- `glBindSampler(i, 0)` for `i < MAX_GL_TEXTURE_SLOTS` (GLES 3 / desktop ≥ 3.3),
  `glActiveTexture(GL_TEXTURE0)`
- `GL_UNPACK_ALIGNMENT` 4 and, on desktop GL *and GLES 3.0+*,
  `GL_UNPACK_ROW_LENGTH`/`SKIP_ROWS`/`SKIP_PIXELS` 0 and `glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0)`
  (a runtime `!gl_extensions.IsGLES || gl_extensions.GLES3` check - all four enums are declared for
  `USING_GLES2` builds via `gl3stub.h`, so no compile-time guard is needed)
- `glColorMask(1,1,1,1)`, `glDepthMask(GL_TRUE)`, `glStencilMask(0xFF)`
- depth test, stencil test, blend, cull face and dither disabled; scissor test enabled
- desktop only: colour logic op, depth clamp and `GL_FRAMEBUFFER_SRGB` disabled, all eight
  `GL_CLIP_DISTANCE*` disabled

Verified on device: with a chain active, PPSSPP's FPS counter, debug-statistics overlay and the ImGui
debugger (menu bar, windows, framebuffer preview texture) all render correctly over the filtered
image, and the present blit is intact — no additional state needed restoring.

**Direct3D 11 (Phase 4 — implemented and verified on Windows, 2026-09-15).** No render manager and no
step type: D3D11 is immediate mode, so `D3D11DrawContext::RunNativeCallback` (`Common/GPU/D3D11/thin3d_d3d11.cpp`)
just calls `fn` synchronously on the calling thread with `cmdBuffer = context_.Get()` (the immediate
context), `srcView` = the source framebuffer's `colorSRView`, `dstView` = the destination's
`colorRTView`, and `srcFormat`/`dstFormat` = the real `DXGI_FORMAT`. Because there is no queue there is
also no readiness gate and no deferred free — the D3D11 adapter creates the chain, renders and frees on
this same thread (see §8).

What has to be unbound and restored is the state the immediate context is already carrying:

- before the call: `PSSetShaderResources(0, MAX_BOUND_TEXTURES, nullptr…)` (the source is normally
  still bound as an SRV, and D3D11 refuses to bind a resource as both SRV and RTV) and
  `OMSetRenderTargets(0, nullptr, nullptr)` (the destination is normally the current RTV)
- after: `OMSetRenderTargets(1, curRenderTargetView_, curDepthStencilView_)` when a render target was
  cached, the SRV and sampler slots cleared again, `Invalidate(InvalidationFlags::CACHED_RENDER_STATE)`
  — which resets exactly the fields `ApplyCurrentState()` compares (`curPipeline_`, `curBlend_`,
  `curDepthStencil_`, `curRaster_`, `curInputLayout_`, `curVS_`, `curPS_`, `curTopology_`) — plus
  `blendFactorDirty_`/`stencilDirty_`, and the `InvalidationCallbackFlags::RENDER_PASS_STATE` callback
  so `DrawEngineD3D11` re-sends viewport/scissor and texture state. Viewport and scissor need no
  explicit re-issue because thin3d caches no last-set values for them.

Verified on device (Windows 11, RTX 4090): `stock.slangp` and `lcd-psp-matrix.slangp` render through
librashader on D3D11, and PPSSPP's own menu bar and FPS/speed overlay draw correctly over the filtered
image afterwards. One behavioural difference from GL/Vulkan is unavoidable at this API level:
`libra_d3d11_filter_chain_frame` takes a bare `ID3D11ShaderResourceView` with no size struct (unlike
`libra_image_gl_t`/`libra_image_vk_t`), so librashader derives `SourceSize`/`OriginalSize` from the
view's resource and a preset's resolution-dependent math runs at the render resolution rather than at
the PSP's native 480×272 when `InternalResolution > 1` — see §12.

### 6.3 thin3d surface — `Common/GPU/thin3d.h`

```cpp
struct NativeCallbackInfo {
    // Vulkan: VkCommandBuffer, VkImage, VkFormat as integers (no Vulkan header in thin3d.h)
    uint64_t cmdBuffer = 0;
    uint64_t srcImage = 0;  uint32_t srcFormat = 0;
    uint64_t dstImage = 0;  uint32_t dstFormat = 0;
    // OpenGL: texture names
    uint32_t srcTexture = 0, dstTexture = 0;
    int srcWidth = 0, srcHeight = 0, dstWidth = 0, dstHeight = 0;
    int frameIndex = 0;     // 0..MAX_INFLIGHT_FRAMES-1
};
using NativeCallbackFn = std::function<void(const NativeCallbackInfo &)>;

// Runs fn on the backend's render thread, outside any render pass, with src readable as a
// shader-sampled color image and dst writable as a color attachment. Returns false if the
// backend does not support native callbacks (D3D9, or GL/D3D11 before their phases).
virtual bool RunNativeCallback(Framebuffer *src, Framebuffer *dst, NativeCallbackFn fn, const char *tag) { return false; }
```
Only `VKContext` overrides it in Phase 1. `GetNativeObject` additionally exposes
`NativeObject::VULKAN_GET_INSTANCE_PROC_ADDR` (the loader's `vkGetInstanceProcAddr`) so the
chain does not include `VulkanLoader.h` directly; instance, physical device, device and
graphics queue are reachable through the existing `NativeObject::CONTEXT` (`VulkanContext *`).

### 6.4 Filter-chain interface — `GPU/Common/Slang/ISlangFilterChain.h`

Extracted verbatim from today's `SlangFilterChain` public surface:
```cpp
class ISlangFilterChain {
public:
    virtual ~ISlangFilterChain() = default;
    virtual bool Load(const Path &presetPath, std::string *error) = 0;
    virtual bool IsValid() const = 0;
    virtual Draw::Framebuffer *Run(Draw::Framebuffer *source, int sourceW, int sourceH,
                                   int viewportW, int viewportH, int frameCount) = 0;
    virtual void SetParamOverrides(const std::map<std::string, float> &overrides) = 0;
    virtual void DeviceLost() = 0;
    virtual void DeviceRestore(Draw::DrawContext *draw) = 0;
    virtual const char *BackendName() const = 0;  // "in-tree" | "librashader"
};
```
`SlangFilterChain` implements it with no behavior change. A factory
`CreateSlangFilterChain(Draw::DrawContext *, SlangChainBackend preference)` picks the
implementation (§6.6).

### 6.5 `LibrashaderFilterChain` — `GPU/Common/Slang/LibrashaderFilterChain.{h,cpp}`

State split by thread:

| Emu-thread state | Render-thread state (touched only inside callbacks) |
|---|---|
| `Path presetPath_`, `libra_shader_preset_t preset_` | `libra_vk_filter_chain_t chain_` |
| `Draw::Framebuffer *output_` + its size | `bool createFailed_` |
| `std::map<std::string,float> overrides_` (copied into each callback) | |
| `std::atomic<bool> chainReady_` | |

- `Load()` (emu thread): `libra_preset_create_with_options(path, ctx, NULL, &preset_)`. The
  context sets `PRESET_DIR`/`PRESET` (automatic), `runtime = vulkan`, `core_name = "PPSSPP"`.
  Any error → `valid_ = false`, error string returned, nothing enqueued. This is cheap
  (parsing only) so it stays synchronous, matching today's `Load` semantics.
- `Run()` (emu thread): (re)creates `output_` when the viewport size changes; then
  `draw_->RunNativeCallback(source, output_, fn, "librashader")` where `fn` captures
  `this`, `frameCount`, a copy of `overrides_`, the viewport, and the native source size.
  Returns `output_` if `chainReady_` is already true, else `nullptr` so the caller presents
  the unfiltered frame while the chain compiles (one or two frames).
- Callback (render thread): if `!chain_ && !createFailed_`, call
  `libra_vk_filter_chain_create_deferred(&preset_, device, cmd, &opts, &chain_)` with
  `frames_in_flight = 3` (`VulkanContext::MAX_INFLIGHT_FRAMES`), `use_dynamic_rendering = 0`,
  and return (the first frame only uploads LUTs; `chainReady_` becomes true for the next
  frame). Otherwise apply overrides through `libra_vk_filter_chain_set_param`, then
  `libra_vk_filter_chain_frame(chain_, cmd, frameCount, in, out, &viewport, NULL, &frameOpts)`
  with `in = {srcImage, srcFormat, srcW, srcH}`, `out = {dstImage, dstFormat, dstW, dstH}`,
  `viewport = {0, 0, dstW, dstH}`, `frameOpts.rotation = 0`, `frame_direction = 1`.
  `SourceSize`/`OriginalSize` semantics: librashader derives them from `in.width/height`,
  so the chain passes the *native* PSP size the way the in-tree chain does today, with the
  upscaled fbo sampled via 0..1 UVs. Phase 1 verification confirmed librashader honours those
  numbers for `SourceSize`/`OriginalSize`/`scale_type = source` without validating them
  against the image - but it *also* uses them as the copy extent when it snapshots the input
  into its `OriginalHistoryN` ring, so a preset that samples history would see only a
  native-sized corner of the upscaled frame.
- **History detection (added after Phase 1 on-device verification).** `Load()` therefore
  re-parses the preset with PPSSPP's in-tree `ParseSlangPreset` and scans each pass's
  `#include`-resolved source for `OriginalHistory[1-9]` / `OriginalHistorySize[1-9]`, storing
  the answer in `needsNativeInput_`. When it is true, `Run()` keeps a second, native-sized
  framebuffer, blits the source into it (`FB_BLIT_LINEAR`) and hands *that* to the callback,
  so the declared size equals the real extents and history covers the whole picture (at
  native resolution). When it is false nothing changes: the upscaled fbo is passed with the
  declared native size, which is what keeps non-history presets bit-identical to the in-tree
  chain. The selected mode is logged once at INFO per preset load. If the re-parse or a
  shader read fails the answer is "no history", i.e. the pre-existing behaviour; librashader's
  own parser remains the one that decides whether the preset loads at all.
- `DeviceLost()` (emu thread; the render thread is already stopped and the device idle by
  the time `FramebufferManagerCommon::DeviceLost` runs, see `VKContext::DeviceLost`): free
  `chain_`, `preset_`, `output_`; keep `presetPath_` for `DeviceRestore`.
- Destructor: same as `DeviceLost` after `draw_->FlushAndWait()`-equivalent
  (`VulkanRenderManager::StopThread` has already run in the shutdown paths that matter;
  the destructor asserts the render thread is not running in debug builds).

### 6.6 Selection and fallback — `FramebufferManagerCommon::UpdateSlangChain`

There is no user-facing toggle (Phases 1–3 had `bSlangUseLibrashader`; Phase 4 removed it
along with the in-tree chain, and an existing `SlangUseLibrashader` ini key is ignored).
librashader is the only rendering core, so the decision is purely a capability check. Pure
decision function, unit-tested:
```cpp
enum class SlangChainBackend { None, Librashader };
SlangChainBackend ChooseSlangChainBackend(bool librashaderLoaded, GPUBackend gpuBackend,
                                          bool drawSupportsNativeCallback);
// Librashader iff all of: library loaded, backend ∈ {VULKAN, OPENGL},
// draw supports native callbacks. Otherwise None.
```
`CreateSlangFilterChain` returns `nullptr` for `None`; `UpdateSlangChain` then clears
`slangChainPresetPath_`, logs `Slang chain backend: none (librashader not loaded or backend
unsupported)` once per reload and skips `Load`, and the unfiltered image is presented.
The chosen backend name is logged at INFO on every preset (re)load and shown in the
Developer Tools system-info line so on-device screenshots are attributable.

### 6.7 Build and distribution

- CMake option `USE_LIBRASHADER` (default ON on Windows/macOS/Linux/Android, OFF on
  iOS/UWP/libretro until verified). Adds `ext/librashader/include` and compiles the
  loader; adds `-DUSE_LIBRASHADER=1`. No cargo invocation in the default build.
- Optional CMake variable `LIBRASHADER_PREBUILT=<path to library>`: post-build copy next to
  the executable (`PPSSPPSDL`, `PPSSPPQt`, unit test not needed).
- Developer instructions in `docs/superpowers/librashader-build.md`: pin the tag, run
  `cargo build -p librashader-capi --release --features runtime-vulkan,runtime-opengl`
  (verify exact feature names against the pinned `librashader-capi/Cargo.toml` during
  Task 1), copy the artifact.
- Phase 3 adds: Android `cargo ndk` build for `arm64-v8a`, `armeabi-v7a`, `x86_64` into
  `jniLibs`. CI deferred to Phase 4 (the Android CI jobs use `android/ab.sh`/ndk-build,
  whose `Android.mk` lists no `GPU/Common/Slang` sources; `.github/workflows/manual_generate_apk.yml`
  is the Gradle-based job that can host a `cargo ndk` step).

## 7. Per-frame data flow (Vulkan, steady state)

1. Emu thread, `PrepareCopyDisplayToOutput`: compute display rect; collect param overrides
   for the current preset; `out = chain->Run(vfb->fbo, bufferW, bufferH, rectW, rectH, flips)`.
2. `LibrashaderFilterChain::Run` ensures `output_` is `rectW×rectH` RGBA8 and enqueues the
   CALLBACK step through thin3d. Returns `output_`.
3. Emu thread continues: `presentation_->SourceFramebuffer(output_, rectW, rectH)`, then the
   normal present blit records a RENDER step that samples `output_`. The render manager
   sees a dependency on `output_`, which the CALLBACK step wrote, so ordering is preserved.
4. Render thread, `RunSteps`: ... RENDER(game) → CALLBACK(librashader) → RENDER(backbuffer).
   `PerformCallback` transitions, calls into librashader, records the final layout.
5. The present RENDER step's `BindFramebufferAsTexture(output_)` finds `output_` in
   `COLOR_ATTACHMENT_OPTIMAL` and inserts the usual transition to shader-read.

## 8. Threading and lifetime rules

- `libra_*_filter_chain_frame` and `_set_param` are called only from inside a callback
  (render thread). `libra_preset_*` and the loader are called from the emu thread, with one
  exception: `libra_preset_free` may also run on the render thread, from the Vulkan
  deletion-queue callback that disposes a preset which was parsed but whose chain was never
  created. librashader's header imposes no thread affinity on it, and the deletion-queue
  callback is the only other place that can own the handle.
- The `std::function` in a step owns copies of everything it needs; it never dereferences
  emu-thread state other than `this`, whose lifetime is guaranteed because destruction
  happens only after the render thread is stopped or idle (`DeviceLost` / destructor).
- Changing presets: `UpdateSlangChain` deletes the old chain object (emu thread) only after
  `draw_->FlushAndWait()`-equivalent ordering, which today's code already has because
  `DeviceLost`/`UpdateSlangChain` run between frames on the emu thread with the render
  thread drained by `VulkanRenderManager::Finish`. Phase 1 adds a debug assertion that no
  CALLBACK step referencing the chain is pending when it is deleted.

## 9. Error handling

| Failure | Behavior |
|---|---|
| Library absent / ABI mismatch | `Librashader::Load` false, INFO log once, factory picks in-tree (Phase 1–3) or "off" (Phase 4). |
| Preset parse error | `Load` false with librashader's error string; `UpdateSlangChain` logs ERROR and renders raw, as today. |
| Chain creation error on render thread | `createFailed_ = true`, chain never becomes ready, `Run` keeps returning `nullptr`; error string surfaced to the emu thread through a mutex-protected `std::string lastError_` and logged once. |
| Backend without native callbacks | Factory never picks librashader; no behavior change. |
| Device lost mid-compile | `DeviceLost` runs after the render thread stopped, so the chain is either complete or never created; both are freed. |

## 10. Platform and phase matrix

| Phase | Scope | Exit criterion |
|---|---|---|
| **1** | Loader, Vulkan CALLBACK step, thin3d API, `ISlangFilterChain`, `LibrashaderFilterChain`, selection, dev toggle, prebuilt-copy CMake option | macOS (MoltenVK) and one Windows/Linux Vulkan machine render `stock.slangp`, `lcd-psp-matrix.slangp`, `crt-royale.slangp` identically to the in-tree chain; unit tests green; in-tree path unchanged when toggled |
| **2** | GL CALLBACK step + `LibrashaderFilterChain` GL runtime, state restore | Desktop GL renders the same three presets |
| **3** | Android: cargo-ndk build, jniLibs packaging; GLES 3 verification (CI deferred to Phase 4: the Android CI jobs use `android/ab.sh`/ndk-build, whose `Android.mk` lists no `GPU/Common/Slang` sources) | APK renders the three presets on Vulkan and GLES 3 on the Adreno test device |
| **4** | Remove in-tree chain, revert thin3d slot/descriptor bumps and sRGB render-pass keying, delete `bSlangUseLibrashader`; D3D11 runtime | **Met (2026-09-15)**: removal done, Windows VK/GL/D3D11 verified (`stock`/`lcd-psp-matrix` on all three backends), GLES 3 fixes landed, perf gate passed (librashader/in-tree GPU time ratio 1.22 ≤ 1.5 on Adreno 740), Vulkan sync validation clean on Android. `git diff upstream/master -- Common/GPU` is additions only. Packaging, jniLibs ABI handling and CI integration completed (Task 7). |

**Phase 4 progress (2026-09-14):** the removal half is done. The perf gate passed (librashader ÷ in-tree GPU time 1.220 ≤ 1.5 on the Adreno device), the in-tree chain and the `SlangUseLibrashader` toggle are deleted, and every thin3d/Vulkan/GL/D3D11 hunk that existed only for it is back to upstream — `git diff upstream/master --stat -- Common/GPU` is 577 insertions with zero deletions. `stock.slangp` and `lcd-psp-matrix.slangp` on macOS Vulkan and GL are pixel-identical to the Phase 2 captures; with the library renamed away, `Slang chain backend: none` is logged once and the raw image is presented. Android Vulkan (`lcd-grid-v2-psp-color`, APK `librashader-p4e`) is byte-identical to the Phase 3 reference capture. 68 unit tests pass.

**Phase 4 Windows (2026-09-15):** Task 5 verified Vulkan and OpenGL on Windows; Task 6 added the D3D11
adapter (`GPU/Common/Slang/LibrashaderRuntimeD3D11.cpp`, `D3D11DrawContext::RunNativeCallback`) and
verified `stock.slangp` and `lcd-psp-matrix.slangp` on the D3D11 backend, with the UI/OSD intact after
the callback and a clean `Slang chain backend: none` fallback when `librashader.dll` is renamed away.
Two findings recorded rather than fixed: the lighter, highlight-clipped Windows present path is common
to D3D11 and Vulkan and absent on GL (a pre-existing PPSSPP present-path difference, not librashader —
D3D11 and Vulkan no-chain captures agree with each other to a mean of 0.4/255), and the D3D11
`SourceSize` caveat above. Packaging (Task 7) is still open.

**Phase 2 exit criterion: met (2026-09-14, macOS 15 / Apple M2 Pro, SDL GL 4.1 core over Metal).**
Desktop GL renders `stock`, `lut`, `feedback`, `lcd-psp-matrix` and `twopass` through librashader
bit-identically to the Vulkan librashader output of the same frame (0 of 2088960 pixels differ in all
five; the captured PNGs are byte-identical), with no vertical flip and no state-restore damage to
PPSSPP's own overlays. `crt-royale` was not available on the test machine, so `twopass` (multi-pass +
`OriginalHistory`) and `lcd-psp-matrix` (subpixel mask, phase-exact) stand in for it, as in Phase 1.
GLES 3 verification stays in Phase 3. Details: `.superpowers/sdd/2026-09-14-librashader-phase2-opengl/task-5-report.md`.

**Phase 3 exit criterion: met (2026-09-14, AYN Thor / Adreno 740, Android 14).** The APK ships
`librashader.so` in `jniLibs` and the loader's bare-name `dlopen` resolves it; `librashader loaded
(ABI 2, API 5)` + `Slang chain backend: librashader` on **both** backends, with no Phase 1/2 runtime
code change on either. Vulkan (device API 1.3.128): `lcd-grid-v2-psp-color` (substituted for the
spec's `stock` preset, which is not present in `assets/shaders/slang_test` on the device while the
full libretro pack is), `presets/crt-royale-downsample` and the heavy `crt/crt-maximus-royale-fast-mode`
(substituted for `crt-royale`) all render; a Khronos-validation run over the CALLBACK step produced
**zero** librashader-attributable messages (core validation only: `VK_LAYER_KHRONOS_validation` default
features; synchronization validation is not enabled by PPSSPP — `Common/GPU/Vulkan/VulkanContext.cpp`
never sets `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT` — and is a Phase 4 item);
sleep/wake plus an in-process preset switch both recover. GLES 3: the device hands PPSSPP a native
Adreno **ES 3.2** context (not the ANGLE-over-Vulkan driver initially assumed — Task 3 found PPSSPP
receives a native Adreno OpenGL ES 3.2 context; the ANGLE string comes from SurfaceFlinger, not from
PPSSPP's EGL context), so `SupportsNativeCallback()` is true and no ES 3 context-request change was
needed; `lcd-grid-v2-psp-color` matches the Vulkan librashader frame to within shader-compiler rounding
(4.74% of pixels, max 49/255, symmetric and confined to lit areas), `crt-royale-downsample` compiles
and renders on GLES with no `create:` error, the GL context-loss drop warning and full chain
recreation both fire on sleep/wake (post-wake frame byte-identical to the fresh-boot frame), and
`libra_gl_filter_chain_free` ran for the first time on any device via an in-process preset switch
without a crash. Toggling librashader off on GL correctly yields a raw image plus the intended
"slang passes require the Vulkan backend in Phase 1" diagnostic, since the in-tree chain is
Vulkan-only. Not covered: ANGLE and non-Adreno GLES drivers (a driver that honours a strict ES 2
context request would silently lose slang on GL — see the risk note), the `armeabi-v7a`/`x86_64` ABIs
at runtime, and the CI jobs (deferred to Phase 4). Details:
`.superpowers/sdd/2026-09-14-librashader-phase3-android/task-2-report.md` and `task-3-report.md`.

Each phase gets its own implementation plan. This spec covers all four; the Phase 1 plan is
`docs/superpowers/plans/2026-09-14-librashader-phase1-vulkan-core.md`.

## 11. Testing strategy

- **Device-free unit tests** (PPSSPP `unittest` harness): loader reports "not loaded"
  cleanly when the library is absent and when `LIBRASHADER_PATH` points at a file that exists
  but is not a loadable library; `ChooseSlangChainBackend` truth table; the in-tree chain
  still passes its 19 tests unchanged. The originally planned `NativeCallbackInfo` marshaling
  test was dropped: marshaling reads a live `VKRFramebuffer` pair (real `VkImage` handles and
  formats owned by a created device), so it cannot be exercised device-free. The loader tests
  and the backend-selection truth table are the device-free suite; marshaling is covered by
  the on-device checks instead.
- **Compile-time**: CMake configure + build with `USE_LIBRASHADER=ON` and `OFF`.
- **Vulkan validation**: run once per phase with `VK_LAYER_KHRONOS_validation` enabled and
  the three presets; zero new validation errors is the bar (the CALLBACK step's layout
  bookkeeping is the risk).
- **Visual**: screenshot the same frame (PPSSPP's screenshot function) with in-tree and
  librashader for the three presets; compare with a pixel-diff tool; differences must be
  explainable (librashader applies rotation only on the final pass; sRGB conversions).
- **Regression**: with the toggle off or the library absent, output must be byte-identical
  to the pre-change build.

## 12. Risks

- **Step optimizer interactions.** The Vulkan queue runner rewrites steps (merging, MGS and
  Sonic hacks). A CALLBACK step that is silently converted or reordered would corrupt
  frames. Mitigation: treat it exactly like COPY in every pass; debug-build sanity check.
- **Native size vs image size.** librashader may take `SourceSize` from the image extents
  rather than the `width/height` fields. Mitigation noted in §6.5 (one blit to a
  native-sized intermediate). This is the first thing Phase 1 verifies on device.
  **Confirmed on D3D11 (2026-09-15):** the D3D11 frame entry point takes only an
  `ID3D11ShaderResourceView` — there are no `width`/`height` fields to declare — so
  `SourceSize`/`OriginalSize` are always the view resource's size. At `InternalResolution = 1`
  the three backends agree; above it, D3D11 runs resolution-dependent shader math at render
  resolution while GL/Vulkan run it at 480×272 (measured: the GL `lcd-psp-matrix` capture is
  unchanged from 1x to 3x, the D3D11 one changes). The fix, if it is ever wanted, is to make the
  D3D11 runtime always take the native-sized copy `LibrashaderFilterChain::EnsureNativeInput`
  already produces for `OriginalHistoryN` presets, at the cost of the upscaled detail.
- **Frames in flight.** Verified during the final Phase 1 review: librashader's Vulkan
  runtime does *not* index its per-frame resources by the `frame_count` we pass. It keeps its
  own internal counter, advanced once per `libra_vk_filter_chain_frame` call, and cycles it
  over the `frames_in_flight` given at creation (we pass `MAX_INFLIGHT_FRAMES = 3`, matching
  PPSSPP's own in-flight depth). The `frame_count` argument only feeds the `FrameCount`
  uniform that shaders read, so passing PPSSPP's monotonically increasing flip count is
  correct even when frames are skipped, and a skipped or repeated flip count cannot alias
  librashader's resource recycling. What we do owe librashader is one call per submitted
  frame, which the CALLBACK step gives us by construction.
- **MoltenVK.** Dynamic rendering is off by default in our options; the render-pass
  fallback path is the one librashader's 86Box integration uses on macOS.
- **Rust toolchain in CI (Phase 3).** Adds minutes to Android/desktop builds; pinning the
  tag and caching the cargo target directory keeps this bounded.
- **Library not shipped.** If distribution is a problem for some store build (iOS), the
  fallback keeps shaders working there until Phase 4 decides whether to keep the in-tree
  chain for that platform only.

## 13. Open questions for the reviewer

1. Is the Android GLES backend a must-have for shaders? If not, Phase 2 could be skipped
   entirely and GL stays "in-tree or off".
2. Should Phase 4 keep the in-tree chain for iOS/UWP/libretro where shipping an MPL
   library may be awkward, or drop slang support on those platforms?
3. Is a 1–2 frame unfiltered flash on preset switch acceptable (current design), or should
   `Run` keep presenting the previous chain's last output until the new chain is ready?
4. **D3D11 `SourceSize` semantics:** librashader's D3D11 frame API takes only an
   `ID3D11ShaderResourceView` (no size struct), so `SourceSize` equals the render resolution
   on D3D11 when `InternalResolution > 1` (Vulkan/GL declare the PSP's native 480×272 size
   over the upscaled image). Options: (a) accept the divergence (resolution-dependent shaders
   like LCD masks tile at the render resolution instead of the native grid on D3D11); (b) feed
   D3D11 a native-sized downsampled input via the existing history-preset blit (softer image,
   one extra blit per frame); (c) request a size-declaring API upstream. Decision pending.
