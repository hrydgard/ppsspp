# PSP LCD Matrix slang shader — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a RetroArch-format slang preset that simulates the PSP LCD look — RGB vertical subpixel stripes, an inter-cell grid, PSP color correction, and a subtle backlight glow — as bundled shader content that runs on the existing (Vulkan) slang filter chain with no engine changes.

**Architecture:** Three `.slang` passes + one `.slangp` preset, authored under `assets/shaders/slang_test/`. Pass 1 (source scale, float FB): sample `Source` nearest, apply PSP color correction, output linear corrected color aliased `CORRECTED`. Pass 2 (source scale, float FB): blur `CORRECTED` for the backlight glow, aliased `GLOW`. Pass 3 (viewport scale, final): sample `CORRECTED` + `GLOW`, apply the RGB `mod(x,3)` subpixel mask + inter-cell grid + optional scanline + glow add + brightness compensation, encode to display gamma. All standard slang the engine already handles (merged UBO via push_constant→UBO transform, alias samplers, size semantics, per-pass scale/format, `#pragma parameter`).

**Tech Stack:** RetroArch slang shader format (Vulkan GLSL `#version 450`, `#pragma stage`/`name`/`parameter`, `.slangp`); PPSSPP's shipped slang subsystem (Vulkan). No C++ (content only). Verification is on-device visual on the AYN Thor (Vulkan), not unit tests.

## Global Constraints

- **Content only — no engine changes.** This plan adds `.slang`/`.slangp` files under `assets/`. It must NOT modify any `GPU/Common/Slang/` C++, thin3d, or config code. If the shader needs something the engine can't do, STOP and escalate — do not change the engine.
- **Standard slang only:** everything used (multi-pass, aliases as samplers, `float_framebuffer`, `scale_type source/viewport`, `#pragma parameter`, `Source`/`OutputSize`/`OriginalSize`/`SourceSize` semantics) is already supported and covered by the shipped subsystem and unit tests. Do not invent new semantics.
- **Vulkan only:** matches the shipped subsystem. GL/GLES/D3D11 are out of scope (Phase 5 reverted). Test on the Vulkan backend only.
- **PSP color correction is reused verbatim** from libretro's public-domain `lcd1x_psp` (see spec §3): `TARGET_GAMMA=2.21`; matrix `CC_R=0.98, CC_G=0.795, CC_B=0.98, CC_RG=0.04, CC_RB=0.01, CC_GR=0.20, CC_GB=0.01, CC_BR=-0.18, CC_BG=0.165`; encode `1/2.2`. Keep the attribution comment.
- **Author under `assets/`, iterate on-device.** The deliverable lives at `assets/shaders/slang_test/lcd-psp-matrix*.{slangp,slang}`. Because a bundled asset requires a full APK rebuild to update, ITERATE by pushing the files to the device custom-shader dir and pointing the config at the absolute path (see Test Harness); only rebuild the APK once, at the end, to confirm the bundled copy loads via VFS.
- **Commit trailer:** end every commit message with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

## Test Harness (verbatim — the fast iteration loop)

The shipped `ReadSlangFile` tries VFS first (bundled `assets/`) then the real filesystem, so an absolute path on the device works for iteration without an APK rebuild.

- **Device:** AYN Thor, Vulkan backend (`GraphicsBackend = 3 (VULKAN)` in `ppsspp.ini`). Adreno.
- **Device shader dir for iteration:** `/storage/9C33-6BBD/ROMs/psp/PSP/shaders/lcd/` (create it). Push the `.slangp` + 3 `.slang` files there.
- **Activate:** set `SlangShaderPreset = /storage/9C33-6BBD/ROMs/psp/PSP/shaders/lcd/lcd-psp-matrix.slangp` in `/storage/9C33-6BBD/ROMs/psp/PSP/SYSTEM/ppsspp.ini`.
- **Launch a game (Lunar):**
  `adb shell am start -a android.intent.action.VIEW -n org.ppsspp.ppsspp/.PpssppActivity -d "content://com.android.externalstorage.documents/tree/9C33%2D6BBD%3AROMs%2Fpsp/document/9C33%2D6BBD%3AROMs%2Fpsp%2FLunar%20%2D%20Silver%20Star%20Harmony%2EISO"`
- **Iterate:** edit the `.slang` on the host, `adb push` it to the device shader dir, force-stop + relaunch. No rebuild needed (the engine re-reads + recompiles the preset each activation).
- **Check for load/compile errors:** `adb logcat -d | grep -iE "slang.*error|slang parse|Failed to load slang"` — WARN/ERROR on failure (the engine logs preset load failures at ERROR).
- **Set higher internal resolution** (subpixels are most visible ≥2×): `InternalResolution = 4` in `ppsspp.ini`.
- **Reference for comparison:** the libretro `lcd1x_psp.slang` (imported tree on device) — the color-correction target.
- **APK build (final task only):** `export ANDROID_HOME=/opt/homebrew/share/android-commandlinetools; ./gradlew -p android assembleNormalDebug -Pandroid.injected.build.abi=arm64-v8a -PANDROID_VERSION_CODE=999999999 -PANDROID_VERSION_NAME=slang-dev --console=plain`; `adb install -t -r android/build/intermediates/apk/normal/debug/android-normal-debug.apk`.

## Slang authoring reference (verbatim conventions from existing fixtures)

- `.slangp` keys (from `twopass.slangp`/`srgb.slangp`): `shaders = N`; per-pass `shaderN`, `aliasN`, `filter_linearN` (`true`/`false`), `scale_typeN` (`source`/`viewport`/`absolute`), `scaleN`, `float_framebufferN`, `srgb_framebufferN`, `wrap_modeN`, `mipmap_inputN`.
- Pass source skeleton (from `twopass_a.slang`): `#version 450` then the UBO/push blocks, `#pragma name <Alias>` (optional), `#pragma stage vertex` … `#pragma stage fragment`. Vertex declares `layout(location=0) in vec4 Position; layout(location=1) in vec2 TexCoord;` and outputs `vTexCoord`. `gl_Position = MVP * Position`.
- UBO + push layout that the engine's push_constant→UBO transform expects (from `lcd1x_psp` — the closest real example):
  ```glsl
  layout(push_constant) uniform Push {
     float PARAM_A; float PARAM_B; /* ... one float per #pragma parameter ... */
     vec4 OutputSize; vec4 OriginalSize; vec4 SourceSize;
  } registers;
  layout(std140, set = 0, binding = 0) uniform UBO { mat4 MVP; } global;
  ```
  Fragment samplers: `layout(set = 0, binding = 2) uniform sampler2D Source;` and additional aliased inputs at bindings 3,4,… (`uniform sampler2D CORRECTED;` etc. — the reflection classifier binds by the alias name). Each `#pragma parameter NAME "Desc" init min max step` must have a matching `float NAME;` member in the Push block, referenced as `registers.NAME`.
- Size semantics: `registers.OutputSize` / `OriginalSize` / `SourceSize` are `vec4(w, h, 1/w, 1/h)`.

## Deliverable files

- `assets/shaders/slang_test/lcd-psp-matrix.slangp`
- `assets/shaders/slang_test/lcd-psp-matrix-pass1.slang` (color-correct)
- `assets/shaders/slang_test/lcd-psp-matrix-pass2.slang` (glow blur)
- `assets/shaders/slang_test/lcd-psp-matrix-pass3.slang` (subpixel + grid + combine)

## Task Overview (implement in order)

- **Task 1** — Pass 1 (PSP color correction) as a standalone single-pass preset; verify color cast matches `lcd1x_psp` on-device.
- **Task 2** — Pass 3 subpixel mask + grid, wired as pass 2 of a 2-pass chain (correct → subpixel); verify RGB stripes + grid on-device.
- **Task 3** — Insert the glow blur pass → full 3-pass preset; verify backlight glow + final look; tune parameter defaults on-device.
- **Task 4** — Finalize: bundle under `assets/`, rebuild APK, confirm the bundled preset loads via VFS; regression-check (raw game + existing fixtures unaffected).

Each task ends at an on-device-verifiable milestone, building the effect one pass at a time so each element is confirmed before the next is layered on.

---

### Task 1: Pass 1 — PSP color correction (standalone single-pass first)

**Files:**
- Create: `assets/shaders/slang_test/lcd-psp-matrix-pass1.slang`
- Create (temporary iteration preset): a 1-pass `.slangp` on the device pointing only at pass1 (do NOT commit this temp preset; the real `.slangp` is built in Task 3).

**Rationale:** verify the color-correction pass in isolation against the `lcd1x_psp` reference before layering the matrix/glow. Isolating color first means any later color oddity is attributable to the mask/grid, not the correction.

**Interfaces:**
- Consumes: `Source` (the game framebuffer), `#pragma parameter COLOR_CORRECT`.
- Produces: `#pragma name CORRECTED` — linear-ish PSP-corrected color. (Aliased `CORRECTED` by the preset in later tasks.)

- [ ] **Step 1: Write `lcd-psp-matrix-pass1.slang`** (GPL/public-domain attribution comment for the reused `psp_color` correction). Full source:
```glsl
#version 450
// PSP LCD matrix shader — Pass 1: PSP color correction.
// PSP color-correction ('psp_color') originally by hunterk, modified by Pokefan531,
// released into the public domain; matrix values as used in libretro lcd1x_psp.
#pragma name CORRECTED
#pragma parameter COLOR_CORRECT "PSP Color Correction" 1.0 0.0 1.0 1.0

layout(push_constant) uniform Push {
   float COLOR_CORRECT;
   vec4 OutputSize;
   vec4 OriginalSize;
   vec4 SourceSize;
} registers;
layout(std140, set = 0, binding = 0) uniform UBO { mat4 MVP; } global;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() { gl_Position = global.MVP * Position; vTexCoord = TexCoord; }

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

#define TARGET_GAMMA 2.21
#define CC_R 0.98
#define CC_G 0.795
#define CC_B 0.98
#define CC_RG 0.04
#define CC_RB 0.01
#define CC_GR 0.20
#define CC_GB 0.01
#define CC_BR -0.18
#define CC_BG 0.165

void main() {
   vec3 c = texture(Source, vTexCoord).rgb;
   if (registers.COLOR_CORRECT > 0.5) {
      c = pow(c, vec3(TARGET_GAMMA));
      c = mat3(CC_R,  CC_RG, CC_RB,
               CC_GR, CC_G,  CC_GB,
               CC_BR, CC_BG, CC_B) * c;
      // NOTE: leave in this (roughly linear) space; pass 3 encodes to display gamma.
      c = clamp(c, 0.0, 1.0);
   }
   FragColor = vec4(c, 1.0);
}
```

- [ ] **Step 2: Deploy + verify color on-device** (per Test Harness): create the device `lcd/` dir; push pass1; write a TEMP 1-pass preset there (`shaders=1`, `shader0=lcd-psp-matrix-pass1.slang`, `scale_type0=viewport`, `float_framebuffer0=true`); point `SlangShaderPreset` at it; launch Lunar. CONTROLLER checks: game renders with the PSP warm/desaturated color cast, visually matching `lcd1x_psp`'s correction (compare by swapping the preset to `lcd1x_psp` if convenient). Confirm no `slang parse`/load error in logcat. Toggle `COLOR_CORRECT` to 0 via the Phase-4 slider (or edit) and confirm the correction turns off.

- [ ] **Step 3: Commit** (pass1 only; not the temp preset):
```bash
git add assets/shaders/slang_test/lcd-psp-matrix-pass1.slang
git commit -m "slang(lcd): pass 1 — PSP color correction

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Subpixel mask + grid pass (2-pass chain: CORRECTED → matrix)

**Files:**
- Create: `assets/shaders/slang_test/lcd-psp-matrix-pass3.slang` (the final combine pass; authored now without the glow term, glow added in Task 3).
- Iterate with a TEMP 2-pass preset on device (pass1 → pass3), not committed.

**Rationale:** with color correct, add and verify the defining RGB-subpixel matrix + grid before introducing glow. Keeps the visual-debugging one variable at a time.

**Interfaces:**
- Consumes: `CORRECTED` (pass1 output, bound by alias), `registers.OutputSize`, params `SUBPIXEL_STRENGTH`, `GRID_STRENGTH`, `SCANLINE_STRENGTH`, `BRIGHTEN`.
- Produces: final display-encoded color. (Glow input `GLOW` + `GLOW_STRENGTH` added in Task 3.)

- [ ] **Step 1: Write `lcd-psp-matrix-pass3.slang`** (glow deferred to Task 3 — a `GLOW` sampler + `GLOW_STRENGTH` param are added then). Full source:
```glsl
#version 450
// PSP LCD matrix shader — final pass: RGB subpixel mask + inter-cell grid + display encode.
#pragma parameter SUBPIXEL_STRENGTH "LCD Subpixel Strength" 0.5 0.0 1.0 0.05
#pragma parameter GRID_STRENGTH     "LCD Grid Strength"     0.3 0.0 1.0 0.05
#pragma parameter SCANLINE_STRENGTH "LCD Scanline Strength" 0.1 0.0 1.0 0.05
#pragma parameter BRIGHTEN          "Brightness Compensation" 1.3 1.0 3.0 0.05

layout(push_constant) uniform Push {
   float SUBPIXEL_STRENGTH;
   float GRID_STRENGTH;
   float SCANLINE_STRENGTH;
   float BRIGHTEN;
   vec4 OutputSize;
   vec4 OriginalSize;
   vec4 SourceSize;
} registers;
layout(std140, set = 0, binding = 0) uniform UBO { mat4 MVP; } global;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() { gl_Position = global.MVP * Position; vTexCoord = TexCoord; }

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;      // = CORRECTED (pass1)
layout(set = 0, binding = 3) uniform sampler2D CORRECTED;   // alias of pass1 (same content; bound by name)

#define INV_DISPLAY_GAMMA (1.0 / 2.2)

void main() {
   vec3 c = texture(CORRECTED, vTexCoord).rgb;

   // Output-pixel coordinate (subpixels render at display density).
   vec2 op = vTexCoord * registers.OutputSize.xy;

   // RGB vertical subpixel mask: each output column emphasizes one of R/G/B.
   // sub in {0,1,2} selects the channel; others attenuated by SUBPIXEL_STRENGTH.
   int sub = int(mod(floor(op.x), 3.0));
   vec3 mask = vec3(1.0 - registers.SUBPIXEL_STRENGTH);
   mask[sub] = 1.0;

   // Inter-cell grid: subtle darkening at cell boundaries (both axes), tunable.
   // Use fract of the per-axis pixel position; darken near the cell edge.
   vec2 f = fract(op);
   float gx = 1.0 - registers.GRID_STRENGTH * smoothstep(0.5, 1.0, abs(f.x - 0.5) * 2.0);
   float gy = 1.0 - registers.GRID_STRENGTH * smoothstep(0.5, 1.0, abs(f.y - 0.5) * 2.0);
   float grid = gx * gy;

   // Subtle horizontal scanline modulation (much milder than a CRT).
   float scan = 1.0 - registers.SCANLINE_STRENGTH * (0.5 - 0.5 * cos(6.28318 * op.y));

   c = c * mask * grid * scan * registers.BRIGHTEN;

   // Encode to display gamma (pass1 left color in ~linear space).
   c = clamp(pow(max(c, vec3(0.0)), vec3(INV_DISPLAY_GAMMA)), 0.0, 1.0);
   FragColor = vec4(c, 1.0);
}
```
Implementer note: the grid uses `fract(op)` at output-pixel scale, which produces a per-output-pixel modulation — fine for a first pass; if on-device it looks too fine/aliased, the grid term is the tuning target (Task 3 Step 3 / escalate if it needs a per-*source*-pixel cell instead). The subpixel `mask[sub]=1` with others at `1-strength` is the core LCD-matrix element; verify stripes are visible at 4× internal res.

- [ ] **Step 2: Deploy + verify on-device** — push pass3; write a TEMP 2-pass preset (pass0=pass1 `alias0=CORRECTED` `scale_type0=source` `float_framebuffer0=true` `filter_linear0=false`; pass1=pass3 `scale_type1=viewport`); activate; launch. CONTROLLER checks at `InternalResolution=4`: (a) visible vertical RGB subpixel stripes, (b) subtle grid between cells, (c) overall brightness reasonable (tune `BRIGHTEN`), (d) `SUBPIXEL_STRENGTH=0` removes stripes. No load error.

- [ ] **Step 3: Commit** (pass3):
```bash
git add assets/shaders/slang_test/lcd-psp-matrix-pass3.slang
git commit -m "slang(lcd): final pass — RGB subpixel mask + grid + display encode

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Glow blur pass + full 3-pass preset + tune defaults

**Files:**
- Create: `assets/shaders/slang_test/lcd-psp-matrix-pass2.slang` (backlight glow blur)
- Modify: `assets/shaders/slang_test/lcd-psp-matrix-pass3.slang` (add the `GLOW` sampler + `GLOW_STRENGTH` term)
- Create: `assets/shaders/slang_test/lcd-psp-matrix.slangp` (the real, committed 3-pass preset)

**Interfaces:**
- Pass 2 consumes `CORRECTED` (pass1), params `GLOW_RADIUS`; produces `#pragma name GLOW`.
- Pass 3 additionally consumes `GLOW` + `GLOW_STRENGTH`.

- [ ] **Step 1: Write `lcd-psp-matrix-pass2.slang`** — a small separable-ish blur of `CORRECTED` for backlight bleed. A single-pass box/gaussian tap set is fine (LCD glow is subtle). Full source:
```glsl
#version 450
// PSP LCD matrix shader — Pass 2: backlight glow (soft blur of the corrected image).
#pragma name GLOW
#pragma parameter GLOW_RADIUS "LCD Glow Radius" 1.0 0.5 4.0 0.1

layout(push_constant) uniform Push {
   float GLOW_RADIUS;
   vec4 OutputSize;
   vec4 OriginalSize;
   vec4 SourceSize;
} registers;
layout(std140, set = 0, binding = 0) uniform UBO { mat4 MVP; } global;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() { gl_Position = global.MVP * Position; vTexCoord = TexCoord; }

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;     // = CORRECTED (pass1)
layout(set = 0, binding = 3) uniform sampler2D CORRECTED;

void main() {
   // 3x3 gaussian-ish blur, radius scaled by GLOW_RADIUS in source texels.
   vec2 px = registers.SourceSize.zw * registers.GLOW_RADIUS;  // 1/size * radius
   vec3 sum = vec3(0.0);
   float wsum = 0.0;
   for (int y = -1; y <= 1; y++) {
      for (int x = -1; x <= 1; x++) {
         float w = (x == 0 && y == 0) ? 4.0 : ((x == 0 || y == 0) ? 2.0 : 1.0); // 4/2/1 gaussian
         sum += w * texture(CORRECTED, vTexCoord + vec2(float(x), float(y)) * px).rgb;
         wsum += w;
      }
   }
   FragColor = vec4(sum / wsum, 1.0);
}
```

- [ ] **Step 2: Add the glow term to `lcd-psp-matrix-pass3.slang`.** Add the parameter + sampler and mix glow into the result. Edits:
  - Add `#pragma parameter GLOW_STRENGTH "LCD Glow Strength" 0.15 0.0 1.0 0.05` (and the matching `float GLOW_STRENGTH;` in the Push block, placed BEFORE the `vec4 OutputSize;` members, matching parameter order).
  - Add `layout(set = 0, binding = 4) uniform sampler2D GLOW;`.
  - After computing `c` (the masked/grid/scan color, before gamma encode), add the glow:
    `c += registers.GLOW_STRENGTH * texture(GLOW, vTexCoord).rgb;`
    (Glow is added in the same ~linear space as `c`, before the display-gamma encode.)

- [ ] **Step 3: Write the real preset `lcd-psp-matrix.slangp`** (this one is committed):
```
shaders = 3

shader0 = lcd-psp-matrix-pass1.slang
alias0 = CORRECTED
filter_linear0 = false
scale_type0 = source
scale0 = 1.0
float_framebuffer0 = true

shader1 = lcd-psp-matrix-pass2.slang
alias1 = GLOW
filter_linear1 = true
scale_type1 = source
scale1 = 1.0
float_framebuffer1 = true

shader2 = lcd-psp-matrix-pass3.slang
filter_linear2 = false
scale_type2 = viewport
scale2 = 1.0
```
(Pass 2 samples `CORRECTED`; pass 3 samples both `CORRECTED` and `GLOW` — both are earlier passes, so the causal PassOutput binding is satisfied. `Source` in pass2/pass3 = the immediately-preceding pass output, but we reference by alias so ordering is explicit.)

- [ ] **Step 4: Deploy full 3-pass + tune (controller).** Push all three `.slang` + the real `.slangp` to the device `lcd/` dir; point config at `lcd-psp-matrix.slangp`; launch. CONTROLLER verifies the complete look: subpixel stripes + grid + a SUBTLE backlight glow (visible as soft bleed in bright areas against dark), PSP color. Tune the parameter DEFAULTS live via the Phase-4 sliders until the look is right (subpixel/grid/glow/brighten/scanline); bake the chosen defaults back into the `#pragma parameter` init values in the `.slang` files. Confirm each slider visibly affects the image and `SUBPIXEL_STRENGTH=0` + `GLOW_STRENGTH=0` degrade gracefully. No load error.

- [ ] **Step 5: Commit** the glow pass, the pass3 glow edit, and the preset:
```bash
git add assets/shaders/slang_test/lcd-psp-matrix-pass2.slang assets/shaders/slang_test/lcd-psp-matrix-pass3.slang assets/shaders/slang_test/lcd-psp-matrix.slangp
git commit -m "slang(lcd): backlight glow pass + full 3-pass PSP LCD matrix preset

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Finalize — bundle via APK, VFS-load check, regression

**Files:** none new (verification + any tuned-default touch-ups already committed).

- [ ] **Step 1: Build + install the APK** (bundles the `assets/shaders/slang_test/` copies): the gradle command from the Test Harness → BUILD SUCCESSFUL; install.

- [ ] **Step 2: Verify the BUNDLED preset loads via VFS.** Point `SlangShaderPreset` at the VFS-relative bundled path `shaders/slang_test/lcd-psp-matrix.slangp` (NOT the device absolute path — this proves the shipped asset works, since `ReadSlangFile` tries VFS first). Launch; CONTROLLER confirms the shader renders identically to the iteration copy. No load error.

- [ ] **Step 3: Regression.** (a) Set `SlangShaderPreset` empty → raw game renders normally on Vulkan. (b) Load an existing bundled fixture (`shaders/slang_test/stock.slangp` / `twopass.slangp`) → still renders (this change only added files). (c) Run the slang unit suite on desktop: `cmake --build build-unittest --target PPSSPPUnitTest` then run `SlangParser SlangReflection SlangPresetLibrary SlangPresetParameters` → all exit 0 (unchanged; content-only change touches no C++). Optionally, if the plan added a fixture-parse smoke test (spec §9, only if cheap), run it too.

- [ ] **Step 4: Clean up the device iteration dir** (optional): remove `/storage/9C33-6BBD/ROMs/psp/PSP/shaders/lcd/` scratch copies so the device isn't left with stale duplicates; leave the config pointing at the bundled path or empty.

- [ ] **Step 5: Commit** any tuned-default adjustments not already committed (if Task 3 baked defaults in a separate edit):
```bash
git add assets/shaders/slang_test/lcd-psp-matrix-*.slang
git commit -m "slang(lcd): final tuned parameter defaults; verified bundled load

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```
(Skip if nothing changed since Task 3.)

---

## Out of scope

- Engine/parser/runtime/UI changes (content-only feature).
- GL/GLES/D3D11 (Vulkan only; Phase 5 reverted).
- Colorimetric exactness to a specific PSP unit; screen curvature; CRT-style bloom/mask.

## Self-Review (completed during authoring)

- **Spec coverage:** 3 passes (§4) → Tasks 1/2/3; the 7 parameters (§5) all appear in the shader source with the spec's defaults/ranges; float framebuffers + source/viewport scale (§6) in the `.slangp`; bundled under `assets/shaders/slang_test/` (§7); on-device verification incl. `lcd1x_psp` comparison and disable-regression (§9); risks (§10: subpixel visibility at low res, over-darkening→`BRIGHTEN`, glow cost, double color-correct→`COLOR_CORRECT` toggle) all have a home in a task or a parameter.
- **No engine change:** every construct used (aliases-as-samplers, float FB, viewport scale, `#pragma parameter`, push_constant→UBO) is already supported and unit-tested by the shipped subsystem; Global Constraints forbid touching C++ and require escalation if the shader needs more.
- **Type/name consistency:** aliases `CORRECTED` (pass1) and `GLOW` (pass2) are declared via `#pragma name` and `aliasN`, and referenced as `uniform sampler2D CORRECTED/GLOW` in later passes — bound by the reflection classifier. Parameter names match between each `#pragma parameter` and its Push-block `float` member.
- **Incremental verifiability:** color (T1) → matrix (T2) → glow+full (T3) → bundle (T4); each is independently visible on-device, so a visual regression is attributable to one pass.
- **Placeholder scan:** every shader step carries complete GLSL; the only deferred values are the final tuned parameter defaults (explicitly a Task-3 on-device tuning step, baked back into the `#pragma parameter` inits), which genuinely require the device to choose well.

