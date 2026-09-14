# Slang Shader Support — Phase 5 (Backend Expansion: OpenGL / GLES / D3D11) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the slang filter chain — today Vulkan-only — render correctly on the OpenGL, OpenGL ES, and Direct3D 11 backends, by cross-compiling each pass's SPIR-V to the active backend's shading dialect and reconciling the Vulkan binding model (descriptor sets / bindings) with each backend's texture-name / register / attribute-semantic conventions.

**Architecture:** Keep the entire slang subsystem unchanged except the one backend-gated seam in `SlangPassCompiler::CompileSlangPass`. `ReflectSlangSource` already compiles both stages to SPIR-V (via glslang, Vulkan rules) and reflects them; today it discards the SPIR-V after reflection. Phase 5 exposes those SPIR-V words, and `CompileSlangPass` branches: for Vulkan it feeds the transformed GLSL as now; for GL/GLES/D3D11 it cross-compiles the SPIR-V with `spirv_cross::CompilerGLSL` / `CompilerHLSL` directly (NOT the existing `TranslateShader`, which takes GLES2 text and hardcodes the legacy postshader cbuffer), then feeds the cross-compiled text to `draw->CreateShaderModule(stage, backendLang, ...)`. Two backend-specific reconciliations are required and are the bulk of the work: (1) **texture binding** — GL binds samplers by *name* and D3D11 by *register*, so the pipeline must carry the SPIRV-Cross-emitted sampler names (GL) or explicit register remaps (D3D11) derived from reflection, instead of relying on Vulkan's binding numbers; (2) **vertex attributes** — the quad's attribute `location`s must be expressed as PPSSPP `SEM_*` values (D3D11 maps these to `POSITION`/`TEXCOORD0` semantic names; GL maps them to attribute names) and the cross-compiled shader's attribute naming must align. sRGB/float intermediate framebuffers already fall back to UNORM on backends lacking them (verified). No `.slangp`/parser/runtime-semantic code changes.

**Tech Stack:** C++17; vendored `spirv_cross` (`ext/SPIRV-Cross/`, `spirv_cross::CompilerGLSL`/`CompilerHLSL`); PPSSPP `thin3d` (`Draw::`) multi-backend abstraction; existing `GPU/Common/Slang/` subsystem; PPSSPP `unittest` harness. No new third-party deps (glslang + SPIRV-Cross already vendored and initialized).

## Global Constraints

- **License header:** every new/modified file keeps the PPSSPP GPL 2.0 header (year `2026-` for new files; copy the style from existing `GPU/Common/Slang/*`).
- **One seam only:** the ONLY behavioral change to the slang subsystem is inside `SlangPassCompiler` (cross-compile branch + the pipeline metadata it needs). `SlangpParser`, `SlangReflection` semantics, `SlangFilterChain::Run`, `SlangPresetLibrary`, the importer, and the UI are NOT touched (except `SlangFilterChain` texture-binding if a per-backend slot/name difference forces it — see Task 6; keep it minimal and gated).
- **Reuse the SPIR-V, no second glslang run:** cross-compile the `vspv`/`fspv` that `ReflectSlangSource` already produces. Do not recompile GLSL→SPIR-V for the cross path.
- **Do NOT reuse `Common/GPU/ShaderTranslation.cpp:TranslateShader`:** it rejects SPIR-V/Vulkan source, only accepts `GLSL_1xx`/`GLSL_3xx` text, and injects the fixed legacy postshader `cbuffer data : register(b0)` — incompatible with slang. Use SPIRV-Cross compilers directly.
- **Vulkan is the reference, must not regress:** the Vulkan path (transformed-GLSL → `CreateShaderModule(GLSL_VULKAN, ...)`) stays byte-for-byte as today; crt-royale on Vulkan must render identically after each task. Every task re-verifies the Vulkan path still builds and the 19 slang unit tests stay green.
- **D3D11 is Windows-only and compile-gated on this dev setup:** the D3D11 backend and its HLSL cross-compile live behind `#ifdef _WIN32` (as `ShaderTranslation.cpp`'s HLSL path and the D3D11 backend already are). On macOS/Android it is written and compiled-checked only where the toolchain allows; runtime verification of D3D11 is deferred to a Windows environment and called out explicitly (no silent "done").
- **Graceful failure:** a pass that fails to cross-compile disables the whole preset with a clear logged error (reuse the existing loud-fail: `CompileSlangPass` returns false → `SlangFilterChain::Load` fails → `UpdateSlangChain` logs ERROR and renders the raw game), never renders garbage.
- **Commit trailer:** end every commit message with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- **Build + test env:**
  - Desktop unit build + run: `cmake --build build-unittest --target PPSSPPUnitTest && ./build-unittest/PPSSPPUnitTest <Name>` (**exit 0 = pass**). Tests: `bool TestXxx()` (unittest/UnitTest.h), registered in `unittest/UnitTest.cpp` (forward decl + `TEST_ITEM`). The cross-compile step is unit-testable device-free (SPIR-V words in → dialect string out), so Tasks 2-4 get real tests.
  - **macOS OpenGL** is the local cross-backend runtime target. Launch the desktop SDL build (or the existing dev build) with the GL backend selected (`iGPUBackend = 0` / GPUBackend::OPENGL). Confirm the current desktop build target and how to force GL before Task 5's on-device-equivalent check.
  - **Android** for GLES3 (and Vulkan regression): APK via `export ANDROID_HOME=/opt/homebrew/share/android-commandlinetools; ./gradlew -p android assembleNormalDebug -Pandroid.injected.build.abi=arm64-v8a -PANDROID_VERSION_CODE=999999999 -PANDROID_VERSION_NAME=slang-dev --console=plain`; `adb install -t -r ...`. The Android GPU-backend toggle is in Settings → Graphics → Backend (Vulkan ↔ OpenGL). Adreno device; imported shaders at `/storage/emulated/0/Android/data/org.ppsspp.ppsspp/files/slang`.

## Verbatim reference: exact APIs and the current seam

**Current `CompileSlangPass` flow** (`GPU/Common/Slang/SlangPassCompiler.cpp:337-439`):
1. `ReflectSlangSource(src, ctx, &out->reflection, error, &transformedVert, &transformedFrag)` — transforms push_constant→UBO, compiles both stages to SPIR-V (`CompileStageToSpirv`, `:35-61`, `EShMsgSpvRules|EShMsgVulkanRules`, 450, ECoreProfile), reflects UBO+samplers, returns transformed Vulkan-GLSL text. **The SPIR-V `vspv`/`fspv` (`:276-278`) are local and discarded — Phase 5 must expose them.**
2. Hard-error if `backendLang != GLSL_VULKAN` (`:351-355`). **← the seam to replace.**
3. `draw->CreateShaderModule(ShaderStage::Vertex/Fragment, GLSL_VULKAN, transformedVert/Frag ...)` (`:360-363`).
4. `UniformBufferDesc` from reflection (`:373-384`); `InputLayoutDesc` with raw locations 0/1/2 (`:397-407`); states; `PipelineDesc`; `CreateGraphicsPipeline` (`:414-423`).

**Reflection data available** (`GPU/Common/Slang/SlangReflection.h`): `PassReflection{ std::vector<SlangUniformMember> uboMembers; uint32_t uboSizeBytes; int uboBinding; std::vector<SlangTextureBinding> textures; }`. `SlangTextureBinding{ std::string name; int binding; SlangSemantic semantic; int index; }` — `name` is the slang variable name (e.g. `Source`, `ORIG_LINEARIZED`), `binding` the Vulkan descriptor binding. Texture slot = `binding - 1` (`SlangFilterChain.cpp:653`).

**SPIRV-Cross usage pattern** (from `Common/GPU/ShaderTranslation.cpp`, adapt — do not call TranslateShader):
```cpp
// GLSL / GLSL-ES:
spirv_cross::CompilerGLSL glsl(std::move(spirvWords));
spirv_cross::CompilerGLSL::Options opts;
opts.version = <desc.GLSLVersion>;   // from draw->GetShaderLanguageDesc()
opts.es = <desc.gles>;
opts.enable_420pack_extension = <desc.bugs/caps or false>;
glsl.set_common_options(opts);
std::string out = glsl.compile();

// HLSL (Windows only):
spirv_cross::CompilerHLSL hlsl(std::move(spirvWords));
spirv_cross::CompilerHLSL::Options hopts; hopts.shader_model = 50;
spirv_cross::CompilerGLSL::Options common; common.vertex.flip_vert_y = true; common.vertex.fixup_clipspace = true;
hlsl.set_hlsl_options(hopts); hlsl.set_common_options(common);
// + explicit binding remaps (see Task 4)
std::string out = hlsl.compile();
```

**thin3d shader-language selection:** `draw->GetShaderLanguageDesc()` → `ShaderLanguageDesc`; `.shaderLanguage` is `GLSL_VULKAN` (Vulkan), `GLSL_3xx`/`GLSL_1xx` (GL/GLES), `HLSL_D3D11` (D3D11). `CreateShaderModule(stage, language, const uint8_t *src, size_t, tag)` takes **source text in that dialect** and compiles internally on every backend.

**Binding gaps (the hard part):**
- **GL** (`Common/GPU/OpenGL/thin3d_gl.cpp`): binds sampler uniforms **by name** at link (`LinkShaders`, `:1321-1354`) and vertex attributes **by name** (`GLRProgram::Semantic`, `:1304-1316`: SEM_POSITION→"Position"/"a_position", SEM_TEXCOORD0→"TexCoord0"/"a_texcoord0"). `PipelineDesc.samplers` (a `Slice<SamplerDef>`) supplies custom sampler names → texture-unit bindings.
- **D3D11** (`Common/GPU/D3D11/thin3d_d3d11.cpp`, `#ifdef _WIN32`): textures at `register(tN)`, samplers `register(sN)`, cbuffer `register(b0)`; input layout maps `AttributeDesc.location` via `semanticToD3D11` (`:805-831`): 0→POSITION, **3→TEXCOORD0**, 1→COLOR0, 2→COLOR1. SPIRV-Cross HLSL auto-splits a combined `sampler2D` into `Texture2D`(tN)+`SamplerState`(sN) sharing N.
- **Attribute locations:** the current slang quad uses raw locations Position=0, TexCoord=1, Color=2 (Vulkan `layout(location=N)`). For D3D11/GL these must be PPSSPP `SEM_*` values (`SEM_POSITION=0`, `SEM_TEXCOORD0=3`, …) AND the cross-compiled shader's attribute naming/semantics must match. The slang vertex shader declares `layout(location=0) Position`, `layout(location=1) TexCoord` — SPIRV-Cross preserves these as HLSL `TEXCOORD0/1` by location index and as GL names/`_locN`.

**Format capability** (`GetDataFormatSupport(fmt)` → `FMT_RENDERTARGET` etc.): GL returns 0 for `R16G16B16A16_FLOAT` and `R8G8B8A8_UNORM_SRGB` (`thin3d_gl.cpp:1671-1723` default case) → slang already falls back to UNORM (`SlangFilterChain.cpp:434-445`). D3D11 supports both. Verified; no change needed.

**Backend enum/config:** `enum class GPUBackend { OPENGL=0, DIRECT3D11=2, VULKAN=3 };` (`Core/ConfigValues.h:131`); `int iGPUBackend` (`Core/Config.h:264`); `GPUBackend GetGPUBackend()`.

## File Structure

**New:**
- `GPU/Common/Slang/SlangCrossCompile.{h,cpp}` — the SPIR-V→dialect step + backend binding reconciliation, isolated and unit-testable. Added to the seven Core/GPU build lists (grep `GPU/Common/Slang/SlangPassCompiler.cpp` to find them all).

**Modified:**
- `GPU/Common/Slang/SlangPassCompiler.{h,cpp}` — `ReflectSlangSource` returns the SPIR-V words; `CompileSlangPass` branches Vulkan vs cross-compile; builds per-backend `InputLayoutDesc` (SEM_* locations) + `PipelineDesc.samplers` names.
- `GPU/Common/Slang/SlangFilterChain.cpp` — ONLY if a per-backend texture-slot/name difference is unavoidable (Task 6); keep gated and minimal.
- `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp` — cross-compile unit tests.

## Task Overview (implement in order)

- **Task 1** — Expose the SPIR-V words from `ReflectSlangSource` (no behavior change; Vulkan still works).
- **Task 2** — `SlangCrossCompile`: SPIR-V → desktop GLSL (`CompilerGLSL`), with sampler-name extraction (+ unit test).
- **Task 3** — Wire the GL branch into `CompileSlangPass`: per-backend shader modules, `PipelineDesc.samplers` names, SEM_* attribute locations. Runtime-verify on **macOS OpenGL**.
- **Task 4** — GLES specifics (ES version/precision) + on-device **Android GLES3** verification, including the float/sRGB→UNORM fallback.
- **Task 5** — D3D11/HLSL cross-compile behind `#ifdef _WIN32` (`CompilerHLSL` + register remaps + TEXCOORD semantics); compile-gated here, runtime-deferred to Windows.
- **Task 6** — Reconcile any texture-binding/attribute-name mismatch surfaced by Tasks 3-5 in `SlangFilterChain`/pipeline (only if needed), and a final cross-backend pass over crt-royale + a simple preset.

Task 1 is a safe refactor. Tasks 2-3 make GL work (the primary new backend, locally testable). Task 4 extends to GLES. Task 5 adds D3D11 (compile-only here). Task 6 is the integration/cleanup gate. Each ends at an independently testable deliverable; the Vulkan reference path is re-verified throughout.

---

### Task 1: Expose the compiled SPIR-V words from `ReflectSlangSource`

**Files:**
- Modify: `GPU/Common/Slang/SlangPassCompiler.h`, `GPU/Common/Slang/SlangPassCompiler.cpp`

**Interfaces:**
- Consumes: existing `ReflectSlangSource` internals (`vspv`/`fspv` at `SlangPassCompiler.cpp:276-278`).
- Produces: two new optional out-params on `ReflectSlangSource` returning the per-stage SPIR-V, so `CompileSlangPass` can cross-compile without re-running glslang:
  ```cpp
  bool ReflectSlangSource(const SlangSource &src, const SlangClassifyContext &ctx, PassReflection *out,
                          std::string *error,
                          std::string *outTransformedVert = nullptr, std::string *outTransformedFrag = nullptr,
                          std::vector<uint32_t> *outVertSpirv = nullptr, std::vector<uint32_t> *outFragSpirv = nullptr);
  ```
  (`#include <cstdint>` and `<vector>` in the header.)

**No behavior change** — this is a pure additive refactor. Existing callers (Vulkan path, unit tests) pass nullptr for the new params and are unaffected.

- [ ] **Step 1: Add the out-params** to the declaration (`SlangPassCompiler.h:32-33`) and definition. In the body, after the SPIR-V is built (`:276-278`), copy it out:
```cpp
	if (outVertSpirv) *outVertSpirv = vspv;
	if (outFragSpirv) *outFragSpirv = fspv;
```
Place this AFTER the `CompileStageToSpirv` calls and BEFORE the reflection uses them (the reflection compilers take the vectors by value/move — confirm `spirv_cross::Compiler vert(vspv)` copies, so copying out first is safe; if it moves, copy out before constructing the compilers).

- [ ] **Step 2: Verify no regression** — `cmake --build build-unittest --target PPSSPPUnitTest` succeeds; run `SlangReflection SlangPushConstant SlangParser` → all exit 0 (the reflection path is unchanged; the new out-params default to nullptr).

- [ ] **Step 3: Confirm Vulkan still builds** — build the Android APK (Vulkan path compiles `CompileSlangPass` unchanged). No on-device check needed yet (behavior identical).

- [ ] **Step 4: Commit**
```bash
git add GPU/Common/Slang/SlangPassCompiler.h GPU/Common/Slang/SlangPassCompiler.cpp
git commit -m "slang: expose per-stage SPIR-V from ReflectSlangSource for cross-compile

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `SlangCrossCompile` — SPIR-V → desktop GLSL, with sampler-name extraction

**Files:**
- Create: `GPU/Common/Slang/SlangCrossCompile.h`, `GPU/Common/Slang/SlangCrossCompile.cpp`
- Modify: the seven build lists (grep `GPU/Common/Slang/SlangPassCompiler.cpp` in CMakeLists.txt, GPU/GPU.vcxproj[.filters], libretro/Makefile.common, android/jni/Android.mk, UWP/*.vcxproj[.filters] — mirror how SlangPassCompiler.cpp is listed).
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Consumes: `spirv_cross::CompilerGLSL` (`ext/SPIRV-Cross/spirv_glsl.hpp`); `Common/GPU/Shader.h` (`ShaderLanguageDesc`).
- Produces (`SlangCrossCompile.h`):
  ```cpp
  #pragma once
  #include <cstdint>
  #include <string>
  #include <vector>
  #include "Common/GPU/Shader.h"

  // One cross-compiled stage: the backend-dialect source plus the sampler names the compiler
  // emitted, in binding order (GL binds samplers by name, so the pipeline needs these).
  struct SlangCrossResult {
      std::string source;
      std::vector<std::string> samplerNames;  // index i = the sampler at descriptor binding i+1
  };

  // Cross-compile a slang stage's SPIR-V to GLSL / GLSL-ES per 'desc'. Returns false + *error on failure.
  // 'stageIsFragment' selects fragment-only handling where needed.
  bool CrossCompileSlangToGLSL(const std::vector<uint32_t> &spirv, const ShaderLanguageDesc &desc,
                               bool stageIsFragment, SlangCrossResult *out, std::string *error);
  ```

- [ ] **Step 1: Write the failing test** — append `TestSlangCrossCompileGLSL()`; register `TEST_ITEM(SlangCrossCompileGLSL)`. Compile a tiny slang-style stage to SPIR-V using the SAME helper the reflector uses (expose or duplicate a minimal `CompileStageToSpirv`; simplest: reuse `ReflectSlangSource`'s new `outFragSpirv`), then cross-compile and assert the output is GLSL (contains `#version`, `void main`) and that a declared sampler appears:
```cpp
bool TestSlangCrossCompileGLSL() {
	// Minimal fragment stage: one UBO (MVP) + one sampler, mirroring slang layout.
	SlangSource src;
	src.name = "xc";
	src.vertex =
		"#version 450\n"
		"layout(std140, set=0, binding=0) uniform UBO { mat4 MVP; } g;\n"
		"layout(location=0) in vec4 Position;\n"
		"void main(){ gl_Position = g.MVP * Position; }\n";
	src.fragment =
		"#version 450\n"
		"layout(std140, set=0, binding=0) uniform UBO { mat4 MVP; } g;\n"
		"layout(set=0, binding=1) uniform sampler2D Source;\n"
		"layout(location=0) out vec4 FragColor;\n"
		"void main(){ FragColor = texture(Source, vec2(0.5)); }\n";
	SlangClassifyContext ctx;
	PassReflection refl; std::string err;
	std::vector<uint32_t> vspv, fspv;
	EXPECT_TRUE(ReflectSlangSource(src, ctx, &refl, &err, nullptr, nullptr, &vspv, &fspv));
	EXPECT_TRUE(!fspv.empty());

	// Desktop GL 3.3-ish desc.
	ShaderLanguageDesc desc; desc.shaderLanguage = GLSL_3xx; desc.gles = false; desc.glslVersionNumber = 330;
	SlangCrossResult res;
	EXPECT_TRUE(CrossCompileSlangToGLSL(fspv, desc, true, &res, &err));
	EXPECT_TRUE(res.source.find("#version") != std::string::npos);
	EXPECT_TRUE(res.source.find("main") != std::string::npos);
	// The Source sampler must be discoverable by name (GL binds by name).
	bool hasSampler = false;
	for (auto &n : res.samplerNames) if (!n.empty()) hasSampler = true;
	EXPECT_TRUE(hasSampler);
	return true;
}
```

- [ ] **Step 2: Run to verify it fails** — compile error (`CrossCompileSlangToGLSL` undefined).

- [ ] **Step 3: Implement `SlangCrossCompile.cpp`** (GPL header). Include `ext/SPIRV-Cross/spirv_glsl.hpp`:
```cpp
#include "GPU/Common/Slang/SlangCrossCompile.h"
#include "ext/SPIRV-Cross/spirv_glsl.hpp"

bool CrossCompileSlangToGLSL(const std::vector<uint32_t> &spirv, const ShaderLanguageDesc &desc,
                             bool stageIsFragment, SlangCrossResult *out, std::string *error) {
	try {
		spirv_cross::CompilerGLSL compiler(spirv);   // copies the words
		spirv_cross::CompilerGLSL::Options opts;
		opts.version = desc.glslVersionNumber > 0 ? desc.glslVersionNumber : (desc.gles ? 300 : 330);
		opts.es = desc.gles;
		// On GL without 420pack, explicit layout(binding=) on samplers is illegal, so SPIRV-Cross
		// emits named uniforms and the pipeline binds by name — which is what thin3d GL wants.
		opts.enable_420pack_extension = false;
		compiler.set_common_options(opts);

		// Record sampler names in binding order so the pipeline can bind by name.
		spirv_cross::ShaderResources resources = compiler.get_shader_resources();
		// binding N (>=1) -> slot N-1; size the vector to the max binding.
		uint32_t maxBinding = 0;
		for (const auto &img : resources.sampled_images) {
			uint32_t b = compiler.get_decoration(img.id, spv::DecorationBinding);
			if (b > maxBinding) maxBinding = b;
		}
		out->samplerNames.assign(maxBinding, "");   // index by (binding-1)
		for (const auto &img : resources.sampled_images) {
			uint32_t b = compiler.get_decoration(img.id, spv::DecorationBinding);
			// SPIRV-Cross may rename; capture the name it will actually emit.
			std::string emitted = compiler.get_name(img.id);
			if (b >= 1 && b - 1 < out->samplerNames.size())
				out->samplerNames[b - 1] = emitted;
		}

		out->source = compiler.compile();   // must run AFTER querying names? get_name is stable pre-compile; if empty, re-read post-compile.
		return true;
	} catch (const std::exception &e) {
		*error = std::string("SPIRV-Cross GLSL: ") + e.what();
		return false;
	}
}
```
Implementer note: SPIRV-Cross may finalize emitted names during `compile()`. If `get_name` returns empty/unstable before compile, call `compile()` first, then re-query `get_name(img.id)` (names are stable after compile). Verify empirically in the test (the test asserts a non-empty sampler name) and reorder if needed. Also confirm the `sampled_images` binding numbering matches the slang convention (UBO=0, samplers=1..N) so slot = binding-1 is consistent with `SlangFilterChain.cpp:653`.

- [ ] **Step 4: Register** the new file in all seven build lists.

- [ ] **Step 5: Run to verify it passes** — `cmake --build build-unittest --target PPSSPPUnitTest && ./build-unittest/PPSSPPUnitTest SlangCrossCompileGLSL` → exit 0.

- [ ] **Step 6: Commit**
```bash
git add GPU/Common/Slang/SlangCrossCompile.h GPU/Common/Slang/SlangCrossCompile.cpp CMakeLists.txt GPU/GPU.vcxproj GPU/GPU.vcxproj.filters libretro/Makefile.common android/jni/Android.mk UWP/*/*.vcxproj UWP/*/*.vcxproj.filters unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: SPIR-V -> GLSL/GLES cross-compile with sampler-name extraction

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Wire the OpenGL branch into `CompileSlangPass`; verify on macOS OpenGL

**Files:**
- Modify: `GPU/Common/Slang/SlangPassCompiler.cpp`

**Interfaces:**
- Consumes: `CrossCompileSlangToGLSL` (Task 2), the SPIR-V from `ReflectSlangSource` (Task 1), `draw->GetShaderLanguageDesc()`, `PipelineDesc.samplers` (`Slice<SamplerDef>`), `Semantic` enum (`SEM_POSITION=0`, `SEM_TEXCOORD0=3`, `SEM_COLOR1=2`).
- Produces: `CompileSlangPass` renders on OpenGL. No signature change.

**Note:** UI/pipeline runtime — verified by macOS OpenGL launch, not a unit test. Vulkan path must stay identical.

- [ ] **Step 1: Capture SPIR-V in `CompileSlangPass`** — change the `ReflectSlangSource` call (`SlangPassCompiler.cpp:343`) to also request `&vspv, &fspv`.

- [ ] **Step 2: Replace the hard-error + module-creation block** (`:351-363`) with a backend branch:
```cpp
	ShaderLanguage backendLang = draw->GetShaderLanguageDesc().shaderLanguage;
	Draw::ShaderModule *vs = nullptr;
	Draw::ShaderModule *fs = nullptr;
	std::vector<std::string> fragSamplerNames;   // for PipelineDesc.samplers on GL
	if (backendLang == GLSL_VULKAN) {
		vs = draw->CreateShaderModule(ShaderStage::Vertex, GLSL_VULKAN, (const uint8_t *)transformedVert.c_str(), transformedVert.size(), src.name.c_str());
		fs = draw->CreateShaderModule(ShaderStage::Fragment, GLSL_VULKAN, (const uint8_t *)transformedFrag.c_str(), transformedFrag.size(), src.name.c_str());
	} else if (backendLang == GLSL_3xx || backendLang == GLSL_1xx) {
		SlangCrossResult vres, fres;
		if (!CrossCompileSlangToGLSL(vspv, draw->GetShaderLanguageDesc(), false, &vres, error) ||
		    !CrossCompileSlangToGLSL(fspv, draw->GetShaderLanguageDesc(), true, &fres, error)) {
			return false;
		}
		fragSamplerNames = fres.samplerNames;
		vs = draw->CreateShaderModule(ShaderStage::Vertex, backendLang, (const uint8_t *)vres.source.c_str(), vres.source.size(), src.name.c_str());
		fs = draw->CreateShaderModule(ShaderStage::Fragment, backendLang, (const uint8_t *)fres.source.c_str(), fres.source.size(), src.name.c_str());
	} else {
		*error = "slang: unsupported backend shader language";
		return false;
	}
```

- [ ] **Step 3: Per-backend attribute locations.** The current `InputLayoutDesc` (`:397-407`) uses raw `LOC_POSITION=0, LOC_TEXCOORD=1, LOC_COLOR=2`. For Vulkan those are correct (raw `layout(location=N)`). For GL, attributes bind by NAME via the `SEM_*`→name table, so set the `AttributeDesc.location` to `SEM_*` values and confirm the cross-compiled GL shader's attribute names match PPSSPP's expected names (Position→"Position", TexCoord→"TexCoord0"). Build the input layout conditionally:
  - Vulkan: `{SEM? no — raw 0/1/2 as today}`.
  - GL/GLES: `{ SEM_POSITION(0), SEM_TEXCOORD0(3), SEM_COLOR0(1) }` for the three attributes, matching thin3d GL's `GLRProgram::Semantic` name table.
  Determine empirically whether SPIRV-Cross's GL output names the attributes such that thin3d GL's name lookup finds them; if not, this is the reconciliation deferred to Task 6 (the cross-compiled GL vertex shader may need attribute renaming, or the slang quad's vertex inputs re-declared). Document what you find; if GL attributes don't bind, Task 6 handles it — but attempt the SEM_* mapping here first.

- [ ] **Step 4: Supply sampler names to the pipeline (GL).** When `backendLang` is GL/GLES and `fragSamplerNames` is non-empty, build a `std::vector<Draw::SamplerDef>` (name = fragSamplerNames[i], binding = i) and set `pipelineDesc.samplers = Slice<SamplerDef>(...)`. Keep the vector alive until `CreateGraphicsPipeline` returns. For Vulkan leave `samplers` empty (unused).

- [ ] **Step 5: Build + macOS OpenGL runtime check.** Build the desktop GL build (confirm the exact desktop build target/command; e.g. the SDL build). Launch with GPU backend = OpenGL (`iGPUBackend=0`). Load a SIMPLE bundled slang fixture first (e.g. `assets/shaders/slang_test/stock.slangp` — a single-pass identity/tint), confirm it renders on GL. Then try crt-royale. The CONTROLLER performs this visual check.

- [ ] **Step 6: Vulkan regression** — build the Android APK, confirm crt-royale still renders on Vulkan unchanged (the `GLSL_VULKAN` branch is behavior-identical). Run the 19 slang unit tests → all green.

- [ ] **Step 7: Commit**
```bash
git add GPU/Common/Slang/SlangPassCompiler.cpp
git commit -m "slang: render on OpenGL via SPIR-V->GLSL cross-compile + name-based sampler binding

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: OpenGL ES specifics + on-device Android GLES3 verification

**Files:**
- Modify: `GPU/Common/Slang/SlangCrossCompile.cpp` (ES-specific options), if needed
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp` (a GLES-target cross-compile test)

**Interfaces:**
- Consumes: `CrossCompileSlangToGLSL` with `desc.gles = true`.
- Produces: cross-compiled GLSL-ES that compiles on Adreno GLES3; the float/sRGB→UNORM framebuffer fallback confirmed on GLES.

- [ ] **Step 1: GLES-target unit test** — append `TestSlangCrossCompileGLES()` (register it): same SPIR-V as Task 2, but `desc.gles=true; desc.glslVersionNumber=300;`. Assert the output contains an ES version directive (`#version 300 es`) and a default-precision qualifier for float (`precision` … `float`), which SPIRV-Cross emits for ES fragment shaders. If SPIRV-Cross doesn't add `precision highp float;` automatically for the ES profile, add it in `CrossCompileSlangToGLSL` when `desc.gles && stageIsFragment` (prepend after the `#version` line), because a GLES fragment shader without a default float precision fails to compile. Assert its presence in the test.

- [ ] **Step 2: Run to verify** — `./build-unittest/PPSSPPUnitTest SlangCrossCompileGLES` → exit 0.

- [ ] **Step 3: On-device Android GLES3 check (controller).** Build+install the APK. In Settings → Graphics → Backend, switch from Vulkan to **OpenGL** (GLES on Android). Restart if required. Load `stock.slangp` (simple) → confirm it renders on GLES. Then crt-royale: it uses sRGB + float intermediate framebuffers, which GLES lacks — confirm the runtime falls back to UNORM (already implemented) and the chain still renders (colors may differ slightly from Vulkan due to the missing sRGB/float RTs; that is expected and acceptable per the design's graceful-degradation goal). Watch logcat for cross-compile or GL link errors. Record findings; if crt-royale's 12-pass chain has a GLES-specific compile failure in a particular pass, note which pass and its error for Task 6.

- [ ] **Step 4: Vulkan + GL desktop regression** — switch the device back to Vulkan, confirm crt-royale still renders (no regression). Confirm macOS GL (Task 3) still works. 19 unit tests green.

- [ ] **Step 5: Commit**
```bash
git add GPU/Common/Slang/SlangCrossCompile.cpp unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: GLES fragment precision + on-device GLES3 verification

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: D3D11 / HLSL cross-compile (Windows-only, compile-gated here)

**Files:**
- Modify: `GPU/Common/Slang/SlangCrossCompile.{h,cpp}` (add the HLSL path behind `#ifdef _WIN32`... see note)
- Modify: `GPU/Common/Slang/SlangPassCompiler.cpp` (D3D11 branch)

**Interfaces:**
- Produces: `bool CrossCompileSlangToHLSL(const std::vector<uint32_t> &spirv, const PassReflection &refl, bool stageIsFragment, std::string *outSource, std::string *error);` — takes the reflection so it can drive explicit register remaps from `SlangTextureBinding.binding` (the existing `sscanf("sampler%d")` trick in ShaderTranslation.cpp won't match slang names like `Source`).

**IMPORTANT scoping:** the D3D11 backend and SPIRV-Cross's HLSL emitter compile everywhere (SPIRV-Cross `CompilerHLSL` is not Windows-gated — only PPSSPP's *D3D11 thin3d backend* is `#ifdef _WIN32`). So: put `CrossCompileSlangToHLSL` itself OUTSIDE `#ifdef _WIN32` (it's pure SPIRV-Cross, compiles + unit-tests on macOS), but the `CompileSlangPass` branch that calls it for a live `HLSL_D3D11` backend only executes on Windows (that backend only exists there). This lets the HLSL cross-compile be UNIT-TESTED on macOS even though runtime D3D11 is Windows-only.

- [ ] **Step 1: Write the HLSL cross-compile unit test** (runs on macOS — SPIRV-Cross HLSL is portable). `TestSlangCrossCompileHLSL()`: same SPIR-V as Task 2, cross-compile to HLSL SM5.0, assert output contains `cbuffer` (the UBO), `register(b0)` (UBO at b0), and a `Texture2D` + `SamplerState` split for the `Source` sampler at matching `t`/`s` register indices derived from its binding. Register it.

- [ ] **Step 2: Run to verify it fails** — undefined function.

- [ ] **Step 3: Implement `CrossCompileSlangToHLSL`** using `spirv_cross::CompilerHLSL` (`ext/SPIRV-Cross/spirv_hlsl.hpp`):
```cpp
	spirv_cross::CompilerHLSL hlsl(spirv);
	spirv_cross::CompilerHLSL::Options hopts; hopts.shader_model = 50;
	spirv_cross::CompilerGLSL::Options common;
	common.vertex.flip_vert_y = true;
	common.vertex.fixup_clipspace = true;
	hlsl.set_hlsl_options(hopts);
	hlsl.set_common_options(common);
	// Pin registers from reflection so they match thin3d's slot = binding-1 model:
	//   UBO (binding 0) -> b0; sampler (binding N) -> t(N-1)/s(N-1).
	spirv_cross::ShaderResources res = hlsl.get_shader_resources();
	for (const auto &ubo : res.uniform_buffers) {
		hlsl.set_decoration(ubo.id, spv::DecorationBinding, 0);   // cbuffer b0
	}
	for (const auto &img : res.sampled_images) {
		uint32_t b = hlsl.get_decoration(img.id, spv::DecorationBinding);
		// SPIRV-Cross HLSL auto-splits combined sampler into Texture2D(tN)+SamplerState(sN)
		// sharing the binding index; pin to (b-1) so t/s align with thin3d texture slot (binding-1).
		hlsl.set_decoration(img.id, spv::DecorationBinding, b >= 1 ? b - 1 : 0);
	}
	*outSource = hlsl.compile();
```
Wrap in try/catch → `*error`. Verify against the test that the register indices land where thin3d D3D11 binds them (`PSSetShaderResources(slot)` / `PSSetSamplers(slot)` with slot = binding-1). If SPIRV-Cross needs `hlsl.remap_num_workgroups_builtin` or a `resource_binding` API instead of `set_decoration` for stable HLSL registers, use the `hlsl.add_hlsl_resource_binding(...)` API — verify the exact SPIRV-Cross API available in the vendored `ext/SPIRV-Cross/spirv_hlsl.hpp` before writing (the version may differ; read the header).

- [ ] **Step 4: Add the D3D11 branch in `CompileSlangPass`** — extend the backend branch from Task 3:
```cpp
	} else if (backendLang == HLSL_D3D11) {
		std::string vsrc, fsrc;
		if (!CrossCompileSlangToHLSL(vspv, out->reflection, false, &vsrc, error) ||
		    !CrossCompileSlangToHLSL(fspv, out->reflection, true, &fsrc, error)) {
			return false;
		}
		vs = draw->CreateShaderModule(ShaderStage::Vertex, HLSL_D3D11, (const uint8_t *)vsrc.c_str(), vsrc.size(), src.name.c_str());
		fs = draw->CreateShaderModule(ShaderStage::Fragment, HLSL_D3D11, (const uint8_t *)fsrc.c_str(), fsrc.size(), src.name.c_str());
	}
```
And the D3D11 input layout: `AttributeDesc.location` must be `SEM_POSITION(0)`, `SEM_TEXCOORD0(3)`, `SEM_COLOR0(1)` so `semanticToD3D11` maps them to `POSITION`/`TEXCOORD0`/`COLOR0`, matching the HLSL vertex input semantics SPIRV-Cross emits by location index. (This is the same SEM_* mapping as GL — factor the input-layout construction so Vulkan uses raw 0/1/2 and both GL and D3D11 use SEM_* values.)

- [ ] **Step 5: Verify** — `cmake --build build-unittest --target PPSSPPUnitTest && ./build-unittest/PPSSPPUnitTest SlangCrossCompileHLSL` → exit 0 (the HLSL *cross-compile* is tested on macOS). The `CompileSlangPass` D3D11 *branch* compiles on macOS (it's plain C++ referencing `HLSL_D3D11` enum + portable SPIRV-Cross) but only executes on Windows. Build the Android APK to confirm nothing broke. **Runtime D3D11 verification is DEFERRED to a Windows environment** — state this explicitly in the commit body; do not claim D3D11 renders.

- [ ] **Step 6: Commit**
```bash
git add GPU/Common/Slang/SlangCrossCompile.h GPU/Common/Slang/SlangCrossCompile.cpp GPU/Common/Slang/SlangPassCompiler.cpp unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: D3D11/HLSL cross-compile with register remaps (Windows runtime deferred)

HLSL cross-compile (portable SPIRV-Cross) is unit-tested; the live D3D11 path
compiles but only runs on Windows, where runtime verification is pending.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Cross-backend reconciliation + final integration pass

**Files:**
- Modify: `GPU/Common/Slang/SlangPassCompiler.cpp` and/or `GPU/Common/Slang/SlangFilterChain.cpp` (only what Tasks 3-5 proved necessary)

**Rationale:** Tasks 3-5 may surface concrete mismatches that can't be known until the cross-compiled shaders actually link/run: (a) GL attribute names not matching thin3d's `SEM_*`→name table (needing attribute renaming or a re-declared quad vertex layout); (b) a sampler-name mismatch between what `SlangCrossCompile` recorded and what the linker sees; (c) a crt-royale pass that compiles on Vulkan but not GLES. This task fixes whatever those are, minimally and gated per-backend, then does the full sweep.

- [ ] **Step 1: Resolve any attribute-binding mismatch** found in Task 3 Step 3 / Task 5 Step 4. Options, cheapest first: (a) set the input-layout `AttributeDesc.location` to the right `SEM_*` per backend (already attempted in Task 3/5); (b) if GL still can't find the attributes by name, force SPIRV-Cross to emit known attribute names via `compiler.set_name(id, "Position"/"TexCoord0")` before `compile()` in `SlangCrossCompile` (add an attribute-name pass keyed off the vertex `stage_inputs` locations); (c) last resort, re-declare the quad's vertex inputs. Pick the minimal fix the runtime proved necessary; document it.

- [ ] **Step 2: Resolve any per-pass GLES compile failure** from Task 4 Step 3. If a specific crt-royale pass fails only on GLES (e.g. a construct SPIRV-Cross emits that Adreno's GLES compiler rejects), capture the exact shader + error and apply the narrowest fix in `SlangCrossCompile` (an option toggle or a post-process string fixup), or document it as a known GLES limitation for that shader if it's a genuine GLES capability gap (not all desktop-GL shaders are GLES-portable).

- [ ] **Step 3: Full cross-backend sweep (controller).** With all fixes in:
  - **Vulkan (Android):** crt-royale renders correctly (reference) — unchanged.
  - **OpenGL (macOS):** `stock.slangp` renders; crt-royale renders (or documented pass-level limitation).
  - **GLES3 (Android):** `stock.slangp` renders; crt-royale renders with UNORM fallback (or documented limitation).
  - **D3D11:** cross-compile unit test green; runtime deferred to Windows (documented).
  Record each result explicitly in the commit body.

- [ ] **Step 4: Re-verify no Vulkan regression + all unit tests green.** Build APK (Vulkan) and desktop unittest; 19+ slang tests exit 0.

- [ ] **Step 5: Commit**
```bash
git add GPU/Common/Slang/SlangPassCompiler.cpp GPU/Common/Slang/SlangFilterChain.cpp GPU/Common/Slang/SlangCrossCompile.cpp
git commit -m "slang: reconcile cross-backend attribute/sampler binding; final multi-backend pass

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Out of scope for Phase 5

- Non-slang shader changes; parser/runtime-semantic changes; UI changes (all stable from Phases 1-4).
- Runtime D3D11 *verification* (needs Windows; the code path + unit test land here, runtime is a follow-up on a Windows machine/CI).
- Enabling float/sRGB intermediate framebuffers on GL/GLES where the backend lacks them — the UNORM fallback stands; native float/sRGB RT support on GL is a possible future enhancement, not this phase.
- Metal (MSL) — PPSSPP uses MoltenVK for Vulkan on Apple; there is no separate Metal thin3d backend, so no MSL path is needed.

## Self-Review (completed during authoring)

- **Design §9 / §5 coverage:** Phase 5 = "D3D11, then OpenGL/GLES, via SPIRV-Cross cross-compile; handle per-backend framebuffer-format capability fallbacks." Delivered: GL (Tasks 2-3), GLES (Task 4), D3D11 (Task 5), format fallback confirmed (already implemented; re-verified Task 4). The design's "Vulkan consumes SPIR-V directly; GL/GLES/D3D11 reuse SPIRV-Cross" is exactly the seam in Task 3/5.
- **Ordering rationale:** GL first because it's the only cross-backend runtime-testable target on this macOS+Android setup; GLES second (Android); D3D11 last (unit-testable cross-compile, runtime deferred). Task 1 is a zero-risk refactor that unblocks all cross paths.
- **The three researched gaps are each assigned:** SPIR-V reuse (Task 1), GL name-based sampler+attribute binding (Tasks 2-3, reconciled in 6), D3D11 register remaps + TEXCOORD semantics (Task 5). `TranslateShader` is explicitly NOT reused (Global Constraints).
- **Regression discipline:** every task re-verifies the Vulkan reference path + the 19 unit tests; the `GLSL_VULKAN` branch is byte-identical to today.
- **Type consistency:** `SlangCrossResult`, `CrossCompileSlangToGLSL`/`ToHLSL` signatures, the `ReflectSlangSource` SPIR-V out-params, and the `PipelineDesc.samplers`/SEM_* input-layout usage are consistent across Tasks 1→6.
- **Placeholder scan:** code steps carry concrete code; three spots explicitly say "verify the exact SPIRV-Cross API / name-stability / attribute naming in-tree before writing" (get_name stability in Task 2, HLSL resource-binding API in Task 5, GL attribute naming in Tasks 3/6) — these are genuine "read the vendored header / observe runtime behavior" instructions, because the exact SPIRV-Cross version's API and its emitted names can't be assumed and are best confirmed at implementation time. Each has a concrete fallback.

