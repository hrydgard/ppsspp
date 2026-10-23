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

A region is `w,h` from the top left, or `x,y,w,h`. A region that doesn't start at the top left only hashes
its rectangle, but its dump and replacement still reach from the top left to its bottom right corner, so the
image is placed the same way as any other. What's above or left of the region in the PNG isn't drawn.

When replacing, the regions of the texture are tried largest first, with the whole texture before all of
them, and the first one with a replacement wins. A region has to cover what draws sample (with two texels of
slack), otherwise the replacement would leave something out. When a later draw samples more, the texture is
looked up again.

### Learned regions

Without anything in `textures.ini` for a texture, each dump of it is made with the region its draws sampled
while the dump waited. These regions are written to `new/shadowranges.ini` under `[learnedshadowranges]`.
They're needed to find the replacements for those dumps again, so after each dumping session, copy the
sections of that file into `textures.ini`, replacing the ones there:

```ini
[learnedshadowranges]
0x08b31020,512,512 = 480,272|0,16,480,120
```

The file only has what's in `textures.ini` and what this session dumped. Regions you deleted don't come back
from earlier sessions, so copy it before you start the next one.

### Declared regions

When you know what sizes a texture's images are, list them under `[shadowranges]` in `textures.ini`:

```ini
[shadowranges]
0x08d70930,256,256 = 256,256|256,240
```

For a texture listed there:

- Dumps only use the listed regions. Nothing is learned for it, and `shadowMaxRegions` doesn't apply.
- Of the listed regions that hold what's drawn, a dump uses the largest one whose texels didn't change while
  the image stayed the same. In the example, a sprite sheet that fills the texture is dumped at `256,256`,
  even when only its top frames were drawn. When the game keeps other data in the last 16 rows and that data
  changes while the image stays, the image is dumped at `256,240` without it.
- If nothing changed while the dump waited, it's the largest listed region, so a sheet is never cut.
  Data after a smaller image that happened not to change is dumped with it then. Raising
  `shadowSettleSeconds` gives it more time to change.
- If draws sample more than any listed region, the dump is the whole texture.
- List the whole texture (`256,256` here) when some of its images fill it. If you leave it out, a dump that
  doesn't fit the other regions still uses the whole texture, but it's only chosen as a last resort.

`new/shadowranges.ini` copies your `[shadowranges]` lines with the same regions (largest first).

`[hashranges]` entries are tried like learned regions. They aren't written to `new/shadowranges.ini`, and
nothing writes new ones. Keep in mind that with `shadowTextures` off, a hash range crops every texture at that
address.

## Options

All in `[options]`:

| Option | Default | |
|---|---|---|
| `shadowTextures` | `false` | Turns it on. |
| `shadowSettleSeconds` | `4` | How long a content's region has to stay the same before it's dumped. |
| `shadowMinVisibleSeconds` | `0` | Contents shown for less than this aren't dumped at all, like each step of text that's typed out letter by letter. Try `0.5`. 0 turns it off. |
| `shadowMaxRegions` | `8` | Learned regions per texture. Each is one more hash for every new content of it. Past this, a dump uses the smallest known region that covers it. Doesn't apply to textures in `[shadowranges]`. |
| `shadowRegionAlign` | `1` | Rounds regions up to a multiple of this, so near-equal ones merge. Rounding can take in rows of other data, so leave it at 1 unless a texture gets many regions. |
| `shadowMemoryMB` | `1024` | RAM for waiting dumps. Over it, the oldest are written early with the region they have. |

With `saveIndexed`, textures with a palette are held back the same way, as their indices. One capture serves
every palette (the indices and the hash don't depend on it), so a sprite sheet drawn with many palettes, or
with an animated one, is written once as `<hash>_indexed.png` with the CLUT hash zeroed. List it in
`[indexed]` to replace it for all palettes.

## Getting fewer dumps

Start with this, then change one option at a time:

```ini
[options]
hash = xxh64
shadowTextures = true
saveIndexed = all
```

Empty the `new` folder before each test run (both the PNGs and `shadowranges.ini`, never just one of them), play
the same part of the game, and compare how many files you get.

The most reliable way to get few, clean dumps is to declare the sizes of the textures you know under
`[shadowranges]` (see [Declared regions](#declared-regions)). The options below help with the ones you don't.

### Finding out why a texture has duplicates

Turn on the TexReplacement log at Info level. Each dump logs a line like
`Shadow texture 08b31020 512x512 dumped with region 0,16 480x120`. Compare the lines of the duplicates of one
image (same address, different hash), and look up what you see:

| What you see | Why | What to change |
|---|---|---|
| The region is the whole texture | It's drawn in 3D, or was once. Shadow textures can't tell which part 3D draws sample. | Declare its sizes under `[shadowranges]`, or try `reduceHash` |
| One image dumped with different regions (480x136, 480x200, 480x272) | It was dumped before its draws reached their full size, or each time it's drawn a bit differently | Declare its size under `[shadowranges]`. Or raise `shadowSettleSeconds`, then `shadowRegionAlign`, then lower `shadowMaxRegions` |
| A sprite sheet is cut (only the frames that were drawn) | It was dumped with the region its draws sampled | Declare the full size under `[shadowranges]` |
| A declared texture is dumped with trash at the edge | Nothing changed there while the dump waited, so the larger region was used | Raise `shadowSettleSeconds`, or declare only the smaller region for it |
| The same image in many colors | Each palette is its own texture | `saveIndexed = all` (or `true` with the texture in `[indexed]`) |
| Text that grows one letter at a time | Every letter is a new image | `shadowMinVisibleSeconds = 0.5`, and replace the box with `[layered]` and HD glyphs (see below) |
| `over the memory budget, dumping early` in the log | Dumps were written before they settled | Raise `shadowMemoryMB` |
| The same image at different addresses | The game loads it to different places | `ignoreAddress = true` (needs xxh32 or xxh64) |
| The images really differ where they're drawn (animation, a counter, a font atlas filling up) | These aren't duplicates | Leave them, or stop dumping them with `[ignore]` |
| Textures you'll never replace, made again for every map, menu or save slot (map tile atlases, thumbnails) | The game builds them from other textures | Put their address range in `[ignore]` (see [Ignoring textures](#ignoring-textures)) |
| Byte-identical dumps of one image at different addresses, with `ignoreAddress` | From a build before dumps in `new/` were checked at start | Delete the copies, they won't come back |
| You can't tell which texture a dump is | With `ignoreAddress`, or for palette dumps, the name doesn't say | Look the file up in `new/dumps.txt` |

### Each option, and which way to turn it

**`shadowSettleSeconds`** (default 4): how long a content's region has to stop growing before it's written.
- Raise it for textures in `[shadowranges]` that get dumped with trash at the edge: it's how long the data
  next to an image has to change, to be left out.
- Raise it (8-15) when one image is dumped with several regions, or when a texture's later draws use more of
  it than its first ones (menus that open up, screens that fade in from a small part). More waiting means
  the region is complete when it's hashed, so one dump instead of one per size.
- Lower it only to see dumps sooner while testing. It never makes fewer files.
- Cost: dumps appear later, and more of them wait in RAM.

**`shadowMinVisibleSeconds`** (default 0, off): contents shown for less than this aren't dumped.
- Raise it (0.3-1) for text typed out letter by letter, and other contents that only last a few frames
  (fades, flashes, transitions). Only what stays on screen gets dumped.
- Lower it, or set it to 0, if a texture you need is missing from the dumps, for example the empty dialog
  box for `[layered]`, which is often only shown for a frame. Anything shown for less than this is never
  written, so too high a value loses textures.

**`shadowRegionAlign`** (default 1): rounds regions out to a multiple of this.
- Raise it (4, 8, 16) when one texture gets many regions that only differ by a few texels (270 and 272,
  or x 0 and x 2). They become the same region, so their contents get the same key.
- Lower it back to 1 if raising it brought duplicates back: rounding out can reach into the data next to an
  image, which is the trash shadow textures keep out of the key.
- Change it before a dumping session, not in the middle of one: regions found with another value don't match.

**`shadowMaxRegions`** (default 8): how many different learned regions a texture can have. Past this, a dump
uses the smallest known region that covers it. Textures in `[shadowranges]` aren't limited by it.
- Lower it (2-4) for sprite sheets and atlases that are drawn in many slightly different ways, so they don't
  end up with one dump per way.
- Raise it if a texture that's really used in several separate parts (an atlas of unrelated images) gets
  duplicates after lowering it: the shared region it snaps to can include data that changes.
- Each region costs one more hash for every new content of that texture while replacing, so keep it low
  for textures that change often.
- Setting it to 1 snaps everything a texture draws to the region it was first dumped with, which cuts sprite
  sheets whose first dump only had part of them drawn. Declare those under `[shadowranges]` instead.

**`shadowMemoryMB`** (default 1024): RAM for dumps that are waiting.
- Raise it when the log says `over the memory budget, dumping early`, or when you raise
  `shadowSettleSeconds` a lot. Dumps written early still have their region growing, so they can be duplicates.
- Lower it only if PPSSPP runs out of memory.

**`saveIndexed`** (off by default, not a shadow option): `all` dumps every texture with a palette once, as
its indices, for all palettes. Use it when the same image comes in several colors. Turn it off if you want
color dumps of those textures instead.

**`ignoreAddress`** (not a shadow option): leaves the address out of the key. Use it when the same image is
loaded to different addresses. Don't use it if different images share a hash at different addresses, which is
rare with xxh64.
- Dumps are still named with the address they were first dumped from, and the address is dropped when the
  names in `[hashes]`, `[filtering]` and `[indexed]` and the files in the pack are matched. So you can still see
  where a texture is, and put that range in `[ignore]`. Packs with `00000000` addresses in their names still work.
- A content is dumped once, named after the address it was first seen at. When dumping starts, the files already
  in `new/` count as dumped with their address dropped, so a later session doesn't write a copy of one under the
  address it's found at then. To get a texture dumped again, delete its file from `new/`.
- An empty entry in `[hashes]` with only an address (`123456780000000000000000 =`) still stops dumping at that
  address. Entries with an address and a CLUT hash but no texture hash are ignored with `ignoreAddress`, since
  without the address they'd match that CLUT anywhere.

## Ignoring textures

Some textures are never worth replacing, and the game makes new ones all the time: map tile atlases that are
put together again for every map, thumbnails of save slots, previews. List where they are in `[ignore]`, and
they're neither dumped nor replaced:

```ini
[ignore]
0x09a00000-0x09c00000 =
0x08b31020,32,16 =
```

- An entry is an address, or a range of addresses with both ends included, optionally followed by a size. With
  a size, only textures of that size there are ignored, so a range can hold thumbnails you ignore and other
  textures you keep.
- It works the same with `ignoreAddress` on or off.
- Use it instead of `[hashes]` entries without an address and texture hash, like `000000000000070700000000 =`.
  For textures without a palette, the middle 8 digits of a name are the texture's size, not a CLUT hash
  (`0707` is 128x128), so an entry like that stops dumping every 128x128 texture of the game.

### Finding where a dump came from

Every dump adds a line to `new/dumps.txt`:

```
08b3102000000000ab12cd34_indexed.png addr=0x08b31020 size=512x256 format=CLUT8 key=08b31020d1e2f3a4ab12cd34 region=0,0,512,256
```

It has the address, size and format of the texture the file was dumped from, and in `key` its name with the
palette hash of the palette it was dumped with, which palette dumps (`_indexed`) don't have in their names. To find the range of a kind
of texture you want to ignore, look up a few of its dumps there.

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
`box_original.png` are the text (and its shadow). A noisy or textured background is fine, since it's the same
in every content and cancels out. Contents of a layered texture aren't dumped.

To get `box_original.png`, dump with `shadowMinVisibleSeconds = 0` and look for the box before its first
letter appears (it's usually there for a frame or more).

### HD text

The text is split into pieces: texels that touch each other (diagonally too), which is usually one letter
with its shadow. Each piece is drawn in one of two ways:

- **With an HD glyph from the pack.** While dumping, each new piece is written once to `new/glyphs/`, as
  `<key>.png` at the game's resolution, with the rest transparent. Make an HD version of it, at exactly the
  box's scale (a 6x11 glyph is 24x44 at 4x), with a transparent background, and put it in `glyphs/` in the
  pack with the same name. It's drawn over the HD box with its transparency, so draw the shadow into it as
  well, as partly transparent texels.
- **Upscaled**, for pieces without an HD glyph yet: the game's texels are smoothed with
  `layeredTextScaler`, and drawn over the HD box. So the text never disappears while the glyph set isn't
  finished.

A piece's key comes from its shape and its main color, not its exact texels: a shadow blended with a
textured background differs with where the letter is, its shape doesn't. Glyphs work with any font width,
and the same glyph files serve every layered texture in the pack.

Options, in `[options]`:

| Option | Default | |
|---|---|---|
| `layeredGlyphs` | `true` | Looks up and dumps HD glyphs. `false` upscales all text. |
| `layeredTextScaler` | `xbrz` | How text without an HD glyph is upscaled: `xbrz`, `hybrid`, `bicubic`, `hybrid_bicubic`, or `none` for plain blocks. Works for a box scale of 2 to 5, above that it's blocks. |

Which way to turn them:

- **The same letter is dumped many times in `new/glyphs/`.**
  - Usually letters whose shadows touch the next letter: each pair (like `rn`) is its own piece. That's a
    fixed set of pairs, so the count stops growing. Make HD glyphs for the pairs too.
  - If one letter keeps coming back in slightly different shapes, the game probably draws it blended
    with the background at its edges (anti-aliased text). Glyphs don't suit that font. Set
    `layeredGlyphs = false` and rely on `layeredTextScaler`.
  - Colored words (names, keywords) are separate glyphs per color. That's expected, since the HD glyph has
    to be in that color too.
- **Glyphs aren't dumped at all.** They're only dumped while a texture is in `[layered]`, with both
  replacing and saving on. Pieces bigger than 64x64 texels (like the whole box during a fade) aren't glyphs.
- **Upscaled text looks too soft or smeared.** Try `hybrid`, which keeps edges sharper, or `none` for clean
  blocks.
- **Upscaled text has jagged or stair-step edges.** Use `xbrz` (the default), or `bicubic` for smooth text.
- **A glyph shows up blocky or smoothed although you made an HD one.** Its size doesn't match the box's
  scale, which the log says (`Layered: glyphs/... is AxB, should be CxD`).

## Limits
- Swizzled textures can only lose whole rows of 8, DXT ones whole rows of 4, and both keep their full width.
- Several draws that sample different parts of one texture while it holds the same contents are merged into
  one rectangle around all of them.
- UVs are only tracked for through mode with 16-bit or float texcoords. Anything else counts as the whole
  texture, and once a texture is drawn that way its new contents are keyed by all of it.
- Dumps made with shadowTextures need it for replacing too, except for textures with a single region.
- Hashing a few regions per new content costs some CPU time while replacing.
