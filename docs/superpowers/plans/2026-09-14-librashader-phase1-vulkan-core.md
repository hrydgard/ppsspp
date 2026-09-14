# librashader Integration — Phase 1 (Desktop Vulkan Core) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render slang presets through the librashader shared library on the Vulkan backend, behind the existing `SlangFilterChain` contract, with the in-tree chain kept as a runtime-selectable fallback.

**Architecture:** A new `CALLBACK` step type in `VulkanRenderManager`/`VulkanQueueRunner` runs a caller-supplied function on the render thread with the frame's command buffer, after transitioning the source framebuffer to `SHADER_READ_ONLY_OPTIMAL` and the destination to `COLOR_ATTACHMENT_OPTIMAL`. `thin3d` exposes it as `DrawContext::RunNativeCallback`. `LibrashaderFilterChain` implements the extracted `ISlangFilterChain` interface: it parses the preset on the emu thread, creates the librashader chain deferred inside the first callback, and records `libra_vk_filter_chain_frame` in every later callback. A pure selection function chooses librashader or the in-tree chain per frame-manager reload.

**Tech Stack:** C++17; librashader 0.12.0 C API (`include/librashader.h`, `librashader_ld.h`, MIT) loaded via `dlopen`/`LoadLibrary`; PPSSPP `thin3d`, `VulkanRenderManager`, `VulkanQueueRunner`, `VulkanBarrierBatch`; PPSSPP `unittest` harness; CMake.

**Spec:** `docs/superpowers/specs/2026-09-14-librashader-integration-design.md` (§6–§9 are the normative parts for this plan).

## Global Constraints

- **License header:** every new file carries the PPSSPP GPL 2.0 header (year `2026-`), copied from `GPU/Common/Slang/SlangFilterChain.h`. Vendored librashader headers keep their upstream MIT notice untouched.
- **Never link librashader.** Only `ext/librashader/include/*.h` is compiled in. No `-lrashader`, no static lib, no cargo call in the default CMake build.
- **Pinned version:** headers from tag `librashader-v0.12.0`; `LIBRASHADER_CURRENT_ABI == 2`, `LIBRASHADER_CURRENT_VERSION == 5`. A mismatch at load time means "not loaded", never a crash.
- **Thread rule:** `libra_vk_filter_chain_*` only inside a native callback (render thread). `libra_preset_*`, `libra_preset_ctx_*`, loader functions only on the emu thread.
- **Vulkan reference must not regress:** with `bSlangUseLibrashader=false` or the library absent, behavior and output are byte-identical to before this plan. Every task rebuilds and reruns the 19 `Slang*` unit tests.
- **Compile-gate:** all librashader code is inside `#if USE_LIBRASHADER`; the CMake option `USE_LIBRASHADER` defaults ON for Windows/macOS/Linux/Android and OFF elsewhere. Configure+build must pass with both values.
- **Framebuffer contract:** the chain output is a PPSSPP `Draw::Framebuffer` sized to the display rect, RGBA8, handed to `PresentationCommon::SourceFramebuffer` exactly as today.
- **Frames in flight:** `filter_chain_vk_opt_t.frames_in_flight = 3` (`VulkanContext::MAX_INFLIGHT_FRAMES`); `frame_count` is PPSSPP's monotonically increasing `gpuStats.totals.numFlips`.
- **Commit trailer:** end every commit message with `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- **Build + test env (macOS dev box):**
  - Configure once: `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DUNITTEST=ON -DUSE_SYSTEM_LIBPNG=ON -S . -B build-unittest`
  - Build: `cmake --build build-unittest --target PPSSPPUnitTest -j12`
  - Run one test: `./build-unittest/PPSSPPUnitTest <Name>` (exit 0 = pass). Tests are `bool TestXxx()` functions declared in `unittest/UnitTest.cpp` and registered with `TEST_ITEM(Xxx)`.
  - Full app for on-device checks: `cmake --build build-unittest --target PPSSPPSDL -j12`, binary `build-unittest/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL`. Vulkan on macOS runs through MoltenVK (bundled in `ext/vulkan/macOS`). Backend is `iGPUBackend` in `ppsspp.ini` (`3` = Vulkan).
  - Test presets: `assets/shaders/slang_test/stock.slangp`, `assets/shaders/slang_test/lcd-psp-matrix.slangp`, and an imported `crt/crt-royale.slangp` under the user slang directory (`Core/Slang/SlangPaths.cpp` decides where; on macOS it is under `~/Library/Application Support/PPSSPP/...` — check `GetSlangUserDir()` in that file for the exact path).

## File structure

| File | Responsibility |
|---|---|
| `ext/librashader/include/librashader.h`, `librashader_ld.h`, `vulkan/vulkan.h` (shim), `LICENSE`, `VERSION` | Vendored MIT headers, pinned; shim routes `<vulkan/vulkan.h>` to `ext/vulkan/vulkan.h` |
| `Common/GPU/Librashader/LibrashaderLoader.h/.cpp` | Load/unload the shared library once; expose `libra_instance_t`; error to string helper |
| `Common/GPU/Vulkan/VulkanQueueRunner.h/.cpp` | `VKRStepType::CALLBACK`, `VKRStep::callback`, `PerformCallback` |
| `Common/GPU/Vulkan/VulkanRenderManager.h/.cpp` | `RunNativeCallback(VKRFramebuffer*, VKRFramebuffer*, fn, tag)` step producer |
| `Common/GPU/thin3d.h` | `NativeCallbackInfo`, `NativeCallbackFn`, `DrawContext::RunNativeCallback`, `NativeObject::VULKAN_GET_INSTANCE_PROC_ADDR` |
| `Common/GPU/Vulkan/thin3d_vulkan.cpp` | `VKContext::RunNativeCallback` override; new native object |
| `GPU/Common/Slang/ISlangFilterChain.h` | Interface + `SlangChainBackend` enum + `ChooseSlangChainBackend` + factory decl |
| `GPU/Common/Slang/SlangFilterChain.h/.cpp` | Implements the interface (mechanical) |
| `GPU/Common/Slang/LibrashaderFilterChain.h/.cpp` | librashader-backed implementation (Vulkan runtime) |
| `GPU/Common/Slang/SlangChainFactory.cpp` | `ChooseSlangChainBackend`, `CreateSlangFilterChain` |
| `GPU/Common/FramebufferManagerCommon.h/.cpp` | Hold `ISlangFilterChain *`; use factory |
| `Core/Config.h/.cpp`, `UI/DeveloperToolsScreen.cpp` | `bSlangUseLibrashader` + checkbox |
| `CMakeLists.txt`, `Common/CMakeLists.txt`, `GPU/CMakeLists.txt` | option, sources, prebuilt copy |
| `unittest/TestLibrashader.cpp`, `unittest/UnitTest.cpp` | device-free tests |
| `docs/superpowers/librashader-build.md` | how to build/obtain the shared library |

---

### Task 1: Vendor headers, CMake option, loader with unit test

**Files:**
- Create: `ext/librashader/include/librashader.h`, `ext/librashader/include/librashader_ld.h`, `ext/librashader/include/vulkan/vulkan.h`, `ext/librashader/LICENSE`, `ext/librashader/VERSION`
- Create: `Common/GPU/Librashader/LibrashaderLoader.h`, `Common/GPU/Librashader/LibrashaderLoader.cpp`
- Create: `unittest/TestLibrashader.cpp`
- Modify: `CMakeLists.txt` (option near line 170–183 where other `option(...)` lines live; unittest sources near `unittest/TestSlangParser.cpp`), `Common/CMakeLists.txt` (inside `add_library(Common STATIC` list near `GPU/thin3d.cpp`), `unittest/UnitTest.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace Librashader {
  bool Load(std::string *error);          // idempotent, thread-safe, false if absent/ABI mismatch
  bool IsLoaded();
  const libra_instance_t &Instance();     // only valid when IsLoaded()
  std::string ErrorToString(libra_error_t err);  // frees err; "" when err == nullptr
  void Unload();
  }
  ```
  Preprocessor: `USE_LIBRASHADER` (0/1) from CMake. Env var `LIBRASHADER_PATH` overrides the library location.

- [ ] **Step 1: Vendor the pinned headers**

```bash
mkdir -p ext/librashader/include/vulkan
for f in librashader.h librashader_ld.h; do
  curl -sfL "https://raw.githubusercontent.com/SnowflakePowered/librashader/librashader-v0.12.0/include/$f" -o ext/librashader/include/$f
done
curl -sfL "https://raw.githubusercontent.com/SnowflakePowered/librashader/librashader-v0.12.0/LICENSE.md" -o ext/librashader/LICENSE || echo "MIT (see header files)" > ext/librashader/LICENSE
echo "librashader-v0.12.0 (C ABI 2, API 5)" > ext/librashader/VERSION
grep -n "LIBRASHADER_CURRENT_ABI 2\|LIBRASHADER_CURRENT_VERSION 5" ext/librashader/include/librashader.h   # must print both
```

Create the Vulkan shim so `#include <vulkan/vulkan.h>` inside `librashader.h` resolves to PPSSPP's vendored header:

```c
// ext/librashader/include/vulkan/vulkan.h
// Shim: librashader.h includes <vulkan/vulkan.h>; route it to PPSSPP's vendored Vulkan header.
#pragma once
#include "ext/vulkan/vulkan.h"
```

- [ ] **Step 2: CMake option and include path**

In `CMakeLists.txt`, next to the other `option(...)` lines (around line 178):

```cmake
if(WIN32 OR APPLE OR ANDROID OR (UNIX AND NOT IOS))
	set(USE_LIBRASHADER_DEFAULT ON)
else()
	set(USE_LIBRASHADER_DEFAULT OFF)
endif()
if(IOS OR LIBRETRO OR UWP)
	set(USE_LIBRASHADER_DEFAULT OFF)
endif()
option(USE_LIBRASHADER "Load librashader at runtime for slang shader rendering" ${USE_LIBRASHADER_DEFAULT})
if(USE_LIBRASHADER)
	add_compile_definitions(USE_LIBRASHADER=1)
	include_directories(ext/librashader/include)
else()
	add_compile_definitions(USE_LIBRASHADER=0)
endif()
```

In `Common/CMakeLists.txt`, inside the `add_library(Common STATIC` source list right after `GPU/thin3d.h`:

```cmake
	GPU/Librashader/LibrashaderLoader.cpp
	GPU/Librashader/LibrashaderLoader.h
```

- [ ] **Step 3: Write the failing test**

`unittest/TestLibrashader.cpp` (GPL header first):

```cpp
#include <cstdlib>
#include <string>
#include "unittest/UnitTest.h"
#include "Common/GPU/Librashader/LibrashaderLoader.h"

// Device-free: with LIBRASHADER_PATH pointing at a non-library, Load must fail cleanly,
// report an error string and leave IsLoaded() false. Load must be idempotent.
bool TestLibrashaderLoaderAbsent() {
#if USE_LIBRASHADER
	setenv("LIBRASHADER_PATH", "/nonexistent/dir/librashader.dylib", 1);
	Librashader::Unload();
	std::string err;
	EXPECT_FALSE(Librashader::Load(&err));
	EXPECT_FALSE(Librashader::IsLoaded());
	EXPECT_FALSE(err.empty());
	std::string err2;
	EXPECT_FALSE(Librashader::Load(&err2));  // second call: same answer, no crash
	EXPECT_FALSE(Librashader::IsLoaded());
	unsetenv("LIBRASHADER_PATH");
	Librashader::Unload();
#endif
	return true;
}
```

Register in `unittest/UnitTest.cpp`: add `bool TestLibrashaderLoaderAbsent();` after the `TestSlangParamOverride();` declaration and `TEST_ITEM(LibrashaderLoaderAbsent),` after `TEST_ITEM(SlangParamOverride),`. Add `unittest/TestLibrashader.cpp` after `unittest/TestSlangParser.cpp` in the `PPSSPPUnitTest` source list in `CMakeLists.txt`.

- [ ] **Step 4: Run test to verify it fails**

Run: `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DUNITTEST=ON -DUSE_SYSTEM_LIBPNG=ON -S . -B build-unittest && cmake --build build-unittest --target PPSSPPUnitTest -j12`
Expected: build FAILS with `'Common/GPU/Librashader/LibrashaderLoader.h' file not found`.

- [ ] **Step 5: Implement the loader**

`Common/GPU/Librashader/LibrashaderLoader.h`:

```cpp
#pragma once
#include <string>
#include "ppsspp_config.h"

#if USE_LIBRASHADER
// Vulkan types must come from PPSSPP's loader so VK_NO_PROTOTYPES etc. match.
#include "Common/GPU/Vulkan/VulkanLoader.h"
#define LIBRA_RUNTIME_VULKAN
#include "librashader_ld.h"

namespace Librashader {
// Loads the shared library once. Search order: $LIBRASHADER_PATH, <exe dir>/<platform name>,
// then the loader's default search path. Returns false (with *error set) if the library is
// absent or its ABI != LIBRASHADER_CURRENT_ABI. Safe to call from any thread, idempotent.
bool Load(std::string *error);
bool IsLoaded();
const libra_instance_t &Instance();
// Converts and frees a libra_error_t. Returns "" for nullptr.
std::string ErrorToString(libra_error_t err);
// For tests and shutdown only.
void Unload();
}
#endif
```

`Common/GPU/Librashader/LibrashaderLoader.cpp`:

```cpp
#include "Common/GPU/Librashader/LibrashaderLoader.h"
#if USE_LIBRASHADER
#include <mutex>
#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/CommonWindows.h"
#else
#include <dlfcn.h>
#endif

namespace Librashader {

static std::mutex g_mutex;
static bool g_attempted = false;
static libra_instance_t g_instance{};
static std::string g_error;
#if PPSSPP_PLATFORM(WINDOWS)
static HMODULE g_preloaded = nullptr;
#else
static void *g_preloaded = nullptr;
#endif

static const char *PlatformLibraryName() {
#if PPSSPP_PLATFORM(WINDOWS)
	return "librashader.dll";
#elif PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
	return "librashader.dylib";
#else
	return "librashader.so";
#endif
}

// Preloads from an explicit path so that librashader_load_instance()'s bare-name
// dlopen/LoadLibrary resolves to the already-loaded module.
static bool Preload(const Path &path) {
	if (!File::Exists(path))
		return false;
#if PPSSPP_PLATFORM(WINDOWS)
	g_preloaded = LoadLibraryW(path.ToWString().c_str());
#else
	g_preloaded = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
#endif
	return g_preloaded != nullptr;
}

bool Load(std::string *error) {
	std::lock_guard<std::mutex> guard(g_mutex);
	if (!g_attempted) {
		g_attempted = true;
		const char *env = getenv("LIBRASHADER_PATH");
		bool preloaded = false;
		if (env && env[0]) {
			preloaded = Preload(Path(env));
			if (!preloaded)
				g_error = std::string("LIBRASHADER_PATH set but not loadable: ") + env;
		}
		if (!preloaded && !(env && env[0])) {
			preloaded = Preload(File::GetExeDirectory() / PlatformLibraryName());
		}
		if (!(env && env[0]) || preloaded) {
			g_instance = librashader_load_instance();
			if (!g_instance.instance_loaded) {
				g_error = std::string("librashader not found or ABI mismatch (want ABI ") +
				          std::to_string(LIBRASHADER_CURRENT_ABI) + ")";
			} else {
				INFO_LOG(Log::G3D, "librashader loaded (ABI %d, API %d)", (int)g_instance.instance_abi_version(),
				         (int)g_instance.instance_api_version());
			}
		}
		if (!g_instance.instance_loaded)
			INFO_LOG(Log::G3D, "librashader unavailable: %s", g_error.c_str());
	}
	if (error)
		*error = g_instance.instance_loaded ? "" : g_error;
	return g_instance.instance_loaded;
}

bool IsLoaded() {
	std::lock_guard<std::mutex> guard(g_mutex);
	return g_attempted && g_instance.instance_loaded;
}

const libra_instance_t &Instance() {
	return g_instance;
}

std::string ErrorToString(libra_error_t err) {
	if (!err)
		return "";
	std::string result = "librashader error";
	if (g_instance.instance_loaded && g_instance.error_write && g_instance.error_free_string) {
		char *msg = nullptr;
		if (g_instance.error_write(err, &msg) == 0 && msg) {
			result = msg;
			g_instance.error_free_string(&msg);
		}
	}
	if (g_instance.instance_loaded && g_instance.error_free)
		g_instance.error_free(&err);
	return result;
}

void Unload() {
	std::lock_guard<std::mutex> guard(g_mutex);
	g_attempted = false;
	g_instance = libra_instance_t{};
	g_error.clear();
	if (g_preloaded) {
#if PPSSPP_PLATFORM(WINDOWS)
		FreeLibrary(g_preloaded);
#else
		dlclose(g_preloaded);
#endif
		g_preloaded = nullptr;
	}
}

}  // namespace Librashader
#endif  // USE_LIBRASHADER
```

Note for the implementer: `librashader_ld.h` defines `static inline` functions; including it from exactly one `.cpp` avoids duplicate-symbol concerns, and the header (`LibrashaderLoader.h`) is included by `LibrashaderFilterChain.cpp` too, which is fine because the functions are `static inline`.

- [ ] **Step 6: Run test to verify it passes**

Run: `cmake --build build-unittest --target PPSSPPUnitTest -j12 && ./build-unittest/PPSSPPUnitTest LibrashaderLoaderAbsent`
Expected: exit 0. Also: `cmake -S . -B build-nolibra -G Ninja -DUSE_LIBRASHADER=OFF -DUNITTEST=ON -DUSE_SYSTEM_LIBPNG=ON && cmake --build build-nolibra --target PPSSPPUnitTest -j12` builds and `./build-nolibra/PPSSPPUnitTest LibrashaderLoaderAbsent` exits 0 (test is a no-op there).

- [ ] **Step 7: Commit**

```bash
git add ext/librashader Common/GPU/Librashader CMakeLists.txt Common/CMakeLists.txt unittest/TestLibrashader.cpp unittest/UnitTest.cpp
git commit -m "librashader: vendor 0.12.0 C headers, USE_LIBRASHADER option, runtime loader

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: Vulkan CALLBACK step in VulkanQueueRunner and VulkanRenderManager

**Files:**
- Modify: `Common/GPU/Vulkan/VulkanQueueRunner.h` (enum at line 119, `VKRStep` union at 139–200, `Perform*` decls near 275–280)
- Modify: `Common/GPU/Vulkan/VulkanQueueRunner.cpp` (`RunSteps` switch ~371–390, `LogSteps` switch ~733–760, the step-dump switch ~826–880, `ApplyMGSHack`/`ApplySonicHack` pattern matching ~414–720)
- Modify: `Common/GPU/Vulkan/VulkanRenderManager.h` (public API near `BlitFramebuffer`), `Common/GPU/Vulkan/VulkanRenderManager.cpp` (after `BlitFramebuffer`, ~1488–1545)

**Interfaces:**
- Produces:
  ```cpp
  struct VKRNativeCallbackInfo { VkCommandBuffer cmd; VKRFramebuffer *src; VKRFramebuffer *dst; int curFrame; };
  using VKRNativeCallbackFn = std::function<void(const VKRNativeCallbackInfo &)>;
  void VulkanRenderManager::RunNativeCallback(VKRFramebuffer *src, VKRFramebuffer *dst, VKRNativeCallbackFn fn, const char *tag);
  ```

- [ ] **Step 1: Add the step type and payload**

`VulkanQueueRunner.h`, enum:

```cpp
enum class VKRStepType : uint8_t {
	RENDER,
	RENDER_SKIP,
	COPY,
	BLIT,
	READBACK,
	READBACK_IMAGE,
	CALLBACK,   // Runs native code on the render thread with src readable / dst writable.
};
```

Before `struct VKRStep`, add (needs `#include <functional>` at the top of the header):

```cpp
struct VKRNativeCallbackInfo {
	VkCommandBuffer cmd;
	VKRFramebuffer *src;  // in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL when the callback runs, may be null
	VKRFramebuffer *dst;  // in VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL when the callback runs, may be null
	int curFrame;         // 0..VulkanContext::MAX_INFLIGHT_FRAMES-1
};
using VKRNativeCallbackFn = std::function<void(const VKRNativeCallbackInfo &)>;
```

Inside the `VKRStep` union, add a member:

```cpp
		struct {
			VKRFramebuffer *src;
			VKRFramebuffer *dst;
			VKRNativeCallbackFn *fn;  // heap-allocated; PerformCallback deletes it after running.
		} callback;
```

Declare in the runner class next to `PerformBlit`:

```cpp
	void PerformCallback(const VKRStep &step, VkCommandBuffer cmd, int curFrame);
```

- [ ] **Step 2: Execute the step**

`VulkanQueueRunner.cpp`, in `RunSteps`'s `switch (step.stepType)` add before `case VKRStepType::RENDER_SKIP:`:

```cpp
		case VKRStepType::CALLBACK:
			PerformCallback(step, cmd, curFrame);
			break;
```

Add the implementation after `PerformBlit`:

```cpp
void VulkanQueueRunner::PerformCallback(const VKRStep &step, VkCommandBuffer cmd, int curFrame) {
	// Put the images in the layouts the callback is promised, outside any render pass.
	if (step.callback.src) {
		recordBarrier_.TransitionColorImageAuto(&step.callback.src->color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	if (step.callback.dst) {
		recordBarrier_.TransitionColorImageAuto(&step.callback.dst->color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}
	recordBarrier_.Flush(cmd);

	if (step.callback.fn) {
		VKRNativeCallbackInfo info{ cmd, step.callback.src, step.callback.dst, curFrame };
		(*step.callback.fn)(info);
		delete step.callback.fn;
		const_cast<VKRStep &>(step).callback.fn = nullptr;
	}

	// The callback is contractually required to leave dst in COLOR_ATTACHMENT_OPTIMAL and to
	// not change src's layout. Record that so later steps insert correct barriers.
	if (step.callback.dst) {
		step.callback.dst->color.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
}
```

Update the two logging switches (`LogSteps` ~line 743 and the dump ~line 867) with:

```cpp
	case VKRStepType::CALLBACK:
		snprintf(buffer, sizeof(buffer), "CALLBACK %s (src=%s dst=%s)", step.tag,
		         step.callback.src ? step.callback.src->Tag() : "-", step.callback.dst ? step.callback.dst->Tag() : "-");
		break;
```

(adjust to the surrounding function's output style; the point is that every `switch` on `stepType` has a `CALLBACK` case and no `default: UNREACHABLE()` can fire).

Optimizer passes: in `ApplyMGSHack` and `ApplySonicHack` the pattern matches only `RENDER`/`COPY` sequences, so a `CALLBACK` step simply breaks the pattern, which is the desired behavior. In the render-pass merging loop (~line 770–800), confirm that merging only considers consecutive `RENDER` steps for the same framebuffer; a `CALLBACK` step between two `RENDER` steps must prevent merging across it. If the loop skips non-RENDER steps when looking for the next RENDER, add `if (steps[j]->stepType == VKRStepType::CALLBACK) break;` at that skip.

Also in `VulkanQueueRunner::~VulkanQueueRunner` / wherever remaining steps are deleted without running (search for `delete steps[` and `RENDER_SKIP` cleanup): add `if (step->stepType == VKRStepType::CALLBACK) delete step->callback.fn;` so an unrun callback does not leak.

- [ ] **Step 3: Produce the step**

`VulkanRenderManager.h`, public section next to `BlitFramebuffer`:

```cpp
	// Runs fn on the render thread with the frame's command buffer, outside any render pass.
	// src (if any) will be in SHADER_READ_ONLY_OPTIMAL, dst (if any) in COLOR_ATTACHMENT_OPTIMAL.
	// fn must leave dst in COLOR_ATTACHMENT_OPTIMAL and must not change src's layout.
	void RunNativeCallback(VKRFramebuffer *src, VKRFramebuffer *dst, VKRNativeCallbackFn fn, const char *tag);
```

`VulkanRenderManager.cpp`, after `BlitFramebuffer`:

```cpp
void VulkanRenderManager::RunNativeCallback(VKRFramebuffer *src, VKRFramebuffer *dst, VKRNativeCallbackFn fn, const char *tag) {
#ifdef _DEBUG
	SanityCheckPassesOnAdd();
#endif
	// Like BlitFramebuffer: count the read so the last RENDER step into src keeps its contents.
	if (src) {
		for (int i = (int)steps_.size() - 1; i >= 0; i--) {
			if (steps_[i]->stepType == VKRStepType::RENDER && steps_[i]->render.framebuffer == src) {
				steps_[i]->render.numReads++;
				break;
			}
		}
	}

	EndCurRenderStep();

	VKRStep *step = new VKRStep{ VKRStepType::CALLBACK };
	step->callback.src = src;
	step->callback.dst = dst;
	step->callback.fn = new VKRNativeCallbackFn(std::move(fn));
	if (src)
		step->dependencies.insert(src);
	if (dst)
		step->dependencies.insert(dst);
	step->tag = tag;
	steps_.push_back(step);
}
```

- [ ] **Step 4: Build and run the existing tests**

Run: `cmake --build build-unittest --target PPSSPPUnitTest -j12 && ./build-unittest/PPSSPPUnitTest all`
Expected: builds without `-Wswitch` warnings in the two Vulkan files; `74 tests passed.`

- [ ] **Step 5: Commit**

```bash
git add Common/GPU/Vulkan/VulkanQueueRunner.h Common/GPU/Vulkan/VulkanQueueRunner.cpp Common/GPU/Vulkan/VulkanRenderManager.h Common/GPU/Vulkan/VulkanRenderManager.cpp
git commit -m "Vulkan: add CALLBACK render step for native code on the render thread

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: thin3d surface — `RunNativeCallback` and the Vulkan proc-addr native object

**Files:**
- Modify: `Common/GPU/thin3d.h` (`NativeObject` enum at 232–253; `DrawContext` virtuals near `BlitFramebuffer` at ~795)
- Modify: `Common/GPU/Vulkan/thin3d_vulkan.cpp` (`VKContext` class; `GetNativeObject` switch at ~1949–1977)

**Interfaces:**
- Consumes: `VulkanRenderManager::RunNativeCallback(VKRFramebuffer*, VKRFramebuffer*, VKRNativeCallbackFn, const char*)` (Task 2); `VKFramebuffer::GetFB()` returns `VKRFramebuffer *`.
- Produces:
  ```cpp
  namespace Draw {
  struct NativeCallbackInfo {
      uint64_t cmdBuffer = 0;                       // VkCommandBuffer
      uint64_t srcImage = 0; uint32_t srcFormat = 0; // VkImage, VkFormat
      uint64_t dstImage = 0; uint32_t dstFormat = 0;
      uint32_t srcTexture = 0, dstTexture = 0;      // GL (Phase 2), 0 on Vulkan
      int srcWidth = 0, srcHeight = 0, dstWidth = 0, dstHeight = 0;
      int frameIndex = 0;
  };
  using NativeCallbackFn = std::function<void(const NativeCallbackInfo &)>;
  virtual bool DrawContext::RunNativeCallback(Framebuffer *src, Framebuffer *dst, NativeCallbackFn fn, const char *tag);
  }
  NativeObject::VULKAN_GET_INSTANCE_PROC_ADDR  // returns PFN_vkGetInstanceProcAddr as uint64_t
  ```

- [ ] **Step 1: Declare in thin3d.h**

Add `VULKAN_GET_INSTANCE_PROC_ADDR,` as the last enumerator of `enum class NativeObject` (after `PUSH_POOL,`).

Add after the `Aspect` enum block (any spot before `class DrawContext`):

```cpp
// Payload handed to a native callback (see DrawContext::RunNativeCallback). Handles are
// backend-specific integers so this header stays free of Vulkan/GL includes.
struct NativeCallbackInfo {
	uint64_t cmdBuffer = 0;    // Vulkan: VkCommandBuffer
	uint64_t srcImage = 0;     // Vulkan: VkImage of src color
	uint32_t srcFormat = 0;    // Vulkan: VkFormat
	uint64_t dstImage = 0;
	uint32_t dstFormat = 0;
	uint32_t srcTexture = 0;   // OpenGL: texture name (Phase 2)
	uint32_t dstTexture = 0;
	int srcWidth = 0, srcHeight = 0;
	int dstWidth = 0, dstHeight = 0;
	int frameIndex = 0;        // 0..(frames in flight - 1)
};
using NativeCallbackFn = std::function<void(const NativeCallbackInfo &)>;
```

In `class DrawContext`, after `BlitFramebuffer`:

```cpp
	// Runs fn on the backend's render thread, outside any render pass, with src's color image
	// readable by shaders and dst's color image writable as a color attachment. fn must leave
	// dst as a color attachment and must not change src. Returns false when the backend does
	// not support native callbacks (then fn is never called).
	virtual bool RunNativeCallback(Framebuffer *src, Framebuffer *dst, NativeCallbackFn fn, const char *tag) { return false; }
```

- [ ] **Step 2: Implement in VKContext**

In `thin3d_vulkan.cpp`, `class VKContext` declarations, next to `BlitFramebuffer`:

```cpp
	bool RunNativeCallback(Framebuffer *src, Framebuffer *dst, NativeCallbackFn fn, const char *tag) override;
```

Definition (after `VKContext::BlitFramebuffer`):

```cpp
bool VKContext::RunNativeCallback(Framebuffer *srcfb, Framebuffer *dstfb, NativeCallbackFn fn, const char *tag) {
	VKRFramebuffer *src = srcfb ? ((VKFramebuffer *)srcfb)->GetFB() : nullptr;
	VKRFramebuffer *dst = dstfb ? ((VKFramebuffer *)dstfb)->GetFB() : nullptr;
	renderManager_.RunNativeCallback(src, dst, [fn = std::move(fn)](const VKRNativeCallbackInfo &vk) {
		NativeCallbackInfo info;
		info.cmdBuffer = (uint64_t)vk.cmd;
		info.frameIndex = vk.curFrame;
		if (vk.src) {
			info.srcImage = (uint64_t)vk.src->color.image;
			info.srcFormat = (uint32_t)vk.src->color.format;
			info.srcWidth = vk.src->width;
			info.srcHeight = vk.src->height;
		}
		if (vk.dst) {
			info.dstImage = (uint64_t)vk.dst->color.image;
			info.dstFormat = (uint32_t)vk.dst->color.format;
			info.dstWidth = vk.dst->width;
			info.dstHeight = vk.dst->height;
		}
		fn(info);
	}, tag);
	return true;
}
```

In `VKContext::GetNativeObject`, add before `default:`:

```cpp
	case NativeObject::VULKAN_GET_INSTANCE_PROC_ADDR:
		return (uint64_t)(uintptr_t)vkGetInstanceProcAddr;
```

(`vkGetInstanceProcAddr` is the function-pointer variable declared in `Common/GPU/Vulkan/VulkanLoader.h`, which `thin3d_vulkan.cpp` already includes.)

- [ ] **Step 3: Build, run tests**

Run: `cmake --build build-unittest --target PPSSPPUnitTest PPSSPPSDL -j12 && ./build-unittest/PPSSPPUnitTest all`
Expected: builds; `74 tests passed.` The GL and D3D11 contexts inherit the default `return false`.

- [ ] **Step 4: Commit**

```bash
git add Common/GPU/thin3d.h Common/GPU/Vulkan/thin3d_vulkan.cpp
git commit -m "thin3d: RunNativeCallback (Vulkan) and VULKAN_GET_INSTANCE_PROC_ADDR native object

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: `ISlangFilterChain` interface, backend selection function, factory

**Files:**
- Create: `GPU/Common/Slang/ISlangFilterChain.h`, `GPU/Common/Slang/SlangChainFactory.cpp`
- Modify: `GPU/Common/Slang/SlangFilterChain.h` (class at lines 16–56), `GPU/CMakeLists.txt` (Slang list after `Common/Slang/SlangFilterChain.h`), `unittest/TestLibrashader.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class SlangChainBackend { InTree, Librashader };
  class ISlangFilterChain { /* see spec §6.4 */ };
  SlangChainBackend ChooseSlangChainBackend(bool userPrefersLibrashader, bool librashaderLoaded,
                                            GPUBackend gpuBackend, bool drawSupportsNativeCallback);
  ISlangFilterChain *CreateSlangFilterChain(Draw::DrawContext *draw, SlangChainBackend backend);  // never null
  const char *SlangChainBackendName(SlangChainBackend b);  // "in-tree" / "librashader"
  ```

- [ ] **Step 1: Write the failing test**

Append to `unittest/TestLibrashader.cpp`:

```cpp
#include "GPU/Common/Slang/ISlangFilterChain.h"
#include "Core/ConfigValues.h"

bool TestSlangChainBackendSelection() {
	// Librashader only when every precondition holds.
	EXPECT_TRUE(ChooseSlangChainBackend(true, true, GPUBackend::VULKAN, true) == SlangChainBackend::Librashader);
	// Any missing precondition falls back to the in-tree chain.
	EXPECT_TRUE(ChooseSlangChainBackend(false, true, GPUBackend::VULKAN, true) == SlangChainBackend::InTree);
	EXPECT_TRUE(ChooseSlangChainBackend(true, false, GPUBackend::VULKAN, true) == SlangChainBackend::InTree);
	EXPECT_TRUE(ChooseSlangChainBackend(true, true, GPUBackend::VULKAN, false) == SlangChainBackend::InTree);
	// Phase 1: Vulkan only.
	EXPECT_TRUE(ChooseSlangChainBackend(true, true, GPUBackend::OPENGL, true) == SlangChainBackend::InTree);
	EXPECT_TRUE(ChooseSlangChainBackend(true, true, GPUBackend::DIRECT3D11, true) == SlangChainBackend::InTree);
	EXPECT_TRUE(strcmp(SlangChainBackendName(SlangChainBackend::InTree), "in-tree") == 0);
	EXPECT_TRUE(strcmp(SlangChainBackendName(SlangChainBackend::Librashader), "librashader") == 0);
	return true;
}
```

Add `#include <cstring>` at the top of the file. Register `TestSlangChainBackendSelection` in `unittest/UnitTest.cpp` (declaration + `TEST_ITEM(SlangChainBackendSelection),`).

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-unittest --target PPSSPPUnitTest -j12`
Expected: FAIL, `'GPU/Common/Slang/ISlangFilterChain.h' file not found`.

- [ ] **Step 3: Write the interface and factory**

`GPU/Common/Slang/ISlangFilterChain.h`:

```cpp
#pragma once
#include <map>
#include <string>
#include "Common/File/Path.h"

namespace Draw { class DrawContext; class Framebuffer; }
enum class GPUBackend;

enum class SlangChainBackend {
	InTree,       // GPU/Common/Slang/SlangFilterChain (thin3d-based)
	Librashader,  // GPU/Common/Slang/LibrashaderFilterChain (librashader shared library)
};

// Contract shared by both slang filter-chain implementations. All methods are emu-thread.
class ISlangFilterChain {
public:
	virtual ~ISlangFilterChain() = default;
	virtual bool Load(const Path &presetPath, std::string *error) = 0;
	virtual bool IsValid() const = 0;
	// Returns the framebuffer holding the filtered image, or nullptr if the caller should
	// present the unfiltered source this frame.
	virtual Draw::Framebuffer *Run(Draw::Framebuffer *source, int sourceW, int sourceH,
	                               int viewportW, int viewportH, int frameCount) = 0;
	virtual void SetParamOverrides(const std::map<std::string, float> &overrides) = 0;
	virtual void DeviceLost() = 0;
	virtual void DeviceRestore(Draw::DrawContext *draw) = 0;
	virtual SlangChainBackend Backend() const = 0;
};

const char *SlangChainBackendName(SlangChainBackend backend);

// Pure decision: librashader iff the user prefers it, the library is loaded, the GPU backend
// is one librashader supports in this phase (Vulkan), and the draw context can run native callbacks.
SlangChainBackend ChooseSlangChainBackend(bool userPrefersLibrashader, bool librashaderLoaded,
                                          GPUBackend gpuBackend, bool drawSupportsNativeCallback);

// Never returns null. Falls back to the in-tree chain if the librashader chain cannot be constructed.
ISlangFilterChain *CreateSlangFilterChain(Draw::DrawContext *draw, SlangChainBackend backend);
```

`GPU/Common/Slang/SlangChainFactory.cpp`:

```cpp
#include "ppsspp_config.h"
#include "GPU/Common/Slang/ISlangFilterChain.h"
#include "GPU/Common/Slang/SlangFilterChain.h"
#include "Core/ConfigValues.h"
#if USE_LIBRASHADER
#include "GPU/Common/Slang/LibrashaderFilterChain.h"
#endif

const char *SlangChainBackendName(SlangChainBackend backend) {
	switch (backend) {
	case SlangChainBackend::Librashader: return "librashader";
	default: return "in-tree";
	}
}

SlangChainBackend ChooseSlangChainBackend(bool userPrefersLibrashader, bool librashaderLoaded,
                                          GPUBackend gpuBackend, bool drawSupportsNativeCallback) {
	if (!userPrefersLibrashader || !librashaderLoaded || !drawSupportsNativeCallback)
		return SlangChainBackend::InTree;
	if (gpuBackend != GPUBackend::VULKAN)
		return SlangChainBackend::InTree;
	return SlangChainBackend::Librashader;
}

ISlangFilterChain *CreateSlangFilterChain(Draw::DrawContext *draw, SlangChainBackend backend) {
#if USE_LIBRASHADER
	if (backend == SlangChainBackend::Librashader)
		return new LibrashaderFilterChain(draw);
#endif
	return new SlangFilterChain(draw);
}
```

Until Task 5 exists, temporarily guard the `LibrashaderFilterChain` include and construction with `#if 0 // Task 5` so the file compiles; Task 5 removes the guard.

Make `SlangFilterChain` implement the interface. In `SlangFilterChain.h`: `#include "GPU/Common/Slang/ISlangFilterChain.h"`, change `class SlangFilterChain {` to `class SlangFilterChain : public ISlangFilterChain {`, mark `~SlangFilterChain() override;`, add `override` to `Load`, `IsValid`, `Run`, `DeviceLost`, `DeviceRestore`, `SetParamOverrides`, and add:

```cpp
	SlangChainBackend Backend() const override { return SlangChainBackend::InTree; }
```

`GPU/CMakeLists.txt`: add `Common/Slang/ISlangFilterChain.h` and `Common/Slang/SlangChainFactory.cpp` after `Common/Slang/SlangFilterChain.h`.

- [ ] **Step 4: Run tests**

Run: `cmake --build build-unittest --target PPSSPPUnitTest -j12 && ./build-unittest/PPSSPPUnitTest SlangChainBackendSelection && ./build-unittest/PPSSPPUnitTest all`
Expected: exit 0; `75 tests passed.`

- [ ] **Step 5: Commit**

```bash
git add GPU/Common/Slang/ISlangFilterChain.h GPU/Common/Slang/SlangChainFactory.cpp GPU/Common/Slang/SlangFilterChain.h GPU/CMakeLists.txt unittest/TestLibrashader.cpp unittest/UnitTest.cpp
git commit -m "slang: extract ISlangFilterChain, backend selection and factory

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: `LibrashaderFilterChain` (Vulkan runtime)

**Files:**
- Create: `GPU/Common/Slang/LibrashaderFilterChain.h`, `GPU/Common/Slang/LibrashaderFilterChain.cpp`
- Modify: `GPU/CMakeLists.txt` (add both files after `Common/Slang/SlangChainFactory.cpp`), `GPU/Common/Slang/SlangChainFactory.cpp` (remove the `#if 0 // Task 5` guard)

**Interfaces:**
- Consumes: `Librashader::Load/IsLoaded/Instance/ErrorToString` (Task 1); `Draw::DrawContext::RunNativeCallback`, `Draw::NativeCallbackInfo`, `NativeObject::CONTEXT` → `VulkanContext *`, `NativeObject::VULKAN_GET_INSTANCE_PROC_ADDR` (Task 3); `ISlangFilterChain` (Task 4); `VulkanContext::GetInstance/GetPhysicalDevice/GetDevice/GetGraphicsQueue`, `VulkanContext::Delete().QueueCallback(std::function<void(VulkanContext*)>)`, `VulkanContext::MAX_INFLIGHT_FRAMES`.
- Produces: `class LibrashaderFilterChain : public ISlangFilterChain` with the constructor `explicit LibrashaderFilterChain(Draw::DrawContext *draw)`.

Design points the code below encodes (spec §6.5, §8, §9):
- Emu-thread state lives in the object. Render-thread state (`chain`, `createFailed`) lives in a heap `RenderState` held by `std::shared_ptr`, captured by every callback, so it stays alive even if the object is destroyed while a step is pending.
- The chain is created deferred inside the first callback with the frame's command buffer, then `ready` flips to true; `Run` returns `nullptr` until then.
- `DeviceLost`/destructor free the librashader chain through `VulkanContext::Delete().QueueCallback`, the same deferred-deletion queue thin3d uses for `VKRFramebuffer`, so the free runs after the last frame that used the chain has completed on the GPU.

- [ ] **Step 1: Header**

`GPU/Common/Slang/LibrashaderFilterChain.h`:

```cpp
#pragma once
#include "ppsspp_config.h"
#if USE_LIBRASHADER

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "Common/File/Path.h"
#include "GPU/Common/Slang/ISlangFilterChain.h"
#include "Common/GPU/Librashader/LibrashaderLoader.h"

namespace Draw { class DrawContext; class Framebuffer; }
class VulkanContext;

class LibrashaderFilterChain : public ISlangFilterChain {
public:
	explicit LibrashaderFilterChain(Draw::DrawContext *draw);
	~LibrashaderFilterChain() override;

	bool Load(const Path &presetPath, std::string *error) override;
	bool IsValid() const override { return valid_; }
	Draw::Framebuffer *Run(Draw::Framebuffer *source, int sourceW, int sourceH,
	                       int viewportW, int viewportH, int frameCount) override;
	void SetParamOverrides(const std::map<std::string, float> &overrides) override { paramOverrides_ = overrides; }
	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;
	SlangChainBackend Backend() const override { return SlangChainBackend::Librashader; }

private:
	// Touched only on the render thread (inside native callbacks), except for the atomics.
	struct RenderState {
		libra_vk_filter_chain_t chain = nullptr;
		std::atomic<bool> ready{false};
		std::atomic<bool> createFailed{false};
		std::mutex errorLock;
		std::string lastError;  // set on the render thread, read on the emu thread
	};

	void ReleaseChain();       // queues the librashader free on the Vulkan deletion queue
	void ReleaseOutput();
	bool EnsureOutput(int w, int h);

	Draw::DrawContext *draw_ = nullptr;
	Path presetPath_;
	libra_shader_preset_t preset_ = nullptr;   // emu thread; consumed by chain creation
	std::shared_ptr<RenderState> render_;
	Draw::Framebuffer *output_ = nullptr;
	int outputW_ = 0, outputH_ = 0;
	std::map<std::string, float> paramOverrides_;
	bool valid_ = false;
	bool loggedCreateError_ = false;
};

#endif  // USE_LIBRASHADER
```

- [ ] **Step 2: Implementation**

`GPU/Common/Slang/LibrashaderFilterChain.cpp`:

```cpp
#include "GPU/Common/Slang/LibrashaderFilterChain.h"
#if USE_LIBRASHADER

#include <algorithm>
#include "Common/Log.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/Vulkan/VulkanContext.h"

LibrashaderFilterChain::LibrashaderFilterChain(Draw::DrawContext *draw)
	: draw_(draw), render_(std::make_shared<RenderState>()) {}

LibrashaderFilterChain::~LibrashaderFilterChain() {
	ReleaseChain();
	ReleaseOutput();
	if (preset_ && Librashader::IsLoaded()) {
		Librashader::Instance().preset_free(&preset_);
		preset_ = nullptr;
	}
}

bool LibrashaderFilterChain::Load(const Path &presetPath, std::string *error) {
	std::string loadErr;
	if (!Librashader::Load(&loadErr)) {
		if (error) *error = "librashader not available: " + loadErr;
		return false;
	}
	const libra_instance_t &lib = Librashader::Instance();

	ReleaseChain();
	if (preset_) {
		lib.preset_free(&preset_);
		preset_ = nullptr;
	}
	valid_ = false;
	presetPath_ = presetPath;

	libra_preset_ctx_t ctx = nullptr;
	std::string err = Librashader::ErrorToString(lib.preset_ctx_create(&ctx));
	if (err.empty()) err = Librashader::ErrorToString(lib.preset_ctx_set_core_name(ctx, "PPSSPP"));
	if (err.empty()) err = Librashader::ErrorToString(lib.preset_ctx_set_runtime(ctx, LIBRA_PRESET_CTX_RUNTIME_VULKAN));
	if (err.empty()) err = Librashader::ErrorToString(lib.preset_create_with_options(presetPath.c_str(), ctx, nullptr, &preset_));
	if (ctx) lib.preset_ctx_free(&ctx);
	if (!err.empty()) {
		if (error) *error = "librashader preset load failed: " + err;
		preset_ = nullptr;
		return false;
	}
	render_ = std::make_shared<RenderState>();  // fresh render-thread state for the new preset
	valid_ = true;
	INFO_LOG(Log::G3D, "LibrashaderFilterChain: preset parsed: %s", presetPath.c_str());
	return true;
}

bool LibrashaderFilterChain::EnsureOutput(int w, int h) {
	if (output_ && outputW_ == w && outputH_ == h)
		return true;
	ReleaseOutput();
	Draw::FramebufferDesc desc{ w, h, 1, 1, 0, false, "librashader_output" };
	output_ = draw_->CreateFramebuffer(desc);
	if (!output_) {
		ERROR_LOG(Log::G3D, "LibrashaderFilterChain: failed to create %dx%d output framebuffer", w, h);
		return false;
	}
	outputW_ = w;
	outputH_ = h;
	return true;
}

Draw::Framebuffer *LibrashaderFilterChain::Run(Draw::Framebuffer *source, int sourceW, int sourceH,
                                               int viewportW, int viewportH, int frameCount) {
	if (!valid_ || !preset_ || !source)
		return nullptr;
	if (render_->createFailed.load()) {
		if (!loggedCreateError_) {
			std::lock_guard<std::mutex> guard(render_->errorLock);
			ERROR_LOG(Log::G3D, "LibrashaderFilterChain: chain creation failed: %s", render_->lastError.c_str());
			loggedCreateError_ = true;
		}
		return nullptr;
	}
	if (!EnsureOutput(std::max(1, viewportW), std::max(1, viewportH)))
		return nullptr;

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	PFN_vkGetInstanceProcAddr getProc = (PFN_vkGetInstanceProcAddr)(uintptr_t)draw_->GetNativeObject(Draw::NativeObject::VULKAN_GET_INSTANCE_PROC_ADDR);
	libra_device_vk_t device{};
	device.physical_device = vulkan->GetPhysicalDevice(vulkan->GetCurrentPhysicalDeviceIndex());
	device.instance = vulkan->GetInstance();
	device.device = vulkan->GetDevice();
	device.queue = vulkan->GetGraphicsQueue();
	device.entry = getProc;

	std::shared_ptr<RenderState> rs = render_;
	libra_shader_preset_t *presetSlot = &preset_;  // consumed (set to null) by create_deferred on success
	std::map<std::string, float> overrides = paramOverrides_;

	bool enqueued = draw_->RunNativeCallback(source, output_, [rs, device, presetSlot, overrides, frameCount, sourceW, sourceH](const Draw::NativeCallbackInfo &info) {
		const libra_instance_t &lib = Librashader::Instance();
		if (rs->createFailed.load())
			return;
		if (!rs->chain) {
			filter_chain_vk_opt_t opts{};
			opts.version = LIBRASHADER_CURRENT_VERSION;
			opts.frames_in_flight = VulkanContext::MAX_INFLIGHT_FRAMES;
			opts.force_no_mipmaps = false;
			opts.use_dynamic_rendering = false;
			opts.disable_cache = false;
			std::string err = Librashader::ErrorToString(lib.vk_filter_chain_create_deferred(presetSlot, device, (VkCommandBuffer)info.cmdBuffer, &opts, &rs->chain));
			if (!err.empty() || !rs->chain) {
				std::lock_guard<std::mutex> guard(rs->errorLock);
				rs->lastError = err.empty() ? "unknown error" : err;
				rs->createFailed.store(true);
				return;
			}
			// LUT uploads were recorded into this frame's command buffer; first real frame is next frame.
			rs->ready.store(true);
			return;
		}
		for (const auto &kv : overrides) {
			libra_error_t e = lib.vk_filter_chain_set_param(rs->chain, kv.first.c_str(), kv.second);
			if (e) Librashader::ErrorToString(e);  // unknown parameter names are not fatal; drop the error
		}
		libra_image_vk_t in{};
		in.handle = (VkImage)info.srcImage;
		in.format = (VkFormat)info.srcFormat;
		in.width = (uint32_t)sourceW;   // native PSP size: SourceSize semantics as in the in-tree chain
		in.height = (uint32_t)sourceH;
		libra_image_vk_t out{};
		out.handle = (VkImage)info.dstImage;
		out.format = (VkFormat)info.dstFormat;
		out.width = (uint32_t)info.dstWidth;
		out.height = (uint32_t)info.dstHeight;
		libra_viewport_t vp{ 0.0f, 0.0f, (uint32_t)info.dstWidth, (uint32_t)info.dstHeight };
		frame_vk_opt_t fopts{};
		fopts.version = LIBRASHADER_CURRENT_VERSION;
		fopts.clear_history = false;
		fopts.frame_direction = 1;
		fopts.rotation = 0;
		fopts.total_subframes = 1;
		fopts.current_subframe = 1;
		fopts.aspect_ratio = (float)info.dstWidth / (float)std::max(1, info.dstHeight);
		fopts.frames_per_second = 60.0f;
		fopts.frametime_delta = 16667;
		fopts.color_space = LIBRA_COLOR_SPACE_SDR;
		std::string err = Librashader::ErrorToString(lib.vk_filter_chain_frame(rs->chain, (VkCommandBuffer)info.cmdBuffer, (size_t)frameCount, in, out, &vp, nullptr, &fopts));
		if (!err.empty()) {
			std::lock_guard<std::mutex> guard(rs->errorLock);
			rs->lastError = err;
			rs->createFailed.store(true);  // stop rendering through a broken chain
		}
	}, "librashader");

	if (!enqueued)
		return nullptr;
	return render_->ready.load() ? output_ : nullptr;
}

void LibrashaderFilterChain::ReleaseChain() {
	if (!render_)
		return;
	std::shared_ptr<RenderState> rs = render_;
	render_ = std::make_shared<RenderState>();
	if (!draw_ || !Librashader::IsLoaded())
		return;
	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	// Runs after every frame that could still reference the chain has completed on the GPU.
	vulkan->Delete().QueueCallback([rs](VulkanContext *) {
		if (rs->chain) {
			Librashader::Instance().vk_filter_chain_free(rs->chain);
			rs->chain = nullptr;
		}
	});
}

void LibrashaderFilterChain::ReleaseOutput() {
	if (output_) {
		output_->Release();
		output_ = nullptr;
	}
	outputW_ = outputH_ = 0;
}

void LibrashaderFilterChain::DeviceLost() {
	ReleaseChain();
	ReleaseOutput();
	if (preset_ && Librashader::IsLoaded()) {
		Librashader::Instance().preset_free(&preset_);
		preset_ = nullptr;
	}
	valid_ = false;
	draw_ = nullptr;
}

void LibrashaderFilterChain::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	if (!presetPath_.empty()) {
		std::string err;
		if (!Load(presetPath_, &err))
			ERROR_LOG(Log::G3D, "LibrashaderFilterChain: reload after device restore failed: %s", err.c_str());
	}
}

#endif  // USE_LIBRASHADER
```

Implementer notes:
- `Draw::FramebufferDesc` field order is `{ width, height, depth, numLayers, multiSampleLevel, z_stencil, tag }` in `Common/GPU/thin3d.h` (~line 300); if the merged header differs (it also has `colorFormat` on this branch), use designated field names instead of positional init.
- `VulkanContext::GetPhysicalDevice(int)` and `GetCurrentPhysicalDeviceIndex()` exist in `VulkanContext.h`; if the accessor names differ after the upstream merge, adapt (search for `physical_devices_`).
- `libra_vk_filter_chain_create_deferred` takes `libra_shader_preset_t *` and nulls it on success, which is why the callback captures a pointer to `preset_`. The object outlives the callback in all supported flows (destruction only through `DeviceLost`/dtor after `ReleaseChain`, and PPSSPP drains the render thread between the last `Run` and those calls; see spec §8). Add `_dbg_assert_msg_(rs.use_count() == 1, ...)` in `ReleaseChain` in debug builds to catch a still-pending step.
- If on-device verification shows librashader validating `in.width/height` against the image extents (spec §12 risk), change `in.width/height` to `info.srcWidth/srcHeight` and blit `source` to a native-sized intermediate first via `draw_->BlitFramebuffer` before `RunNativeCallback`.

- [ ] **Step 3: Wire into the factory and build**

Remove the `#if 0 // Task 5` guard in `SlangChainFactory.cpp`; add the two files to `GPU/CMakeLists.txt`.

Run: `cmake --build build-unittest --target PPSSPPUnitTest PPSSPPSDL -j12 && ./build-unittest/PPSSPPUnitTest all`
Expected: builds cleanly; `75 tests passed.`

- [ ] **Step 4: Commit**

```bash
git add GPU/Common/Slang/LibrashaderFilterChain.h GPU/Common/Slang/LibrashaderFilterChain.cpp GPU/Common/Slang/SlangChainFactory.cpp GPU/CMakeLists.txt
git commit -m "slang: LibrashaderFilterChain — Vulkan runtime through native callback steps

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Frame-manager wiring, config toggle, developer UI

**Files:**
- Modify: `GPU/Common/FramebufferManagerCommon.h` (forward decl line 40, members 564–583), `GPU/Common/FramebufferManagerCommon.cpp` (`UpdateSlangChain` 124–153; destructor ~74; `DeviceLost`/`DeviceRestore` ~3513–3535; include at line 40)
- Modify: `Core/Config.h` (next to `sSlangShaderPreset`, ~line 402), `Core/Config.cpp` (next to the `SlangShaderPreset` setting, line 709)
- Modify: `UI/DeveloperToolsScreen.cpp` (a "Slang shaders" item header + checkbox in the graphics/dev list; place after the texture-replacement block at ~line 96)

**Interfaces:**
- Consumes: `ISlangFilterChain`, `ChooseSlangChainBackend`, `CreateSlangFilterChain`, `SlangChainBackendName` (Task 4); `Librashader::Load` (Task 1); `GetGPUBackend()` from `Core/System.h`.
- Produces: `bool g_Config.bSlangUseLibrashader` (ini key `SlangUseLibrashader`, default `true`).

- [ ] **Step 1: Config field**

`Core/Config.h`, after `std::string sSlangShaderPreset;`:

```cpp
	bool bSlangUseLibrashader;  // Prefer the librashader runtime over the in-tree chain when available.
```

`Core/Config.cpp`, after the `SlangShaderPreset` line:

```cpp
	ConfigSetting("SlangUseLibrashader", SETTING(g_Config, bSlangUseLibrashader), true, CfgFlag::DEFAULT),
```

- [ ] **Step 2: Frame manager uses the interface and the factory**

`FramebufferManagerCommon.h`: replace `class SlangFilterChain;` with `class ISlangFilterChain;` and the member with `ISlangFilterChain *slangChain_ = nullptr;`.

`FramebufferManagerCommon.cpp`: replace `#include "GPU/Common/Slang/SlangFilterChain.h"` with `#include "GPU/Common/Slang/ISlangFilterChain.h"` and add `#include "Common/GPU/Librashader/LibrashaderLoader.h"` (guarded by `#if USE_LIBRASHADER`) and `#include "Core/System.h"` if missing. In `UpdateSlangChain`, replace `slangChain_ = new SlangFilterChain(draw_);` with:

```cpp
	bool librashaderLoaded = false;
#if USE_LIBRASHADER
	{
		std::string loadErr;
		librashaderLoaded = Librashader::Load(&loadErr);
	}
#endif
	// A probe with null framebuffers is never executed by any backend; it only tells us whether
	// the backend supports native callbacks. Backends that don't support them return false.
	bool supportsCallbacks = false;
#if USE_LIBRASHADER
	supportsCallbacks = draw_->RunNativeCallback(nullptr, nullptr, [](const Draw::NativeCallbackInfo &) {}, "probe");
#endif
	SlangChainBackend backend = ChooseSlangChainBackend(g_Config.bSlangUseLibrashader, librashaderLoaded, GetGPUBackend(), supportsCallbacks);
	slangChain_ = CreateSlangFilterChain(draw_, backend);
	INFO_LOG(Log::G3D, "Slang chain backend: %s", SlangChainBackendName(backend));
```

Note: the probe enqueues one empty CALLBACK step on Vulkan (harmless, runs once per preset reload). If the reviewer prefers zero side effects, add `virtual bool SupportsNativeCallback() const { return false; }` to `DrawContext` (override `true` in `VKContext`) and use that instead — do this variant if Task 3 is still open; otherwise keep the probe.

The rest of `UpdateSlangChain` (Load, error logging, delete on failure), the destructor, `DeviceLost`, `DeviceRestore` and the call site in `PrepareCopyDisplayToOutput` compile unchanged against the interface.

- [ ] **Step 3: Developer Tools checkbox**

`UI/DeveloperToolsScreen.cpp`, after the texture replacement checkboxes:

```cpp
#if USE_LIBRASHADER
	list->Add(new ItemHeader(dev->T("Slang shaders")));
	list->Add(new CheckBox(&g_Config.bSlangUseLibrashader, dev->T("Use librashader for slang shaders")))->OnClick.Add([](UI::EventParams &) {
		// Force the frame manager to rebuild the chain with the new backend on the next frame.
		if (gpu) gpu->NotifyConfigChanged();
		return UI::EVENT_DONE;
	});
#endif
```

Check `gpu->NotifyConfigChanged()` exists (search `NotifyConfigChanged` in `GPU/GPUCommon.h`); it is what other graphics toggles call so `CheckPostShaders`/`UpdateSlangChain` runs. If a different hook is used for `sSlangShaderPreset` changes in `SlangShaderScreen.cpp`, call that one instead. Add the two strings with the `add-string` skill (`Tools/langtool`) so all `assets/lang/*.ini` files get the keys.

- [ ] **Step 4: Build, unit tests, both option values**

Run: `cmake --build build-unittest --target PPSSPPUnitTest PPSSPPSDL -j12 && ./build-unittest/PPSSPPUnitTest all`
Expected: `75 tests passed.`
Run: `cmake --build build-nolibra --target PPSSPPUnitTest -j12` (from Task 1) — still builds with `USE_LIBRASHADER=OFF`.

- [ ] **Step 5: Regression check of the in-tree path**

Launch `PPSSPPSDL` with Vulkan, `SlangUseLibrashader = False` in `ppsspp.ini`, preset `stock.slangp`: log shows `Slang chain backend: in-tree`, rendering is unchanged from the pre-branch build (take a screenshot for the Task 8 comparison).

- [ ] **Step 6: Commit**

```bash
git add GPU/Common/FramebufferManagerCommon.h GPU/Common/FramebufferManagerCommon.cpp Core/Config.h Core/Config.cpp UI/DeveloperToolsScreen.cpp assets/lang
git commit -m "slang: select librashader or in-tree chain per reload; SlangUseLibrashader toggle

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: Obtaining the shared library — build doc and CMake prebuilt copy

**Files:**
- Create: `docs/superpowers/librashader-build.md`
- Modify: `CMakeLists.txt` (after the `USE_LIBRASHADER` block from Task 1; and after the `PPSSPPSDL` target definition, search `add_executable(${TargetBin}` / `PPSSPPSDL`)

**Interfaces:**
- Produces: CMake cache variable `LIBRASHADER_PREBUILT` (path to `librashader.dylib`/`.so`/`.dll`); when set, the library is copied next to the built executable post-build.

- [ ] **Step 1: Build the library once on the dev box and record the exact commands that worked**

```bash
git clone --depth 1 --branch librashader-v0.12.0 https://github.com/SnowflakePowered/librashader.git /tmp/librashader
cd /tmp/librashader
cargo build -p librashader-capi --release --no-default-features --features runtime-vulkan,runtime-opengl
ls -la target/release/librashader.dylib     # macOS; .so on Linux; librashader.dll on Windows
otool -D target/release/librashader.dylib   # install name must be plain "librashader.dylib" or an @rpath form the loader resolves; if not, run:
# install_name_tool -id librashader.dylib target/release/librashader.dylib
```

If `--no-default-features --features ...` is rejected, fall back to `cargo build -p librashader-capi --release` (default `runtime-all`; larger but fine). Record whichever worked in the doc. Rust stable ≥ 1.88 is required.

- [ ] **Step 2: Write the doc**

`docs/superpowers/librashader-build.md` must contain: pinned tag and ABI/API numbers; the exact cargo command from Step 1 per platform (macOS, Linux, Windows MSVC); the `otool`/`install_name_tool` note; where PPSSPP looks for the library (`$LIBRASHADER_PATH`, executable directory, default loader search path); the `LIBRASHADER_PREBUILT` CMake variable; the licensing note (library MPL-2.0, headers MIT, PPSSPP does not link it); and how to confirm it loaded (`INFO` log line `librashader loaded (ABI 2, API 5)` and Developer Tools → System Information line added in Task 6 if implemented, else the log).

- [ ] **Step 3: CMake copy step**

```cmake
set(LIBRASHADER_PREBUILT "" CACHE FILEPATH "Optional path to a prebuilt librashader shared library to copy next to the executable")
```

After the SDL executable target is defined (search for `set_target_properties(${TargetBin}` or the `PPSSPPSDL` bundle setup):

```cmake
if(USE_LIBRASHADER AND LIBRASHADER_PREBUILT AND TARGET ${TargetBin})
	if(APPLE)
		set(LIBRASHADER_DEST "$<TARGET_BUNDLE_CONTENT_DIR:${TargetBin}>/MacOS")
	else()
		set(LIBRASHADER_DEST "$<TARGET_FILE_DIR:${TargetBin}>")
	endif()
	add_custom_command(TARGET ${TargetBin} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIBRASHADER_PREBUILT}" "${LIBRASHADER_DEST}"
		COMMENT "Copying librashader next to ${TargetBin}")
endif()
```

- [ ] **Step 4: Verify**

Run: `cmake -S . -B build-unittest -DLIBRASHADER_PREBUILT=/tmp/librashader/target/release/librashader.dylib && cmake --build build-unittest --target PPSSPPSDL -j12 && ls build-unittest/PPSSPPSDL.app/Contents/MacOS/`
Expected: `librashader.dylib` sits next to `PPSSPPSDL`.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/librashader-build.md CMakeLists.txt
git commit -m "librashader: build instructions and LIBRASHADER_PREBUILT copy step

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: On-device verification (macOS Vulkan via MoltenVK) and fixes

**Files:**
- Possibly modify: `GPU/Common/Slang/LibrashaderFilterChain.cpp` (native-size fallback, spec §12), `Common/GPU/Vulkan/VulkanQueueRunner.cpp` (layout bookkeeping)
- Modify: `docs/superpowers/plans/2026-09-14-librashader-phase1-vulkan-core.md` (record results in the checklist below)

**Interfaces:** none new.

- [x] **Step 1: Load check**

Launch `PPSSPPSDL` (Vulkan). Log must show `librashader loaded (ABI 2, API 5)`. Select `stock.slangp` in the shader screen. Log must show `Slang chain backend: librashader` and `LibrashaderFilterChain: preset parsed`. The game image must appear within two frames (first callback creates the chain).

- [ ] **Step 2: Validation layers** — NOT RUN, validation layers are not installed on the verification machine

Run once with `VK_LAYER_KHRONOS_validation` enabled (PPSSPP: Developer Tools → "Enable Vulkan validation layers" if present in this build, else `export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`). Switch between `stock.slangp`, `lcd-psp-matrix.slangp`, `crt-royale.slangp`, and Off. Zero new validation errors. Typical failures and the fix:
  - "image layout mismatch" on the output image when the present pass samples it → check `PerformCallback` sets `dst->color.layout = COLOR_ATTACHMENT_OPTIMAL` (Task 2).
  - "command buffer in render pass" → a RENDER step was not ended before the CALLBACK; confirm `EndCurRenderStep()` in `RunNativeCallback`.
  - Errors from librashader's own pipelines on MoltenVK → try `opts.use_dynamic_rendering = true` only if MoltenVK reports Vulkan 1.3; otherwise report upstream.

- [x] **Step 3: Pixel comparison vs the in-tree chain** — run for `stock`, `lcd-psp-matrix`, `twopass`, `feedback`, `lut` (no crt-royale on this machine)

For each of the three presets: pause the same game frame (use a save state), screenshot with `SlangUseLibrashader=True`, then `False`. Compare with ImageMagick: `compare -metric AE a.png b.png diff.png`. Expected: `stock` identical or ≤ 0.1% pixels differing (sRGB rounding); `lcd-psp-matrix` and `crt-royale` visually identical, differences confined to rounding. If `SourceSize`-dependent shaders (scanlines) tile at the wrong frequency, apply the native-size fallback from Task 5's implementer notes.

- [x] **Step 4: Parameters and reload** — parameters verified via `[SlangParams]`; in-app slider/UI switching NOT RUN (no Accessibility permission for synthetic input)

Change a parameter slider in the shader screen while running: the change applies within a frame (overrides pass through `set_param`). Switch presets three times, toggle Off, toggle back: no crash, no leak warnings in the log at exit, no validation errors.

- [x] **Step 5: Absent library**

Rename `librashader.dylib`, relaunch: log shows `librashader unavailable`, backend is `in-tree`, rendering matches Task 6 Step 5.

- [x] **Step 6: Record and commit**

Fill in the results table in this file:

Run on 2026-09-14, macOS 15 / Apple M2 Pro, MoltenVK (Vulkan 1.4.323), `PPSSPPSDL` built from
`7ebcfe2401` + this commit, content = GE frame dump `frametests/dumps/locoroco.ppdmp` (replays one
static frame, so A/B captures are directly comparable). Window 960x544 logical = 1920x1088 px,
`InternalResolution = 0` (auto -> 4x = 1920x1088) unless stated. Full commands, log excerpts and
screenshot paths are in
`.superpowers/sdd/2026-09-14-librashader-phase1-vulkan-core/task-8-report.md`.

| Check | Result | Notes |
|---|---|---|
| Load + first frame | PASS | `librashader loaded (ABI 2, API 5)`, `Slang chain backend: librashader`, `LibrashaderFilterChain: preset parsed`, no `LibrashaderFilterChain:` errors; game image visible (screenshots below), no black/frozen frame after the 3-frame warm-up. |
| Validation layers clean | NOT RUN (layers not installed) | No `VK_LAYER_KHRONOS_validation` on this machine and nothing may be installed. As a proxy: zero `mvk-error`/`VUID`/assert lines across all 24 runs. |
| stock pixel diff | PASS | 0 / 2088960 pixels differ from the in-tree chain (bit-identical). |
| lcd-psp-matrix pixel diff | PASS | Visually identical; 29.0% of pixels differ by >=1/255, 14.3% by >2/255, max 67, mean |diff| 1.5/255, mean signed +0.03/+0.17/+0.36 per channel. A shift search shows (0,0) is the clear minimum (mean 1.5 vs 12-14 at +/-1 px), so the subpixel grid period and phase match exactly - librashader honours the native `SourceSize` we declare. Residual is float16 intermediate rounding on mask edges. |
| twopass pixel diff | **FAIL (known limitation, not fixed)** | 83.4% of pixels differ, max 121. Root cause: `OriginalHistory1`. See the "native size" row. Bit-identical at `InternalResolution = 1`. |
| feedback pixel diff | PASS | 0 pixels differ (feedback ring lives in librashader's own viewport-sized buffers, so no size lie is involved). |
| lut pixel diff | PASS | 0 pixels differ; LUT texture loaded by librashader itself. |
| Native-size behaviour (spec §12) | PARTIAL | `SourceSize`/`OriginalSize`/`scale_type = source` are taken from `libra_image_vk_t::width/height` as hoped, so the fallback blit is **not** needed for mask frequency (see lcd row). But librashader also uses those numbers as the *copy extent* when snapshotting the input into its `OriginalHistoryN` ring, so with an upscaled render target the snapshot captures only the native-sized top-left corner. Proven with a one-pass probe that outputs `OriginalHistory1` (`/tmp/ppsspp-t8/hist.slangp`): librashader shows the top-left 480x272 of the 1920x1088 frame stretched to full screen, in-tree shows the whole frame; at `InternalResolution = 1` (480x272 render target) the two are bit-identical. The brief's fallback (blit source into a native-sized intermediate) would fix this but would also throw away the upscaled detail pass 0 samples today, regressing `stock`/`lut`/`feedback` from bit-identical to a blurry native upscale - so it is deliberately **not** applied. `LibrashaderFilterChain::Run` now warns once when the declared and real extents disagree. Decide in a later phase: accept, gate on a per-preset "uses history" check, or make it a user setting. |
| Params live update | PASS | `[SlangParams]` overrides `<preset>|SUBPIXEL_SIZE = 8` and `<preset>|GRID_STRENGTH = 0` on `lcd-psp-matrix`: librashader output changes on 89.0% of pixels vs its own default run (grid lines gone, stripes 8 px wide) and tracks the in-tree chain with the same overrides inside the same rounding envelope (25.0% / max 66). In-app slider drag NOT RUN (see below). |
| Preset switching / Off | PARTIAL | 12 process runs covering 6 different presets, plus `SlangShaderPreset = ""` (Off -> no chain created, no errors, output = raw framebuffer) and back On: no crash, no `LibrashaderFilterChain:` error, no Vulkan error, no crash report in `~/Library/Logs/DiagnosticReports`. In-process switching and in-app UI navigation NOT RUN: `osascript` has no Accessibility permission on this machine (`osascript is not allowed to send keystrokes` / `not allowed assistive access`), so no synthetic keystrokes or window resizing are possible and nothing may be installed to work around it. The in-process teardown/recreate path (`ReleaseChain` with frames in flight) is therefore still unverified. |
| Library absent fallback | PASS | `librashader.dylib` renamed: `librashader unavailable: librashader not found or ABI mismatch (want ABI 2)`, `Slang chain backend: in-tree`, and the render is bit-identical to the librashader-off in-tree run of the same preset. Library restored. |
| Unit tests | PASS | `./build-unittest/PPSSPPUnitTest all` -> `75 tests passed.` before and after the change in this commit. |

**Also found (outside this plan's scope, not fixed):** a preset set only in `ppsspp.ini` is not
applied until something sets `FramebufferManagerCommon::updatePostShaders_` (display/render resize
or a UI config change), because `UpdateSlangChain` only runs from `CheckPostShaders`. On this box the
startup `NotifyDisplayResized` does fire, so it works, but there is no explicit "apply the configured
preset at boot" step. Pre-existing, affects the in-tree chain identically, lives in
`GPU/Common/FramebufferManagerCommon.cpp`.

```bash
git add docs/superpowers/plans/2026-09-14-librashader-phase1-vulkan-core.md GPU/Common/Slang/LibrashaderFilterChain.cpp Common/GPU/Vulkan/VulkanQueueRunner.cpp
git commit -m "librashader: Phase 1 on-device verification results and fixes

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Out of scope for this plan (later phases, per spec §10)

- Phase 2: `GLRStepType::CALLBACK`, GL runtime in `LibrashaderFilterChain`, GL state restore.
- Phase 3: Android `cargo ndk` build into `jniLibs`, CI jobs, GLES 3 verification.
- Phase 4: remove the in-tree chain, revert the thin3d slot/descriptor bumps and sRGB render-pass keying, D3D11 runtime.
