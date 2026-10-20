# Shadow textures (experimental)

PSP games often declare a texture bigger than the image in it, because texture sizes are powers of two. A
480x272 title screen sits in a 512x512 texture, and whatever the game's allocator puts after the image (other
textures, sound, game state) fills the rest. Texture replacement hashes the whole declared texture, so every
change to that other data is a new hash, and dumping writes the same image again and again.

With `shadowTextures = true` in `[options]` of `textures.ini`:

- The replacement key only covers the rectangle of the texture that draws sample. In through mode (2D:
  menus, HUD, title screens) that's known from the vertex UVs. For 3D draws it's the whole texture, as before.
- The texture cache keeps a separate hash of the whole texture, only to notice changes, so nothing goes
  stale on screen.
- Dumps aren't written right away. Each new content waits in RAM with a copy of the texture's memory (its
  "shadow") until the region draws sample has stopped growing for `shadowSettleSeconds`. Then it's hashed
  again over its final region and written cropped to it. Contents that only differed outside that region get
  the same key there, and only the first one is written.
- A content that was already dumped is recognized right away and isn't captured again.

## Regions

Each dump is made with the region its draws sampled. The regions found are written to
`new/shadowranges.ini`, which also includes those from `textures.ini` and from earlier dumping sessions. Copy
its two sections into `textures.ini`, replacing the `[hashranges]` and `[shadowranges]` there.

- A texture with one region gets a plain `[hashranges]` entry. Those dumps also work without shadowTextures.
- A texture whose contents were drawn with different regions (an atlas that's sometimes used in full,
  sometimes in part) gets a `[shadowranges]` entry listing them:

```ini
[shadowranges]
0x08b31020,512,512 = 480,272|0,16,480,120
```

A region is `w,h` from the top left, or `x,y,w,h`. A region that doesn't start at the top left only hashes
its rectangle, but its dump and replacement still reach from the top left to its bottom right corner, so the
image is placed the same way as any other. What's above or left of the region in the PNG isn't drawn.

When replacing, the regions of the texture are tried largest first, with the whole texture before all of
them, and the first one with a replacement wins. A region has to cover what draws sample (with two texels of
slack), otherwise the replacement would leave something out. When a later draw samples more, the texture is
looked up again.

## Options

All in `[options]`:

| Option | Default | |
|---|---|---|
| `shadowTextures` | `false` | Turns it on. |
| `shadowSettleSeconds` | `4` | How long a content's region has to stay the same before it's dumped. |
| `shadowMinVisibleSeconds` | `0` | Contents shown for less than this aren't dumped at all, like each step of text that's typed out letter by letter. Try `0.5`. 0 turns it off. |
| `shadowMaxRegions` | `8` | Regions per texture. Each is one more hash for every new content of it. Past this, a dump uses the smallest known region that covers it. |
| `shadowRegionAlign` | `1` | Rounds regions up to a multiple of this, so near-equal ones merge. Rounding can take in rows of other data, so leave it at 1 unless a texture gets many regions. |
| `shadowMemoryMB` | `1024` | RAM for waiting dumps. Over it, the oldest are written early with the region they have. |

With `saveIndexed`, textures with a palette are held back the same way, as their indices. One capture serves
every palette (the indices and the hash don't depend on it), so a sprite sheet drawn with many palettes, or
with an animated one, is written once as `<hash>_indexed.png` with the CLUT hash zeroed. List it in
`[indexed]` to replace it for all palettes.

## Layered textures

Some games draw text into a texture on the CPU, one letter at a time, over a background like a dialog box.
Every letter is a new texture, and every page of text a new image, so they can't be replaced one by one.
`[layered]` replaces the background instead:

```ini
[layered]
0x08b31020,256,128 = box_original.png|box_hd.png
```

- `box_original.png` is a dump of the texture without any text, as PPSSPP wrote it (dump with texture
  scaling off). It can be smaller than the texture, from the top left.
- `box_hd.png` is your HD version of it, scaled by a whole number (2x, 4x...).

For every content of that texture without a replacement of its own, the texels that differ from
`box_original.png` (the text and its shadow) are drawn over `box_hd.png`, each as a block of the scale's
size. A noisy or textured background is fine, since it's the same in every content and cancels out. Contents
of a layered texture aren't dumped.

To get `box_original.png`, dump with `shadowMinVisibleSeconds = 0` and look for the box before its first
letter appears (it's usually there for a frame or more).

## Limits
- Swizzled textures can only lose whole rows of 8, DXT ones whole rows of 4, and both keep their full width.
- Several draws that sample different parts of one texture while it holds the same contents are merged into
  one rectangle around all of them.
- UVs are only tracked for through mode with 16-bit or float texcoords. Anything else counts as the whole
  texture, and once a texture is drawn that way its new contents are keyed by all of it.
- Dumps made with shadowTextures need it for replacing too, except for textures with a single region.
- Hashing a few regions per new content costs some CPU time while replacing.
