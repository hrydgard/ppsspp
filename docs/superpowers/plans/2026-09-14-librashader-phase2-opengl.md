# librashader Integration — Phase 2 (OpenGL / GLES Runtime) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render slang presets through librashader on PPSSPP's OpenGL backend (desktop GL 3.3+ and GLES 3.0+), reusing the Phase 1 chain, selector, loader and UI unchanged in behavior.

**Architecture:** Mirror Phase 1's Vulkan `CALLBACK` step in `GLRenderManager`/`GLQueueRunner`: a step that runs a caller function on the GL thread with the source framebuffer's color texture readable and the destination's color texture writable, then restores PPSSPP's baseline GL state (FBO binding caches, program, VAO, texture unit, sampler objects, sRGB). thin3d's `OpenGLContext` implements `RunNativeCallback`/`SupportsNativeCallback` and exposes a `GL_GET_PROC_ADDRESS` native object. `LibrashaderFilterChain` is split into a backend-neutral core and two small runtime adapters (Vulkan, OpenGL) behind an internal `LibrashaderRuntime` interface; the GL adapter creates the chain and renders in the same callback (no deferred-create contract), and frees it through a CALLBACK step. The selector admits `GPUBackend::OPENGL`.

**Tech Stack:** C++17; librashader 0.12.0 C API GL runtime (`libra_gl_filter_chain_*`, `libra_gl_loader_t`); PPSSPP `GLRenderManager`/`GLQueueRunner`, `thin3d_gl.cpp`, `gl_extensions`; PPSSPP `unittest`; CMake.

**Spec:** `docs/superpowers/specs/2026-09-14-librashader-integration-design.md` (§6.2 "OpenGL (Phase 2)", §6.3, §6.5, §8, §9, §10 row "2"). Phase 1 plan for reference: `docs/superpowers/plans/2026-09-14-librashader-phase1-vulkan-core.md`.

## Global Constraints

- **License header:** every new file carries the PPSSPP GPL 2.0 header (year `2026-`), copied from `GPU/Common/Slang/SlangFilterChain.h`.
- **No behavior change on Vulkan:** the Vulkan librashader path and the in-tree chain must produce identical output to Phase 1 (`stock`/`lut`/`feedback` 0-px A/B still holds). Every task rebuilds and reruns the unit suite (76 tests; Task 4 changes an existing test rather than adding one).
- **No behavior change for existing GL step types:** the GL `CALLBACK` step is a pure addition; every `switch` on `GLRStepType` handles it; it is never merged/reordered/skipped by any GL step pre-pass; the heap `std::function` is freed exactly once (run, or discarded at `RunSteps` cleanup / `ThreadEnd`).
- **GL state contract:** after the callback returns, `PerformCallback` restores the baseline `PerformRenderPass` assumes: `glBindVertexArray(globalVAO_)` (when VAOs are in use), `glUseProgram(0)`, `glActiveTexture(GL_TEXTURE0)`, `glBindSampler(i, 0)` for `i < MAX_GL_TEXTURE_SLOTS` when sampler objects exist (GLES3 or desktop ≥ 3.3), `glPixelStorei(GL_UNPACK_ALIGNMENT, 4)`, `glColorMask(1,1,1,1)`, `glDepthMask(GL_TRUE)`, `glStencilMask(0xFF)`, `glDisable(GL_FRAMEBUFFER_SRGB)` on desktop, and invalidates the FBO binding caches (`currentDrawHandle_ = currentReadHandle_ = (GLuint)-1`) so the next `fbo_bind_fb_target` rebinds.
- **skipGLCalls:** when `RunSteps` runs with `skipGLCalls == true` (context lost), a CALLBACK step must not invoke its function; it only frees it.
- **GL thread rule:** `libra_gl_filter_chain_*` only inside a native callback (GL thread, context current). `libra_gl_filter_chain_free` requires the creating context current → free via a CALLBACK step; if the context is already gone (`draw_ == nullptr` at `DeviceLost`), drop the state without calling free and log once (GL objects died with the context; the Rust-side memory is leaked knowingly).
- **Version gate:** `OpenGLContext::SupportsNativeCallback()` returns true only for `gl_extensions.IsGLES ? gl_extensions.GLES3 : gl_extensions.VersionGEThan(3, 3)`. GLES 2 never selects librashader.
- **Formats:** PPSSPP GL framebuffer color textures are created with unsized `GL_RGBA`/`GL_UNSIGNED_BYTE`; report `GL_RGBA8` (0x8058) as `libra_image_gl_t.format` for both input and output (librashader uses it as the sized internal format of the image).
- **librashader GL options:** `glsl_version = 0` (auto-detect from the context), `use_dsa = false`, `force_no_mipmaps = false`, `disable_cache = false` (librashader disables its cache itself without DSA).
- **Commit trailer:** end every commit message with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- **Build + test env (macOS):** configure once `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DUNITTEST=ON -DUSE_SYSTEM_LIBPNG=ON -DLIBRASHADER_PREBUILT=/tmp/librashader/dist/librashader.dylib -S . -B build-unittest`; build `cmake --build build-unittest --target PPSSPPUnitTest PPSSPPSDL -j12`; tests `./build-unittest/PPSSPPUnitTest all`; OFF variant `cmake --build build-nolibra --target PPSSPPUnitTest -j12`. App: `build-unittest/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL` with `librashader.dylib` beside it (built with `runtime-vulkan,runtime-opengl`). Config `~/.config/ppsspp/PSP/SYSTEM/ppsspp.ini`: `GraphicsBackend = 0 (OPENGL)` selects GL (macOS gives a 4.1 core context), `3 (VULKAN)` selects Vulkan. Test content `/tmp/ppsspp-t8/locoroco.ppdmp`; presets `assets/shaders/slang_test/{stock,lut,feedback,lcd-psp-matrix,twopass}.slangp`. Note: the in-tree chain is Vulkan-only, so on GL "toggle off" renders the raw image; the GL A/B baseline is the Vulkan librashader screenshot of the same frame.

## File structure

| File | Responsibility |
|---|---|
| `Common/GPU/OpenGL/GLQueueRunner.h/.cpp` | `GLRStepType::CALLBACK`, `GLRStep::callback`, `GLRNativeCallbackInfo/Fn`, `PerformCallback` + state restore, cleanup at discard |
| `Common/GPU/OpenGL/GLRenderManager.h/.cpp` | `RunNativeCallback(GLRFramebuffer*, GLRFramebuffer*, fn, tag)`; free callback fns in `ThreadEnd` |
| `Common/GPU/thin3d.h` | `NativeObject::GL_GET_PROC_ADDRESS`; doc comment that `*Format` fields are backend-native enums |
| `Common/GPU/OpenGL/thin3d_gl.cpp` | `OpenGLContext::SupportsNativeCallback/RunNativeCallback`, proc-address resolver |
| `Common/GPU/Librashader/LibrashaderLoader.h` | `#define LIBRA_RUNTIME_OPENGL` next to Vulkan |
| `GPU/Common/Slang/LibrashaderRuntime.h` | `RenderState` (moved), `LibrashaderRuntime` interface, `CreateLibrashaderRuntime` |
| `GPU/Common/Slang/LibrashaderRuntimeVulkan.cpp` | Vulkan adapter (code moved from `LibrashaderFilterChain.cpp`, behavior unchanged) |
| `GPU/Common/Slang/LibrashaderRuntimeOpenGL.cpp` | GL adapter (new) |
| `GPU/Common/Slang/LibrashaderFilterChain.h/.cpp` | backend-neutral core using the runtime |
| `GPU/Common/Slang/SlangChainFactory.cpp`, `unittest/TestLibrashader.cpp` | selector admits OPENGL; truth table updated |
| `GPU/CMakeLists.txt` | new sources |
| `docs/superpowers/librashader-build.md`, spec | GL notes |

---

### Task 1: GL CALLBACK step in GLQueueRunner and GLRenderManager

**Files:**
- Modify: `Common/GPU/OpenGL/GLQueueRunner.h` (enum `GLRStepType` ~line 277; `struct GLRStep` ~302–341 with anonymous union; `Perform*` declarations; cached members `currentDrawHandle_/currentReadHandle_/globalVAO_` ~389–410)
- Modify: `Common/GPU/OpenGL/GLQueueRunner.cpp` (`RunSteps` ~626–740: pre-pass switch ~635, dispatch switch ~689–722, step deletion ~655 and ~735; `PerformBlit` ~742 as the pattern; `LogSteps`/step-name switch ~1837–1855)
- Modify: `Common/GPU/OpenGL/GLRenderManager.h/.cpp` (`BlitFramebuffer` at .cpp ~274–288 as the producer pattern; `ThreadEnd` ~99–118 deletes leftover steps)

**Interfaces:**
- Produces:
  ```cpp
  struct GLRNativeCallbackInfo { GLRFramebuffer *src; GLRFramebuffer *dst; };
  using GLRNativeCallbackFn = std::function<void(const GLRNativeCallbackInfo &)>;
  void GLRenderManager::RunNativeCallback(GLRFramebuffer *src, GLRFramebuffer *dst, GLRNativeCallbackFn fn, const char *tag);
  ```

- [ ] **Step 1: Step type and payload**

`GLQueueRunner.h`: add `CALLBACK,` to `enum class GLRStepType` after `RENDER_SKIP`. Add `#include <functional>` if absent. Before `struct GLRStep`:

```cpp
struct GLRNativeCallbackInfo {
	GLRFramebuffer *src;  // color_texture.texture readable; may be null
	GLRFramebuffer *dst;  // color_texture.texture writable (callee renders into it via its own FBO); may be null
};
using GLRNativeCallbackFn = std::function<void(const GLRNativeCallbackInfo &)>;
```

Inside the `GLRStep` union add:

```cpp
		struct {
			GLRFramebuffer *src;
			GLRFramebuffer *dst;
			GLRNativeCallbackFn *fn;  // heap-allocated; freed exactly once (PerformCallback, or discard paths)
		} callback;
```

Declare in `GLQueueRunner`: `void PerformCallback(const GLRStep &step, bool skipGLCalls);`

- [ ] **Step 2: Execute the step and restore baseline state**

`GLQueueRunner.cpp`, in `RunSteps`' dispatch switch add:

```cpp
		case GLRStepType::CALLBACK:
			PerformCallback(step, skipGLCalls);
			break;
```

If the pre-pass switch at ~635 (render-pass bookkeeping) has no `default`, add `case GLRStepType::CALLBACK: break;`. Implementation after `PerformBlit`:

```cpp
void GLQueueRunner::PerformCallback(const GLRStep &step, bool skipGLCalls) {
	CHECK_GL_ERROR_IF_DEBUG();
	GLRNativeCallbackFn *fn = step.callback.fn;
	const_cast<GLRStep &>(step).callback.fn = nullptr;
	if (!fn)
		return;
	if (!skipGLCalls) {
		GLRNativeCallbackInfo info{ step.callback.src, step.callback.dst };
		(*fn)(info);
		RestoreBaselineStateAfterCallback();
	}
	delete fn;
	CHECK_GL_ERROR_IF_DEBUG();
}

// PerformRenderPass assumes this state on entry (see its prologue: it only disables tests/blend/cull
// and rebinds the global VAO). Foreign code (librashader) binds its own FBOs, programs, VAOs, sampler
// objects and may enable GL_FRAMEBUFFER_SRGB, so put everything it can touch back.
void GLQueueRunner::RestoreBaselineStateAfterCallback() {
	// Force the next fbo_bind_fb_target to actually bind.
	currentDrawHandle_ = (GLuint)-1;
	currentReadHandle_ = (GLuint)-1;
	fbo_bind_fb_target(false, 0);
	fbo_bind_fb_target(true, 0);
	if (gl_extensions.ARB_vertex_array_object || gl_extensions.IsGLES) {
		glBindVertexArray(globalVAO_);
	}
	glUseProgram(0);
	if (gl_extensions.GLES3 || gl_extensions.ARB_sampler_objects) {
		for (int i = 0; i < MAX_GL_TEXTURE_SLOTS; i++)
			glBindSampler(i, 0);
	}
	glActiveTexture(GL_TEXTURE0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glStencilMask(0xFF);
#ifndef USING_GLES2
	if (!gl_extensions.IsGLES)
		glDisable(GL_FRAMEBUFFER_SRGB);
#endif
}
```

Declare `void RestoreBaselineStateAfterCallback();` in the class. Check the exact names of the VAO/sampler capability flags in `Common/GPU/OpenGL/GLFeatures.h` (`gl_extensions.ARB_vertex_array_object`, `ARB_sampler_objects` or equivalents); if a flag does not exist, gate on `gl_extensions.GLES3 || gl_extensions.VersionGEThan(3, 3)`. `globalVAO_` is only created when VAOs are supported — mirror the condition `PerformRenderPass` uses at ~line 802 for `glBindVertexArray(globalVAO_)`.

Free-exactly-once at discard: at both `delete steps[i];` sites in `RunSteps` (~655, ~735) and in `GLRenderManager::ThreadEnd` (~114) add, before the delete: `if (steps[i]->stepType == GLRStepType::CALLBACK) delete steps[i]->callback.fn;` (the `PerformCallback` path already nulls `fn`, so this is a no-op for executed steps).

Step-name/log switches (~1837–1855 and any other `switch (step.stepType)`): add `case GLRStepType::CALLBACK:` with a `"CALLBACK %s"` string. Compile with `-Wswitch` clean.

- [ ] **Step 3: Produce the step**

`GLRenderManager.h` public API next to `BlitFramebuffer`:

```cpp
	// Runs fn on the GL thread with the context current, outside any render pass. src/dst may be null.
	// fn may change any GL state; the runner restores PPSSPP's baseline afterwards.
	void RunNativeCallback(GLRFramebuffer *src, GLRFramebuffer *dst, GLRNativeCallbackFn fn, const char *tag);
```

`GLRenderManager.cpp` after `BlitFramebuffer`:

```cpp
void GLRenderManager::RunNativeCallback(GLRFramebuffer *src, GLRFramebuffer *dst, GLRNativeCallbackFn fn, const char *tag) {
	curRenderStep_ = nullptr;  // EndCurRenderStep equivalent in this manager (see Finish()).
	GLRStep *step = new GLRStep{ GLRStepType::CALLBACK };
	step->callback.src = src;
	step->callback.dst = dst;
	step->callback.fn = new GLRNativeCallbackFn(std::move(fn));
	if (src)
		step->dependencies.insert(src);
	if (dst)
		step->dependencies.insert(dst);
	step->tag = tag;
	steps_.push_back(step);
}
```

Confirm how `BlitFramebuffer`/`CopyFramebuffer` end the current render step in this file (they do not touch `curRenderStep_` at ~274–288 because GL's `BindFramebufferAsRenderTarget` starts a new step whenever needed); follow whatever the COPY/BLIT producers do and do not invent extra bookkeeping.

- [ ] **Step 4: Build + tests**

Run: `cmake --build build-unittest --target PPSSPPUnitTest PPSSPPSDL -j12 && ./build-unittest/PPSSPPUnitTest all`
Expected: builds without new warnings in the two GL files; `76 tests passed.`

- [ ] **Step 5: Commit**

```bash
git add Common/GPU/OpenGL/GLQueueRunner.h Common/GPU/OpenGL/GLQueueRunner.cpp Common/GPU/OpenGL/GLRenderManager.h Common/GPU/OpenGL/GLRenderManager.cpp
git commit -m "OpenGL: add CALLBACK render step for native code on the GL thread

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: thin3d OpenGL — `SupportsNativeCallback`, `RunNativeCallback`, `GL_GET_PROC_ADDRESS`

**Files:**
- Modify: `Common/GPU/thin3d.h` (`enum class NativeObject`: add `GL_GET_PROC_ADDRESS` after `VULKAN_GET_INSTANCE_PROC_ADDR`; add a comment on `NativeCallbackInfo` that `srcFormat/dstFormat` carry backend-native enums — `VkFormat` on Vulkan, sized GL internal format on OpenGL)
- Modify: `Common/GPU/OpenGL/thin3d_gl.cpp` (`class OpenGLContext` ~321; `OpenGLFramebuffer` ~931 wraps `GLRFramebuffer *framebuffer_`; `BlitFramebuffer` ~1585 shows the cast pattern; `GetNativeObject` ~1630–1640)

**Interfaces:**
- Consumes: `GLRenderManager::RunNativeCallback(GLRFramebuffer*, GLRFramebuffer*, GLRNativeCallbackFn, const char*)` (Task 1); `GLRFramebuffer::color_texture.texture`, `width`, `height`.
- Produces: `OpenGLContext::SupportsNativeCallback() const` (version-gated), `OpenGLContext::RunNativeCallback(...)`, `NativeObject::GL_GET_PROC_ADDRESS` returning a `const void *(*)(const char *)` as `uint64_t`.

- [ ] **Step 1: thin3d.h**

Add `GL_GET_PROC_ADDRESS,` as the last `NativeObject` enumerator. Above `struct NativeCallbackInfo` extend the comment: "`srcFormat`/`dstFormat` are backend-native: `VkFormat` on Vulkan, a sized GL internal format (e.g. `GL_RGBA8`) on OpenGL."

- [ ] **Step 2: OpenGLContext**

Declarations in `class OpenGLContext`:

```cpp
	bool SupportsNativeCallback() const override;
	bool RunNativeCallback(Framebuffer *src, Framebuffer *dst, NativeCallbackFn fn, const char *tag) override;
```

Definitions:

```cpp
bool OpenGLContext::SupportsNativeCallback() const {
	// librashader's GL runtime needs GL 3.3+ or GLES 3.0+ (UBOs, sampler objects, #version 330/300 es).
	return gl_extensions.IsGLES ? gl_extensions.GLES3 : gl_extensions.VersionGEThan(3, 3);
}

bool OpenGLContext::RunNativeCallback(Framebuffer *srcfb, Framebuffer *dstfb, NativeCallbackFn fn, const char *tag) {
	if (!SupportsNativeCallback())
		return false;
	GLRFramebuffer *src = srcfb ? ((OpenGLFramebuffer *)srcfb)->framebuffer_ : nullptr;
	GLRFramebuffer *dst = dstfb ? ((OpenGLFramebuffer *)dstfb)->framebuffer_ : nullptr;
	renderManager_.RunNativeCallback(src, dst, [fn = std::move(fn)](const GLRNativeCallbackInfo &gl) {
		NativeCallbackInfo info;
		const uint32_t GL_RGBA8_SIZED = 0x8058;  // PPSSPP creates FBO color as unsized GL_RGBA/UNSIGNED_BYTE; report the sized equivalent.
		if (gl.src) {
			info.srcTexture = gl.src->color_texture.texture;
			info.srcFormat = GL_RGBA8_SIZED;
			info.srcWidth = gl.src->width;
			info.srcHeight = gl.src->height;
		}
		if (gl.dst) {
			info.dstTexture = gl.dst->color_texture.texture;
			info.dstFormat = GL_RGBA8_SIZED;
			info.dstWidth = gl.dst->width;
			info.dstHeight = gl.dst->height;
		}
		fn(info);
	}, tag);
	return true;
}
```

If `framebuffer_` is private on `OpenGLFramebuffer`, use its existing accessor (search the class for a getter such as `GetFramebuffer()`/`framebuffer()`), or add `GLRFramebuffer *GetFB() const { return framebuffer_; }` mirroring `VKFramebuffer::GetFB()`.

- [ ] **Step 3: Proc-address resolver**

In `thin3d_gl.cpp` add a file-static resolver and expose it:

```cpp
#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/CommonWindows.h"
#else
#include <dlfcn.h>
#endif
#if defined(__ANDROID__) || defined(USING_EGL)
#include <EGL/egl.h>
#endif

// librashader's GL runtime loads every entry point through this. It must resolve both core
// functions and extensions for the *current* context.
static const void *GLGetProcAddress(const char *name) {
#if PPSSPP_PLATFORM(WINDOWS)
	void *p = (void *)wglGetProcAddress(name);
	if (!p) {
		static HMODULE opengl32 = GetModuleHandleW(L"opengl32.dll");
		if (opengl32)
			p = (void *)GetProcAddress(opengl32, name);
	}
	return p;
#else
#if defined(__ANDROID__) || defined(USING_EGL)
	void *p = (void *)eglGetProcAddress(name);
	if (p)
		return p;
#endif
	return dlsym(RTLD_DEFAULT, name);  // macOS OpenGL.framework, Linux libGL/libGLESv2 already loaded
#endif
}
```

In `OpenGLContext::GetNativeObject` add `case NativeObject::GL_GET_PROC_ADDRESS: return (uint64_t)(uintptr_t)&GLGetProcAddress;`. On Windows `wglGetProcAddress` needs `<GL/wgl.h>`/`windows.h` — `GLCommon.h` already brings glew on desktop, which declares it; verify with a grep and adjust includes so the macOS build stays clean (`#if` blocks compile-checked only where the platform allows; state this in the report).

- [ ] **Step 4: Build + tests**

Run: `cmake --build build-unittest --target PPSSPPUnitTest PPSSPPSDL -j12 && ./build-unittest/PPSSPPUnitTest all`
Expected: `76 tests passed.`; Vulkan and D3D `GetNativeObject` switches still compile (they have `default:`; if any lacks it and warns, add the case returning 0).

- [ ] **Step 5: Commit**

```bash
git add Common/GPU/thin3d.h Common/GPU/OpenGL/thin3d_gl.cpp
git commit -m "thin3d: OpenGL RunNativeCallback/SupportsNativeCallback and GL_GET_PROC_ADDRESS

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: Split `LibrashaderFilterChain` into a core plus Vulkan/OpenGL runtime adapters; add the GL runtime

**Files:**
- Create: `GPU/Common/Slang/LibrashaderRuntime.h`, `GPU/Common/Slang/LibrashaderRuntimeVulkan.cpp`, `GPU/Common/Slang/LibrashaderRuntimeOpenGL.cpp`
- Modify: `GPU/Common/Slang/LibrashaderFilterChain.h/.cpp` (remove Vulkan specifics: the `VulkanContext.h` include, `libra_device_vk_t` assembly, the `vk_*` callback body, the deletion-queue free), `Common/GPU/Librashader/LibrashaderLoader.h` (add `#define LIBRA_RUNTIME_OPENGL` beside `LIBRA_RUNTIME_VULKAN`), `GPU/CMakeLists.txt`

**Interfaces:**
- Consumes: `Draw::NativeCallbackInfo` (both backends' fields), `Draw::DrawContext::RunNativeCallback`, `NativeObject::CONTEXT` / `VULKAN_GET_INSTANCE_PROC_ADDR` (Vulkan), `NativeObject::GL_GET_PROC_ADDRESS` (Task 2), `GetGPUBackend()` (`Core/System.h`), `Librashader::Instance()` members `gl_filter_chain_create/frame/set_param/free` (signatures below).
- Produces:
  ```cpp
  // GPU/Common/Slang/LibrashaderRuntime.h
  struct LibrashaderRenderState {            // was LibrashaderFilterChain::RenderState; same fields plus glChain
      libra_shader_preset_t preset = nullptr;
      libra_vk_filter_chain_t vkChain = nullptr;
      libra_gl_filter_chain_t glChain = nullptr;
      int callbacksSinceCreate = 0;
      std::atomic<bool> ready{false};
      std::atomic<bool> failed{false};
      std::mutex errorLock;
      std::string lastError;
  };
  struct LibrashaderFrameArgs { int frameCount; int sourceW, sourceH; std::map<std::string, float> overrides; };
  class LibrashaderRuntime {
  public:
      virtual ~LibrashaderRuntime() = default;
      virtual LIBRA_PRESET_CTX_RUNTIME PresetRuntime() const = 0;       // VULKAN or GL_CORE
      virtual bool Init(Draw::DrawContext *draw, std::string *error) = 0;  // grabs device handles / proc loader
      // Emu thread. Returns the function RunNativeCallback executes on the render thread.
      virtual Draw::NativeCallbackFn MakeFrameCallback(std::shared_ptr<LibrashaderRenderState> rs, LibrashaderFrameArgs args) = 0;
      // Emu thread. Frees rs->preset / chain on the thread the backend requires; draw may be null (device gone).
      virtual void QueueFree(Draw::DrawContext *draw, std::shared_ptr<LibrashaderRenderState> rs) = 0;
  };
  std::unique_ptr<LibrashaderRuntime> CreateLibrashaderRuntime(GPUBackend backend);  // nullptr if unsupported
  ```

librashader GL signatures (vendored header, authoritative): `libra_error_t gl_filter_chain_create(libra_shader_preset_t *preset, libra_gl_loader_t loader, const filter_chain_gl_opt_t *options, libra_gl_filter_chain_t *out)`; `gl_filter_chain_frame(libra_gl_filter_chain_t *chain, size_t frame_count, libra_image_gl_t image, libra_image_gl_t out, const libra_viewport_t *viewport, const float *mvp, const frame_gl_opt_t *opt)`; `gl_filter_chain_set_param(libra_gl_filter_chain_t *chain, const char *name, float value)`; `gl_filter_chain_free(libra_gl_filter_chain_t *chain)`. `libra_gl_loader_t = const void *(*)(const char *)`. `libra_image_gl_t { uint32_t handle; uint32_t format; uint32_t width; uint32_t height; }`. `filter_chain_gl_opt_t { LIBRASHADER_API_VERSION version; uint16_t glsl_version; bool use_dsa; bool force_no_mipmaps; bool disable_cache; }`. `frame_gl_opt_t` has the same fields as `frame_vk_opt_t` (`version, clear_history, frame_direction, rotation, total_subframes, current_subframe, aspect_ratio, frames_per_second, frametime_delta, color_space, ...`).

- [ ] **Step 1: Write the runtime interface and move the Vulkan code (no behavior change)**

Create `LibrashaderRuntime.h` with the declarations above (GPL header, `#if USE_LIBRASHADER`, includes `LibrashaderLoader.h`, `<atomic>`, `<map>`, `<memory>`, `<mutex>`, `<string>`; forward-declare `namespace Draw { class DrawContext; }`, `enum class GPUBackend;` and include `Common/GPU/thin3d.h` for `NativeCallbackFn`).

Create `LibrashaderRuntimeVulkan.cpp`: class `LibrashaderRuntimeVulkan : public LibrashaderRuntime` whose `Init` reads `VulkanContext *` and `vkGetInstanceProcAddr` via `NativeObject::CONTEXT` / `VULKAN_GET_INSTANCE_PROC_ADDR` and fills a member `libra_device_vk_t device_`; `MakeFrameCallback` returns exactly the lambda body currently in `LibrashaderFilterChain::Run` (deferred create with `callbacksSinceCreate` gate, set_param loop, `vk_filter_chain_frame` with the native-size comment), renamed `chain` → `vkChain`; `QueueFree` is the current `ReleaseChain` deletion-queue body (`VulkanContext::Delete().QueueCallback` freeing `vkChain` and `preset`), with the null-`draw` early-out. `PresetRuntime()` returns `LIBRA_PRESET_CTX_RUNTIME_VULKAN`.

Rewrite `LibrashaderFilterChain.cpp` to use the runtime: constructor takes `Draw::DrawContext *` and calls `runtime_ = CreateLibrashaderRuntime(GetGPUBackend())` then `runtime_->Init(draw_, &err)` (fail `Load` with the error if either is null/false); `Load` sets the preset ctx runtime from `runtime_->PresetRuntime()`; `Run` builds `LibrashaderFrameArgs` and calls `draw_->RunNativeCallback(chainInput, output_, runtime_->MakeFrameCallback(render_, args), "librashader")`; `ReleaseChain` calls `runtime_->QueueFree(draw_, std::move(oldState))`. `RenderState` becomes `LibrashaderRenderState` from the header. Keep the history detection, native-input blit, output management, `ready` semantics, logging and error surfacing unchanged. `CreateLibrashaderRuntime` lives in `LibrashaderFilterChain.cpp` (or a tiny `LibrashaderRuntime.cpp`) and returns the Vulkan adapter for `GPUBackend::VULKAN`; for `OPENGL` it returns the GL adapter added in Step 2; otherwise `nullptr`.

Build and run: `cmake --build build-unittest --target PPSSPPUnitTest PPSSPPSDL -j12 && ./build-unittest/PPSSPPUnitTest all` → `76 tests passed.` Then verify **no Vulkan regression on device**: run PPSSPPSDL (Vulkan) with `stock.slangp` and the frame dump, confirm `librashader loaded`, `Slang chain backend: librashader`, rendering visible, and a screenshot pixel-identical to Phase 1's `/tmp/ppsspp-t8/shot-final-stock-libra.png` (or re-take both with the toggle to compare). Commit:

```bash
git add GPU/Common/Slang/LibrashaderRuntime.h GPU/Common/Slang/LibrashaderRuntimeVulkan.cpp GPU/Common/Slang/LibrashaderFilterChain.h GPU/Common/Slang/LibrashaderFilterChain.cpp GPU/CMakeLists.txt
git commit -m "slang: split LibrashaderFilterChain into a core and a Vulkan runtime adapter

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

- [ ] **Step 2: GL runtime adapter**

`LibrashaderLoader.h`: add `#define LIBRA_RUNTIME_OPENGL` next to `#define LIBRA_RUNTIME_VULKAN` (before including `librashader_ld.h`). The GL section of `librashader.h` uses plain `uint32_t` handles and pulls in no GL headers.

Create `LibrashaderRuntimeOpenGL.cpp`:

```cpp
class LibrashaderRuntimeOpenGL : public LibrashaderRuntime {
public:
	LIBRA_PRESET_CTX_RUNTIME PresetRuntime() const override { return LIBRA_PRESET_CTX_RUNTIME_GL_CORE; }

	bool Init(Draw::DrawContext *draw, std::string *error) override {
		loader_ = (libra_gl_loader_t)(uintptr_t)draw->GetNativeObject(Draw::NativeObject::GL_GET_PROC_ADDRESS);
		if (!loader_) {
			if (error) *error = "OpenGL backend exposes no proc-address loader";
			return false;
		}
		return true;
	}

	Draw::NativeCallbackFn MakeFrameCallback(std::shared_ptr<LibrashaderRenderState> rs, LibrashaderFrameArgs args) override {
		libra_gl_loader_t loader = loader_;
		return [rs, loader, args](const Draw::NativeCallbackInfo &info) {
			const libra_instance_t &lib = Librashader::Instance();
			if (rs->failed.load())
				return;
			if (!rs->glChain) {
				if (!rs->preset)
					return;
				filter_chain_gl_opt_t opts{};
				opts.version = LIBRASHADER_CURRENT_VERSION;
				opts.glsl_version = 0;      // auto-detect from the current context (330 core / 300 es ...)
				opts.use_dsa = false;       // needs GL 4.5; macOS is 4.1, GLES has none
				opts.force_no_mipmaps = false;
				opts.disable_cache = false; // librashader disables its own cache without DSA
				std::string err = Librashader::ErrorToString(lib.gl_filter_chain_create(&rs->preset, loader, &opts, &rs->glChain));
				rs->preset = nullptr;  // consumed either way (see LibrashaderRuntimeVulkan for the same rule)
				if (!err.empty() || !rs->glChain) {
					std::lock_guard<std::mutex> guard(rs->errorLock);
					rs->lastError = "create: " + (err.empty() ? std::string("unknown error") : err);
					rs->failed.store(true);
					return;
				}
				// No command-buffer contract on GL: the chain is usable immediately.
				rs->ready.store(true);
			}
			for (const auto &kv : args.overrides) {
				libra_error_t e = lib.gl_filter_chain_set_param(&rs->glChain, kv.first.c_str(), kv.second);
				if (e) (void)Librashader::ErrorToString(e);  // unknown parameter names are not fatal
			}
			libra_image_gl_t in{};
			in.handle = info.srcTexture;
			in.format = info.srcFormat;
			in.width = (uint32_t)args.sourceW;   // declared native size; see spec §12 and the Vulkan adapter
			in.height = (uint32_t)args.sourceH;
			libra_image_gl_t out{};
			out.handle = info.dstTexture;
			out.format = info.dstFormat;
			out.width = (uint32_t)info.dstWidth;
			out.height = (uint32_t)info.dstHeight;
			libra_viewport_t vp{ 0.0f, 0.0f, (uint32_t)info.dstWidth, (uint32_t)info.dstHeight };
			frame_gl_opt_t fopts{};
			fopts.version = LIBRASHADER_CURRENT_VERSION;
			fopts.clear_history = false;
			fopts.frame_direction = 1;
			fopts.rotation = 0;
			fopts.total_subframes = 1;
			fopts.current_subframe = 1;
			fopts.aspect_ratio = 0.0f;
			fopts.frames_per_second = 60.0f;
			fopts.frametime_delta = 16;  // librashader.h: milliseconds
			fopts.color_space = LIBRA_COLOR_SPACE_SDR;
			std::string err = Librashader::ErrorToString(lib.gl_filter_chain_frame(&rs->glChain, (size_t)args.frameCount, in, out, &vp, nullptr, &fopts));
			if (!err.empty()) {
				std::lock_guard<std::mutex> guard(rs->errorLock);
				rs->lastError = "frame: " + err;
				rs->failed.store(true);
			}
		};
	}

	void QueueFree(Draw::DrawContext *draw, std::shared_ptr<LibrashaderRenderState> rs) override {
		if (!draw || !Librashader::IsLoaded()) {
			// Context is gone (DeviceLost): GL objects died with it; the Rust-side allocation is leaked knowingly.
			if (rs->glChain || rs->preset)
				WARN_LOG(Log::G3D, "LibrashaderRuntimeOpenGL: dropping chain without freeing (no GL context)");
			return;
		}
		// libra_gl_filter_chain_free requires the creating context to be current: do it on the GL thread.
		draw->RunNativeCallback(nullptr, nullptr, [rs](const Draw::NativeCallbackInfo &) {
			const libra_instance_t &lib = Librashader::Instance();
			if (rs->glChain) {
				(void)Librashader::ErrorToString(lib.gl_filter_chain_free(&rs->glChain));
				rs->glChain = nullptr;
			}
			if (rs->preset) {
				(void)Librashader::ErrorToString(lib.preset_free(&rs->preset));
				rs->preset = nullptr;
			}
		}, "librashader_free");
	}

private:
	libra_gl_loader_t loader_ = nullptr;
};
```

Wire it into `CreateLibrashaderRuntime` for `GPUBackend::OPENGL`. Add the file to `GPU/CMakeLists.txt`. Note on the frame the chain is created: `Run()` on the emu thread already returned `nullptr` for that frame (it read `ready == false` before enqueuing), so the first GL frame is rendered and discarded exactly as on Vulkan; acceptable (spec §13 Q3).

Build + tests: `76 tests passed.`; OFF variant builds. Commit:

```bash
git add Common/GPU/Librashader/LibrashaderLoader.h GPU/Common/Slang/LibrashaderRuntimeOpenGL.cpp GPU/Common/Slang/LibrashaderFilterChain.cpp GPU/CMakeLists.txt
git commit -m "slang: OpenGL runtime adapter for LibrashaderFilterChain

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: Admit OpenGL in the selector (test first)

**Files:**
- Modify: `unittest/TestLibrashader.cpp` (`TestSlangChainBackendSelection`, the row `ChooseSlangChainBackend(true, true, GPUBackend::OPENGL, true) == InTree`), `GPU/Common/Slang/SlangChainFactory.cpp` (`ChooseSlangChainBackend`)

**Interfaces:** unchanged signature `SlangChainBackend ChooseSlangChainBackend(bool userPrefersLibrashader, bool librashaderLoaded, GPUBackend gpuBackend, bool drawSupportsNativeCallback)`. New truth: Librashader iff all three bools AND `gpuBackend ∈ {VULKAN, OPENGL}`.

- [ ] **Step 1: Change the test first**

Replace the OPENGL row with:

```cpp
	// Phase 2: OpenGL is admitted; the version gate lives in DrawContext::SupportsNativeCallback().
	EXPECT_TRUE(ChooseSlangChainBackend(true, true, GPUBackend::OPENGL, true) == SlangChainBackend::Librashader);
	EXPECT_TRUE(ChooseSlangChainBackend(true, true, GPUBackend::OPENGL, false) == SlangChainBackend::InTree);
```

Keep the `DIRECT3D11 → InTree` row.

- [ ] **Step 2: See it fail**

Run: `cmake --build build-unittest --target PPSSPPUnitTest -j12 && ./build-unittest/PPSSPPUnitTest SlangChainBackendSelection`
Expected: exit ≠ 0 (OPENGL still maps to InTree).

- [ ] **Step 3: Implement**

In `ChooseSlangChainBackend`: `if (gpuBackend != GPUBackend::VULKAN && gpuBackend != GPUBackend::OPENGL) return SlangChainBackend::InTree;`

- [ ] **Step 4: See it pass**

Run the focused test (exit 0) and `./build-unittest/PPSSPPUnitTest all` → `76 tests passed.`

- [ ] **Step 5: Commit**

```bash
git add unittest/TestLibrashader.cpp GPU/Common/Slang/SlangChainFactory.cpp
git commit -m "slang: select librashader on OpenGL when the context supports native callbacks

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: On-device verification (macOS OpenGL) and docs

**Files:**
- Possibly modify (fixes only): `GPU/Common/Slang/LibrashaderRuntimeOpenGL.cpp`, `Common/GPU/OpenGL/GLQueueRunner.cpp`, `Common/GPU/OpenGL/thin3d_gl.cpp`
- Modify: `docs/superpowers/librashader-build.md` (GL section: requirements GL 3.3+/GLES 3.0+, options used, macOS 4.1 note, in-tree chain is Vulkan-only so "toggle off" on GL = raw image), `docs/superpowers/specs/2026-09-14-librashader-integration-design.md` (§6.2 marks Phase 2 done with the state-restore list; §10 row 2 exit criterion result), this plan (results table below)

- [x] **Step 1: Load on GL**

Set `GraphicsBackend = 0 (OPENGL)`, `SlangUseLibrashader = True`, `SlangShaderPreset = .../stock.slangp`; launch with `/tmp/ppsspp-t8/locoroco.ppdmp`; log must show `librashader loaded (ABI 2, API 5)`, `Slang chain backend: librashader`, `LibrashaderFilterChain: preset parsed`, no `LibrashaderFilterChain:`/`LibrashaderRuntimeOpenGL:` errors, and the dump image visible (not black) within a few frames. Check `glGetError` spam: run once with PPSSPP's GL debug logging if available (`CHECK_GL_ERROR_IF_DEBUG` is compiled in debug builds; a `-DCMAKE_BUILD_TYPE=Debug` configure in a separate dir is acceptable if needed).

- [x] **Step 2: Visual A/B GL vs Vulkan (same chain)**

For `stock`, `lut`, `feedback`, `lcd-psp-matrix`, `twopass`: screenshot on GL, then the same preset on Vulkan (`GraphicsBackend = 3 (VULKAN)`), same window size/position. Expect visually identical output; differences must be explainable (GL vs Vulkan rounding, y-flip conventions — if the GL output is vertically flipped relative to Vulkan, that is a real bug in how the output texture is presented and must be fixed: compare with how `PresentationCommon` samples `output_` on GL and whether librashader's final pass expects a flipped MVP; the `mvp` argument of `gl_filter_chain_frame` accepts a 16-float matrix — pass a y-flipping MVP if needed and document it).

- [x] **Step 3: State restore**

After the chain runs, PPSSPP's own UI/OSD and the present blit must render correctly (no missing textures, wrong FBO, or black UI). Toggle the pause menu / settings over the running dump and screenshot. Any corruption points at `RestoreBaselineStateAfterCallback` — extend it with the missing state and note it in the plan's Global Constraints.

- [x] **Step 4: Switching, toggle, absent library**

Change presets across three runs, toggle Off/On (Off on GL means raw image — confirm no crash), rename the dylib → `librashader unavailable`, in-tree (raw image on GL), restore.

- [x] **Step 5: Record**

Fill the table and update the docs:

Environment: macOS 15 (Darwin 25.6.0), Apple M2 Pro, SDL GL **4.1 core** over Metal (`GPU Vendor : Apple ; renderer: Apple M2 Pro version str: 4.1 Metal - 90.5 ; GLSL version str: 4.10`), `librashader.dylib` 0.12.0 (ABI 2 / API 5, `runtime-vulkan,runtime-opengl`), HEAD `ab255dd8d3`, `InternalResolution = 0` (4x → 1920x1088), window 960x544 logical at (100,100), content `/tmp/ppsspp-t8/locoroco.ppdmp`. Harness `/tmp/ppsspp-t8/runp2.sh` + `setini.py` + `setsect.py` + `cmp.py`. Full report: `.superpowers/sdd/2026-09-14-librashader-phase2-opengl/task-5-report.md`.

| Check | Result | Notes |
|---|---|---|
| GL load + first frame | **PASS** | `librashader loaded (ABI 2, API 5)`, `Slang chain backend: librashader`, `LibrashaderFilterChain: preset parsed: …/stock.slangp`. No `LibrashaderFilterChain:`/`LibrashaderRuntimeOpenGL:` error, no chain-disabled line, image correct and upright (`shot-p2-stock-gl.png`). `glsl_version = 0` auto-detect worked on the 4.1 core context — no `330`/`410` override needed. |
| stock GL vs Vulkan | **PASS — bit-identical** | 0 / 2088960 px differ; the two PNGs are byte-identical (same MD5). |
| lut GL vs Vulkan | **PASS — bit-identical** | 0 / 2088960 px differ (same MD5). |
| feedback GL vs Vulkan | **PASS — bit-identical** | 0 / 2088960 px differ (same MD5). |
| lcd-psp-matrix GL vs Vulkan | **PASS — bit-identical** | 0 / 2088960 px differ (same MD5); subpixel mask period *and* phase identical, so no y-flip and no half-pixel offset. |
| twopass GL vs Vulkan | **PASS — bit-identical** | 0 / 2088960 px differ (same MD5); `input mode: native-sized copy, preset samples OriginalHistoryN` on both backends. |
| No vertical flip | **PASS** | Byte-identical captures rule out any flip; no `mvp` argument needed (`nullptr` passed to `gl_filter_chain_frame`). |
| State restore (UI/OSD intact) | **PASS** | With the chain active on GL: FPS counter + full debug-statistics overlay (`shot-p2-overlay-gl.png`) and the ImGui debugger — menu bar, window chrome, the framebuffer-preview texture — over the filtered image (`shot-p2-imdbg-gl.png`). Text, shadows, backgrounds and the present blit all correct; `RestoreBaselineStateAfterCallback` needed no extension. Pause menu itself NOT RUN (needs a synthetic Esc; osascript has no Accessibility permission on this machine) — the overlays exercise the same UI/present path. |
| Preset switching / toggle | **PASS (across runs)** | Five different presets loaded correctly in five consecutive GL runs; `SlangUseLibrashader = False` on GL gives the raw image (`Slang chain backend: in-tree` + `Failed to load slang preset …: slang passes require the Vulkan backend in Phase 1`), no crash, clean `Leaving main`. In-process switching (chain teardown while a CALLBACK step may be pending) still NOT RUN — same Accessibility limitation as Phase 1 Task 8. |
| Library absent fallback | **PASS** | Dylib renamed away → `librashader unavailable: librashader not found or ABI mismatch (want ABI 2)`, `Slang chain backend: in-tree`, raw image on GL, no crash; restored and reloaded fine afterwards. |
| Chain free / shutdown | **PASS** | No `LibrashaderRuntimeOpenGL: dropping chain without freeing` in any run, no GL/driver error at exit, `Leaving main` in all 12 GL runs (8 with a librashader chain, 3 with the toggle off, 1 with the dylib renamed away). |
| `glGetError` spam | **NOT RUN — substituted** | `DEBUG_OPENGL` cannot be enabled on macOS: `CHECK_GL_ERROR_IF_DEBUG()` expands to `__debugbreak()`, which PPSSPP only defines on Windows (`Common/CommonFuncs.h`), and fixing that means editing `Common/GPU/OpenGL/GLDebugLog.h` — outside this task's allowed files. Substituted a run with `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1` (macOS GL runs on Metal): Metal API + GPU validation enabled, chain active, **no validation error**, clean exit (`run-p2-mtldebug-gl.log`). |
| GLES 3 (Android) | NOT RUN — Phase 3 (no librashader.so on device yet) | |
| Unit tests | **PASS** | `./build-unittest/PPSSPPUnitTest all` → `76 tests passed.` at `ab255dd8d3` (no code change was needed in this task). |

```bash
git add docs/superpowers/plans/2026-09-14-librashader-phase2-opengl.md docs/superpowers/librashader-build.md docs/superpowers/specs/2026-09-14-librashader-integration-design.md GPU/Common/Slang/LibrashaderRuntimeOpenGL.cpp Common/GPU/OpenGL/GLQueueRunner.cpp Common/GPU/OpenGL/thin3d_gl.cpp
git commit -m "librashader: Phase 2 OpenGL on-device verification results and docs

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Out of scope (later phases)

- Phase 3: Android `cargo ndk` build of `librashader.so`, jniLibs packaging, CI jobs, GLES 3 verification on device.
- Phase 4: remove the in-tree chain, revert thin3d slot/descriptor bumps and sRGB render-pass keying, D3D11 runtime.
