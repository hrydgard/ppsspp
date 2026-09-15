# Coarse-pitch PSP LCD grid shader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship two `.slangp` presets that draw cgwg's `lcd-grid-v2` LCD grid at a coarser pitch — one fixed at 2×, one with a runtime-tunable `PITCH` — so the grid stays visible on the AYN Thor's 367 PPI 6" panel, and fix the parser bug that would otherwise reset their preset-level parameter values.

**Architecture:** Three text files laid out as an overlay over an imported libretro `slang-shaders` pack, plus one small engine fix. The overlay's only new shader is a fork of `handheld/shaders/lcd-cgwg/lcd-grid-v2.slang` keyed to `SourceSize` instead of `OriginalSize` and taking a `PITCH` parameter, which is what allows a `b-spline-4-taps` resampling pass (lifted from `crt-royale-downsample.slangp`) to set the LCD cell pitch. The engine fix makes `.slangp`-level parameter values reach `GetPresetParameters()`, without which opening the shader parameter screen overwrites them with the shaders' `#pragma parameter` defaults.

**Tech Stack:** RetroArch slang shaders (GLSL 450 + `#pragma` directives), `.slangp` multi-pass presets, librashader via PPSSPP's `LibrashaderFilterChain`, C++17 for the parser fix, PPSSPP's `unittest/` harness.

**Spec:** `docs/superpowers/specs/2026-09-15-lcd-grid-hidpi-shader-design.md`

## Global Constraints

- **Branch:** `feature/lcd-grid-hidpi-shader`. Already checked out, already holds the spec commit. Do not work on `master`. Never `git push` without asking the user first.
- **Commit messages:** no session marker of any kind (no `Claude-Session:` trailer, no bare `https://claude.ai/code/session_...` line). End with a blank line then `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`. Run `git log -1` after committing to confirm. Present the message in chat for review before committing.
- **Line endings:** all new files are LF. Existing repo assets under `assets/shaders/` are LF; `unittest/TestSlangParser.cpp` is LF. Never convert a file's endings — check `git diff --stat` before committing, a whole-file rewrite is obvious there.
- **Indentation:** PPSSPP C++ is 4-wide **tabs**. The forked `.slang` file keeps **upstream's 4 spaces** and brace style deliberately, so the diff against upstream stays readable — do not retab it.
- **Do not modify any file under `/tmp/slang-shaders`.** It is the reference pack, read-only. The deliverable is an overlay; the upstream reference preset and every other pack file stay untouched.
- **Upstream provenance:** libretro `slang-shaders`, commit `4ecd48510e4a0f936617c5e899dd6c4fd50abbd7`, cloned at `/tmp/slang-shaders`. The fork's header records this. `lcd-grid-v2.slang` is cgwg's, GPL-2.0-or-later — the fork stays GPL-2.0-or-later with attribution and a note of what changed.
- **Target device numbers (tuning basis, spec §3):** AYN Thor, 6.0" 1920×1080 AMOLED, 367 PPI, 0.0692 mm pixel pitch. PPSSPP aspect-fits 480×272 at **1906×1080**, so one PSP pixel is **3.97 display px = 0.275 mm**. Default pitch **2× = 240×136**, giving 7.94 display px per cell and 2.65 px (0.183 mm) per subpixel stripe.
- **`PITCH = 1.0` must reproduce upstream `lcd-grid-v2` exactly.** That is what lets one forked shader serve both variants; any change that breaks it is a bug.
- **Preset parameter values, copied verbatim from the reference preset** (`presets/handheld-plus-color-mod/lcd-grid-v2-psp-color.slangp`) — identity subpixel matrix `RSUBPIX_R=1 RSUBPIX_G=0 RSUBPIX_B=0 GSUBPIX_R=0 GSUBPIX_G=1 GSUBPIX_B=0 BSUBPIX_R=0 BSUBPIX_G=0 BSUBPIX_B=1`, plus `gain=1 gamma=2.2 blacklevel=0 ambient=0 BGR=0`. These must appear in both new presets unchanged.
- **No new build-file registration.** `CMakeLists.txt:1287` installs `assets/shaders` as a whole directory, so new files under it need no build-system edit. The parser fix touches only files already in every build.
- **Build:** `cd build-unittest && make -j32` (already configured with `UNITTEST=ON`; it also produces `PPSSPPSDL.app`). Unit tests: `build-unittest/PPSSPPUnitTest all`, or a subset by name, e.g. `build-unittest/PPSSPPUnitTest SlangPresetParameters`.

---

## File Structure

**Created (the overlay — mirrors the pack's layout so it installs with one recursive copy):**

| File | Responsibility |
|---|---|
| `assets/shaders/slang_overlay/handheld/shaders/lcd-cgwg/lcd-grid-v2-pitch.slang` | The forked grid shader. Owns the cell geometry and the `PITCH` parameter. The only derivative work here. |
| `assets/shaders/slang_overlay/presets/handheld-plus-color-mod/lcd-grid-v2-psp-color-hidpi-2x.slangp` | Variant A. Pitch fixed at 2× by an absolute 240×136 downsample pass. |
| `assets/shaders/slang_overlay/presets/handheld-plus-color-mod/lcd-grid-v2-psp-color-hidpi-tunable.slangp` | Variant B. Pitch set live by `PITCH`, cells averaged through the mip chain. |
| `assets/shaders/slang_overlay/README.md` | What the overlay is, the imported-pack prerequisite, how to install it, provenance and licence. |

**Modified (the §9.1 parser fix):**

| File | Change |
|---|---|
| `GPU/Common/Slang/SlangPreset.h` | Replace the vacant `params` vector with `std::map<std::string, std::string> values`. |
| `GPU/Common/Slang/SlangpParser.cpp` | Populate `values` from the preset's key/value lines. |
| `Core/Slang/SlangPresetLibrary.cpp` | In `GetPresetParameters()`, let a preset-level value override a declared parameter's `initial`. |
| `unittest/TestSlangParser.cpp` | Extend `TestSlangPresetParameters` to cover the override and the deliberate no-clamp behaviour. |

Task 1 is the engine fix and stands alone. Tasks 2 and 3 each ship one runnable preset. Task 4 is the on-device acceptance gate. Task 1 must land before Task 3's `PITCH = "2.0"` means anything in the UI.

---

### Task 1: Preset-level parameter values reach the UI

Spec §9.1. Today `ParseSlangPreset` drops every `.slangp`-level parameter value, `SlangShaderScreen` seeds `mSlangParams` from the shaders' `#pragma parameter` defaults, and `FramebufferManagerCommon` pushes all of those as overrides — so opening the parameter screen overwrites what the preset asked for. librashader honours preset values when it builds the chain, so the preset renders correctly right up until the user looks at its parameters.

**Files:**
- Modify: `GPU/Common/Slang/SlangPreset.h:20-21` (includes) and `:77-83` (the struct)
- Modify: `GPU/Common/Slang/SlangpParser.cpp:47-142` (`ParseSlangPreset`)
- Modify: `Core/Slang/SlangPresetLibrary.cpp:24-26` (includes) and `:128` onward (`GetPresetParameters`)
- Test: `unittest/TestSlangParser.cpp:379-402` (`TestSlangPresetParameters`)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `SlangPreset::values` — `std::map<std::string, std::string>`, every `key = value` line in the preset, values unquoted, keys stripped of surrounding spaces. `GetPresetParameters(const Path &presetPath, std::vector<SlangParamDesc> *out, std::string *error) -> bool` keeps its signature; only the `initial` field of returned descriptors changes behaviour. `SlangPreset::params` is **removed** — it had exactly one reader, `*out = preset.params;` at `SlangPresetLibrary.cpp:149`, and was always empty.

- [ ] **Step 1: Write the failing test**

Append to `TestSlangPresetParameters` in `unittest/TestSlangParser.cpp`, immediately before the closing `File::DeleteDirRecursively(root);` and `return true;` (the fixture's `a.slang` declares `gamma` with default 2.2, range 1.0–3.0, step 0.1, description "Gamma", and `bright` with default 1.0):

```cpp
	// A .slangp-level value overrides the shader's #pragma parameter default. librashader applies
	// preset values when it builds the chain, so the UI has to start from the same number: it seeds
	// g_Config.mSlangParams from `initial` and FramebufferManagerCommon pushes every seeded entry back
	// as an override, so a stale `initial` silently overwrites what the preset asked for. Range, step
	// and description keep coming from the declaration - only the value moves.
	File::WriteStringToFile(true,
		"shaders = 1\n"
		"shader0 = a.slang\n"
		"gamma = \"2.5\"\n", root / "override.slangp");
	std::vector<SlangParamDesc> overridden; std::string overrideErr;
	EXPECT_TRUE(GetPresetParameters(root / "override.slangp", &overridden, &overrideErr));
	EXPECT_EQ_INT((int)overridden.size(), 2);   // structural keys are not parameters
	EXPECT_TRUE(overridden[0].name == "gamma");
	EXPECT_EQ_FLOAT(overridden[0].initial, 2.5f);
	EXPECT_EQ_FLOAT(overridden[0].minimum, 1.0f);
	EXPECT_EQ_FLOAT(overridden[0].maximum, 3.0f);
	EXPECT_EQ_FLOAT(overridden[0].step, 0.1f);
	std::string overriddenDesc = overridden[0].description;
	std::string expectDesc = "Gamma";
	EXPECT_EQ_STR(overriddenDesc, expectDesc);
	EXPECT_EQ_FLOAT(overridden[1].initial, 1.0f);   // a parameter the preset does not mention

	// A value outside the declared range is kept as-is rather than clamped: it is what librashader
	// will use, and clamping here would make the slider disagree with the rendered image.
	File::WriteStringToFile(true,
		"shaders = 1\n"
		"shader0 = a.slang\n"
		"gamma = \"9.0\"\n", root / "wide.slangp");
	std::vector<SlangParamDesc> wide; std::string wideErr;
	EXPECT_TRUE(GetPresetParameters(root / "wide.slangp", &wide, &wideErr));
	EXPECT_EQ_FLOAT(wide[0].initial, 9.0f);
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd build-unittest && make -j32 PPSSPPUnitTest && cd ..
build-unittest/PPSSPPUnitTest SlangPresetParameters
```

Expected: FAIL, at the `EXPECT_EQ_FLOAT(overridden[0].initial, 2.5f)` line, printing `2.2000000` vs `2.5000000`. That is the bug: the preset's value was parsed into the parser's internal map and dropped. If it fails earlier or passes, stop and re-read the fixture — the rest of this task assumes this exact failure.

- [ ] **Step 3: Replace the vacant `params` vector with a raw value map**

In `GPU/Common/Slang/SlangPreset.h`, add `<map>` to the includes:

```cpp
#include <map>
#include <string>
#include <vector>
```

and in `struct SlangPreset`, replace the `params` line with `values`:

```cpp
struct SlangPreset {
	Path basePath;   // directory the .slangp lives in
	std::vector<SlangPassDesc> passes;
	// Every `key = value` line in the preset, unparsed. A .slangp may set any shader parameter by
	// name (`gamma = "2.2"`), which librashader honours when it builds the chain - but the
	// authoritative parameter *names* live in the pass shaders' #pragma parameter lines, so keep the
	// raw lines here and let GetPresetParameters() resolve them per declared parameter. That is also
	// what keeps structural keys (shader0, scale_type1, LUT names, ...) from being read as parameters.
	std::map<std::string, std::string> values;
	std::vector<SlangLutDesc> luts;
	int feedbackPass = -1;   // global feedback_pass; -1 = none
};
```

- [ ] **Step 4: Populate it in the parser**

In `GPU/Common/Slang/SlangpParser.cpp`, in `ParseSlangPreset`, change the `out->params.clear();` line (line 50) to:

```cpp
	out->values.clear();
```

and directly after the `while (std::getline(ss, line))` loop that builds `kv` — i.e. immediately before `auto it = kv.find("shaders");` — add:

```cpp
	// Keep the raw lines for GetPresetParameters(); a one-time copy per preset load.
	out->values = kv;
```

- [ ] **Step 5: Apply the override in `GetPresetParameters`**

In `Core/Slang/SlangPresetLibrary.cpp`, add `<cstdlib>` to the includes:

```cpp
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <set>
```

Delete these two now-dangling lines (the vector is gone, and `out` was already cleared at the top of the function):

```cpp
	// Seed output with preset-level parameters (usually empty)
	*out = preset.params;
```

Then, after the `for (const auto &pass : preset.passes)` loop and immediately before the closing `return true;`, add:

```cpp
	// A .slangp may override any declared parameter's value by name (`gamma = "2.2"`), and librashader
	// applies those when it builds the chain - so the UI has to start from the same number. Otherwise
	// SlangShaderScreen seeds g_Config.mSlangParams with the pragma default and FramebufferManagerCommon
	// pushes it straight back over the preset's value. Looking names up (rather than scanning the
	// preset for parameter-shaped keys) is what keeps shader0/scale_type1/LUT names out of the list:
	// the pass shaders own the authoritative name set. A value outside the declared range is kept
	// as-is, because that is what librashader will use and clamping would make the UI disagree with
	// the rendered image.
	for (SlangParamDesc &param : *out) {
		auto valueIt = preset.values.find(param.name);
		if (valueIt != preset.values.end()) {
			param.initial = (float)atof(valueIt->second.c_str());
		}
	}
```

- [ ] **Step 6: Run the test to verify it passes**

```bash
cd build-unittest && make -j32 PPSSPPUnitTest && cd ..
build-unittest/PPSSPPUnitTest SlangPresetParameters
```

Expected: PASS.

- [ ] **Step 7: Run the whole suite for regressions**

```bash
build-unittest/PPSSPPUnitTest all
```

Expected: no new failures. Pay attention to the other `Slang*` tests — `SlangParser`, `SlangParserPhase2Keys`, `SlangParserLuts`, `SlangPresetLibrary` — since they all construct a `SlangPreset`.

- [ ] **Step 8: Commit**

Present the message in chat for review first, then:

```bash
git add GPU/Common/Slang/SlangPreset.h GPU/Common/Slang/SlangpParser.cpp Core/Slang/SlangPresetLibrary.cpp unittest/TestSlangParser.cpp
git diff --cached --stat   # sanity: four files, small diffs, no whole-file rewrite
git commit -m "$(cat <<'EOF'
slang: honour .slangp-level parameter values

A preset's own parameter values (`gamma = "2.2"`) were parsed into the key/value
map and dropped, so GetPresetParameters returned only the shaders' #pragma
defaults. The parameter screen seeds g_Config.mSlangParams from those and the
framebuffer manager pushes every seeded entry back as an override, so opening the
screen silently overwrote what the preset asked for - the stock
lcd-grid-v2-psp-color preset moved from gamma 2.2 to 3.0 and blacklevel 0 to 0.05
on arrival.

Keep the raw lines on SlangPreset and resolve them per declared parameter, so
structural keys can't be mistaken for parameters. Range, step and description
still come from the declaration; an out-of-range value is kept as-is, since that
is what librashader uses.

Replaces SlangPreset::params, which was never populated and had one reader.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
git log -1   # confirm: no session marker
```

---

### Task 2: The forked shader and variant A (fixed 2× pitch)

Spec §5 and §6. The fork is the enabling piece; variant A is the first preset that uses it, with the pitch set by an absolute 240×136 `b-spline-4-taps` pass so the grid draws one cell per downsampled pixel (`PITCH = 1.0`).

**Files:**
- Create: `assets/shaders/slang_overlay/handheld/shaders/lcd-cgwg/lcd-grid-v2-pitch.slang`
- Create: `assets/shaders/slang_overlay/presets/handheld-plus-color-mod/lcd-grid-v2-psp-color-hidpi-2x.slangp`
- Create: `assets/shaders/slang_overlay/README.md`
- Reference (read-only): `/tmp/slang-shaders/handheld/shaders/lcd-cgwg/lcd-grid-v2.slang`, `/tmp/slang-shaders/presets/handheld-plus-color-mod/lcd-grid-v2-psp-color.slangp`, `/tmp/slang-shaders/presets/crt-royale-downsample.slangp`

**Interfaces:**
- Consumes: Task 1's fix, so the preset's `gamma = "2.2"` and the identity subpixel matrix survive a visit to the parameter screen. Nothing else.
- Produces: the shader's parameter contract for Task 3 — `PITCH`, a `float` at the **end** of the `push_constant` block, declared `#pragma parameter PITCH "LCD cell pitch (source px)" 1.0 1.0 3.0 0.25`. The relative path both presets reference it by is `../../handheld/shaders/lcd-cgwg/lcd-grid-v2-pitch.slang`.

- [ ] **Step 1: Create the forked shader**

Write `assets/shaders/slang_overlay/handheld/shaders/lcd-cgwg/lcd-grid-v2-pitch.slang`. This is the complete file — 4 spaces, not tabs:

```glsl
#version 450

// lcd-grid-v2-pitch.slang - a fork of handheld/shaders/lcd-cgwg/lcd-grid-v2.slang from the libretro
// slang-shaders pack (commit 4ecd48510e4a0f936617c5e899dd6c4fd50abbd7), by cgwg.
// GPL-2.0-or-later, as the original.
//
// Draws the LCD cell grid coarser than one cell per source pixel, so it stays visible on a physically
// small high-PPI panel. Design notes, including why one cell per PSP pixel washes out at 367 PPI:
// PPSSPP's docs/superpowers/specs/2026-09-15-lcd-grid-hidpi-shader-design.md
//
// Three changes from upstream, and nothing else:
//   1. The grid is keyed to SourceSize, not OriginalSize. OriginalSize is the size of the *chain
//      input*, so upstream keeps drawing a 480x272 grid when a resampling pass is put in front of it,
//      while sampling the resampled image with 480x272 texel steps. SourceSize follows the previous
//      pass, which is what lets that pass set the cell pitch.
//   2. A PITCH parameter: a cell is PITCH source pixels wide, and its colour is the average of the
//      block it covers, fetched with textureLod. PITCH = 1.0 reproduces upstream exactly.
//   3. fetch_offset was a macro reading the global OriginalSize; the grid metrics are locals now, so
//      it became a function taking them as arguments. It also moved into the fragment stage - a real
//      function referencing Source cannot sit in the prologue, which is shared with the vertex stage.
//
// Upstream's formatting (4 spaces, its brace style) is kept deliberately, to keep the diff readable.

layout(push_constant) uniform Push
{
    float RSUBPIX_R;
    float RSUBPIX_G;
    float RSUBPIX_B;
    float GSUBPIX_R;
    float GSUBPIX_G;
    float GSUBPIX_B;
    float BSUBPIX_R;
    float BSUBPIX_G;
    float BSUBPIX_B;
    float gain;
    float gamma;
    float blacklevel;
    float ambient;
    float BGR;
    float PITCH;
} params;

#pragma parameter RSUBPIX_R  "Colour of R subpixel: R" 1.0 0.0 1.0 0.01
#pragma parameter RSUBPIX_G  "Colour of R subpixel: G" 0.0 0.0 1.0 0.01
#pragma parameter RSUBPIX_B  "Colour of R subpixel: B" 0.0 0.0 1.0 0.01
#pragma parameter GSUBPIX_R  "Colour of G subpixel: R" 0.0 0.0 1.0 0.01
#pragma parameter GSUBPIX_G  "Colour of G subpixel: G" 1.0 0.0 1.0 0.01
#pragma parameter GSUBPIX_B  "Colour of G subpixel: B" 0.0 0.0 1.0 0.01
#pragma parameter BSUBPIX_R  "Colour of B subpixel: R" 0.0 0.0 1.0 0.01
#pragma parameter BSUBPIX_G  "Colour of B subpixel: G" 0.0 0.0 1.0 0.01
#pragma parameter BSUBPIX_B  "Colour of B subpixel: B" 1.0 0.0 1.0 0.01
#pragma parameter gain       "Gain"                    1.0 0.5 2.0 0.05
#pragma parameter gamma      "LCD Gamma"               3.0 0.5 5.0 0.1
#pragma parameter blacklevel "Black level"            0.05 0.0 0.5 0.01
#pragma parameter ambient    "Ambient"                 0.0 0.0 0.5 0.01
#pragma parameter BGR        "BGR"                     0 0 1 1
#pragma parameter PITCH      "LCD cell pitch (source px)" 1.0 1.0 3.0 0.25

layout(std140, set = 0, binding = 0) uniform UBO
{
    mat4 MVP;
    vec4 OutputSize;
    vec4 OriginalSize;
    vec4 SourceSize;
} global;

#define outgamma 2.2

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

// integral of (1 - x^2 - x^4 + x^6)^2
float coeffs_x[7] = float[](1.0, -2.0/3.0, -1.0/5.0, 4.0/7.0, -1.0/9.0, -2.0/11.0, 1.0/13.0);
// integral of (1 - 2x^4 + x^6)^2
float coeffs_y[7] = float[](1.0,      0.0, -4.0/5.0, 2.0/7.0,  4.0/9.0, -4.0/11.0, 1.0/13.0);

float intsmear_func(float z, float coeffs[7])
{
    float z2 = z*z;
    float zn = z;
    float ret = 0.0;
    for (int i = 0; i < 7; i++) {
        ret += zn*coeffs[i];
        zn *= z2;
    }
    return ret;
}

float intsmear(float x, float dx, float d, float coeffs[7])
{
    float zl = clamp((x-dx*0.5)/d,-1.0,1.0);
    float zh = clamp((x+dx*0.5)/d,-1.0,1.0);
    return d * ( intsmear_func(zh,coeffs) - intsmear_func(zl,coeffs) )/dx;
}

// Upstream's fetch_offset macro. texelSize is one cell rather than one texel now, and lod picks the
// mip level whose texels are that cell, so a cell shows the average of the block it covers instead of
// one pixel out of it. At lod 0 this is exactly upstream's texture() fetch.
vec3 fetch_cell(ivec2 coord, ivec2 offset, vec2 texelSize, float lod)
{
    vec3 c = textureLod(Source, (vec2(coord + offset) + 0.5) * texelSize, lod).rgb;
    return pow(vec3(params.gain) * c + vec3(params.blacklevel), vec3(params.gamma)) + vec3(params.ambient);
}

void main()
{
    float pitch = max(params.PITCH, 1.0);
    vec2 texelSize = global.SourceSize.zw * pitch;
    float lod = log2(pitch);
    /* float2 range = IN.video_size / (IN.output_size * IN.texture_size); */
    vec2 range = global.OutputSize.zw;

    vec3 cred   = pow(vec3(params.RSUBPIX_R, params.RSUBPIX_G, params.RSUBPIX_B), vec3(outgamma));
    vec3 cgreen = pow(vec3(params.GSUBPIX_R, params.GSUBPIX_G, params.GSUBPIX_B), vec3(outgamma));
    vec3 cblue  = pow(vec3(params.BSUBPIX_R, params.BSUBPIX_G, params.BSUBPIX_B), vec3(outgamma));

    ivec2 tli = ivec2(floor(vTexCoord/texelSize-vec2(0.4999)));

    vec3 lcol, rcol;
    float subpix = (vTexCoord.x/texelSize.x - 0.4999 - float(tli.x))*3.0;
    float rsubpix = range.x/texelSize.x * 3.0;

    lcol = vec3(intsmear(subpix+1.0, rsubpix, 1.5, coeffs_x),
                intsmear(subpix    , rsubpix, 1.5, coeffs_x),
                intsmear(subpix-1.0, rsubpix, 1.5, coeffs_x));
    rcol = vec3(intsmear(subpix-2.0, rsubpix, 1.5, coeffs_x),
                intsmear(subpix-3.0, rsubpix, 1.5, coeffs_x),
                intsmear(subpix-4.0, rsubpix, 1.5, coeffs_x));

    if (params.BGR > 0.5) {
        lcol.rgb = lcol.bgr;
        rcol.rgb = rcol.bgr;
    }

    float tcol, bcol;
    subpix = vTexCoord.y/texelSize.y - 0.4999 - float(tli.y);
    rsubpix = range.y/texelSize.y;
    tcol = intsmear(subpix    ,rsubpix, 0.63, coeffs_y);
    bcol = intsmear(subpix-1.0,rsubpix, 0.63, coeffs_y);

    vec3 topLeftColor     = fetch_cell(tli, ivec2(0,0), texelSize, lod) * lcol * vec3(tcol);
    vec3 bottomRightColor = fetch_cell(tli, ivec2(1,1), texelSize, lod) * rcol * vec3(bcol);
    vec3 bottomLeftColor  = fetch_cell(tli, ivec2(0,1), texelSize, lod) * lcol * vec3(bcol);
    vec3 topRightColor    = fetch_cell(tli, ivec2(1,0), texelSize, lod) * rcol * vec3(tcol);

    vec3 averageColor = topLeftColor + bottomRightColor + bottomLeftColor + topRightColor;

    averageColor = mat3(cred, cgreen, cblue) * averageColor;

    FragColor = vec4(pow(averageColor, vec3(1.0/outgamma)),0.0);
}
```

- [ ] **Step 2: Verify the fork against upstream**

```bash
git diff --no-index --stat /tmp/slang-shaders/handheld/shaders/lcd-cgwg/lcd-grid-v2.slang \
    assets/shaders/slang_overlay/handheld/shaders/lcd-cgwg/lcd-grid-v2-pitch.slang
git diff --no-index /tmp/slang-shaders/handheld/shaders/lcd-cgwg/lcd-grid-v2.slang \
    assets/shaders/slang_overlay/handheld/shaders/lcd-cgwg/lcd-grid-v2-pitch.slang
```

Expected: the diff contains **only** these changes, and nothing else — no reformatting, no whitespace churn, no reordered pragmas.

1. The header comment block added after `#version 450`.
2. `float PITCH;` appended as the last member of the `Push` block.
3. The `#pragma parameter PITCH ...` line appended after the `BGR` pragma.
4. The `#define fetch_offset(...)` line removed.
5. `vec3 fetch_cell(...)` added in the fragment stage, after `intsmear` and before `main`.
6. In `main`: the `pitch`/`texelSize`/`lod` lines replacing `vec2 texelSize = global.OriginalSize.zw;`, and the four `fetch_offset(tli, ...)` calls becoming `fetch_cell(tli, ..., texelSize, lod)`.

Then confirm the `PITCH = 1.0` equivalence by reading, not running: `pitch = 1.0` gives `texelSize = global.SourceSize.zw` and `lod = 0.0`; `SourceSize` equals `OriginalSize` when nothing precedes the pass, and `textureLod(..., 0.0)` on a nearest sampler equals `texture(...)`. Everything downstream of `texelSize` is untouched.

- [ ] **Step 3: Create variant A**

Write `assets/shaders/slang_overlay/presets/handheld-plus-color-mod/lcd-grid-v2-psp-color-hidpi-2x.slangp`:

```
# Coarse-pitch PSP LCD grid, fixed 2x pitch - for physically small high-PPI panels.
# Derived from presets/handheld-plus-color-mod/lcd-grid-v2-psp-color.slangp, with
# crt-royale-downsample.slangp's b-spline pass prepended to set the LCD cell pitch.
# Design notes: PPSSPP's docs/superpowers/specs/2026-09-15-lcd-grid-hidpi-shader-design.md
#
# Requires the libretro slang-shaders pack: every path below points into it.

shaders = "4"

# Pass 0 sets the cell pitch. 240x136 is exactly half of 480x272, so cells land on integer PSP-pixel
# pairs with no beat against the source grid. On PPSSPP this is also a genuine downsample of the
# internal-resolution render, since the chain is handed the upscaled target with a native declared
# size - so raising the internal resolution improves this pass's antialiasing for free.
shader0 = "../../interpolation/shaders/b-spline-4-taps.slang"
filter_linear0 = "true"
wrap_mode0 = "clamp_to_edge"
scale_type_x0 = "absolute"
scale_x0 = "240"
scale_type_y0 = "absolute"
scale_y0 = "136"

shader1 = "../../reshade/shaders/LUT/multiLUT.slang"
filter_linear1 = "false"
scale_type1 = "source"
scale1 = "1.0"

# One cell per pass-0 pixel: 7.94 display px per cell on a 6" 1080p panel.
# clamp_to_edge because the grid fetches one cell right and down, and clamp_to_border would blend
# black into the right and bottom edges - PITCH pixels wide at coarser pitches.
shader2 = "../../handheld/shaders/lcd-cgwg/lcd-grid-v2-pitch.slang"
filter_linear2 = "false"
wrap_mode2 = "clamp_to_edge"
scale_type2 = "viewport"
scale2 = "1.0"

shader3 = "../../handheld/shaders/color/psp-color.slang"
filter_linear3 = "false"
scale_type3 = "source"
scale3 = "1.0"

textures = "SamplerLUT1;SamplerLUT2"
SamplerLUT1 = "../../handheld/shaders/color/lut/psp-grey1.png"
SamplerLUT1_linear = "true"
SamplerLUT2 = "../../handheld/shaders/color/lut/psp-grey2.png"
SamplerLUT2_linear = "true"

# Pass 0 already set the pitch, so the grid draws one cell per pixel it receives.
PITCH = "1.0"
RSUBPIX_R = "1"
RSUBPIX_G = "0"
RSUBPIX_B = "0"
GSUBPIX_R = "0"
GSUBPIX_G = "1"
GSUBPIX_B = "0"
BSUBPIX_R = "0"
BSUBPIX_G = "0"
BSUBPIX_B = "1"
gain = "1"
gamma = "2.2"
blacklevel = "0"
ambient = "0"
BGR = "0"
```

- [ ] **Step 4: Create the overlay README**

Write `assets/shaders/slang_overlay/README.md`:

````markdown
# Coarse-pitch PSP LCD grid overlay

An overlay for an imported libretro [slang-shaders](https://github.com/libretro/slang-shaders) pack,
adding LCD grid presets whose cell pitch is coarser than one cell per PSP pixel. At 480x272 on a small
high-PPI panel one cell is under 4 display pixels wide, so `lcd-grid-v2`'s subpixel stripes fall below
what the eye resolves and the grid washes out to a flat dimming. These presets draw larger cells.

Tuned for a 6" 1920x1080 panel (367 PPI, e.g. the AYN Thor), where PPSSPP fits 480x272 at 1906x1080 -
3.97 display px per PSP pixel. Design notes and the pitch arithmetic:
`docs/superpowers/specs/2026-09-15-lcd-grid-hidpi-shader-design.md`.

## Prerequisite

**The libretro slang-shaders pack must already be imported.** Every path in these presets points into
it - `b-spline-4-taps`, `multiLUT`, `psp-color` and the two `psp-grey` LUTs are all pack files. Without
the pack the presets do not appear in the shader browser. They are also not loadable from inside the
PPSSPP install: the relative paths only resolve once this tree is merged into the pack.

## Installing

Copy the contents of this directory over the imported pack, merging directories:

- Windows: `<memstick>\PSP\SHADERS\slang\`
- macOS/Linux: `<memstick>/PSP/SHADERS/slang/` (a pre-existing lower-case `shaders/` is used instead)
- Android: the app-private external files dir, `<extFilesDir>/slang/`

```bash
cp -R assets/shaders/slang_overlay/. "<memstick>/PSP/SHADERS/slang/"
```

Nothing is overwritten - every file added here is new.

## Presets

| Preset | Pitch |
|---|---|
| `presets/handheld-plus-color-mod/lcd-grid-v2-psp-color-hidpi-2x.slangp` | Fixed 2x. A `b-spline-4-taps` pass resamples to an absolute 240x136 and the grid draws one cell per pixel. |

The colour path is the reference `lcd-grid-v2-psp-color` preset's, unchanged: `multiLUT` with the PSP
grey LUTs, then `psp-color`, with the same identity subpixel matrix and tone parameters.

Raising PPSSPP's internal resolution improves the downsample pass's antialiasing at no cost to the
grid; 3x-5x is a good range.

## Provenance and licence

`handheld/shaders/lcd-cgwg/lcd-grid-v2-pitch.slang` is a fork of the pack's
`handheld/shaders/lcd-cgwg/lcd-grid-v2.slang` by cgwg (commit
`4ecd48510e4a0f936617c5e899dd6c4fd50abbd7`), GPL-2.0-or-later like the original, with three localised
changes recorded in its header. The presets reference pack shaders without copying them.
````

- [ ] **Step 5: Install the overlay and run it**

The compile-and-render gate. There is no local `glslangValidator`/`glslc`, so librashader's own compile inside a running PPSSPP is the check — `build-unittest/PPSSPPSDL.app/Contents/MacOS/librashader.dylib` is already built.

```bash
# One-time: a writable copy of the pack as the imported slang root.
mkdir -p ~/.config/ppsspp/PSP/SHADERS/slang
cp -R /tmp/slang-shaders/. ~/.config/ppsspp/PSP/SHADERS/slang/
# The overlay:
cp -R assets/shaders/slang_overlay/. ~/.config/ppsspp/PSP/SHADERS/slang/
ls ~/.config/ppsspp/PSP/SHADERS/slang/presets/handheld-plus-color-mod/ | grep hidpi
cd build-unittest && make -j32 && cd ..
open build-unittest/PPSSPPSDL.app
```

In the app: load any game, then Settings → Graphics → Postprocessing (slang shader) → `handheld-plus-color-mod` → `lcd-grid-v2-psp-color-hidpi-2x`.

Expected:
- The preset appears in the browser and selecting it changes the image.
- No `librashader preset load failed` / `create:` / `frame:` error in the log (Debug → Log console, or run the binary from a terminal to see stderr).
- A visibly coarser grid than the stock `lcd-grid-v2-psp-color` preset — switch between the two.
- Open Postprocessing → the parameter screen. `LCD Gamma` reads **2.2** and `Black level` **0**, not 3.0/0.05: Task 1 working. `LCD cell pitch (source px)` reads 1.0. Leave the screen and confirm the image did not change.

If the preset fails to compile, the log line names the pass and the error. Do not proceed to Task 3 until it renders.

- [ ] **Step 6: Commit**

Present the message in chat for review first, then:

```bash
git add assets/shaders/slang_overlay
git status --short   # sanity: three new files under assets/shaders/slang_overlay, nothing else
git commit -m "$(cat <<'EOF'
shaders: coarse-pitch LCD grid overlay, fixed 2x preset

lcd-grid-v2 draws one cell per source pixel, which on a 6" 1080p panel is 3.97
display px - the subpixel stripes land at 0.09 mm and the grid washes out to a
flat dimming. Fork it to key the grid off SourceSize instead of OriginalSize
(OriginalSize is the chain input, so upstream cannot have a resampling pass in
front of it) and to take a PITCH parameter, then prepend crt-royale-downsample's
b-spline pass at an absolute 240x136 to set the pitch. PITCH = 1.0 reproduces
upstream exactly.

An overlay over an imported slang-shaders pack, not a bundled asset set: the
colour path is the reference lcd-grid-v2-psp-color preset's, referenced not copied.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
git log -1   # confirm: no session marker
```

---

### Task 3: Variant B (runtime-tunable pitch)

Spec §7. Same four passes and the same colour path, but the pitch comes from `PITCH` at runtime instead of from pass 0's output size. Pass 0 stays — at `source 1.0` it still resamples PPSSPP's upscaled render down to native 480×272, so it is the antialiasing stage rather than the pitch stage (spec §3.4). The grid pass now needs mips, because a cell wider than one texel has to average the block it covers.

**Files:**
- Create: `assets/shaders/slang_overlay/presets/handheld-plus-color-mod/lcd-grid-v2-psp-color-hidpi-tunable.slangp`
- Modify: `assets/shaders/slang_overlay/README.md` (add the preset to the table)

**Interfaces:**
- Consumes: Task 2's `lcd-grid-v2-pitch.slang` at `../../handheld/shaders/lcd-cgwg/lcd-grid-v2-pitch.slang`, with `PITCH` in range 1.0–3.0, step 0.25. Task 1's fix, which is what makes `PITCH = "2.0"` hold once the parameter screen has been opened.
- Produces: nothing later tasks build on.

- [ ] **Step 1: Create variant B**

Write `assets/shaders/slang_overlay/presets/handheld-plus-color-mod/lcd-grid-v2-psp-color-hidpi-tunable.slangp`. Same as variant A except pass 0's scale, `mipmap_input2`, and `PITCH` — written out in full because it is a separate file, not a diff:

```
# Coarse-pitch PSP LCD grid, pitch tunable at runtime - for physically small high-PPI panels.
# Derived from presets/handheld-plus-color-mod/lcd-grid-v2-psp-color.slangp, with
# crt-royale-downsample.slangp's b-spline pass prepended as the antialiasing stage.
# Design notes: PPSSPP's docs/superpowers/specs/2026-09-15-lcd-grid-hidpi-shader-design.md
#
# Requires the libretro slang-shaders pack: every path below points into it.
#
# Set the grid coarseness with the "LCD cell pitch (source px)" parameter. 2.0 is the default and,
# like 1.0, an exact block average; values between snap or blend mip levels, which only softens the
# cell colour slightly - the grid geometry tracks the parameter continuously either way.

shaders = "4"

# Pass 0 resamples PPSSPP's upscaled render down to native 480x272 - a real downsample, because the
# chain is handed the upscaled target with a native declared size. It is the antialiasing stage here,
# not the pitch stage; raising the internal resolution improves it for free.
shader0 = "../../interpolation/shaders/b-spline-4-taps.slang"
filter_linear0 = "true"
wrap_mode0 = "clamp_to_edge"
scale_type0 = "source"
scale0 = "1.0"

shader1 = "../../reshade/shaders/LUT/multiLUT.slang"
filter_linear1 = "false"
scale_type1 = "source"
scale1 = "1.0"

# mipmap_input2 so a cell can be the average of the PITCH x PITCH block it covers, fetched as one
# textureLod per corner. clamp_to_edge because the grid fetches one cell right and down, and
# clamp_to_border would blend black into the right and bottom edges - PITCH pixels wide.
shader2 = "../../handheld/shaders/lcd-cgwg/lcd-grid-v2-pitch.slang"
filter_linear2 = "false"
mipmap_input2 = "true"
wrap_mode2 = "clamp_to_edge"
scale_type2 = "viewport"
scale2 = "1.0"

shader3 = "../../handheld/shaders/color/psp-color.slang"
filter_linear3 = "false"
scale_type3 = "source"
scale3 = "1.0"

textures = "SamplerLUT1;SamplerLUT2"
SamplerLUT1 = "../../handheld/shaders/color/lut/psp-grey1.png"
SamplerLUT1_linear = "true"
SamplerLUT2 = "../../handheld/shaders/color/lut/psp-grey2.png"
SamplerLUT2_linear = "true"

# 7.94 display px per cell on a 6" 1080p panel. Tunable at runtime.
PITCH = "2.0"
RSUBPIX_R = "1"
RSUBPIX_G = "0"
RSUBPIX_B = "0"
GSUBPIX_R = "0"
GSUBPIX_G = "1"
GSUBPIX_B = "0"
BSUBPIX_R = "0"
BSUBPIX_G = "0"
BSUBPIX_B = "1"
gain = "1"
gamma = "2.2"
blacklevel = "0"
ambient = "0"
BGR = "0"
```

- [ ] **Step 2: Add it to the README table**

In `assets/shaders/slang_overlay/README.md`, replace the single-row table with:

```markdown
| Preset | Pitch |
|---|---|
| `presets/handheld-plus-color-mod/lcd-grid-v2-psp-color-hidpi-2x.slangp` | Fixed 2x. A `b-spline-4-taps` pass resamples to an absolute 240x136 and the grid draws one cell per pixel. |
| `presets/handheld-plus-color-mod/lcd-grid-v2-psp-color-hidpi-tunable.slangp` | Set by the `LCD cell pitch (source px)` parameter, 1.0-3.0, default 2.0. Cells are averaged through the mip chain. |
```

- [ ] **Step 3: Install and run both presets**

```bash
cp -R assets/shaders/slang_overlay/. ~/.config/ppsspp/PSP/SHADERS/slang/
open build-unittest/PPSSPPSDL.app
```

Select `lcd-grid-v2-psp-color-hidpi-tunable`. Expected:

- It compiles and renders — no librashader error in the log. A `mipmap_input` failure would show up here; PPSSPP sets `force_no_mipmaps = false` on both the Vulkan and GL runtimes (`LibrashaderRuntimeVulkan.cpp:59`, `LibrashaderRuntimeOpenGL.cpp:53`), so mips are available.
- The parameter screen shows `LCD cell pitch (source px)` = **2.0** (Task 1; it would read 1.0 without the fix), `LCD Gamma` = 2.2, `Black level` = 0.
- Sweeping `PITCH` 1.0 → 3.0 changes the grid coarseness live, and the image stays registered — no drift, no half-cell offset, no crawl of the grid relative to the picture.
- At `PITCH = 2.0` it looks like variant A. They are not pixel-identical: variant A's pass 0 resamples to 240×136 with the b-spline kernel while variant B's resamples to 480×272 and then box-averages 2×2 blocks, so expect variant B to be slightly crisper.
- At `PITCH = 1.0` it looks like the stock `lcd-grid-v2-psp-color` preset apart from pass 0's softening.

- [ ] **Step 4: Commit**

Present the message in chat for review first, then:

```bash
git add assets/shaders/slang_overlay
git status --short
git commit -m "$(cat <<'EOF'
shaders: coarse-pitch LCD grid overlay, runtime-tunable preset

Same four passes and colour path as the 2x preset, but the pitch comes from the
PITCH parameter rather than from pass 0's output size, so it can be dialled in on
the device. Pass 0 stays at source 1.0, where it still resamples PPSSPP's upscaled
render down to native - the antialiasing stage rather than the pitch stage.
mipmap_input on the grid pass so a cell averages the block it covers.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
git log -1   # confirm: no session marker
```

---

### Task 4: On-device acceptance and final regression

Spec §10. The whole point of the feature is perceptual and cannot be settled on a desktop monitor: whether the grid reads as a grid at arm's length on a 367 PPI 6" panel. This task is a manual gate on the AYN Thor, run by the user, plus the repo-side regression check.

Note what does and does not need an app build: **the presets are data**, so an existing PPSSPP install on the device can run them after a file copy. Only the parameter-screen checks (2, 6) need an APK containing Task 1.

**Files:**
- No source changes. Record the outcome in the commit message of any fix this turns up, or report "no changes needed".

**Interfaces:**
- Consumes: everything from Tasks 1–3.
- Produces: nothing.

- [ ] **Step 1: Copy the overlay onto the device**

The Android slang root is the app-private external files dir (`SlangPaths.cpp:36-38`), not the memstick. Confirm the package name first — `org.ppsspp.ppsspp` for the free build, `org.ppsspp.ppssppgold` for Gold:

```bash
adb shell pm list packages | grep ppsspp
adb push /tmp/slang-shaders/. /sdcard/Android/data/org.ppsspp.ppsspp/files/slang/
adb push assets/shaders/slang_overlay/. /sdcard/Android/data/org.ppsspp.ppsspp/files/slang/
adb shell ls /sdcard/Android/data/org.ppsspp.ppsspp/files/slang/presets/handheld-plus-color-mod/ | grep hidpi
```

(If the pack is already imported on the device, skip the first push. MTP or the device's own file manager works just as well; `adb` is only the scriptable option.)

- [ ] **Step 2: Work through the checks, on Vulkan and then on GLES**

Set the backend in Settings → Graphics → Backend, and restart between the two. For each backend:

1. **Grid distinctness.** Both variants, at arm's length (~35 cm), against the stock `lcd-grid-v2-psp-color` for comparison. The stripes should be resolvable rather than a flat dimming. Expected: 2.65 display px (0.183 mm) per stripe at 2× pitch.
2. **Parameter values.** Open the parameter screen for each variant. `LCD Gamma` 2.2, `Black level` 0, `Ambient` 0, `Gain` 1, and `LCD cell pitch (source px)` 1.0 for variant A / **2.0** for variant B. Leave and re-enter: the image must not change. (Needs a build with Task 1; without it these read 3.0 / 0.05 / 1.0 and the image shifts.)
3. **Colour and gamma parity** with the stock preset — no colour shift, no overall darkening, no banding introduced by the added pass. If edges look darker than the stock preset, that is the gamma-space downsample (spec §8.1); the fix is a linear-light `b-spline-4-taps` fork, not an sRGB framebuffer.
4. **`PITCH` sweep** in variant B, 1.0 → 3.0: the pitch tracks it and the image stays registered.
5. **Variant A ≈ variant B at `PITCH = 2.0`**, allowing for the different pass-0 kernel targets (Task 3, Step 3).
6. **Internal resolution sweep** (1×, 3×, 5×): pass 0's antialiasing should improve with internal resolution, not shimmer. This is the check that confirms the declared-native-size mechanism (spec §3.4) is doing what it should.
7. **60 fps held** at 1906×1080 with four passes. Enable the FPS/speed display. Both variants, both backends.

- [ ] **Step 3: Repo-side regression**

```bash
build-unittest/PPSSPPUnitTest all
git status --short              # expected: clean
git diff master --stat          # expected: the spec, the plan, 4 engine files, 4 overlay files
git log master..HEAD --oneline
```

Expected: tests green, no stray edits, and — check explicitly — **nothing under `/tmp/slang-shaders` and no pack file in the diff**. `git log` output must contain no session marker in any commit.

- [ ] **Step 4: Report**

Report per check: pass, or the observed behaviour. Anything that fails gets diagnosed with `superpowers:systematic-debugging` before a fix, and the fix gets its own commit. Do not claim the feature done on the strength of the desktop run alone — the desktop cannot answer check 1, which is the goal.

---

## Notes for the executor

- **`/tmp/slang-shaders` may be gone.** It is a shallow clone in a temp directory. If it is missing, re-clone it before Task 2's diff check: `git clone --depth 1 https://github.com/libretro/slang-shaders.git /tmp/slang-shaders`. The pinned commit is `4ecd48510e4a0f936617c5e899dd6c4fd50abbd7`; a newer HEAD is fine for the pack itself, but if `lcd-grid-v2.slang` has changed upstream, diff against the pinned version (`git -C /tmp/slang-shaders show 4ecd485:handheld/shaders/lcd-cgwg/lcd-grid-v2.slang`) rather than silently forking a different file.
- **No local GLSL compiler.** There is no `glslangValidator`, `glslc` or `naga` on this machine, so a shader typo surfaces as a librashader error at preset load inside a running PPSSPP, not at build time. That is why Tasks 2 and 3 each end with a run.
- **The unit tests are hermetic.** They write fixtures into a temp directory and never read the repo's assets. Do not add a test that loads the shipped preset files by path — that would need new plumbing for little return (spec §10). Task 1's test is the one with real behaviour to drive.
- **Task 1 is the only engine change.** If it turns out larger than the plan implies, stop and say so rather than growing it: the spec's non-goals rule out runtime, chain and rendering changes.
