# Slang Shader Support — Phase 2 (Full Vulkan Conformance) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the Phase-1 Vulkan slang rendering core to full libretro conformance: cross-pass output/alias references, one-frame pass feedback, the `OriginalHistory#` input ring, PNG LUT textures, `mipmap_input` + per-pass wrap modes, `frame_count_mod`, and sRGB / float intermediate framebuffers — with **crt-royale** as the conformance target.

**Architecture:** Build on the existing `GPU/Common/Slang/` subsystem (`SlangpParser`, `SlangReflection`, `SlangPassCompiler`, `SlangFilterChain`). Phase 2 widens the reflected texture-semantic set, generalizes `SlangFilterChain`'s single-input model into a keyed pool of pass outputs / feedback buffers / history frames / LUTs, threads per-pass render-target format + mipmap + wrap through both the `.slangp` parser and the runtime, and — the one change that reaches outside the subsystem — adds an optional color-format field to thin3d's `FramebufferDesc` so intermediate framebuffers can be sRGB or float. Still Vulkan-only; D3D11/GL/GLES remain Phase 5.

**Tech Stack:** C++17, PPSSPP `thin3d` (Draw::) with a small core extension, vendored `glslang` + `SPIRV-Cross`, PPSSPP `pngLoad` (`Common/Data/Format/PngLoad.h`), PPSSPP `unittest` harness.

## Global Constraints

- **License header:** every new/modified file keeps the PPSSPP GPL 2.0 header (existing Slang files use year `2026-`; copy that style for new files).
- **Backend:** Vulkan only. `CompileSlangPass` already hard-errors on non-Vulkan backends; keep that guard. Any new framebuffer-format capability must degrade gracefully (see Task 8) rather than assume a format exists.
- **Slang spec conformance:** follow the libretro slang spec exactly — `*Size` uniforms are `vec4 (w, h, 1/w, 1/h)`; `PassOutput#` is causal (a pass may only read outputs of *earlier* passes this frame); `PassFeedback#` reads the previous frame's output of any pass; `OriginalHistory0` == `Original`, `OriginalHistoryN` == input N frames ago; the reflected max `OriginalHistoryN`/`PassOutputN` index determines ring/pool depth.
- **Loud-fail preserved:** any UBO member or sampler whose name cannot be classified to a supported semantic must still make `ReflectSlangSource` return false with a clear error (never render garbage). Phase 2 *expands* the supported set; it does not weaken the guard.
- **Zero regression:** the Phase-1 single-pass path (e.g. `assets/shaders/slang_test/stock.slangp`) must keep rendering identically; existing 5 slang unit tests stay green.
- **thin3d changes are additive:** the new `FramebufferDesc` color-format field must default to the current behavior (`R8G8B8A8_UNORM`) so every existing thin3d caller is unaffected.
- **Build + test env (from Phase 1, still valid):**
  - Desktop unit build: reconfigure with
    `SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk; cmake -S . -B build-unittest -G Ninja -DUNITTEST=ON -DHEADLESS=OFF -DUSE_FFMPEG=OFF -DUSE_DISCORD=OFF -DUSING_QT_UI=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew -DCMAKE_OSX_SYSROOT="$SDK" -DCMAKE_CXX_FLAGS="-isystem $SDK/usr/include/c++/v1" -DCMAKE_C_FLAGS="-isystem $SDK/usr/include"` then `cmake --build build-unittest --target PPSSPPUnitTest`.
  - Unit tests are `bool TestXxx()` using `unittest/UnitTest.h` macros, registered in `unittest/UnitTest.cpp` (forward decl + `TEST_ITEM`). Run `./build-unittest/PPSSPPUnitTest <Name>`; **exit 0 = pass** (no success banner).
  - On-device: Android arm64 APK via
    `export ANDROID_HOME=/opt/homebrew/share/android-commandlinetools; ./gradlew -p android assembleNormalDebug -Pandroid.injected.build.abi=arm64-v8a -PANDROID_VERSION_CODE=999999999 -PANDROID_VERSION_NAME=slang-dev --console=plain`; install with `adb install -t -r <apk>`. Device: AYN Thor (Adreno, Vulkan). PSP games under `/storage/9C33-6BBD/ROMs/psp/`.
- **Commit trailer:** end every commit message with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

## Known Phase-1 state this plan builds on (verbatim current interfaces)

- `SlangPreset.h`: `SlangScaleType{Source,Viewport,Absolute}`; `SlangParamDesc{name,initial,minimum,maximum,step}`; `SlangPassDesc{shaderPath,alias,filterLinear,scaleTypeX,scaleTypeY,scaleX,scaleY}`; `SlangPreset{basePath,passes,params}`.
- `SlangReflection.h`: `enum class SlangSemantic{Unknown,MVP,OutputSize,FinalViewportSize,FrameCount,FrameDirection,Rotation,SourceSize,OriginalSize,UserParameter,TexSource,TexOriginal}`; `SlangUniformMember{name,semantic,offsetBytes,sizeBytes}`; `SlangTextureBinding{name,semantic,binding}`; `PassReflection{uboMembers,uboSizeBytes,uboBinding,textures}`.
- `SlangReflection.cpp`: `ClassifyUniform(name, knownParams)`, `ClassifyTexture(name)` (only `Source`/`Original` today; returns Unknown otherwise).
- `SlangFilterChain`: owns `draw_`, `preset_`, `passes_` (`SlangCompiledPass{pipeline, reflection}`), `passFramebuffers_`, `quad_`, `samplerLinear_/Nearest_`, `presetPath_`. `Load()` parses+compiles; `Run(source, sourceW, sourceH, viewportW, viewportH, frameCount)` executes; `ReleaseResources()` frees GPU objects keeping `draw_`; `DeviceLost()` also nulls `draw_`.
- Per-pass draw order in `Run` (established in Phase 1, must be preserved): `BindFramebufferAsRenderTarget` → viewport/scissor → bind textures+samplers (texture slot = shader `binding - 1`) → `BindPipeline` → `UpdateDynamicUniformBuffer` → `BindVertexBuffer` → `Draw(4)`.
- Vertex quad layout: `float pos[3] + float uv[2] + uint32 color`, stride 24; attribute locations must be explicit (Position=0, TexCoord=1, Color=2) — NOT PPSSPP Semantic enums.

## File Structure

**Modified (subsystem):**
- `GPU/Common/Slang/SlangPreset.h` — add per-pass `srgbFramebuffer`, `floatFramebuffer`, `mipmapInput`, `wrapMode`, `frameCountMod`, `formatOverride`; add `SlangLutDesc` + `preset_.luts`; add `feedbackPass`.
- `GPU/Common/Slang/SlangpParser.{h,cpp}` — parse `srgb_framebufferN`, `float_framebufferN`, `mipmap_inputN`, `wrap_modeN`, `frame_count_modN`, `feedback_pass`, `textures`/per-LUT keys, and `#pragma format` from the `.slang`.
- `GPU/Common/Slang/SlangReflection.{h,cpp}` — new semantics: `TexPassOutput`, `TexPassFeedback`, `TexOriginalHistory`, `TexLut`, plus their `*Size` uniform companions; parameterize classification with the preset's alias/LUT names and index metadata (return an index alongside the semantic).
- `GPU/Common/Slang/SlangFilterChain.{h,cpp}` — generalize into a keyed texture registry (pass outputs, feedback buffers, history ring, LUTs); per-pass format/mipmap/wrap; feedback ping-pong; history advance.
- `GPU/Common/Slang/SlangResolution.h` — unchanged (already handles per-axis source/viewport/absolute).

**Modified (thin3d core — additive, Task 8):**
- `Common/GPU/thin3d.h` — add `DataFormat colorFormat = DataFormat::R8G8B8A8_UNORM;` to `FramebufferDesc`.
- `Common/GPU/Vulkan/thin3d_vulkan.cpp`, `Common/GPU/Vulkan/VulkanFramebuffer.{h,cpp}` — thread the color format into `VKRFramebuffer` image + render-pass creation (defaulting to today's `VK_FORMAT_R8G8B8A8_UNORM`).

**New (tests):**
- Extend `unittest/TestSlangParser.cpp` with parser + reflection + resolution cases for every new feature.

**New (fixtures):**
- `assets/shaders/slang_test/twopass.slangp` (+ two `.slang`) — a source→viewport two-pass chain using `PassOutput0` and `OriginalHistory1`.
- `assets/shaders/slang_test/feedback.slangp` (+ `.slang`) — a single feedback pass (motion-blur style) to exercise `PassFeedback`.
- A LUT fixture: a small PNG + `.slangp`/`.slang` sampling it.

**Conformance target (not committed as a fixture; fetched/side-loaded for on-device verification):**
- crt-royale from the libretro slang-shaders repo (12 passes, 6 LUTs, mixed scale types, sRGB intermediates).

---

### Task 1: Extend preset structs and `.slangp` parser for new per-pass keys

**Files:**
- Modify: `GPU/Common/Slang/SlangPreset.h`
- Modify: `GPU/Common/Slang/SlangpParser.cpp`
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Consumes: existing `SlangPassDesc`, `SlangPreset`, `ParseSlangPreset`.
- Produces (added to `SlangPreset.h`):
  ```cpp
  enum class SlangWrapMode { ClampToBorder, ClampToEdge, Repeat, MirroredRepeat };
  enum class SlangFbFormat { Default, Srgb, Float };   // maps to R8G8B8A8_UNORM / _SRGB / R16G16B16A16_FLOAT
  ```
  and new fields on `SlangPassDesc` (append, keep existing order):
  ```cpp
  bool srgbFramebuffer = false;   // srgb_framebufferN
  bool floatFramebuffer = false;  // float_framebufferN
  bool mipmapInput = false;       // mipmap_inputN
  SlangWrapMode wrapMode = SlangWrapMode::ClampToBorder;  // wrap_modeN (slang default is clamp_to_border)
  int frameCountMod = 0;          // frame_count_modN; 0 = no modulo
  SlangFbFormat formatOverride = SlangFbFormat::Default;  // from #pragma format, filled in Task 4
  ```
  and on `SlangPreset`:
  ```cpp
  int feedbackPass = -1;          // global feedback_pass; -1 = none
  ```

- [ ] **Step 1: Write the failing test** — append `TestSlangParserPhase2Keys()` to `TestSlangParser.cpp`, register `TEST_ITEM(SlangParserPhase2Keys)`:

```cpp
bool TestSlangParserPhase2Keys() {
	const std::string preset =
		"shaders = 2\n"
		"feedback_pass = 0\n"
		"shader0 = a.slang\n"
		"srgb_framebuffer0 = true\n"
		"mipmap_input0 = true\n"
		"wrap_mode0 = repeat\n"
		"frame_count_mod0 = 60\n"
		"shader1 = b.slang\n"
		"float_framebuffer1 = true\n"
		"wrap_mode1 = clamp_to_edge\n";
	SlangPreset out; std::string err; Path base("/tmp/x");
	EXPECT_TRUE(ParseSlangPreset(preset, base, &out, &err));
	EXPECT_EQ_INT(out.feedbackPass, 0);
	EXPECT_TRUE(out.passes[0].srgbFramebuffer);
	EXPECT_FALSE(out.passes[0].floatFramebuffer);
	EXPECT_TRUE(out.passes[0].mipmapInput);
	EXPECT_TRUE(out.passes[0].wrapMode == SlangWrapMode::Repeat);
	EXPECT_EQ_INT(out.passes[0].frameCountMod, 60);
	EXPECT_TRUE(out.passes[1].floatFramebuffer);
	EXPECT_TRUE(out.passes[1].wrapMode == SlangWrapMode::ClampToEdge);
	// Defaults on an unspecified pass field:
	EXPECT_TRUE(out.passes[1].wrapMode != SlangWrapMode::ClampToBorder);  // it was set
	EXPECT_FALSE(out.passes[1].srgbFramebuffer);
	return true;
}
```

- [ ] **Step 2: Run to verify it fails**

`./build-unittest/PPSSPPUnitTest SlangParserPhase2Keys` → compile error (fields don't exist yet). Add the enums/fields to `SlangPreset.h`, then it should compile-fail on missing parse logic (test asserts fail).

- [ ] **Step 3: Add enums + fields** to `SlangPreset.h` as specified in Interfaces.

- [ ] **Step 4: Implement parsing** in `SlangpParser.cpp`. Add a `ParseWrapMode` helper next to `ParseScaleType`:

```cpp
static SlangWrapMode ParseWrapMode(const std::string &v) {
	if (v == "clamp_to_edge") return SlangWrapMode::ClampToEdge;
	if (v == "repeat") return SlangWrapMode::Repeat;
	if (v == "mirrored_repeat") return SlangWrapMode::MirroredRepeat;
	return SlangWrapMode::ClampToBorder;  // slang default
}
```
Inside the per-pass loop (after the existing scale parsing, before `out->passes.push_back(pass)`):
```cpp
if (getStr("srgb_framebuffer" + idx, &tmp)) pass.srgbFramebuffer = (tmp == "true" || tmp == "1");
if (getStr("float_framebuffer" + idx, &tmp)) pass.floatFramebuffer = (tmp == "true" || tmp == "1");
if (getStr("mipmap_input" + idx, &tmp)) pass.mipmapInput = (tmp == "true" || tmp == "1");
if (getStr("wrap_mode" + idx, &tmp)) pass.wrapMode = ParseWrapMode(tmp);
if (getStr("frame_count_mod" + idx, &tmp)) pass.frameCountMod = atoi(tmp.c_str());
```
After the loop, parse the global feedback pass:
```cpp
std::string fp;
if (getStr("feedback_pass", &fp)) out->feedbackPass = atoi(fp.c_str());
```

- [ ] **Step 5: Run to verify it passes** — `./build-unittest/PPSSPPUnitTest SlangParserPhase2Keys` → exit 0. Also run `SlangParser` (Phase-1) to confirm no regression.

- [ ] **Step 6: Commit**

```bash
git add GPU/Common/Slang/SlangPreset.h GPU/Common/Slang/SlangpParser.cpp unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: parse per-pass srgb/float/mipmap/wrap/frame_count_mod and feedback_pass

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Parse LUT texture declarations (`textures` + per-LUT keys)

**Files:**
- Modify: `GPU/Common/Slang/SlangPreset.h`, `GPU/Common/Slang/SlangpParser.cpp`
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Produces (added to `SlangPreset.h`):
  ```cpp
  struct SlangLutDesc {
      std::string name;          // identifier used by shaders (e.g. "SamplerLUT1")
      std::string path;          // resolved absolute path to the PNG
      bool linear = false;       // <name>_linear
      bool mipmap = false;       // <name>_mipmap
      SlangWrapMode wrapMode = SlangWrapMode::ClampToBorder;  // <name>_wrap_mode
  };
  ```
  and on `SlangPreset`: `std::vector<SlangLutDesc> luts;`

- [ ] **Step 1: Write the failing test** — `TestSlangParserLuts()` + register:

```cpp
bool TestSlangParserLuts() {
	const std::string preset =
		"shaders = 1\n"
		"shader0 = a.slang\n"
		"textures = \"LUT1;LUT2\"\n"
		"LUT1 = ../luts/one.png\n"
		"LUT1_linear = true\n"
		"LUT1_mipmap = true\n"
		"LUT1_wrap_mode = repeat\n"
		"LUT2 = two.png\n";
	SlangPreset out; std::string err; Path base("/tmp/dir");
	EXPECT_TRUE(ParseSlangPreset(preset, base, &out, &err));
	EXPECT_EQ_INT((int)out.luts.size(), 2);
	std::string n0 = out.luts[0].name; std::string e0 = "LUT1"; EXPECT_EQ_STR(n0, e0);
	std::string p0 = out.luts[0].path; std::string ep0 = (base / "../luts/one.png").ToString(); EXPECT_EQ_STR(p0, ep0);
	EXPECT_TRUE(out.luts[0].linear);
	EXPECT_TRUE(out.luts[0].mipmap);
	EXPECT_TRUE(out.luts[0].wrapMode == SlangWrapMode::Repeat);
	std::string n1 = out.luts[1].name; std::string e1 = "LUT2"; EXPECT_EQ_STR(n1, e1);
	EXPECT_FALSE(out.luts[1].linear);
	return true;
}
```

- [ ] **Step 2: Run to verify it fails** — compile error / assert fail.

- [ ] **Step 3: Add `SlangLutDesc` + `luts`** to `SlangPreset.h`.

- [ ] **Step 4: Implement** in `SlangpParser.cpp`, after the pass loop. Split the `textures` value on `;` (reuse a small split; `SplitString` exists in `Common/StringUtils.h` — verify signature, else split inline):
```cpp
std::string texList;
if (getStr("textures", &texList)) {
	std::vector<std::string> names;
	// split on ';', trim each
	size_t start = 0;
	while (start <= texList.size()) {
		size_t sep = texList.find(';', start);
		std::string nm = StripSpaces(texList.substr(start, sep == std::string::npos ? std::string::npos : sep - start));
		if (!nm.empty()) {
			SlangLutDesc lut;
			lut.name = nm;
			std::string p;
			if (getStr(nm, &p)) lut.path = (basePath / p).ToString();
			std::string t;
			if (getStr(nm + "_linear", &t)) lut.linear = (t == "true" || t == "1");
			if (getStr(nm + "_mipmap", &t)) lut.mipmap = (t == "true" || t == "1");
			if (getStr(nm + "_wrap_mode", &t)) lut.wrapMode = ParseWrapMode(t);
			out->luts.push_back(lut);
		}
		if (sep == std::string::npos) break;
		start = sep + 1;
	}
}
```

- [ ] **Step 5: Run to verify it passes** — `SlangParserLuts` exit 0; `SlangParser`/`SlangParserPhase2Keys` still green.

- [ ] **Step 6: Commit**

```bash
git add GPU/Common/Slang/SlangPreset.h GPU/Common/Slang/SlangpParser.cpp unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: parse LUT texture declarations from .slangp

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Capture `#pragma format` from `.slang` source

**Files:**
- Modify: `GPU/Common/Slang/SlangpParser.h`, `GPU/Common/Slang/SlangpParser.cpp`
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Produces: add `SlangFbFormat format = SlangFbFormat::Default;` to `struct SlangSource` (in `SlangpParser.h`; requires `#include "GPU/Common/Slang/SlangPreset.h"` there, which it already has transitively via the parser). `SplitSlangSource` sets it from a `#pragma format <FMT>` line. Map: `R16G16B16A16_SFLOAT`/`R32G32B32A32_SFLOAT`/any `*_SFLOAT` → `Float`; `*_SRGB` → `Srgb`; everything else (incl. `R8G8B8A8_UNORM`) → `Default`.

- [ ] **Step 1: Write the failing test** — `TestSlangFormatPragma()` + register:

```cpp
bool TestSlangFormatPragma() {
	const std::string src =
		"#version 450\n"
		"#pragma format R16G16B16A16_SFLOAT\n"
		"#pragma stage vertex\n"
		"void main(){ gl_Position = vec4(0.0); }\n"
		"#pragma stage fragment\n"
		"layout(location=0) out vec4 FragColor;\n"
		"void main(){ FragColor = vec4(1.0); }\n";
	SlangSource out; std::string err;
	EXPECT_TRUE(SplitSlangSource(src, &out, &err));
	EXPECT_TRUE(out.format == SlangFbFormat::Float);
	// #pragma format line must NOT leak into emitted GLSL:
	EXPECT_TRUE(out.fragment.find("#pragma format") == std::string::npos);
	return true;
}
```

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Add the field** to `SlangSource` and implement in the existing `#pragma format` branch of `SplitSlangSource` (it currently just `continue`s). Replace:
```cpp
} else if (startsWith(rest, "format")) {
	continue;  // consumed; Phase 1 uses default RT format
}
```
with:
```cpp
} else if (startsWith(rest, "format")) {
	std::string fmt = std::string(StripSpaces(rest.substr(strlen("format"))));
	if (fmt.find("_SFLOAT") != std::string::npos) out->format = SlangFbFormat::Float;
	else if (fmt.find("_SRGB") != std::string::npos) out->format = SlangFbFormat::Srgb;
	else out->format = SlangFbFormat::Default;
	continue;
}
```

- [ ] **Step 4: Run to verify it passes** — `SlangFormatPragma` exit 0; `SlangSplit` (Phase 1) still green.

- [ ] **Step 5: Commit**

```bash
git add GPU/Common/Slang/SlangpParser.h GPU/Common/Slang/SlangpParser.cpp unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: capture #pragma format (sRGB/float) from .slang source

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Expand reflection semantics — PassOutput/Feedback/History/LUT/alias (with indices)

**Files:**
- Modify: `GPU/Common/Slang/SlangReflection.h`, `GPU/Common/Slang/SlangReflection.cpp`
- Modify: `GPU/Common/Slang/SlangPassCompiler.cpp` (pass classification context through)
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Produces (added to `SlangReflection.h`):
  - New `SlangSemantic` values: `TexPassOutput, TexPassFeedback, TexOriginalHistory, TexLut` and their size companions `PassOutputSize, PassFeedbackSize, OriginalHistorySize, LutSize`.
  - Add `int index = -1;` to `SlangUniformMember` and `SlangTextureBinding` (the `#` in `PassOutput3`, `OriginalHistory2`, etc; or the LUT/alias list position; -1 when not indexed).
  - New classification signature carrying context:
    ```cpp
    struct SlangClassifyContext {
        std::vector<std::string> paramNames;    // #pragma parameter names
        std::vector<std::string> aliasNames;    // pass #pragma name / aliasN, in pass order
        std::vector<std::string> lutNames;      // preset LUT identifiers
    };
    SlangSemantic ClassifyUniform(const std::string &name, const SlangClassifyContext &ctx, int *outIndex);
    SlangSemantic ClassifyTexture(const std::string &name, const SlangClassifyContext &ctx, int *outIndex);
    ```
    Keep the old 2-arg `ClassifyUniform`/`ClassifyTexture` as thin wrappers (empty context) so Phase-1 tests still compile, OR update those tests — prefer updating the tests to the new signature to avoid dead overloads.
- Classification rules (name → semantic, set `*outIndex`):
  - Textures: `Source`→TexSource; `Original`→TexOriginal; `OriginalHistoryN`→TexOriginalHistory idx N (`OriginalHistory0`==Original is allowed, idx 0); `PassOutputN`→TexPassOutput idx N; `PassFeedbackN`→TexPassFeedback idx N; an alias name in `ctx.aliasNames`→TexPassOutput with idx = that alias's pass index; `<alias>Feedback`→TexPassFeedback idx = alias pass index; a LUT name in `ctx.lutNames`→TexLut idx = LUT position. Else Unknown.
  - Uniforms: existing built-ins unchanged; additionally `PassOutputSizeN`/`PassFeedbackSizeN`/`OriginalHistorySizeN`→ respective size semantic + idx; `<X>Size` where `<X>` is an alias→PassOutputSize idx=alias pass; `<lut>Size`→LutSize idx=lut. A name in `ctx.paramNames`→UserParameter. Else Unknown (loud-fail preserved).

- [ ] **Step 1: Write the failing test** — `TestSlangSemanticsPhase2()` + register. Cover: `PassOutput0`→(TexPassOutput,0); `PassFeedback2`→(TexPassFeedback,2); `OriginalHistory1`→(TexOriginalHistory,1); a LUT name from ctx→(TexLut, its index); an alias name→(TexPassOutput, alias pass index); `<alias>Feedback`→(TexPassFeedback, that index); `PassOutputSize0`→(PassOutputSize,0); an unknown name→Unknown. Example core:

```cpp
bool TestSlangSemanticsPhase2() {
	SlangClassifyContext ctx;
	ctx.paramNames = {"Bright"};
	ctx.aliasNames = {"FirstPass", "SecondPass"};  // pass 0, pass 1
	ctx.lutNames   = {"MaskTex"};
	int idx = -99;
	EXPECT_TRUE(ClassifyTexture("Source", ctx, &idx) == SlangSemantic::TexSource);
	EXPECT_TRUE(ClassifyTexture("PassOutput0", ctx, &idx) == SlangSemantic::TexPassOutput); EXPECT_EQ_INT(idx, 0);
	EXPECT_TRUE(ClassifyTexture("PassFeedback2", ctx, &idx) == SlangSemantic::TexPassFeedback); EXPECT_EQ_INT(idx, 2);
	EXPECT_TRUE(ClassifyTexture("OriginalHistory1", ctx, &idx) == SlangSemantic::TexOriginalHistory); EXPECT_EQ_INT(idx, 1);
	EXPECT_TRUE(ClassifyTexture("MaskTex", ctx, &idx) == SlangSemantic::TexLut); EXPECT_EQ_INT(idx, 0);
	EXPECT_TRUE(ClassifyTexture("SecondPass", ctx, &idx) == SlangSemantic::TexPassOutput); EXPECT_EQ_INT(idx, 1);
	EXPECT_TRUE(ClassifyTexture("FirstPassFeedback", ctx, &idx) == SlangSemantic::TexPassFeedback); EXPECT_EQ_INT(idx, 0);
	EXPECT_TRUE(ClassifyTexture("Nonsense", ctx, &idx) == SlangSemantic::Unknown);
	EXPECT_TRUE(ClassifyUniform("PassOutputSize0", ctx, &idx) == SlangSemantic::PassOutputSize); EXPECT_EQ_INT(idx, 0);
	EXPECT_TRUE(ClassifyUniform("Bright", ctx, &idx) == SlangSemantic::UserParameter);
	return true;
}
```

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement** the new enum values, struct `index` fields, `SlangClassifyContext`, and the two classification functions in `SlangReflection.{h,cpp}`. Use a small helper to parse a trailing integer suffix (`"PassOutput12"` → base `"PassOutput"`, index 12). Match longest/most-specific first (e.g. check `OriginalHistory` before `Original`; check `<alias>Feedback` before `<alias>`). Update `ReflectSlangSource` (in `SlangPassCompiler.cpp`) to build a `SlangClassifyContext` from the arguments it now needs — **change `ReflectSlangSource`'s signature** to accept the context:
  ```cpp
  bool ReflectSlangSource(const SlangSource &src, const SlangClassifyContext &ctx, PassReflection *out, std::string *error);
  ```
  and set `m.index` / `t.index` from the classify `outIndex`. Update `CompileSlangPass` to accept and forward the context too:
  ```cpp
  bool CompileSlangPass(Draw::DrawContext *draw, const SlangSource &src, const SlangClassifyContext &ctx, SlangCompiledPass *out, std::string *error);
  ```
  Update the Phase-1 `TestSlangReflection` call site accordingly (pass a context whose `paramNames` come from `src.params`, empty alias/lut lists).

- [ ] **Step 4: Run to verify it passes** — `SlangSemanticsPhase2` + `SlangReflection` (updated) + all prior slang tests exit 0.

- [ ] **Step 5: Commit**

```bash
git add GPU/Common/Slang/SlangReflection.h GPU/Common/Slang/SlangReflection.cpp GPU/Common/Slang/SlangPassCompiler.cpp GPU/Common/Slang/SlangPassCompiler.h unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: classify PassOutput/Feedback/History/LUT/alias semantics with indices

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Load LUT PNGs into textures at chain build time

**Files:**
- Modify: `GPU/Common/Slang/SlangFilterChain.h`, `GPU/Common/Slang/SlangFilterChain.cpp`

**Interfaces:**
- Consumes: `preset_.luts` (Task 2), `pngLoad`/`pngLoadPtr` from `Common/Data/Format/PngLoad.h`, VFS `g_VFS.ReadFile`, `draw_->CreateTexture`.
- Produces: a member `std::vector<Draw::Texture *> lutTextures_;` (index-aligned to `preset_.luts`) and per-LUT sampler states, created in `Load()` and released in `ReleaseResources()`.

This task has no pure unit test (needs a device); verification is that it compiles into both builds and that a LUT fixture renders in Task 9. Keep the logic self-contained and reviewed by inspection.

- [ ] **Step 1: Add members + release** — in `SlangFilterChain.h` add `std::vector<Draw::Texture *> lutTextures_;` and `std::vector<Draw::SamplerState *> lutSamplers_;`. In `ReleaseResources()` add `DoReleaseVector(lutTextures_);` and `DoReleaseVector(lutSamplers_);` (both already null-guarded per the Phase-1 fix).

- [ ] **Step 2: Load LUTs in `Load()`** — after preset parse, before/after pass compile (LUTs don't depend on passes). For each `SlangLutDesc`:
  - Read bytes via `g_VFS.ReadFile(lut.path.c_str(), &sz)` (LUT paths resolve through the same VFS as shaders).
  - Decode with `pngLoadPtr((const unsigned char*)data, sz, &w, &h, &pixels)` (returns RGBA8; check the return code and free `pixels` with `free()` per PngLoad.h contract — verify the exact free function it documents).
  - Create a `Draw::TextureDesc{ TextureType::LINEAR2D, DataFormat::R8G8B8A8_UNORM, w, h, 1, lut.mipmap ? <mipLevels> : 1, lut.mipmap, TextureSwizzle::DEFAULT, "slang-lut", { pixels } }` and `draw_->CreateTexture(desc)`. For mipmaps set `generateMips = true` and `mipLevels` to the full chain count (`floor(log2(max(w,h)))+1`).
  - Create a sampler from `lut.linear`/`lut.wrapMode` (map `SlangWrapMode`→`Draw::TextureAddressMode`: ClampToBorder→CLAMP_TO_BORDER, ClampToEdge→CLAMP_TO_EDGE, Repeat→REPEAT, MirroredRepeat→REPEAT_MIRROR).
  - On any failure (missing file, decode error): `*error = "failed to load LUT: " + lut.name; ReleaseResources(); return false;` (loud-fail).

- [ ] **Step 3: Build both targets** — desktop unittest (`cmake --build`… → no errors; run all slang tests, exit 0) AND Android arm64 APK (BUILD SUCCESSFUL). No behavior change yet for presets without LUTs.

- [ ] **Step 4: Commit**

```bash
git add GPU/Common/Slang/SlangFilterChain.h GPU/Common/Slang/SlangFilterChain.cpp
git commit -m "slang: load PNG LUT textures at chain build time

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Keyed texture registry + multi-input binding in Run (PassOutput / History / LUT)

**Files:**
- Modify: `GPU/Common/Slang/SlangFilterChain.h`, `GPU/Common/Slang/SlangFilterChain.cpp`

**Interfaces:**
- Consumes: expanded `PassReflection` texture semantics + indices (Task 4), `lutTextures_` (Task 5).
- Produces: generalized per-pass texture binding in `Run` that resolves each `SlangTextureBinding` by (semantic, index) to a concrete `Draw::Framebuffer*`/`Draw::Texture*`, plus an `OriginalHistory` ring. Keeps the Phase-1 slot mapping (`slot = shader binding - 1`) and per-pass draw order.

No pure unit test (device path); verified in Task 9. The resolution *logic* (which source maps to which semantic/index) is small and pure — factor it into a helper that Task 9's fixtures exercise on-device; optionally add a tiny host-side unit test for the history-ring index math.

- [ ] **Step 1: History ring** — add members: `std::vector<Draw::Framebuffer *> historyRing_;` and `int historyDepth_ = 0;`. In `Load()`, after reflecting all passes, compute `historyDepth_ = max OriginalHistoryN index referenced across all passes` (0 if none). Allocate `historyDepth_ + 1` history framebuffers sized to the source (created lazily in `Run` once source size is known, like `passFramebuffers_`). Document: `OriginalHistory0` is the current frame's Original (== `source`), so only indices ≥1 need retained copies.

- [ ] **Step 2: Per-frame history advance** — at the END of `Run`, if `historyDepth_ > 0`: copy `source` into the ring's newest slot and rotate (ring of `historyDepth_` retained frames). Use `draw_->BlitFramebuffer` or a copy pass (check thin3d for `CopyFramebufferImage`/`BlitFramebuffer` — mirror how `FramebufferManagerCommon` copies FBOs). If no cheap copy exists, render a passthrough blit into the history slot.

- [ ] **Step 3: Generalized binding** — replace the Phase-1 texture loop body. For each `SlangTextureBinding tex` with `slot = tex.binding - 1` (skip if <0):
  ```
  switch (tex.semantic):
    TexSource            -> (i==0) ? source : passFramebuffers_[i-1]
    TexOriginal          -> source
    TexOriginalHistory   -> tex.index==0 ? source : historyRing_[<resolve idx-1 in ring>]
    TexPassOutput        -> passFramebuffers_[tex.index]   // causal: tex.index < i (enforce; else loud error once)
    TexPassFeedback      -> feedbackBuffers_[tex.index]    // previous frame (Task 7)
    TexLut               -> bind lutTextures_[tex.index] via BindTexture (not BindFramebufferAsTexture)
  ```
  For framebuffer sources use `BindFramebufferAsTexture(fb, slot, COLOR_BIT, 0)`; for LUTs use `draw_->BindTexture(slot, lutTextures_[tex.index])`. Bind the matching sampler (LUT uses its own `lutSamplers_[tex.index]`; framebuffer inputs use the pass's linear/nearest per `filterLinear`, or a wrap-aware sampler from Task for wrap modes — see Task below).
- Also fill the new size-uniform semantics in the UBO scratch loop: `PassOutputSize`/`OriginalHistorySize`/`PassFeedbackSize`/`LutSize` → `(w,h,1/w,1/h)` of the resolved texture (query dims via `GetFramebufferDimensions` or the LUT's stored w/h). Reuse the Phase-1 bounds-clamp + min-1 divisor guards.

- [ ] **Step 4: Causality guard** — if a pass references `PassOutput`/alias with `index >= currentPassIndex`, that's a malformed preset: log once and treat as unavailable (bind a safe fallback / fail the chain). Match the slang spec (reading a not-yet-produced pass is an error).

- [ ] **Step 5: Build both targets + run all slang unit tests** (exit 0). Commit.

```bash
git add GPU/Common/Slang/SlangFilterChain.h GPU/Common/Slang/SlangFilterChain.cpp
git commit -m "slang: multi-input binding (PassOutput/History/LUT) + history ring in Run

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: Pass feedback (one-frame) + frame_count_mod

**Files:**
- Modify: `GPU/Common/Slang/SlangFilterChain.h`, `GPU/Common/Slang/SlangFilterChain.cpp`

**Interfaces:**
- Consumes: `TexPassFeedback` semantic (Task 4), `preset_.feedbackPass` + per-pass `frameCountMod` (Task 1), the pass output pool (Task 6).
- Produces: for any pass whose output is referenced via `PassFeedback#`/`<alias>Feedback` (or the global `feedback_pass`), a dedicated previous-frame buffer that is ping-ponged with the live output each frame; and `FrameCount` uniform wrapped by the pass's `frameCountMod`.

- [ ] **Step 1: Feedback buffers** — add `std::vector<Draw::Framebuffer *> feedbackBuffers_;` (index-aligned to passes; only allocated for passes actually referenced as feedback — compute a `std::vector<bool> passHasFeedback_` in `Load` from the union of all passes' `TexPassFeedback` indices and `preset_.feedbackPass`). Release in `ReleaseResources`.

- [ ] **Step 2: Ping-pong** — for a feedback pass, maintain two framebuffers (live + previous). Each frame: bind the *previous* buffer where `TexPassFeedback[idx]` is sampled; after the pass renders into the *live* buffer, swap live/previous so next frame reads this frame's output. First frame (no history) binds a cleared/black buffer. Model the swap on `PresentationCommon`'s `previousFramebuffers_` + `previousIndex_` ring (`GPU/Common/PresentationCommon.cpp`).

- [ ] **Step 3: frame_count_mod** — where the `FrameCount` uniform is written in the UBO scratch loop, if `passDesc.frameCountMod > 0` write `frameCount % passDesc.frameCountMod` instead of the raw count.

- [ ] **Step 4: Build both targets + all slang tests exit 0. Commit.**

```bash
git add GPU/Common/Slang/SlangFilterChain.h GPU/Common/Slang/SlangFilterChain.cpp
git commit -m "slang: one-frame pass feedback buffers + frame_count_mod

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 8: sRGB / float intermediate framebuffers (thin3d core extension) + wrap-mode/mipmap wiring

**Files:**
- Modify: `Common/GPU/thin3d.h` (FramebufferDesc), `Common/GPU/Vulkan/thin3d_vulkan.cpp`, `Common/GPU/Vulkan/VulkanFramebuffer.h`, `Common/GPU/Vulkan/VulkanFramebuffer.cpp`
- Modify: `GPU/Common/Slang/SlangFilterChain.cpp` (request the format; per-pass wrap samplers; mipmap_input)

**HIGH RISK — this is the one task that changes shared thin3d core.** PPSSPP's Vulkan framebuffers are currently hardcoded to `VK_FORMAT_R8G8B8A8_UNORM` (`VulkanFramebuffer.cpp:64,319,343`). The change is additive and defaulted, but touches a widely-used struct. If threading the format proves too invasive within a reasonable diff, use the **documented fallback** (Step 5) and record the limitation — do NOT block the rest of Phase 2 on it.

**Interfaces:**
- Produces: `DataFormat colorFormat = DataFormat::R8G8B8A8_UNORM;` on `Draw::FramebufferDesc`; the Vulkan backend honors `R8G8B8A8_UNORM` (default), `R8G8B8A8_UNORM_SRGB`, and `R16G16B16A16_FLOAT` for the framebuffer's color attachment. All existing callers keep the default and are unaffected.

- [ ] **Step 1: Add the field (defaulted)** — `Common/GPU/thin3d.h` `struct FramebufferDesc`: add `DataFormat colorFormat = DataFormat::R8G8B8A8_UNORM;`. This alone must compile with zero behavior change (every existing brace-init that omits it still works because it's the last field with a default — VERIFY existing aggregate initializers of FramebufferDesc still compile; if any use positional init that would break, convert this to a defaulted trailing field carefully or add a setter).

- [ ] **Step 2: Thread into VulkanFramebuffer** — `VKRFramebuffer` constructor currently hardcodes `VK_FORMAT_R8G8B8A8_UNORM`. Add a `VkFormat colorFormat` parameter (default `VK_FORMAT_R8G8B8A8_UNORM`), plumb it from `VKContext::CreateFramebuffer` (which reads `desc.colorFormat`, converts via the existing `DataFormatToVulkan`), through to the `CreateImage(...)` calls (lines ~64, 73) and the render-pass attachment format (lines ~319, 343 — the non-backbuffer branch). The MSAA color image must use the same format. Confirm the render-pass compatibility key (RPKey / renderpass cache) incorporates the format so two framebuffers of different formats don't collide in the cache.

- [ ] **Step 3: Map slang format → DataFormat in SlangFilterChain** — when creating each pass framebuffer, choose the color format from the pass: `srgbFramebuffer || formatOverride==Srgb` → `R8G8B8A8_UNORM_SRGB`; `floatFramebuffer || formatOverride==Float` → `R16G16B16A16_FLOAT`; else default. Set `FramebufferDesc.colorFormat` accordingly.

- [ ] **Step 4: Capability check + graceful fallback** — before using a non-default format, query `draw_->GetDataFormatSupport(fmt)` (or the nearest thin3d capability query — verify the method name in thin3d.h) for render-target support. If unsupported on the device, fall back to `R8G8B8A8_UNORM`, and `WARN_LOG` once per preset (e.g. "slang: float framebuffer unsupported, falling back to unorm — colors may band"). Never hard-fail on format alone.

- [ ] **Step 5: DOCUMENTED FALLBACK (if Step 2 is too invasive)** — if threading the format through VulkanFramebuffer's render-pass cache balloons the diff or risks regressions, STOP the core change, revert Steps 1–2, keep all framebuffers `R8G8B8A8_UNORM`, and `WARN_LOG` once when a preset requests sRGB/float. Record in the plan/PR that sRGB/float intermediates are deferred (crt-royale will render with slightly wrong gamma but still runs). This keeps Phase 2's other features shippable. **Escalate this decision to the human** (it changes the conformance claim) rather than silently choosing.

- [ ] **Step 6: Per-pass wrap-mode samplers + mipmap_input** — in `SlangFilterChain`, replace the two fixed `samplerLinear_/Nearest_` with samplers built per pass from (`filterLinear`, `wrapMode`): map `SlangWrapMode`→`Draw::TextureAddressMode` (as in Task 5). For `mipmapInput` passes, the *input* framebuffer must have mipmaps generated before sampling — check whether thin3d framebuffers can be sampled with mips (they likely can't without extra work); if not supported cheaply, `WARN_LOG` and sample base level (documented limitation, acceptable — very few shaders use `mipmap_input` on non-LUT inputs). LUT mipmaps (Task 5) are the common case and already handled via `TextureDesc.generateMips`.

- [ ] **Step 7: Verify** — desktop unittest build clean + all slang tests exit 0 + **full suite `PPSSPPUnitTest all` exit 0 (thin3d change touches shared code — run everything)**; Android arm64 APK BUILD SUCCESSFUL. On-device smoke: the Phase-1 `stock.slangp` still renders identically (default format path unchanged).

- [ ] **Step 8: Commit** (one commit for the thin3d extension + slang wiring, or split core vs slang into two):

```bash
git add Common/GPU/thin3d.h Common/GPU/Vulkan/thin3d_vulkan.cpp Common/GPU/Vulkan/VulkanFramebuffer.h Common/GPU/Vulkan/VulkanFramebuffer.cpp GPU/Common/Slang/SlangFilterChain.cpp
git commit -m "thin3d+slang: optional sRGB/float framebuffer color format, per-pass wrap/mipmap

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 9: On-device conformance — multi-pass fixtures + crt-royale

**Files:**
- Create: `assets/shaders/slang_test/twopass.slangp` + `twopass_a.slang` + `twopass_b.slang`
- Create: `assets/shaders/slang_test/feedback.slangp` + `feedback.slang`
- Create: `assets/shaders/slang_test/lut.slangp` + `lut.slang` + a small `lut.png`
- No production code changes (verification task).

**Interfaces:** none produced; consumes the whole Phase-2 stack.

- [ ] **Step 1: Two-pass fixture** — `twopass_a.slang` writes a recognizable transform (e.g. horizontal 2px blur) into an aliased pass output; `twopass_b.slang` samples both `Source` (pass 0 output) and `OriginalHistory1`, blending — exercises PassOutput chaining + history ring. `twopass.slangp`: `shaders=2`, pass0 `alias=FirstPass scale_type=source scale=1.0`, pass1 `scale_type=viewport scale=1.0`.

- [ ] **Step 2: Feedback fixture** — `feedback.slang` samples `Source` and its own `PassFeedback0` (or `<alias>Feedback`), blending ~0.9 previous + 0.1 current (motion-persistence). `feedback.slangp`: `shaders=1`, `feedback_pass=0`. Visual proof = trailing/ghosting when the game image moves.

- [ ] **Step 3: LUT fixture** — a tiny (e.g. 16×1) `lut.png` gradient; `lut.slang` samples the LUT by its declared name and uses it as a color map; `lut.slangp` declares `textures = "PaletteLUT"` + `PaletteLUT = lut.png`. Visual proof = the game recolored by the palette.

- [ ] **Step 4: Build + deploy** — rebuild the arm64 APK, `adb install -t -r`. For each fixture, set `SlangShaderPreset` in the device `ppsspp.ini` (`/storage/9C33-6BBD/ROMs/psp/PSP/SYSTEM/ppsspp.ini`, `[Graphics]` `SlangShaderPreset = shaders/slang_test/<name>.slangp`) via adb push, force-stop, relaunch with the Lunar ISO VIEW intent. Confirm on device (user or, where the effect is on the game layer, via described expectation): two-pass blends, feedback ghosts, LUT recolors — each without crash or Vulkan validation errors (`adb logcat` clean of SIGSEGV/VALIDATION).

- [ ] **Step 5: crt-royale conformance** — side-load crt-royale from the libretro slang-shaders repo into the device shader dir (or `assets/shaders/`), point `SlangShaderPreset` at `crt-royale.slangp`, boot a game. Expected: the full 12-pass CRT effect renders (scanlines, phosphor mask, curvature) without crash or validation errors. This is the Phase-2 gate. If sRGB was deferred (Task 8 Step 5), note the expected gamma difference. Capture `adb logcat` to confirm no errors and, if possible, a photo/description of the CRT output.

- [ ] **Step 6: Full regression** — `./build-unittest/PPSSPPUnitTest all` exit 0; Phase-1 `stock.slangp` still renders identically on device.

- [ ] **Step 7: Commit fixtures**

```bash
git add assets/shaders/slang_test/twopass* assets/shaders/slang_test/feedback* assets/shaders/slang_test/lut*
git commit -m "slang: add Phase 2 multi-pass/feedback/LUT on-device test fixtures

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase 2 Done Criteria

- All new unit tests pass (`SlangParserPhase2Keys`, `SlangParserLuts`, `SlangFormatPragma`, `SlangSemanticsPhase2`) plus the full suite (`PPSSPPUnitTest all`) green — no regression to Phase 1's tests or any other subsystem (the thin3d change is the reason the full suite must be run).
- Multi-pass presets using `PassOutput#`/aliases, `OriginalHistory#`, `PassFeedback#`, and LUTs render correctly on the Vulkan device with no validation errors.
- sRGB/float intermediate framebuffers work (or are explicitly, loudly deferred per Task 8 Step 5 with human sign-off).
- **crt-royale renders on-device** (the conformance gate), gamma caveat noted only if sRGB was deferred.
- Unknown/unsupported semantics still loud-fail; Phase-1 single-pass path unchanged; feature remains inert when `sSlangShaderPreset` is empty.

## What Phase 2 still excludes (later phases)

- Download/unpack/import from the libretro buildbot (Phase 3).
- `SlangPresetLibrary` + category-browsing UI + parameter sliders (Phase 4) — Phase 2 presets are still set via config string.
- D3D11 / OpenGL / GLES backends (Phase 5) — `CompileSlangPass` still guards Vulkan-only.
- `mipmap_input` on non-LUT framebuffer inputs if thin3d can't sample FBO mips cheaply (documented limitation from Task 8 Step 6).

---

### Task 9b: `#include` resolution + custom-shader-dir loading (unblocks crt-royale)

**Added after Task 9 on-device testing revealed crt-royale needs two capabilities the original task set omitted:** (1) `.slang` sources use `#include` (crt-royale: 17 include files, nested); Phase 1/2 never resolved them. (2) `SlangFilterChain::Load` reads only via `g_VFS.ReadFile` (bundled assets), so a preset in the runtime custom-shader dir (`GetSysDirectory(DIRECTORY_CUSTOM_SHADERS)` = `PSP/shaders/`) isn't found. Both are required for real user shader packs and for the crt-royale conformance gate.

**Files:** `GPU/Common/Slang/SlangpParser.{h,cpp}` (include resolution), `GPU/Common/Slang/SlangFilterChain.cpp` (custom-dir read), `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`.

**Interfaces:**
- Produces a recursive include resolver. Change `SplitSlangSource` (or add a pre-pass `ResolveSlangIncludes(const std::string &src, const Path &sourceDir, std::function<bool(const Path&, std::string*)> readFile, std::string *out, std::string *error)`) that, BEFORE stage-splitting, replaces `#include "rel/path"` lines with the referenced file's contents, resolving the path relative to the including file's directory, recursively (with a depth guard against cycles, e.g. max 32). Also handle `#pragma include_optional "path"` (skip silently if missing). A read-callback is injected so the resolver works with both VFS and real-file reads and stays unit-testable (tests pass a fake reader).

**Part A — include resolution (unit-testable):**
- [ ] **Step 1: failing test** `TestSlangIncludes()` in TestSlangParser.cpp: feed a source with `#include "common.inc"` and a fake reader mapping `common.inc`→`"float helper() { return 1.0; }"`; assert the resolved output contains `helper()` and no `#include` line; test nested include (a.inc includes b.inc); test `#pragma include_optional "missing.inc"` resolves to empty without error; test a cycle (a→a) fails with a clear error or is depth-guarded. Register `TEST_ITEM(SlangIncludes)`.
- [ ] **Step 2:** implement `ResolveSlangIncludes` with the injected reader + depth guard; call it at the top of `SplitSlangSource` (or in `Load` before splitting) using the source file's directory as the base. Paths resolve relative to the *including* file (so nested includes work). Strip the `#include`/`#pragma include`/`#pragma include_optional` lines, substituting content.
- [ ] **Step 3:** green + no regression (`PPSSPPUnitTest all`).

**Part B — custom-shader-dir loading (device code):**
- [ ] **Step 4:** in `SlangFilterChain::Load`, and for each `.slang`/LUT read, when `g_VFS.ReadFile(path)` returns null, fall back to a real-filesystem read via `File::ReadFileToString`/`File::ReadFile` on the same path (mirror how `PostShader.cpp` tries `LoadFromVFS` then `ini.Load(fullName)`). This lets presets under `GetSysDirectory(DIRECTORY_CUSTOM_SHADERS)` load. Preset/shader/LUT/include paths all resolve against the preset's own directory (absolute once resolved), so a preset referencing `shaders/crt-royale/src/foo.slang` works whether it's in assets or the custom dir.
- [ ] **Step 5:** the include resolver's reader callback must use the SAME VFS-then-realfile fallback so includes in a custom-dir shader resolve.
- [ ] **Step 6:** build both targets; `PPSSPPUnitTest all` green.

**Part C — crt-royale conformance gate (on-device, controller-driven):**
- [ ] **Step 7:** push the full crt-royale tree (preset + `shaders/crt-royale/**` incl. LUT PNGs + include headers) to `PSP/shaders/` on the device; set `SlangShaderPreset` to its custom-dir path; boot a game. Expected: 12-pass CRT effect (scanlines, phosphor mask, curvature) renders without crash/validation errors. Capture logcat clean + user/photo visual confirmation. If glslang reports errors from crt-royale's GLSL that stem from unsupported constructs (e.g. it relies on `#pragma parameter` in includes, or `user-preset-constants.h` overrides), document precisely which and treat genuine spec-conformance-vs-PPSSPP-toolchain gaps as findings for a later phase — the gate is "renders correctly," but a specific documented incompatibility in one upstream shader is acceptable to note rather than block Phase 2 if all the generic mechanisms work.

**Done when:** include + custom-dir unit/build green AND crt-royale renders on-device (or its specific remaining incompatibility is precisely documented).

---

### Task 9c: push_constant support (final crt-royale unblocker)

**Discovered during Task 9b crt-royale testing:** after custom-dir loading + #include resolution work, crt-royale (and even crt-lottes) fail reflection with "push_constant blocks not supported" — real slang shaders put their `*Size`/params/FrameCount in a `layout(push_constant) uniform Push {...} params;` block and MVP in a separate std140 UBO (`global`). PPSSPP's thin3d exposes only ONE dynamic UBO (descriptor set 0, binding 0) via `UpdateDynamicUniformBuffer` — no push-constant API.

**Approach — source transform (before glslang) merging push_constant into the single thin3d UBO.** Rather than adding a push-constant path to thin3d (huge shared-code change), rewrite the slang GLSL so both blocks become one std140 UBO at set0/binding0:
- Reflect BOTH `res.uniform_buffers` and `res.push_constant_buffers`; combine their members into `PassReflection.uboMembers` with correct std140 offsets within a single merged block.
- Transform the source: convert the `layout(push_constant) uniform Push {...} params;` block into a member group of one UBO, and if a separate `layout(std140,...) uniform UBO {...} global;` exists, merge both member lists into a single `layout(set=0,binding=0,std140) uniform _MergedUBO { <MVP + all push members> } <?>;`. The catch: members are accessed via instance names (`global.MVP`, `params.SourceSize`). Simplest robust transform: keep BOTH instance names but back them with the same binding-0 block is NOT valid GLSL (two blocks can't share a binding). Instead, pick ONE of these proven strategies and implement it:
  - **Strategy A (rename-and-single-block):** emit one `layout(set=0,binding=0,std140) uniform CombinedUBO { mat4 MVP; vec4 SourceSize; ... } _ubo;` and inject `#define global _ubo` and `#define params _ubo` so all `global.X`/`params.X` accesses resolve to the one block. Requires the transform to (1) find both blocks, (2) collect their members in a stable order (MVP/global members first, then push members) matching the offsets you report in `uboMembers`, (3) delete the original block declarations, (4) emit the combined block + the two `#define`s. Verify std140 offsets you compute match what SPIRV-Cross reports for the combined block (reflect the TRANSFORMED source to get authoritative offsets — do NOT hand-compute).
  - The reliable implementation: do the textual merge, then compile the transformed source, then reflect the transformed SPIR-V (which now has a single UBO) with the EXISTING Task-4 UBO reflection path — so offsets/semantics come straight from SPIRV-Cross on the merged block. This reuses all existing binding logic; `Run()` needs no change.
- Remove the two push_constant loud-fail early-returns in `ReflectSlangSource` once the transform handles them.

**Files:** `GPU/Common/Slang/SlangPassCompiler.cpp` (transform + reflect merged block), maybe `SlangpParser.cpp` if the transform lives with source prep. Add unit coverage.

- [ ] **Step 1: failing unit test** `TestSlangPushConstant()`: feed a source with a std140 `global{mat4 MVP;}` UBO AND a `push_constant Push{vec4 SourceSize; float ColorMod;} params;`, run the transform+reflect, assert the resulting `PassReflection` has MVP, SourceSize, ColorMod as UBO members with sane offsets and classified semantics, uboBinding==0, and NO failure. Also assert a push_constant-only shader (no separate UBO) works. Register `TEST_ITEM(SlangPushConstant)`.
- [ ] **Step 2:** implement the transform + merged-block reflection; remove loud-fails. Green + `PPSSPPUnitTest all`.
- [ ] **Step 3 (device, controller-driven):** re-run crt-royale on-device. Expected: 12-pass CRT renders. Document any remaining per-shader incompatibility precisely.

**Done when:** push_constant unit test green, both builds clean, and crt-royale renders on-device (or a precise remaining incompatibility is documented).
