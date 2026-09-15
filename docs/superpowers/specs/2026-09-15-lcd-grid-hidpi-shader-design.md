# Design: coarse-pitch PSP LCD grid shader for high-PPI handheld displays

- **Status:** Approved
- **Date:** 2026-09-15
- **Branch:** `feature/lcd-grid-hidpi-shader`
- **Target device:** AYN Thor (6.0" 1920×1080 AMOLED, 367 PPI)
- **Depends on:** the shipped librashader-backed slang subsystem. This is a *content* deliverable — an
  overlay for an imported libretro `slang-shaders` pack — plus one unit-test addition. No engine change.

## 1. Summary

Two `.slangp` presets derived from the libretro pack's
`presets/handheld-plus-color-mod/lcd-grid-v2-psp-color.slangp`, whose LCD cell grid is **coarser than
one cell per PSP pixel** so the grid reads as a grid on a physically small, high-density panel. The
coarser pitch is produced by the same mechanism `presets/crt-royale-downsample.slangp` uses to force a
clean 240-line signal: a `b-spline-4-taps` resampling pass in front of the effect.

Both presets share one new shader: a fork of `handheld/shaders/lcd-cgwg/lcd-grid-v2.slang` that keys
its grid to `SourceSize` instead of `OriginalSize` and takes a `PITCH` parameter.

- **`lcd-grid-v2-psp-color-hidpi-2x.slangp`** — fixed 2× pitch. The b-spline pass downsamples to an
  absolute 240×136 and the grid draws one cell per downsampled pixel.
- **`lcd-grid-v2-psp-color-hidpi-tunable.slangp`** — runtime pitch. The b-spline pass downsamples to
  native 480×272 and the grid's `PITCH` parameter (1.0–3.0, default 2.0) sets the cell size live,
  averaging each cell through `textureLod` over a mipmapped input.

## 2. Goals & Non-Goals

### Goals

- An LCD grid that is visibly a grid at arm's length on a 367 PPI 6" panel.
- Preserve the reference preset's colour pipeline exactly: `multiLUT` with the PSP grey LUTs, then
  `psp-color`, with the same identity subpixel matrix and `gain`/`gamma`/`blacklevel`/`ambient`/`BGR`.
- Incorporate crt-royale-downsample's b-spline resampling pass, which on PPSSPP does double duty:
  it sets the cell pitch *and* collapses PPSSPP's upscaled render target to the effect's working
  resolution with a real filter kernel.
- Runtime tunability in one of the two variants, so the pitch can be dialled in on the device through
  the existing slang parameter UI.

### Non-Goals

- No engine, parser, or runtime change. Everything here is preset content the subsystem already runs.
- No change to the upstream reference preset or to any other pack file.
- Not a colorimetric match to a specific PSP unit — the colour path is inherited unchanged.
- No new grid/mask *model*. This is cgwg's `lcd-grid-v2` at a different pitch, not a new effect.

## 3. Research basis

### 3.1 The target panel

AYN Thor: 6.0" AMOLED, 1920×1080, 16:9, **367 PPI** → 0.0692 mm pixel pitch, 132.9 × 74.7 mm active
area. Device 150 × 94 × 25.6 mm closed (177 mm open), 380 g. SoC Snapdragon 8 Gen 2 (Vulkan + GLES);
a Lite tier ships an SD865.

PPSSPP aspect-fits 480×272 content into that panel at **1906 × 1080** (height-limited: 1080/272 =
3.97 < 1920/480 = 4.0), so **one PSP pixel is 3.97 display px = 0.275 mm**.

### 3.2 Why the stock preset's grid disappears there

`lcd-grid-v2` renders three subpixel stripes per source pixel. At one cell per PSP pixel that is
**1.32 display px (0.092 mm) per stripe**. The shader's own analytic antialiasing then integrates each
stripe over an output-pixel footprint of `3 · SourceWidth / OutputWidth` = 0.755 subpixels — i.e.
each output pixel covers most of a stripe — so the stripe pattern is averaged away and only a faint
overall dimming survives. Two things make it worse on this device rather than better:

- 0.092 mm subtends ~0.9 arcmin at a 35 cm handheld viewing distance, at or below the eye's
  resolving limit.
- The panel is AMOLED, whose own subpixels are not RGB stripes, so a 1.3 px emulated stripe pattern
  interferes with the physical layout instead of aligning with it.

For reference, a real PSP-1000 is 4.3"/480×272 → 128 PPI, 0.198 mm per pixel and 0.066 mm per
physical subpixel. The Thor therefore draws PSP *pixels* 1.39× larger than the real hardware, but the
emulated *stripes* still fall below what is visible. Matching real PSP subpixel geometry is not the
goal; being visible is.

### 3.3 Pitch selection

| pitch | cells | display px / cell | px / stripe | mm / stripe | verdict |
|---|---|---|---|---|---|
| 1× | 480×272 | 3.97 | 1.32 | 0.092 | washes out — the current behaviour |
| 1.5× | 320×181 | 5.96 | 1.99 | 0.137 | marginal |
| **2×** | **240×136** | **7.94** | **2.65** | **0.183** | **default** |
| 3× | 160×91 | 11.9 | 3.97 | 0.275 | distinct, image too soft for PSP UI/text |

2× is the default because it is the smallest pitch whose stripes are comfortably resolvable
(0.183 mm ≈ 1.8 arcmin at 35 cm), and because 240×136 is exactly half of 480×272 — cells land on
integer PSP-pixel pairs and the aspect ratio is preserved with no beat between the cell grid and the
source pixel grid.

### 3.4 The downsampling mechanism, and what it does on PPSSPP

`presets/crt-royale-downsample.slangp` puts `interpolation/shaders/b-spline-4-taps.slang` at pass 0
with `scale_type_x0 = viewport, scale_type_y0 = absolute, scale_y0 = 240`, so everything downstream
sees a clean 240-line signal regardless of the incoming resolution. The same pass, retargeted, is
what sets the LCD cell pitch here.

On PPSSPP it also does something the RetroArch preset does not need. The chain is handed the
**upscaled** render target while `SourceSize`/`OriginalSize` are declared as native 480×272
(`FramebufferManagerCommon.cpp:1792-1820`, `LibrashaderRuntimeVulkan.cpp:98-108`). A pass that reads
`SourceSize` therefore resamples the upscaled image at native pitch: pass 0 is a genuine downsample of
PPSSPP's internal-resolution render through a b-spline kernel, and raising the internal resolution
improves its antialiasing at no cost to the grid. 3×–5× internal resolution is the sweet spot on the
Thor.

Because both presets read `SourceSize`, `LibrashaderFilterChain::Load()` classifies them as
size-dependent. On Vulkan/GLES that changes nothing (the declared size is honoured). On D3D11, which
cannot declare an input size, PPSSPP pre-blits the render target down to 480×272 with a linear filter
first — so a Windows test sees a slightly softer pass-0 input than the Thor does. Expected, not a bug.

## 4. Deliverables

Three text files, laid out mirroring the pack so the overlay installs with a single recursive copy
over the imported slang root (`GetSlangShaderDir()`; on Android the app-private
`<extFilesDir>/slang`):

```
assets/shaders/slang_overlay/
  handheld/shaders/lcd-cgwg/lcd-grid-v2-pitch.slang
  presets/handheld-plus-color-mod/lcd-grid-v2-psp-color-hidpi-2x.slangp
  presets/handheld-plus-color-mod/lcd-grid-v2-psp-color-hidpi-tunable.slangp
```

`assets/shaders/slang_overlay/` is the in-repo home so the files are versioned and ship with the build
(~8 KB), which also leaves room for an in-app "install overlay" action later. They are *not* loadable
from there — the relative paths resolve only once the tree is merged into a pack, which is the
documented prerequisite.

Prerequisite for use: the libretro `slang-shaders` pack must already be imported, since the presets
reference its `b-spline-4-taps`, `multiLUT`, `psp-color` and the two `psp-grey` LUT PNGs. Without it
the presets simply do not appear in the browser.

## 5. The forked shader — `lcd-grid-v2-pitch.slang`

A copy of `handheld/shaders/lcd-cgwg/lcd-grid-v2.slang` (cgwg, GPL-2.0-or-later) with a header
recording the upstream file and the commit it was taken from, and exactly three changes.

### 5.1 `OriginalSize` → `SourceSize`

This is the enabling change. Upstream derives both the grid pitch (`texelSize`) and the texel fetch
scale from `global.OriginalSize.zw`, which in RetroArch semantics is the size of the **chain input**,
not of the previous pass. Put any resampling pass in front of it and the shader keeps drawing a
480×272 grid while sampling a 240×136 `Source` with 480×272 texel steps: the grid desynchronises from
the image and only the top-left quarter of the image is ever read.

`crt-royale-downsample` does not hit this because crt-royale republishes its own input under the
`ORIG_LINEARIZED` alias; `lcd-grid-v2` has no such indirection. Keying it to `SourceSize` makes it
follow the previous pass, which is what a pass in the middle of a chain should do.

### 5.2 A `PITCH` parameter

```
float PITCH;   // appended to the end of the Push block
#pragma parameter PITCH "LCD cell pitch (source pixels)" 1.0 1.0 3.0 0.25
```

Appended at the end of the `push_constant` block; librashader reflects members by name, so the
existing parameters keep working. In `main()`:

`SourceSize` is already the third member of upstream's UBO block, so §5.1 and §5.2 together change no
uniform layout — only which member is read, plus one appended `float` in the push block.

- `vec2 texelSize = global.SourceSize.zw * params.PITCH;` — the grid cell, in UV, is now `PITCH`
  source pixels wide. `tli`, `subpix` and `rsubpix` are all expressed in terms of `texelSize`
  already, so cgwg's `intsmear` antialiasing footprint (`range = OutputSize.zw`) stays correct with
  no further change, and the stripe-width constants (`d = 1.5` in x, `0.63` in y) scale with the cell
  automatically.
- `float lod = log2(max(params.PITCH, 1.0));` — the cell colour is the average of the `PITCH × PITCH`
  block it covers, fetched as one `textureLod` per corner.

`PITCH = 1.0` gives `lod = 0` and an unscaled texel: identical output to upstream. That is what lets
one shader serve both variants.

### 5.3 `fetch_offset` macro → function

Upstream's `fetch_offset` is a macro that reads the global `OriginalSize`. Since the fork's grid
metrics are locals in `main()`, it becomes a small function taking them explicitly:

```
vec3 fetch_cell(ivec2 coord, ivec2 offset, vec2 texelSize, float lod)
```

with the same body — `pow(gain * sample + blacklevel, gamma) + ambient` — and `textureLod(Source,
(vec2(coord + offset) + 0.5) * texelSize, lod)` in place of the `texture()` call. Behaviour at
`PITCH = 1.0` is unchanged; this is a readability change forced by the locals, not a rewrite.

Everything else — the four-corner reconstruction, `intsmear`/`intsmear_func`, the coefficient tables,
the subpixel colour matrix, `BGR` swizzling, the final `pow(1/2.2)` — is untouched.

## 6. Variant A — `lcd-grid-v2-psp-color-hidpi-2x.slangp`

```
shaders = "4"

shader0 = "../../interpolation/shaders/b-spline-4-taps.slang"
filter_linear0 = "true"          # b-spline-4-taps requires linear sampling
wrap_mode0 = "clamp_to_edge"
scale_type_x0 = "absolute"
scale_x0 = "240"
scale_type_y0 = "absolute"
scale_y0 = "136"

shader1 = "../../reshade/shaders/LUT/multiLUT.slang"
filter_linear1 = "false"
scale_type1 = "source"
scale1 = "1.0"

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

PITCH = "1.0"
RSUBPIX_R = "1"   RSUBPIX_G = "0"   RSUBPIX_B = "0"
GSUBPIX_R = "0"   GSUBPIX_G = "1"   GSUBPIX_B = "0"
BSUBPIX_R = "0"   BSUBPIX_G = "0"   BSUBPIX_B = "1"
gain = "1"   gamma = "2.2"   blacklevel = "0"   ambient = "0"   BGR = "0"
```

(The subpixel matrix and tone parameters are written one per line in the file, as in the reference
preset; they are grouped here only for brevity.)

Pass order follows the reference preset with the downsample prepended, exactly as
`crt-royale-downsample` prepends it before `multiLUT`. `PITCH = 1.0` because pass 0 has already set
the pitch: one cell per downsampled pixel, 7.94 display px per cell on the Thor.

## 7. Variant B — `lcd-grid-v2-psp-color-hidpi-tunable.slangp`

Identical except:

```
scale_type0 = "source"      # replaces the absolute 240×136
scale0 = "1.0"
...
mipmap_input2 = "true"      # the grid pass needs mips to average a cell
PITCH = "2.0"
```

Pass 0 still resamples PPSSPP's upscaled render to native 480×272 (§3.4), so the b-spline pass earns
its place here too — it is the antialiasing stage rather than the pitch stage. The grid pass then
takes the pitch from `PITCH`, averaging each cell through the mip chain of pass 1's output.

Verified available: PPSSPP sets `force_no_mipmaps = false` on both the Vulkan and OpenGL librashader
runtimes (`LibrashaderRuntimeVulkan.cpp:59`, `LibrashaderRuntimeOpenGL.cpp:53`), and `mipmap_inputN`
is parsed and honoured.

Non-power-of-two `PITCH` (1.25, 1.5, 2.5 …) gets an approximate box: trilinear blending between two
mip levels, and cell boundaries that no longer align with mip texel boundaries. Visually
inconsequential at these sizes; the exact-average cases are 1× and 2×.

PPSSPP keys parameter overrides by `presetPath|paramName` (`FramebufferManagerCommon.cpp:1813-1817`),
so a `PITCH` tuned in variant B does not disturb variant A.

## 8. Decisions and rationale

### 8.1 No `srgb_framebuffer`

`crt-royale-downsample` sets `srgb_framebuffer0/1 = true`; this design does not, matching the
reference `lcd-grid-v2-psp-color` preset.

The reason is that it would not buy what it appears to. PPSSPP's framebuffer is not sRGB-tagged, so
pass 0 reads gamma-encoded values and blends its four taps in gamma space regardless of how its own
output is stored; `srgb_framebuffer` would only change the storage precision of the intermediate.
Against that it adds a decode/encode pair on a path where a double-encode is a known failure mode in
this repo (the gamma bug fixed in `lcd-psp-matrix-pass1.slang`). crt-royale needs linear storage
because it does physical light math across many passes; `lcd-grid-v2` applies its own
`pow(gain·c + blacklevel, gamma)` and inverts it at the end, so it does not.

If 2:1 gamma-space blending produces visible edge darkening on device, the correct fix is a
linear-light fork of `b-spline-4-taps` (`pow(2.2)` in, `pow(1/2.2)` out), not an sRGB framebuffer.
Deferred until measured.

### 8.2 `wrap_mode2 = clamp_to_edge` on the grid pass

The reference preset leaves the grid pass at the slang default, `clamp_to_border`. `lcd-grid-v2`
fetches the cell one step right and one step down, so at the right and bottom edges it samples the
border and blends in black — a faint dark edge. Harmless at 1× and visible at coarser pitches, where
the overshoot is `PITCH` pixels wide. `clamp_to_edge` costs nothing and removes it. This is an
intentional, documented deviation from the reference preset.

### 8.3 Naming

`-hidpi-` rather than `-thor-`: the shader responds to pixel density, not to one device, and the same
presets are the right answer on any small high-PPI panel. The Thor numbers are the tuning basis,
recorded in §3.

## 9. Parameters exposed in the UI

`GetPresetParameters()` merges `#pragma parameter` declarations from every pass with `.slangp`-level
overrides winning, so the browser shows the values written above as the starting point. The
interesting knobs on the device:

| name | pass | default (A / B) | range | note |
|---|---|---|---|---|
| `PITCH` | grid | 1.0 / 2.0 | 1.0–3.0, step 0.25 | fixed by pass 0 in A; the live control in B |
| `gamma` | grid | 2.2 | 0.5–5.0 | cgwg's LCD gamma; drives cell contrast |
| `blacklevel` | grid | 0.0 | 0.0–0.5 | lifts the grid gaps out of pure black |
| `ambient` | grid | 0.0 | 0.0–0.5 | flat glare term |
| `gain` | grid | 1.0 | 0.5–2.0 | compensates the darkening the grid causes |
| `BGR` | grid | 0 | 0/1 | stripe order |
| `LUT_selector_param` | multiLUT | 1.0 | 1.0–2.0 | which psp-grey LUT |
| `mode` | psp-color | 1.0 | 1.0–3.0 | sRGB / DCI / Rec2020 target |

## 10. Testing

**Unit.** The presets introduce no parser construct that is not already covered: `absolute` scale,
`mipmap_inputN`, `wrap_modeN`, `textures` + `_linear`, and `#pragma parameter` extraction all have
existing assertions in `unittest/TestSlangParser.cpp`. Unit tests here are also self-contained by
convention — they write fixtures into a temp directory rather than reading the repo's assets — so
testing the shipped files by path would mean new test plumbing for little return. Following the
`lcd-psp-matrix` precedent, no new parser test is warranted for the presets themselves.

One gap does matter, because the two variants are distinguished *only* by preset-level values:
`TestSlangPresetParameters` never exercises a `.slangp`-level override, so the rule the design leans on
— preset value beats `#pragma parameter` default — is untested. Extend that test with a fixture whose
`.slangp` overrides a declared parameter and assert the override wins. Self-contained, ~10 lines, same
pattern as the existing case.

**On-device (AYN Thor, Vulkan and GLES).**

1. Grid is distinct at arm's length in both variants; compare against the unmodified reference preset
   side by side.
2. Colour and gamma parity with the reference preset — no shift, no darkening, no banding introduced
   by the added pass.
3. Sweep `PITCH` 1.0 → 3.0 in variant B and confirm the grid pitch tracks it and the image stays
   correctly registered (no drift, no half-cell offset).
4. Variant A ≡ variant B at `PITCH = 2.0`, allowing for pass 0's different kernel target.
5. Internal resolution sweep (1×, 3×, 5×) to confirm pass 0 improves antialiasing rather than
   introducing shimmer.
6. 60 fps held at 1906×1080 with four passes.

**Regression.** The existing unit tests stay green; the reference preset and every other pack file are
untouched.

## 11. Risks

| Risk | Mitigation |
|---|---|
| `mipmap_input` unsupported or slow on the device | `force_no_mipmaps = false` verified on both Vulkan and GL; if it misbehaves, replace the single `textureLod` with a bounded `PITCH × PITCH` tap loop (max 4×4) |
| Half resolution too soft for text-heavy games | that is what variant B's slider is for; a 1.5× fixed preset is a ~30-line addition |
| B-spline is a smoothing kernel, so variant B's pass 0 softens even at 1:1 | measured on device against a nearest/stock pass; if too soft, retarget pass 0 to `stock.slang` with `filter_linear` for B while keeping b-spline for A's real 2:1 reduction |
| Gamma-space downsample darkens edges | side-by-side against the reference preset; linear-light b-spline fork if it shows (§8.1) |
| Fork drifts from upstream `lcd-grid-v2` | header records the upstream path and commit; the fork is three localised changes |
| Pack not imported | presets do not appear in the browser; prerequisite documented in §4 |
| D3D11 pre-blit softens pass-0 input on Windows | expected and explained in §3.4; the Thor path is Vulkan/GLES |

## 12. Licensing

A pack overlay, so nothing is vendored into PPSSPP's own asset tree beyond the three files above:

- `b-spline-4-taps.slang` — MIT (stated in the file), referenced, not copied.
- `psp-color.slang` — public domain (hunterk / Pokefan531), referenced.
- `multiLUT.slang` and the `psp-grey` LUTs — libretro pack content, referenced.
- `lcd-grid-v2-pitch.slang` — the only derivative work: a fork of cgwg's `lcd-grid-v2.slang`,
  GPL-2.0-or-later, with attribution and a note of what was changed. Compatible with PPSSPP's
  GPL-2.0-or-later.
