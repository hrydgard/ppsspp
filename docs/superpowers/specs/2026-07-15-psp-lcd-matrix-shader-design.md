# Design: PSP LCD Matrix slang shader

- **Status:** Approved
- **Date:** 2026-07-15
- **Branch:** `feature/slang-shader-support`
- **Depends on:** the shipped slang subsystem (Phases 1–4, Vulkan) — this is a *content* deliverable (a `.slangp` preset), not an engine change.

## 1. Summary

A RetroArch-format slang shader that simulates the look of the PSP's color LCD screen: fine
vertical **RGB subpixel stripes** (the signature "LCD matrix" appearance up close), a subtle **dark
grid** between pixel cells, the PSP's specific **color correction**, and a soft **backlight glow**.
It is authored as an unmodified `.slang`/`.slangp` preset and runs on the existing slang filter
chain with **no emulator/engine code changes**.

## 2. Goals & Non-Goals

### Goals
- Reproduce the up-close PSP-LCD look: RGB vertical subpixel stripes + inter-cell grid gaps.
- Apply the established PSP color correction (gamma 2.21 → color-correction matrix → display gamma),
  matching libretro's `lcd1x_psp`.
- Add a subtle LCD backlight glow (soft bleed), the one LCD effect that justifies a multi-pass
  structure.
- Expose tunable `#pragma parameter`s so the effect is adjustable via the Phase-4 slider UI.
- Ship as a bundled fixture so it is always present and testable, like the other `slang_test`
  fixtures.

### Non-Goals
- No screen curvature, CRT phosphor mask, halation, or heavy bloom — an LCD is flat and sharp.
- No new engine/parser/runtime code — this is pure slang content the subsystem already supports.
- Vulkan only (matches the shipped subsystem; GL/GLES/D3D11 remain out of scope per the reverted
  Phase 5).
- Not a pixel-exact colorimetric match to a specific PSP unit — a faithful, tunable *look*.

## 3. Research basis (how a PSP LCD image should look)

- **RGB subpixel structure** (Wikipedia, Subpixel rendering): each LCD pixel is three individually
  addressable vertical subpixels ordered R→G→B; the stripes are visible up close and blend at a
  distance. This vertical-stripe structure is the defining "LCD matrix" characteristic and is what
  the simpler `lcd1x_psp` omits.
- **PSP color correction** (libretro `lcd1x_psp.slang`, public-domain `psp_color` by hunterk /
  Pokefan531): `TARGET_GAMMA = 2.21`, a 3×3 correction matrix (`CC_R=0.98, CC_G=0.795, CC_B=0.98,
  CC_RG=0.04, CC_RB=0.01, CC_GR=0.20, CC_GB=0.01, CC_BR=-0.18, CC_BG=0.165`), then encode at
  `1/2.2`. Reproduces the PSP's warm, slightly desaturated gamut. Reused verbatim.
- **Grid / scanline modulation** (libretro `lcd1x_psp`): a sinusoidal darkening between pixel cells
  (`sin(angle.x) * sin(angle.y)` with a 0.25-pixel offset so gaps fall between pixels), with
  brightness-compensation constants. We generalize this: an explicit RGB-subpixel mask (new) plus a
  softer inter-cell grid.
- **LCD vs CRT:** flat, sharp, no bloom/curvature; the only soft element is faint backlight bleed.

## 4. Architecture — three passes

Preset `lcd-psp-matrix.slangp` with three `.slang` stages. All standard slang the engine already
handles (single merged UBO via the push_constant→UBO transform, `Source`/`PassOutput`/size
semantics, per-pass scale + framebuffer format).

### Pass 1 — Linearize + PSP color-correct (`scale_type = source`, linear/float framebuffer)
- Sample `Source` (nearest).
- Apply PSP color correction: `pow(c, 2.21)` → 3×3 CC matrix → this pass outputs **linear-ish
  corrected color** (kept in a float/linear framebuffer; encode happens in Pass 3).
- Output is the clean corrected image; it is the input to both Pass 2 (glow) and Pass 3 (matrix),
  referenced by alias (e.g. `alias1 = "CORRECTED"`).

### Pass 2 — Backlight glow (`scale_type = source`, float framebuffer)
- A small-radius blur of Pass 1's output producing the soft backlight-bleed component.
- Kept cheap (small separable-style kernel in one pass, or a modest box/gaussian). Aliased
  `HALATION`-style (e.g. `alias2 = "GLOW"`) for Pass 3.

### Pass 3 — Subpixel matrix + grid + combine (`scale_type = viewport`, final pass)
- Runs at **output/viewport resolution** so subpixels are rendered at display pixel density.
- Reconstruct the corrected pixel from the `CORRECTED` pass; sample `GLOW`.
- **RGB subpixel mask:** using the output pixel x-coordinate, `mod(floor(x), 3)` selects R/G/B; emit
  that subpixel channel emphasized and the others attenuated by `SUBPIXEL_STRENGTH` (0 = flat, no
  stripes). This is the core new element vs `lcd1x_psp`.
- **Inter-cell grid:** darken cell edges (a subtle x/y modulation) by `GRID_STRENGTH`.
- **Backlight glow:** add `GLOW_STRENGTH * GLOW`.
- **Brightness compensation** (`BRIGHTEN`) to offset the darkening from mask+grid.
- Optional subtle **horizontal scanline** modulation (`SCANLINE_STRENGTH`, default low).
- Encode to display gamma (`1/2.2`) and output.

## 5. Parameters (`#pragma parameter`, tunable via Phase-4 UI)

| name | meaning | default | min | max | step |
|---|---|---|---|---|---|
| `SUBPIXEL_STRENGTH` | RGB stripe intensity (0 = off) | 0.5 | 0.0 | 1.0 | 0.05 |
| `GRID_STRENGTH` | inter-pixel gap darkness | 0.3 | 0.0 | 1.0 | 0.05 |
| `GLOW_STRENGTH` | backlight bleed amount | 0.15 | 0.0 | 1.0 | 0.05 |
| `GLOW_RADIUS` | glow spread (blur kernel scale) | 1.0 | 0.5 | 4.0 | 0.1 |
| `COLOR_CORRECT` | PSP color correction on/off | 1.0 | 0.0 | 1.0 | 1.0 |
| `BRIGHTEN` | brightness compensation | 1.3 | 1.0 | 3.0 | 0.05 |
| `SCANLINE_STRENGTH` | horizontal modulation (subtle) | 0.1 | 0.0 | 1.0 | 0.05 |

(Exact defaults tuned during on-device verification.)

## 6. Framebuffers & scale

- Pass 1, Pass 2: `float_framebuffer = true` (linear-light intermediate). Falls back to UNORM on
  backends lacking float RT — already handled by `SlangFilterChain` (Vulkan supports it).
- Pass 1, Pass 2: `scale_type = source` (native content res — glow/correction don't need display
  density).
- Pass 3: `scale_type = viewport` (output resolution, so subpixels map to display pixels).
- `filter_linear`: nearest for Pass 1 (sharp source sampling); linear for the glow input.

## 7. Deliverables & placement

- `assets/shaders/slang_test/lcd-psp-matrix.slangp` + `lcd-psp-matrix-pass1.slang`,
  `-pass2.slang`, `-pass3.slang` (or a shared include for the PSP color-correction constants).
- Bundled with the other `slang_test` fixtures so it ships in the APK/build and is always available
  for selection/testing.
- Selectable via the existing config (`sSlangShaderPreset` = the preset's path) and, once imported
  shaders coexist, via the Phase-4 browser.

## 8. Error handling / robustness

- Standard slang failure path: any compile/reflection error disables the preset with a logged ERROR
  and falls back to the raw game (existing behavior). Nothing new.

## 9. Testing strategy

- **Unit:** if the shader introduces no new parser/reflection constructs (it should not — it's
  standard multi-pass slang with `#pragma parameter`, aliases, float framebuffers, viewport scale,
  all already covered), no new unit test is required. If a fixture-load smoke test is cheap to add
  to `TestSlangParser`, include one that parses the `.slangp` and asserts 3 passes + the parameter
  list.
- **On-device (Vulkan, AYN Thor):** load the preset on a real game; verify (a) RGB subpixel stripes
  are visible at higher internal resolution, (b) the PSP color cast matches `lcd1x_psp`, (c) the
  backlight glow is subtle and correct, (d) parameters visibly change the look via the Phase-4
  sliders, (e) no regression to the raw game when disabled. Compare side-by-side with the existing
  `lcd1x_psp`.
- **Regression:** the existing bundled fixtures and slang unit tests stay green (this only adds
  files).

## 10. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Subpixel stripes invisible at native res (1 output pixel < 3 subpixels) | Effect keys off OutputSize/viewport; document that stripes are most visible at ≥2× internal res or large displays; keep `SUBPIXEL_STRENGTH` tunable to 0. |
| Over-darkening from mask+grid stacking | `BRIGHTEN` compensation parameter; tune defaults on-device. |
| Glow pass too expensive / too strong | Small kernel, low default `GLOW_STRENGTH`; it's the only soft pass. |
| Color correction double-applied vs game's own | `COLOR_CORRECT` toggle; document it assumes uncorrected input (raw framebuffer). |
