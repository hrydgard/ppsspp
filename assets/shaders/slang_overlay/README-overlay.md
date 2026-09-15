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
the pack the presets appear in the shader browser but fail to load, naming the missing pack shader.
They are also not loadable from inside the PPSSPP install: the relative paths only resolve once this
tree is merged into the pack.

## Installing

Copy the contents of this directory over the imported pack, merging directories. A pre-existing
lower-case `shaders/` is used instead of `SHADERS/` if one exists.

- Windows: `<memstick>\PSP\SHADERS\slang\`
- macOS/Linux: `<memstick>/PSP/SHADERS/slang/`
- Android: the app-private external files dir, `<extFilesDir>/slang/`

```bash
cp -R assets/shaders/slang_overlay/. "<memstick>/PSP/SHADERS/slang/"
```

Nothing is overwritten - every file added here is new.

## Presets

These appear under the **`presets`** category in PPSSPP's shader browser.

| Preset | Pitch |
|---|---|
| `presets/handheld-plus-color-mod/lcd-grid-v2-psp-color-hidpi-2x.slangp` | Fixed 2x. A `b-spline-4-taps` pass resamples to an absolute 240x136 and the grid draws one cell per pixel. Leave `PITCH` at 1.0 - this preset has no mip chain to average with. |
| `presets/handheld-plus-color-mod/lcd-grid-v2-psp-color-hidpi-tunable.slangp` | Set by the `LCD cell pitch (source px)` parameter, 1.0-3.0, default 2.0. Cells are averaged through the mip chain. |

The colour path is the reference `lcd-grid-v2-psp-color` preset's, unchanged: `multiLUT` with the PSP
grey LUTs, then `psp-color`, with the same identity subpixel matrix and tone parameters. Two things
do differ: in the 2x preset `multiLUT` runs on the resampled 240x136 image (pass 0 is `absolute` at
that size), while in the tunable preset it stays at 480x272 (pass 0 is `source`/`1.0`); and both
presets clamp to edge rather than to border, so edge pixels differ slightly from the reference.

Raising PPSSPP's internal resolution improves the downsample pass's antialiasing at no cost to the
grid; 3x-5x is a good range.

## Provenance and licence

`handheld/shaders/lcd-cgwg/lcd-grid-v2-pitch.slang` is a fork of the pack's
`handheld/shaders/lcd-cgwg/lcd-grid-v2.slang` by cgwg (commit
`4ecd48510e4a0f936617c5e899dd6c4fd50abbd7`), GPL-2.0-or-later like the original, with three localised
changes recorded in its header. The presets reference pack shaders without copying them.
