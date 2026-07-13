# Slang Shader Support — Phase 1 (Vulkan Rendering Core) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the foundational Vulkan-only slang rendering core: parse a `.slangp` preset, compile its `.slang` passes through the already-vendored glslang + SPIRV-Cross toolchain with reflection-driven uniform binding, and run a linear multi-pass full-screen filter chain, wired into PPSSPP's existing present path so a manually-placed preset renders on Vulkan.

**Architecture:** A new parallel subsystem in `GPU/Common/Slang/` (`SlangpParser`, `SlangPassCompiler`, `SlangFilterChain`) that emits `Draw::Pipeline`s and executes them into intermediate `Draw::Framebuffer`s, producing a final texture that `FramebufferManagerCommon` blits to the backbuffer via the existing output-rect math. Phase 1 deliberately excludes download/import, category UI, history/feedback/LUT textures, sRGB/float framebuffers, and non-Vulkan backends — those are later phases. Phase 1 supports: N passes, `Source`/`Original` textures, `#pragma parameter` user floats, built-in size/frame semantics, and source/viewport/absolute scaling.

**Tech Stack:** C++17, PPSSPP `thin3d` (Draw::) GPU abstraction, vendored `ext/glslang` (GLSL→SPIR-V) and `ext/SPIRV-Cross` (SPIR-V reflection + cross-compile), PPSSPP `IniFile` parser, PPSSPP `unittest` harness (`unittest/UnitTest.h` macros).

## Global Constraints

- **License header:** every new `.cpp`/`.h` starts with the PPSSPP GPL 2.0 header block (copy verbatim from `GPU/Common/PostShader.h` lines 1–17, updating the year comment to `2026-`).
- **Source language of slang shaders:** real Vulkan GLSL, first line `#version 450` (or `#version 310 es`). These bypass the legacy `ConvertToVulkanGLSL` string-rewriter and are fed to glslang with Vulkan rules directly.
- **Uniform layout:** all UBO/sampler bindings live in descriptor set 0; slang binds uniforms **by member name** via SPIR-V reflection — no fixed uniform struct.
- **`*Size` convention:** every `*Size` uniform is a `vec4` laid out as `(width, height, 1.0/width, 1.0/height)`.
- **Pass indexing:** `.slangp` uses **zero-based** `shaderN` indices.
- **Zero regression:** the existing `PostShader`/`PresentationCommon` path must remain byte-for-byte behaviorally unchanged; slang and legacy post-shaders are mutually exclusive per frame.
- **New code location:** all Phase 1 rendering-core code under `GPU/Common/Slang/`. Unit tests under `unittest/`.
- **Test harness:** unit tests are plain `bool TestXxx()` functions returning `true` on success, using macros from `unittest/UnitTest.h` (`EXPECT_TRUE`, `EXPECT_FALSE`, `EXPECT_EQ_INT`, `EXPECT_EQ_STR`, `RET`). They are registered in `unittest/UnitTest.cpp`'s `availableTests[]` array via `TEST_ITEM(Xxx)` and declared near line 1337.
- **Build wiring:** new non-test sources are added to `CMakeLists.txt` in the `GPU/Common/*` source group (near line 2021–2053); new unittest sources near line 2980. Also add to the corresponding Visual Studio filter/project files if present (`GPU/GPU.vcxproj*`), matching how `PostShader.cpp` is listed.
- **Commit style:** end every commit message with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`. Commit after each green step group.

## File Structure

**New (rendering core):**
- `GPU/Common/Slang/SlangPreset.h` — plain data structs: `SlangScaleType`, `SlangPassDesc`, `SlangParamDesc`, `SlangPreset`. No logic.
- `GPU/Common/Slang/SlangpParser.h` / `.cpp` — `.slangp` text → `SlangPreset`; also `.slang` stage-splitting + `#pragma` extraction helpers.
- `GPU/Common/Slang/SlangReflection.h` — `SlangSemantic` enum + `SlangUniformMember` / `PassReflection` structs.
- `GPU/Common/Slang/SlangPassCompiler.h` / `.cpp` — compile one `.slang` (both stages) to SPIR-V via glslang, reflect via SPIRV-Cross, map member names → semantics, produce `Draw::Pipeline` + `PassReflection`.
- `GPU/Common/Slang/SlangFilterChain.h` / `.cpp` — own the compiled passes, resolve per-pass resolution, allocate framebuffers, bind resources by semantic each frame, execute the chain, expose the final texture.

**New (tests):**
- `unittest/TestSlangParser.cpp` — parser + stage-split + resolution-math tests (no GPU).

**Modified:**
- `GPU/Common/FramebufferManagerCommon.h` / `.cpp` — own an optional `SlangFilterChain *slangChain_`; delegate to it in the present path when a slang preset is active.
- `Core/Config.h` / `.cpp` — add `std::string sSlangShaderPreset` (active preset path, empty = none).
- `unittest/UnitTest.cpp` — register `TestSlangParser`.
- `CMakeLists.txt` (+ `GPU/GPU.vcxproj*` if present) — build wiring.

---

### Task 1: Slang preset data structs

**Files:**
- Create: `GPU/Common/Slang/SlangPreset.h`

**Interfaces:**
- Consumes: nothing.
- Produces: `enum class SlangScaleType { Source, Viewport, Absolute }`; `struct SlangParamDesc { std::string name; float initial, minimum, maximum, step; }`; `struct SlangPassDesc { std::string shaderPath; std::string alias; bool filterLinear=false; SlangScaleType scaleTypeX=SlangScaleType::Source, scaleTypeY=SlangScaleType::Source; float scaleX=1.0f, scaleY=1.0f; }`; `struct SlangPreset { Path basePath; std::vector<SlangPassDesc> passes; std::vector<SlangParamDesc> params; }`.

- [ ] **Step 1: Create the header**

Copy the GPL header (from `GPU/Common/PostShader.h:1-17`, year `2026-`), then:

```cpp
#pragma once

#include <string>
#include <vector>

#include "Common/File/Path/Path.h"

enum class SlangScaleType {
	Source,    // multiplier of this pass's input size
	Viewport,  // multiplier of the final display viewport
	Absolute,  // fixed pixel count
};

struct SlangParamDesc {
	std::string name;     // must match a float UBO/push member
	float initial = 0.0f;
	float minimum = 0.0f;
	float maximum = 1.0f;
	float step = 0.01f;
};

struct SlangPassDesc {
	std::string shaderPath;   // resolved absolute path to the .slang file
	std::string alias;        // #pragma name / aliasN, "" if none
	bool filterLinear = false;
	SlangScaleType scaleTypeX = SlangScaleType::Source;
	SlangScaleType scaleTypeY = SlangScaleType::Source;
	float scaleX = 1.0f;
	float scaleY = 1.0f;
};

struct SlangPreset {
	Path basePath;   // directory the .slangp lives in
	std::vector<SlangPassDesc> passes;
	std::vector<SlangParamDesc> params;
};
```

- [ ] **Step 2: Add to build**

In `CMakeLists.txt`, add `GPU/Common/Slang/SlangPreset.h` to the `GPU/Common` source group next to `GPU/Common/PostShader.cpp` (line ~2053). Headers alone need no compile unit, but list it so IDEs and the vcxproj filters pick it up.

- [ ] **Step 3: Commit**

```bash
git add GPU/Common/Slang/SlangPreset.h CMakeLists.txt
git commit -m "slang: add preset data structs

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: `.slangp` preset parser

**Files:**
- Create: `GPU/Common/Slang/SlangpParser.h`, `GPU/Common/Slang/SlangpParser.cpp`
- Create: `unittest/TestSlangParser.cpp`
- Modify: `unittest/UnitTest.cpp` (register test), `CMakeLists.txt`

**Interfaces:**
- Consumes: `SlangPreset` structs from Task 1.
- Produces: `bool ParseSlangPreset(const std::string &text, const Path &basePath, SlangPreset *out, std::string *error);` in `SlangpParser.h`. `text` is the raw `.slangp` contents; `basePath` is the directory used to resolve relative `shaderN` paths; returns `false` and sets `*error` on fatal errors (missing `shaders`, out-of-range count). Unknown keys are ignored.

- [ ] **Step 1: Write the failing test**

Create `unittest/TestSlangParser.cpp` (GPL header, year `2026-`):

```cpp
#include <string>
#include "unittest/UnitTest.h"
#include "Common/File/Path/Path.h"
#include "GPU/Common/Slang/SlangpParser.h"

bool TestSlangParser() {
	// Two-pass preset with per-axis scale, alias, and a parameter list line.
	const std::string preset =
		"shaders = 2\n"
		"shader0 = shaders/first.slang\n"
		"filter_linear0 = true\n"
		"scale_type0 = source\n"
		"scale0 = 1.0\n"
		"alias0 = FirstPass\n"
		"shader1 = shaders/second.slang\n"
		"scale_type_x1 = viewport\n"
		"scale_x1 = 1.0\n"
		"scale_type_y1 = absolute\n"
		"scale_y1 = 240\n";

	SlangPreset out;
	std::string error;
	Path base("/tmp/presetdir");
	EXPECT_TRUE(ParseSlangPreset(preset, base, &out, &error));
	EXPECT_EQ_INT((int)out.passes.size(), 2);

	// Pass 0
	std::string p0 = out.passes[0].shaderPath;
	std::string expect0 = (base / "shaders/first.slang").ToString();
	EXPECT_EQ_STR(p0, expect0);
	EXPECT_TRUE(out.passes[0].filterLinear);
	EXPECT_TRUE(out.passes[0].scaleTypeX == SlangScaleType::Source);
	EXPECT_TRUE(out.passes[0].scaleTypeY == SlangScaleType::Source);
	EXPECT_EQ_FLOAT(out.passes[0].scaleX, 1.0f);
	std::string a0 = out.passes[0].alias;
	std::string expectA0 = "FirstPass";
	EXPECT_EQ_STR(a0, expectA0);

	// Pass 1: per-axis override
	EXPECT_FALSE(out.passes[1].filterLinear);
	EXPECT_TRUE(out.passes[1].scaleTypeX == SlangScaleType::Viewport);
	EXPECT_TRUE(out.passes[1].scaleTypeY == SlangScaleType::Absolute);
	EXPECT_EQ_FLOAT(out.passes[1].scaleY, 240.0f);

	// Missing shaders count -> error
	SlangPreset bad;
	std::string badErr;
	EXPECT_FALSE(ParseSlangPreset("shader0 = x.slang\n", base, &bad, &badErr));
	return true;
}
```

Add its declaration and registration in `unittest/UnitTest.cpp`: near line 1337 add `bool TestSlangParser();`, and inside `availableTests[]` (near line 1391, next to `TEST_ITEM(IniFile)`) add `TEST_ITEM(SlangParser),`.

- [ ] **Step 2: Create the header**

`GPU/Common/Slang/SlangpParser.h` (GPL header):

```cpp
#pragma once

#include <string>
#include "Common/File/Path/Path.h"
#include "GPU/Common/Slang/SlangPreset.h"

// Parse a .slangp preset. Relative shaderN paths resolve against basePath.
// Unknown keys are ignored (forward-compat). Returns false + *error on fatal errors.
bool ParseSlangPreset(const std::string &text, const Path &basePath, SlangPreset *out, std::string *error);
```

- [ ] **Step 3: Run test to verify it fails**

Build the unittest target and run:
```bash
./unitTest SlangParser
```
Expected: link/compile failure — `ParseSlangPreset` not defined (`.cpp` not written yet).

- [ ] **Step 4: Write minimal implementation**

`GPU/Common/Slang/SlangpParser.cpp` (GPL header). Reuse PPSSPP's `IniFile` for tokenizing key=value lines is possible, but `.slangp` has no sections; parse line-by-line instead. Note quoted values (`shaders = "2"`) and `#` comments both occur.

```cpp
#include <cstdlib>
#include <sstream>

#include "Common/StringUtils.h"
#include "GPU/Common/Slang/SlangpParser.h"

static std::string Unquote(std::string s) {
	s = StripSpaces(s);
	if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
		s = s.substr(1, s.size() - 2);
	return s;
}

static SlangScaleType ParseScaleType(const std::string &v) {
	if (v == "viewport") return SlangScaleType::Viewport;
	if (v == "absolute") return SlangScaleType::Absolute;
	return SlangScaleType::Source;  // default
}

bool ParseSlangPreset(const std::string &text, const Path &basePath, SlangPreset *out, std::string *error) {
	out->basePath = basePath;
	out->passes.clear();
	out->params.clear();

	// key -> value map (last write wins, matching RetroArch).
	std::map<std::string, std::string> kv;
	std::stringstream ss(text);
	std::string line;
	while (std::getline(ss, line)) {
		size_t hash = line.find('#');
		if (hash != std::string::npos) line = line.substr(0, hash);
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string key = StripSpaces(line.substr(0, eq));
		std::string val = Unquote(line.substr(eq + 1));
		if (!key.empty()) kv[key] = val;
	}

	auto it = kv.find("shaders");
	if (it == kv.end()) { *error = "missing 'shaders' count"; return false; }
	int count = atoi(it->second.c_str());
	if (count <= 0 || count > 64) { *error = "bad 'shaders' count"; return false; }

	auto getStr = [&](const std::string &k, std::string *v) -> bool {
		auto f = kv.find(k); if (f == kv.end()) return false; *v = f->second; return true;
	};

	for (int i = 0; i < count; i++) {
		SlangPassDesc pass;
		std::string s;
		std::string idx = std::to_string(i);
		if (!getStr("shader" + idx, &s)) { *error = "missing shader" + idx; return false; }
		pass.shaderPath = (basePath / s).ToString();

		std::string tmp;
		if (getStr("filter_linear" + idx, &tmp)) pass.filterLinear = (tmp == "true" || tmp == "1");
		if (getStr("alias" + idx, &tmp)) pass.alias = tmp;

		// scale_typeN sets both axes; per-axis overrides win.
		if (getStr("scale_type" + idx, &tmp)) { pass.scaleTypeX = pass.scaleTypeY = ParseScaleType(tmp); }
		if (getStr("scale_type_x" + idx, &tmp)) pass.scaleTypeX = ParseScaleType(tmp);
		if (getStr("scale_type_y" + idx, &tmp)) pass.scaleTypeY = ParseScaleType(tmp);
		if (getStr("scale" + idx, &tmp)) { pass.scaleX = pass.scaleY = (float)atof(tmp.c_str()); }
		if (getStr("scale_x" + idx, &tmp)) pass.scaleX = (float)atof(tmp.c_str());
		if (getStr("scale_y" + idx, &tmp)) pass.scaleY = (float)atof(tmp.c_str());

		out->passes.push_back(pass);
	}
	return true;
}
```

Add `#include <map>` at the top. Add both `.cpp`/`.h` to `CMakeLists.txt` (GPU/Common group, ~line 2053) and add `unittest/TestSlangParser.cpp` to the unittest sources (~line 2980). Add to `GPU/GPU.vcxproj*` filters if present.

- [ ] **Step 5: Run test to verify it passes**

```bash
./unitTest SlangParser
```
Expected: `SlangParser: passed` (exit 0).

- [ ] **Step 6: Commit**

```bash
git add GPU/Common/Slang/SlangpParser.h GPU/Common/Slang/SlangpParser.cpp \
        unittest/TestSlangParser.cpp unittest/UnitTest.cpp CMakeLists.txt
git commit -m "slang: add .slangp preset parser with tests

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: `.slang` stage splitting and `#pragma` extraction

**Files:**
- Modify: `GPU/Common/Slang/SlangpParser.h`, `GPU/Common/Slang/SlangpParser.cpp`
- Modify: `unittest/TestSlangParser.cpp`

**Interfaces:**
- Consumes: `SlangParamDesc` from Task 1.
- Produces (add to `SlangpParser.h`):
  ```cpp
  struct SlangSource {
      std::string vertex;    // full GLSL for the vertex stage (shared prologue + vertex body)
      std::string fragment;  // full GLSL for the fragment stage (shared prologue + fragment body)
      std::string name;      // #pragma name value, "" if none
      std::vector<SlangParamDesc> params;  // one per #pragma parameter
  };
  bool SplitSlangSource(const std::string &src, SlangSource *out, std::string *error);
  ```
  Semantics: lines before the first `#pragma stage` are the **shared prologue** prepended to both stages. `#pragma stage vertex` / `#pragma stage fragment` switch the active stage. `#pragma name X` sets `name`. `#pragma parameter NAME "Desc" INIT MIN MAX [STEP]` appends a `SlangParamDesc` (STEP defaults to `0.0f` when absent). `#pragma stage`/`#pragma name`/`#pragma parameter`/`#pragma format` lines are consumed (not emitted into output GLSL); all other lines (including `#version`, other `#pragma`s, and code) pass through to the active target(s). Phase 1 does **not** resolve `#include` (documented limitation; presets needing includes fail to compile later with a clear glslang error).

- [ ] **Step 1: Write the failing test**

Append to `TestSlangParser.cpp` a new function and register it:

```cpp
bool TestSlangSplit() {
	const std::string src =
		"#version 450\n"
		"layout(set=0,binding=0,std140) uniform UBO { vec4 SourceSize; float ColorMod; };\n"
		"#pragma name StockShader\n"
		"#pragma parameter ColorMod \"Color intensity\" 1.0 0.1 2.0 0.1\n"
		"#pragma stage vertex\n"
		"void main() { gl_Position = vec4(0.0); }\n"
		"#pragma stage fragment\n"
		"layout(location=0) out vec4 FragColor;\n"
		"void main() { FragColor = vec4(ColorMod); }\n";

	SlangSource out;
	std::string error;
	EXPECT_TRUE(SplitSlangSource(src, &out, &error));

	std::string name = out.name;
	std::string expectName = "StockShader";
	EXPECT_EQ_STR(name, expectName);

	// Shared prologue (#version + UBO) present in BOTH stages.
	EXPECT_TRUE(out.vertex.find("#version 450") != std::string::npos);
	EXPECT_TRUE(out.fragment.find("#version 450") != std::string::npos);
	EXPECT_TRUE(out.vertex.find("uniform UBO") != std::string::npos);
	EXPECT_TRUE(out.fragment.find("uniform UBO") != std::string::npos);

	// Stage bodies land in the right stage only.
	EXPECT_TRUE(out.vertex.find("gl_Position") != std::string::npos);
	EXPECT_TRUE(out.fragment.find("gl_Position") == std::string::npos);
	EXPECT_TRUE(out.fragment.find("FragColor") != std::string::npos);
	EXPECT_TRUE(out.vertex.find("FragColor") == std::string::npos);

	// #pragma lines are stripped from emitted GLSL.
	EXPECT_TRUE(out.fragment.find("#pragma") == std::string::npos);

	// Parameter parsed.
	EXPECT_EQ_INT((int)out.params.size(), 1);
	std::string pn = out.params[0].name;
	std::string expectPn = "ColorMod";
	EXPECT_EQ_STR(pn, expectPn);
	EXPECT_EQ_FLOAT(out.params[0].initial, 1.0f);
	EXPECT_EQ_FLOAT(out.params[0].minimum, 0.1f);
	EXPECT_EQ_FLOAT(out.params[0].maximum, 2.0f);
	EXPECT_EQ_FLOAT(out.params[0].step, 0.1f);
	return true;
}
```

Register: add `bool TestSlangSplit();` near the other declaration (line ~1337) and `TEST_ITEM(SlangSplit),` in `availableTests[]`.

- [ ] **Step 2: Add declaration to header**

Add the `SlangSource` struct and `SplitSlangSource` declaration (signatures above) to `SlangpParser.h`, after the `ParseSlangPreset` declaration.

- [ ] **Step 3: Run test to verify it fails**

```bash
./unitTest SlangSplit
```
Expected: link failure — `SplitSlangSource` not defined.

- [ ] **Step 4: Write minimal implementation**

Add to `SlangpParser.cpp`. Parameter tokenizing must handle the quoted description (which may contain spaces).

```cpp
// Parse: #pragma parameter NAME "Description" INIT MIN MAX [STEP]
static bool ParseParameterPragma(const std::string &rest, SlangParamDesc *p) {
	// rest is everything after "#pragma parameter ".
	size_t q1 = rest.find('"');
	size_t q2 = (q1 == std::string::npos) ? std::string::npos : rest.find('"', q1 + 1);
	if (q1 == std::string::npos || q2 == std::string::npos) return false;
	p->name = StripSpaces(rest.substr(0, q1));
	std::string tail = rest.substr(q2 + 1);  // " INIT MIN MAX [STEP]"
	std::istringstream nums(tail);
	nums >> p->initial >> p->minimum >> p->maximum;
	if (!(nums >> p->step)) p->step = 0.0f;
	return !p->name.empty();
}

bool SplitSlangSource(const std::string &src, SlangSource *out, std::string *error) {
	out->vertex.clear();
	out->fragment.clear();
	out->name.clear();
	out->params.clear();

	std::string prologue;
	// stage: 0 = prologue (shared), 1 = vertex, 2 = fragment
	int stage = 0;
	std::stringstream ss(src);
	std::string line;
	while (std::getline(ss, line)) {
		std::string trimmed = StripSpaces(line);
		if (startsWith(trimmed, "#pragma")) {
			std::string rest = StripSpaces(trimmed.substr(strlen("#pragma")));
			if (startsWith(rest, "stage")) {
				std::string st = StripSpaces(rest.substr(strlen("stage")));
				stage = (st == "vertex") ? 1 : (st == "fragment") ? 2 : stage;
				continue;
			} else if (startsWith(rest, "name")) {
				out->name = StripSpaces(rest.substr(strlen("name")));
				continue;
			} else if (startsWith(rest, "parameter")) {
				SlangParamDesc p;
				if (ParseParameterPragma(StripSpaces(rest.substr(strlen("parameter"))), &p))
					out->params.push_back(p);
				continue;
			} else if (startsWith(rest, "format")) {
				continue;  // consumed; Phase 1 uses default RT format
			}
			// Unknown pragma: fall through and emit it.
		}
		if (stage == 0) prologue += line + "\n";
		else if (stage == 1) out->vertex += line + "\n";
		else out->fragment += line + "\n";
	}
	out->vertex = prologue + out->vertex;
	out->fragment = prologue + out->fragment;
	return true;
}
```

Ensure `#include "Common/StringUtils.h"` (for `startsWith`, `StripSpaces`) and `<cstring>` are present.

- [ ] **Step 5: Run test to verify it passes**

```bash
./unitTest SlangSplit
```
Expected: `SlangSplit: passed`.

- [ ] **Step 6: Commit**

```bash
git add GPU/Common/Slang/SlangpParser.h GPU/Common/Slang/SlangpParser.cpp unittest/TestSlangParser.cpp unittest/UnitTest.cpp
git commit -m "slang: split .slang stages and extract #pragma metadata

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Per-pass resolution resolver

**Files:**
- Create: `GPU/Common/Slang/SlangResolution.h` (header-only, inline function)
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`

**Interfaces:**
- Consumes: `SlangScaleType`, `SlangPassDesc` from Task 1.
- Produces:
  ```cpp
  struct SlangSize { int w, h; };
  // inputSize = this pass's input (prev pass output, or source for pass 0);
  // viewportSize = final on-screen display rect.
  SlangSize ResolvePassSize(const SlangPassDesc &pass, SlangSize inputSize, SlangSize viewportSize);
  ```
  Rules per axis: `Source` → round(input * scale); `Viewport` → round(viewport * scale); `Absolute` → round(scale) as pixels. Result clamped to a minimum of 1.

- [ ] **Step 1: Write the failing test**

Append to `TestSlangParser.cpp`:

```cpp
bool TestSlangResolution() {
	SlangSize input{480, 272};
	SlangSize viewport{1920, 1080};

	SlangPassDesc a;  // source x2 both axes
	a.scaleTypeX = a.scaleTypeY = SlangScaleType::Source;
	a.scaleX = a.scaleY = 2.0f;
	SlangSize ra = ResolvePassSize(a, input, viewport);
	EXPECT_EQ_INT(ra.w, 960);
	EXPECT_EQ_INT(ra.h, 544);

	SlangPassDesc b;  // x = viewport 1.0, y = absolute 240
	b.scaleTypeX = SlangScaleType::Viewport; b.scaleX = 1.0f;
	b.scaleTypeY = SlangScaleType::Absolute; b.scaleY = 240.0f;
	SlangSize rb = ResolvePassSize(b, input, viewport);
	EXPECT_EQ_INT(rb.w, 1920);
	EXPECT_EQ_INT(rb.h, 240);

	SlangPassDesc c;  // degenerate scale clamps to 1
	c.scaleTypeX = c.scaleTypeY = SlangScaleType::Source;
	c.scaleX = c.scaleY = 0.0f;
	SlangSize rc = ResolvePassSize(c, input, viewport);
	EXPECT_EQ_INT(rc.w, 1);
	EXPECT_EQ_INT(rc.h, 1);
	return true;
}
```

Register `bool TestSlangResolution();` and `TEST_ITEM(SlangResolution),`.

- [ ] **Step 2: Create the header with implementation**

`GPU/Common/Slang/SlangResolution.h` (GPL header):

```cpp
#pragma once

#include <algorithm>
#include <cmath>
#include "GPU/Common/Slang/SlangPreset.h"

struct SlangSize { int w, h; };

inline int ResolveAxis(SlangScaleType type, float scale, int input, int viewport) {
	float v;
	switch (type) {
	case SlangScaleType::Viewport: v = viewport * scale; break;
	case SlangScaleType::Absolute: v = scale; break;
	case SlangScaleType::Source:
	default: v = input * scale; break;
	}
	int r = (int)std::lround(v);
	return std::max(1, r);
}

inline SlangSize ResolvePassSize(const SlangPassDesc &pass, SlangSize inputSize, SlangSize viewportSize) {
	SlangSize out;
	out.w = ResolveAxis(pass.scaleTypeX, pass.scaleX, inputSize.w, viewportSize.w);
	out.h = ResolveAxis(pass.scaleTypeY, pass.scaleY, inputSize.h, viewportSize.h);
	return out;
}
```

Add `#include "GPU/Common/Slang/SlangResolution.h"` to the top of `TestSlangParser.cpp`. Add the header to `CMakeLists.txt` GPU/Common group.

- [ ] **Step 3: Verify the test fails before the header exists**

Write the test and its `#include` **before** creating `SlangResolution.h`, build once, and confirm the failure is a real compile error (`ResolvePassSize`/`SlangSize` unknown) — this proves the test exercises new code rather than silently passing. Then create the header (Step 2 content) and rebuild.

- [ ] **Step 4: Run test to verify it passes**

```bash
./unitTest SlangResolution
```
Expected: `SlangResolution: passed`.

- [ ] **Step 5: Commit**

```bash
git add GPU/Common/Slang/SlangResolution.h unittest/TestSlangParser.cpp unittest/UnitTest.cpp CMakeLists.txt
git commit -m "slang: add per-pass resolution resolver with tests

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Semantic classification (name → semantic)

**Files:**
- Create: `GPU/Common/Slang/SlangReflection.h`, `GPU/Common/Slang/SlangReflection.cpp`
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing (pure string classification).
- Produces:
  ```cpp
  enum class SlangSemantic {
      Unknown,
      // uniform (UBO/push member) semantics:
      MVP, OutputSize, FinalViewportSize, FrameCount, FrameDirection, Rotation,
      SourceSize, OriginalSize,          // Phase 1 texture-size companions
      UserParameter,                     // matches a #pragma parameter float
      // texture (sampler2D) semantics:
      TexSource, TexOriginal,
  };
  // Classify a UBO/push member name. knownParams = names from #pragma parameter.
  SlangSemantic ClassifyUniform(const std::string &name, const std::vector<std::string> &knownParams);
  // Classify a sampler2D name.
  SlangSemantic ClassifyTexture(const std::string &name);
  ```
  Phase 1 recognizes the built-ins above and `*Size` companions for `Source`/`Original` only; `PassOutput#`, `PassFeedback#`, `OriginalHistory#`, and LUT names are **not** classified in Phase 1 (they return `Unknown`, which the compiler treats as a hard compile error so unsupported presets fail loudly rather than render wrong).

- [ ] **Step 1: Write the failing test**

Append to `TestSlangParser.cpp`:

```cpp
bool TestSlangSemantics() {
	std::vector<std::string> params = { "ColorMod", "Sharpness" };

	EXPECT_TRUE(ClassifyUniform("MVP", params) == SlangSemantic::MVP);
	EXPECT_TRUE(ClassifyUniform("SourceSize", params) == SlangSemantic::SourceSize);
	EXPECT_TRUE(ClassifyUniform("OriginalSize", params) == SlangSemantic::OriginalSize);
	EXPECT_TRUE(ClassifyUniform("OutputSize", params) == SlangSemantic::OutputSize);
	EXPECT_TRUE(ClassifyUniform("FinalViewportSize", params) == SlangSemantic::FinalViewportSize);
	EXPECT_TRUE(ClassifyUniform("FrameCount", params) == SlangSemantic::FrameCount);
	EXPECT_TRUE(ClassifyUniform("ColorMod", params) == SlangSemantic::UserParameter);
	EXPECT_TRUE(ClassifyUniform("Sharpness", params) == SlangSemantic::UserParameter);
	EXPECT_TRUE(ClassifyUniform("SomethingElse", params) == SlangSemantic::Unknown);

	EXPECT_TRUE(ClassifyTexture("Source") == SlangSemantic::TexSource);
	EXPECT_TRUE(ClassifyTexture("Original") == SlangSemantic::TexOriginal);
	// Not supported in Phase 1:
	EXPECT_TRUE(ClassifyTexture("PassOutput0") == SlangSemantic::Unknown);
	return true;
}
```

Register `bool TestSlangSemantics();` + `TEST_ITEM(SlangSemantics),`.

- [ ] **Step 2: Create header**

`GPU/Common/Slang/SlangReflection.h` (GPL header): the `SlangSemantic` enum, `ClassifyUniform`, `ClassifyTexture` declarations (signatures above), plus these structs used by Task 6:

```cpp
#pragma once
#include <string>
#include <vector>

// (enum SlangSemantic as specified in the plan interface)

struct SlangUniformMember {
	std::string name;
	SlangSemantic semantic;
	uint32_t offsetBytes;   // offset within the UBO block
	uint32_t sizeBytes;     // member size (4 for float, 16 for vec4, 64 for mat4)
};

struct SlangTextureBinding {
	std::string name;
	SlangSemantic semantic;
	int binding;            // sampler binding slot
};

struct PassReflection {
	std::vector<SlangUniformMember> uboMembers;
	uint32_t uboSizeBytes = 0;
	int uboBinding = -1;    // -1 if no UBO
	std::vector<SlangTextureBinding> textures;
};
```

- [ ] **Step 3: Create implementation**

`GPU/Common/Slang/SlangReflection.cpp` (GPL header):

```cpp
#include <algorithm>
#include "GPU/Common/Slang/SlangReflection.h"

SlangSemantic ClassifyUniform(const std::string &name, const std::vector<std::string> &knownParams) {
	if (name == "MVP") return SlangSemantic::MVP;
	if (name == "OutputSize") return SlangSemantic::OutputSize;
	if (name == "FinalViewportSize") return SlangSemantic::FinalViewportSize;
	if (name == "FrameCount") return SlangSemantic::FrameCount;
	if (name == "FrameDirection") return SlangSemantic::FrameDirection;
	if (name == "Rotation") return SlangSemantic::Rotation;
	if (name == "SourceSize") return SlangSemantic::SourceSize;
	if (name == "OriginalSize") return SlangSemantic::OriginalSize;
	if (std::find(knownParams.begin(), knownParams.end(), name) != knownParams.end())
		return SlangSemantic::UserParameter;
	return SlangSemantic::Unknown;
}

SlangSemantic ClassifyTexture(const std::string &name) {
	if (name == "Source") return SlangSemantic::TexSource;
	if (name == "Original") return SlangSemantic::TexOriginal;
	return SlangSemantic::Unknown;
}
```

Add `#include "GPU/Common/Slang/SlangReflection.h"` to `TestSlangParser.cpp`. Add both files to `CMakeLists.txt` GPU/Common group.

- [ ] **Step 4: Run test to verify it fails then passes**

```bash
./unitTest SlangSemantics
```
Expected first run (before `.cpp`): link failure. After implementing: `SlangSemantics: passed`.

- [ ] **Step 5: Commit**

```bash
git add GPU/Common/Slang/SlangReflection.h GPU/Common/Slang/SlangReflection.cpp unittest/TestSlangParser.cpp unittest/UnitTest.cpp CMakeLists.txt
git commit -m "slang: classify uniform/texture names to semantics

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: SlangPassCompiler — GLSL→SPIR-V + reflection → Draw::Pipeline

**Files:**
- Create: `GPU/Common/Slang/SlangPassCompiler.h`, `GPU/Common/Slang/SlangPassCompiler.cpp`
- Modify: `unittest/TestSlangParser.cpp`, `unittest/UnitTest.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `SlangSource` (Task 3), `PassReflection`/`SlangUniformMember`/`SlangSemantic` (Task 5), `TranslateShader`/`ShaderTranslationInit` from `Common/GPU/ShaderTranslation.h`, `Draw::DrawContext`/`ShaderModule`/`Pipeline` from `Common/GPU/thin3d.h`.
- Produces:
  ```cpp
  // Reflect a compiled slang source: compile both stages to SPIR-V (glslang, Vulkan rules),
  // reflect the fragment+vertex SPIR-V (SPIRV-Cross), classify every UBO member and sampler.
  // Does NOT create GPU objects — pure compile+reflect, unit-testable without a device.
  bool ReflectSlangSource(const SlangSource &src, PassReflection *out, std::string *error);

  struct SlangCompiledPass {
      Draw::Pipeline *pipeline = nullptr;
      PassReflection reflection;
  };
  // Full compile: reflect + create a Draw::Pipeline for the given backend language.
  // For Vulkan, the SPIR-V is consumed directly; other backends cross-compile via SPIRV-Cross.
  bool CompileSlangPass(Draw::DrawContext *draw, const SlangSource &src,
                        SlangCompiledPass *out, std::string *error);
  ```
- Note: `ReflectSlangSource` requires `ShaderTranslationInit()` to have run. The unittest harness must call it once (see Step 1). `CompileSlangPass` requires a live `Draw::DrawContext` and is exercised by the integration verification in Task 9, not a pure unit test.

- [ ] **Step 1: Write the failing reflection test**

Append to `TestSlangParser.cpp` (top of file add `#include "GPU/Common/Slang/SlangPassCompiler.h"` and `#include "Common/GPU/ShaderTranslation.h"`):

```cpp
bool TestSlangReflection() {
	ShaderTranslationInit();  // idempotent-safe within a single test run
	const std::string srcText =
		"#version 450\n"
		"layout(set=0,binding=0,std140) uniform UBO {\n"
		"  mat4 MVP;\n"
		"  vec4 SourceSize;\n"
		"  float ColorMod;\n"
		"};\n"
		"#pragma parameter ColorMod \"Color\" 1.0 0.1 2.0 0.1\n"
		"#pragma stage vertex\n"
		"layout(location=0) in vec4 Position;\n"
		"layout(location=1) in vec2 TexCoord;\n"
		"layout(location=0) out vec2 vTexCoord;\n"
		"void main() { gl_Position = MVP * Position; vTexCoord = TexCoord; }\n"
		"#pragma stage fragment\n"
		"layout(location=0) in vec2 vTexCoord;\n"
		"layout(location=0) out vec4 FragColor;\n"
		"layout(binding=1) uniform sampler2D Source;\n"
		"void main() { FragColor = texture(Source, vTexCoord) * ColorMod; }\n";

	SlangSource src;
	std::string error;
	EXPECT_TRUE(SplitSlangSource(srcText, &src, &error));

	PassReflection refl;
	EXPECT_TRUE(ReflectSlangSource(src, &refl, &error));

	// UBO binding 0, three members with correct semantics + offsets (std140).
	EXPECT_EQ_INT(refl.uboBinding, 0);
	EXPECT_EQ_INT((int)refl.uboMembers.size(), 3);

	bool sawMVP = false, sawSourceSize = false, sawColorMod = false;
	for (const auto &m : refl.uboMembers) {
		if (m.semantic == SlangSemantic::MVP) { sawMVP = true; EXPECT_EQ_INT((int)m.offsetBytes, 0); EXPECT_EQ_INT((int)m.sizeBytes, 64); }
		if (m.semantic == SlangSemantic::SourceSize) { sawSourceSize = true; EXPECT_EQ_INT((int)m.offsetBytes, 64); EXPECT_EQ_INT((int)m.sizeBytes, 16); }
		if (m.semantic == SlangSemantic::UserParameter) { sawColorMod = true; EXPECT_EQ_INT((int)m.offsetBytes, 80); }
	}
	EXPECT_TRUE(sawMVP); EXPECT_TRUE(sawSourceSize); EXPECT_TRUE(sawColorMod);

	// Sampler "Source" at binding 1, classified TexSource.
	EXPECT_EQ_INT((int)refl.textures.size(), 1);
	EXPECT_EQ_INT(refl.textures[0].binding, 1);
	EXPECT_TRUE(refl.textures[0].semantic == SlangSemantic::TexSource);
	return true;
}
```

Register `bool TestSlangReflection();` + `TEST_ITEM(SlangReflection),`.

- [ ] **Step 2: Create the header**

`GPU/Common/Slang/SlangPassCompiler.h` (GPL header): include `Common/GPU/thin3d.h`, `GPU/Common/Slang/SlangpParser.h`, `GPU/Common/Slang/SlangReflection.h`; declare `ReflectSlangSource`, `SlangCompiledPass`, `CompileSlangPass` (signatures above).

- [ ] **Step 3: Run test to verify it fails**

```bash
./unitTest SlangReflection
```
Expected: link failure — `ReflectSlangSource` undefined.

- [ ] **Step 4: Write the implementation**

`GPU/Common/Slang/SlangPassCompiler.cpp` (GPL header). Model the glslang invocation on `Common/GPU/ShaderTranslation.cpp:215-255` (TShader/TProgram, `parse` with **SPIR-V/Vulkan rules enabled** — unlike the legacy path — so `#version 450` and `layout(set=,binding=)` are accepted), then reflect with SPIRV-Cross. Include guards match ShaderTranslation.cpp (DbgNew undef block, glslang + SPIRV-Cross headers).

```cpp
#include "ppsspp_config.h"
#ifdef DBG_NEW
#undef new
#undef free
#undef malloc
#undef realloc
#endif

#include <vector>
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/GPU/Shader.h"   // InitShaderResources (already public here)
#include "GPU/Common/Slang/SlangPassCompiler.h"
// GlslangToSpv.h transitively provides TShader/TProgram/EShClient* — this is the
// exact include set VulkanContext.cpp and ShaderTranslation.cpp use; do not add a
// separate ShaderLang.h include (its path differs across glslang versions).
#include "ext/glslang/SPIRV/GlslangToSpv.h"
#include "ext/SPIRV-Cross/spirv_cross.hpp"

static bool CompileStageToSpirv(EShLanguage stage, const std::string &src,
                                std::vector<unsigned int> *spirv, std::string *error) {
	TBuiltInResource resources{};
	InitShaderResources(resources);
	// Mirror the verified pattern in VulkanContext.cpp GLSLtoSPV (GLSLVariant::VULKAN):
	// Vulkan+SPIR-V rules, defaultVersion 450, ECoreProfile, forwardCompatible=true.
	// Do NOT add setEnvInput/setEnvClient/setEnvTarget — this glslang fork compiles
	// #version 450 + layout(set=,binding=) with the messages flags alone.
	glslang::TShader shader(stage);
	const char *strings[1] = { src.c_str() };
	shader.setStrings(strings, 1);
	EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
	if (!shader.parse(&resources, 450, ECoreProfile, false, true, messages)) {
		*error = std::string("slang parse: ") + shader.getInfoLog() + shader.getInfoDebugLog();
		return false;
	}
	glslang::TProgram program;
	program.addShader(&shader);
	if (!program.link(messages)) {
		*error = std::string("slang link: ") + shader.getInfoLog() + shader.getInfoDebugLog();
		return false;
	}
	glslang::SpvOptions options;
	options.disableOptimizer = false;
	glslang::GlslangToSpv(*program.getIntermediate(stage), *spirv, &options);
	return !spirv->empty();
}

static uint32_t MemberSizeBytes(const spirv_cross::Compiler &comp, const spirv_cross::SPIRType &type) {
	// vec4 = 16, mat4 = 64, float = 4, uint/int = 4.
	uint32_t base = 4;  // float/int/uint base
	uint32_t comps = type.vecsize * type.columns;
	return base * comps;
}

bool ReflectSlangSource(const SlangSource &src, PassReflection *out, std::string *error) {
	std::vector<unsigned int> vspv, fspv;
	if (!CompileStageToSpirv(EShLangVertex, src.vertex, &vspv, error)) return false;
	if (!CompileStageToSpirv(EShLangFragment, src.fragment, &fspv, error)) return false;

	std::vector<std::string> paramNames;
	for (const auto &p : src.params) paramNames.push_back(p.name);

	// Reflect the fragment stage for UBO + samplers; merge vertex-only UBO members if present.
	spirv_cross::Compiler frag(fspv);
	spirv_cross::ShaderResources res = frag.get_shader_resources();

	out->uboMembers.clear();
	out->textures.clear();
	out->uboBinding = -1;
	out->uboSizeBytes = 0;

	if (!res.uniform_buffers.empty()) {
		const auto &ubo = res.uniform_buffers[0];
		out->uboBinding = frag.get_decoration(ubo.id, spv::DecorationBinding);
		const spirv_cross::SPIRType &blockType = frag.get_type(ubo.base_type_id);
		out->uboSizeBytes = (uint32_t)frag.get_declared_struct_size(blockType);
		uint32_t count = (uint32_t)blockType.member_types.size();
		for (uint32_t i = 0; i < count; i++) {
			SlangUniformMember m;
			m.name = frag.get_member_name(ubo.base_type_id, i);
			m.offsetBytes = frag.type_struct_member_offset(blockType, i);
			m.sizeBytes = MemberSizeBytes(frag, frag.get_type(blockType.member_types[i]));
			m.semantic = ClassifyUniform(m.name, paramNames);
			if (m.semantic == SlangSemantic::Unknown) {
				*error = "unsupported uniform member in Phase 1: " + m.name;
				return false;
			}
			out->uboMembers.push_back(m);
		}
	}

	for (const auto &img : res.sampled_images) {
		SlangTextureBinding t;
		t.name = frag.get_name(img.id);
		t.binding = frag.get_decoration(img.id, spv::DecorationBinding);
		t.semantic = ClassifyTexture(t.name);
		if (t.semantic == SlangSemantic::Unknown) {
			*error = "unsupported texture in Phase 1: " + t.name;
			return false;
		}
		out->textures.push_back(t);
	}
	return true;
}
```

For `CompileSlangPass`, reuse `TranslateShader(..., draw->GetShaderLanguageDesc(), ...)` from `ShaderTranslation.h` to obtain backend GLSL/HLSL/SPIR-V for each stage, then `draw->CreateShaderModule(...)` and `draw->CreatePipeline(...)` mirroring `PresentationCommon::CompileShaderModule`/`CreatePipeline` (`GPU/Common/PresentationCommon.cpp:593`). Full-screen-quad input layout matches the existing post-shader pipeline (position + texcoord0). Its behavioral coverage comes from Task 9 (on-device render).

`InitShaderResources` is already declared in `Common/GPU/Shader.h` (defined in `Common/GPU/Shader.cpp:118`) and used by both `ShaderTranslation.cpp` and `VulkanContext.cpp`; just include `Common/GPU/Shader.h` and call it — no promotion needed. (This means Task 6 does **not** modify `ShaderTranslation.h`/`.cpp`; drop those two paths from the Step 7 `git add`.)

- [ ] **Step 5: Ensure the unittest harness links glslang + SPIRV-Cross**

Confirm the `unittest` target in `CMakeLists.txt` already links the libraries used by `TestShaderGenerators` (it compiles shaders, so glslang/SPIRV-Cross are almost certainly linked). If `TestSlangReflection` fails to link, add the same link deps the main GPU target uses (glslang, SPIRV, spirv-cross-*).

- [ ] **Step 6: Run test to verify it passes**

```bash
./unitTest SlangReflection
```
Expected: `SlangReflection: passed`, confirming std140 offsets (MVP@0, SourceSize@64, ColorMod@80) and sampler binding.

- [ ] **Step 7: Commit**

```bash
git add GPU/Common/Slang/SlangPassCompiler.h GPU/Common/Slang/SlangPassCompiler.cpp \
        unittest/TestSlangParser.cpp unittest/UnitTest.cpp CMakeLists.txt
git commit -m "slang: compile passes to SPIR-V and reflect uniforms/samplers

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: SlangFilterChain runtime

**Files:**
- Create: `GPU/Common/Slang/SlangFilterChain.h`, `GPU/Common/Slang/SlangFilterChain.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `SlangPreset`/`SlangpParser` (Task 2/3), `ResolvePassSize` (Task 4), `SlangCompiledPass`/`CompileSlangPass` (Task 6), `Draw::` from `thin3d.h`.
- Produces:
  ```cpp
  class SlangFilterChain {
  public:
      explicit SlangFilterChain(Draw::DrawContext *draw);
      ~SlangFilterChain();
      // Parse+compile a preset file (reads .slangp + each .slang via g_VFS/File).
      bool Load(const Path &presetPath, std::string *error);
      bool IsValid() const { return valid_; }
      // Run the chain for one frame. `source` is the game framebuffer; viewport is the
      // on-screen display rect. Returns the final pass output framebuffer, or nullptr on failure.
      Draw::Framebuffer *Run(Draw::Framebuffer *source, int sourceW, int sourceH,
                             int viewportW, int viewportH, int frameCount);
      void DeviceLost();
      void DeviceRestore(Draw::DrawContext *draw);
  private:
      Draw::DrawContext *draw_;
      SlangPreset preset_;
      std::vector<SlangCompiledPass> passes_;
      std::vector<Draw::Framebuffer *> passFramebuffers_;
      Draw::Buffer *quad_ = nullptr;
      Draw::SamplerState *samplerLinear_ = nullptr, *samplerNearest_ = nullptr;
      bool valid_ = false;
  };
  ```

- [ ] **Step 1: Implement Load()**

`Load` reads the `.slangp` text (via `File::ReadFileToString` / `g_VFS.ReadFile`), calls `ParseSlangPreset`, then for each pass reads the `.slang` file, `SplitSlangSource`, and `CompileSlangPass`. On any failure, set `*error`, clear state, return false. Store the union of all passes' `#pragma parameter`s into `preset_.params`. Create a shared full-screen-quad vertex buffer and nearest/linear samplers (mirror `PresentationCommon::CreateDeviceObjects`).

- [ ] **Step 2: Implement Run() — per-pass execution**

For each pass `i` (0..N-1):
1. Compute input size: pass 0 → (sourceW, sourceH); else the previous pass's resolved size.
2. `ResolvePassSize(preset_.passes[i], input, {viewportW, viewportH})` → output size.
3. Allocate/reuse `passFramebuffers_[i]` at that size (recreate if size changed; `Draw::FramebufferDesc` with `R8G8B8A8_UNORM`, no z_stencil).
4. Bind pass `i`'s input texture to the slot(s) its reflection expects:
   - `TexSource` → previous pass output (or `source` for pass 0);
   - `TexOriginal` → `source`.
   Use `draw_->BindFramebufferAsTexture(...)` and the pass's `filterLinear` sampler.
5. Build the UBO scratch buffer: for each `SlangUniformMember`, write the value for its semantic at `offsetBytes`:
   - `MVP` → identity mat4 (PPSSPP feeds a pre-transformed quad, so identity is correct);
   - `SourceSize`/`OriginalSize` → `(inW, inH, 1/inW, 1/inH)` using the pass input size (both equal source in Phase 1's linear chain for `OriginalSize`);
   - `OutputSize` → `(outW, outH, 1/outW, 1/outH)`;
   - `FinalViewportSize` → `(viewportW, viewportH, 1/vw, 1/vh)`;
   - `FrameCount` → `frameCount` (as uint bits);
   - `FrameDirection` → `1`; `Rotation` → `0`;
   - `UserParameter` → the current value for that param name (from config map; default = `initial`).
   `draw_->UpdateDynamicUniformBuffer(scratch, refl.uboSizeBytes)`.
6. `BindFramebufferAsRenderTarget(passFramebuffers_[i], {RPAction::CLEAR, ...})`, `BindPipeline(pass.pipeline)`, bind the quad vbuffer, `Draw(4)`.
7. Return `passFramebuffers_.back()`.

- [ ] **Step 3: Implement DeviceLost/DeviceRestore + destructor**

Release all `Draw::` objects (`passFramebuffers_`, `passes_[].pipeline`, samplers, quad). `DeviceRestore` re-runs `Load(presetPath_, ...)` (store the path in `Load`). Follow the `DoRelease`/`DoReleaseVector` pattern from `PresentationCommon.cpp`.

- [ ] **Step 4: Build**

Add both files to `CMakeLists.txt` GPU/Common group and vcxproj filters.

- [ ] **Step 5: Commit**

```bash
git add GPU/Common/Slang/SlangFilterChain.h GPU/Common/Slang/SlangFilterChain.cpp CMakeLists.txt
git commit -m "slang: add filter-chain runtime (parse, compile, execute passes)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

Note: this task has no pure unit test (it requires a live GPU device). Its behavior is verified end-to-end in Task 9. The pure logic it depends on (parsing, resolution, reflection) is already covered by Tasks 2–6.

---

### Task 8: Config flag + present-path integration

**Files:**
- Modify: `Core/Config.h`, `Core/Config.cpp` (add `sSlangShaderPreset`)
- Modify: `GPU/Common/FramebufferManagerCommon.h`, `GPU/Common/FramebufferManagerCommon.cpp`

**Interfaces:**
- Consumes: `SlangFilterChain` (Task 7), `g_Config.sSlangShaderPreset`.
- Produces: an owned `SlangFilterChain *slangChain_` on `FramebufferManagerCommon`, activated when `sSlangShaderPreset` is non-empty and compiled successfully.

- [ ] **Step 1: Add config key**

In `Core/Config.h` add `std::string sSlangShaderPreset;` in the graphics section. In `Core/Config.cpp` register it in the graphics config setting table with default `""` (empty = disabled), matching how `sPostShaderNames`-adjacent string settings are declared.

- [ ] **Step 2: Own the chain in FramebufferManagerCommon**

Add `SlangFilterChain *slangChain_ = nullptr;` member (forward-declare the class in the header). Add a private `void UpdateSlangChain();` that: if `g_Config.sSlangShaderPreset` is empty → delete and null `slangChain_`; else if the path changed → (re)create `SlangFilterChain(draw_)` and `Load()` it, logging errors and leaving it null on failure. Call `UpdateSlangChain()` where `presentation_->UpdatePostShader` is already called (on config/resize change).

- [ ] **Step 3: Delegate in the present path**

In `CopyDisplayToOutput` (`FramebufferManagerCommon.cpp:1580`), before the normal `presentation_->SourceFramebuffer(...); presentation_->RunPostshaderPasses(...)` sequence: if `slangChain_ && slangChain_->IsValid()`, run `Draw::Framebuffer *out = slangChain_->Run(vfb->fbo, vfb->bufferWidth, vfb->bufferHeight, <display w>, <display h>, gpuStats.numFlips)`, then feed `out` into `presentation_->SourceFramebuffer(out, outW, outH)` and call `presentation_->RunPostshaderPasses` with **no legacy post-shader active** (the slang chain replaces it) so the existing blit-to-screen (aspect/rotation/insets) is reused unchanged. Mutual exclusivity: when a slang preset is active, skip building the legacy post-shader chain.

- [ ] **Step 4: DeviceLost/DeviceRestore wiring**

In `FramebufferManagerCommon::DeviceLost()` / `DeviceRestore()`, forward to `slangChain_->DeviceLost()` / `DeviceRestore(draw)` if non-null (next to the existing `presentation_` handling).

- [ ] **Step 5: Build and smoke-check compile**

Build the main target (Vulkan). Expected: compiles clean, no behavioral change when `sSlangShaderPreset` is empty (the default), so all existing post-shaders keep working.

- [ ] **Step 6: Commit**

```bash
git add Core/Config.h Core/Config.cpp GPU/Common/FramebufferManagerCommon.h GPU/Common/FramebufferManagerCommon.cpp
git commit -m "slang: wire filter chain into present path behind config flag

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 9: End-to-end on-device verification

**Files:**
- Create: `assets/shaders/slang_test/stock.slang`, `assets/shaders/slang_test/stock.slangp` (test fixtures, single-pass identity-with-tint)
- No production code changes (verification task).

**Interfaces:** none produced; consumes the whole Phase 1 stack.

- [ ] **Step 1: Author a minimal single-pass fixture**

`stock.slang` — the spec's reference StockShader (from the design doc §6, the `#version 450` example with `MVP`, `SourceSize`, `ColorMod`). `stock.slangp`:
```ini
shaders = 1
shader0 = stock.slang
filter_linear0 = true
scale_type0 = viewport
scale0 = 1.0
```

- [ ] **Step 2: Author a two-pass fixture**

A second preset chaining two passes (e.g. horizontal then vertical blur, or stock→stock) to exercise intermediate framebuffer ping-pong and per-pass resolution. `shaders = 2` with a `source`-scaled pass 0 and a `viewport`-scaled pass 1.

- [ ] **Step 3: Run PPSSPP on Vulkan with the fixture active**

Use the `run` skill (or build + launch the Vulkan build). Set `sSlangShaderPreset` to the fixture path (via config file or the Phase 4 UI if landed; for Phase 1, edit `ppsspp.ini` directly). Load any game/homebrew and confirm:
- the single-pass preset renders (tint visible, `ColorMod` applied);
- the two-pass preset renders without validation errors;
- setting `sSlangShaderPreset = ""` restores the normal image and legacy post-shaders still work.

- [ ] **Step 4: Verify with Vulkan validation layers**

Run with validation layers enabled (PPSSPP has a Vulkan validation toggle in developer settings). Expected: no errors/warnings from the slang framebuffer/pipeline/descriptor usage.

- [ ] **Step 5: Run the full unit-test suite**

```bash
./unitTest
```
Expected: all tests pass, including `SlangParser`, `SlangSplit`, `SlangResolution`, `SlangSemantics`, `SlangReflection`, and no regression in existing tests.

- [ ] **Step 6: Commit fixtures**

```bash
git add assets/shaders/slang_test/stock.slang assets/shaders/slang_test/stock.slangp
git commit -m "slang: add Phase 1 on-device test fixtures

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Phase 1 Done Criteria

- All six unit tests pass (`SlangParser`, `SlangSplit`, `SlangResolution`, `SlangSemantics`, `SlangReflection`, plus the existing suite green).
- A hand-placed single-pass and two-pass slang preset render correctly on the Vulkan backend with no validation-layer errors.
- With `sSlangShaderPreset` empty, behavior is identical to before (legacy post-shaders unaffected).
- Unsupported slang features (LUTs, history, feedback, PassOutput#) cause a **loud compile failure with a clear error**, not silent wrong rendering.

## What Phase 1 deliberately excludes (later phases)

- Download/unpack/import from the buildbot (Phase 3).
- `SlangPresetLibrary` + category browsing UI + parameter sliders (Phase 4).
- `OriginalHistory#`, `PassFeedback#`, `PassOutput#`/aliases, LUT PNG textures, sRGB/float framebuffers, mipmaps, wrap modes, `frame_count_mod` (Phase 2).
- D3D11 / OpenGL / GLES backends (Phase 5).
