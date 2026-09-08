# The PKG package format (PSP game updates)

A `.pkg` is the NPDRM container Sony distributed downloadable content in - full PSN games, DLC,
themes, and the thing these notes are about: **game updates**. An update package holds a patched
EBOOT (`PBOOT.PBP`) plus whatever data files the patch replaces, and installing it drops them in
`ms0:/PSP/GAME/<DISC_ID>/`. The patched EBOOT then runs with the original UMD (or the original
PSN game) still supplying everything it doesn't override.

This document is what was learned decoding some update packages on 2026-08-21. `Tools/pkg.py` is a
working parser/extractor built from it; PPSSPP reads and installs them itself now - see "How PPSSPP
handles them" and the verification sections at the end.

Format reference: <https://www.psdevwiki.com/ps3/PKG_files>, cross-checked against
[pkg2zip](https://github.com/mmozeiko/pkg2zip).

## File layout

```
+0x000  header            (0xC0 bytes, plaintext)
+0x0C0  extended header   (0x40 bytes, plaintext, PSP/Vita only)
+0x100  hashes/signatures
+0x280  metadata          (plaintext, offset and count are in the header)
 ...
+data_offset              encrypted area: item table, then filenames, then file contents
```

### Header

```
+0x00  u32       "\x7FPKG" magic (0x7F504B47)
+0x04  u16       revision. 0x8000 retail, 0x0000 debug
+0x06  u16       type. 1 = PS3, 2 = PSP/Vita
+0x08  u32       metadata offset (0x280 in everything seen)
+0x0C  u32       metadata entry count
+0x10  u32       metadata size
+0x14  u32       item count
+0x18  u64       total package size
+0x20  u64       data offset - start of the encrypted area
+0x28  u64       data size
+0x30  char[0x30] content id, e.g. "JP0177-ULJM05681_00-PJD2UPDATEVR0101"
+0x60  u8[0x10]  QA digest
+0x70  u8[0x10]  riv - the AES counter block (see below)
+0x80  u8[0x40]  header CMAC and signatures
```

### Extended header

Present on PSP and Vita packages, magic `"\x7Fext"` (0x7F657874) at +0xC0. The only field that
matters for reading the package is the **key index**, a u32 at +0xE4 - or equivalently
`header[0xE7] & 7`, which is how pkg2zip reads it. Every PSP package seen uses key index 1.

### Metadata

A flat sequence of `u32 id, u32 size, u8 value[size]` records. The ones worth reading:

| id | Meaning |
| --- | --- |
| 2 | content type. **7 = PSP** (also 0xE/0xF/0x10 for PC Engine / Minis / NeoGeo) |
| 4 | package size |
| 6 | title id |
| 13 | offset and size of the item table, inside the encrypted area |
| 14 | offset and size of `PARAM.SFO`, inside the encrypted area |

Everything else (DRM type, SDK revision, QA digest, install dir) is informational.

## Encryption

The whole area from `data_offset` on is AES-128-CTR, with the counter block starting at `riv`
(header +0x70) and incrementing once per 16 bytes: the block at byte offset *n* of the encrypted
area uses counter `riv + n/16`. Offsets in the item table are relative to `data_offset`, so
that division is straightforward - no separate bookkeeping.

Two different keys are used *within the same package*, and each item says which one applies via
its own `psp_type` byte (see the item table below):

| Package / item | Key |
| --- | --- |
| PS3 package, or PSP item with `psp_type != 0x90` | `2e7b71d7c9c9a14ea3221f188828b8f8` |
| PSP item with `psp_type == 0x90` | `07f2c68290b50d2c33818d709b60e62b` |
| Vita, key index 2/3/4 | `AES-ECB(vita_key_N, riv)`, key by index |

That per-item split is the one thing that isn't obvious from the wiki page and will make a reader
produce garbage filenames for most of a package while a couple of entries decode perfectly. In a
game update it's the `PBOOT.PBP` and the patch data files that carry `0x90`; the icons,
`PARAM.SFO`, `PS3LOGO.DAT`, the directory entries and `ISO.BIN.EDAT` use the PS3 key.

## Item table

`item_count` records of 0x20 bytes, at the item table offset from metadata id 13 (0 in every
update package seen, i.e. right at the start of the encrypted area):

```
+0x00  u32  filename offset (relative to data_offset, always 16-byte aligned)
+0x04  u32  filename length
+0x08  u64  data offset (relative to data_offset, always 16-byte aligned)
+0x10  u64  data size
+0x18  u8   psp_type - 0x90 selects the PSP key, see above
+0x19  u8[2] padding
+0x1B  u8   flags
+0x1C  u32  padding
```

Filenames are stored in the encrypted area too, and are decrypted with **the item's own key**, not
the package's main key.

`flags` is a content type, and in these packages it maps 1:1 onto how the file contents are
encrypted - a decoder can tell what it is holding before looking at it:

| flags | Meaning | Contents start with |
| --- | --- | --- |
| 2 | NPDRM EDAT | `NPD\0` - only ever `ISO.BIN.EDAT` |
| 3 | plain file | whatever it is (PNG, `\0PSF`, ...) |
| 4 | directory | - |
| 5 | PSP EDAT | `\0PSPEDAT` |
| 8 | PSP EDAT (`.sprx` modules) | `\0PSPEDAT` |
| 11 | PBP | `\0PBP` |

Both flags 5 and 8 give you a `\0PSPEDAT`, but they are not the same thing inside. A flags-5 data
file wraps a PGD, which PPSSPP decrypts at runtime through `sceNpDrmEdataSetupKey()` in
`Core/HLE/scePspNpDrm_user.cpp`. A flags-8 `.sprx` wraps an encrypted PRX instead, and goes through
the module loader - see "NPDRM `.sprx` modules" below. The byte at 0x0E of the header tells them apart
(3 for the PGD kind, 1 for the PRX kind).

## What an update package contains

Always this shape:

```
PARAM.SFO                      CATEGORY=PP, TITLE_ID, VERSION
PS3LOGO.DAT                    a PNG, despite the name
ICON0.PNG / PIC0.PNG / PIC2.PNG   sometimes
USRDIR/                        directory entry
USRDIR/CONTENT/                directory entry
USRDIR/CONTENT/PBOOT.PBP       the patched EBOOT
USRDIR/CONTENT/...             the patch data files
USRDIR/ISO.BIN.EDAT            272 bytes, NPD header
```

The outer `PARAM.SFO` is the *package's* - `CATEGORY=PP` (game patch), and its `VERSION` is the
package version, not the patch version. The interesting SFO is the one **inside** `PBOOT.PBP`:
that one has `CATEGORY=PG`, `DISC_ID`, `DISC_VERSION`, `APP_VER` (the patch version) and
`PSP_SYSTEM_VER` (the firmware the patch needs), which is what a real PSP matches against the
disc before deciding to boot the patch.

## What the packages measured

- All are content type 7, `CATEGORY=PP`. Some are for digital NP\* titles, others for UMD UL\*/UC\*
  titles. `PSP_SYSTEM_VER` ranges 6.10 to 6.60.
- **Every single `PBOOT.PBP` uses PRX tag `0x2E5E10F0`** in its `DATA.PSP`. PPSSPP already has
  that key - `Core/ELF/PrxDecrypter.cpp`, in the `TAG_INFO2` table, commented
  "5.00 PSP-2000 (Game PSN Update 2 LBP)". So **no new crypto is needed to run these**.
- Verified end to end rather than assumed: extract Hatsune Miku Project DIVA 2nd's update, rename
  `PBOOT.PBP` to `EBOOT.PBP`, and boot it headless with the UMD mounted -

  ```
  ./build/PPSSPPHeadless -i --graphics=software --memstick=<ms> \
      --mount="Hatsune Miku - Project Diva 2nd (Japan).iso" \
      <ms>/PSP/GAME/ULJM05681/EBOOT.PBP
  ```

  It logs `Decrypting tag 2E5E10F0`, loads the ELF (`tag=ELF/PdvApp`), resolves its imports, and
  reads `Diva2Data.cpk` / `Diva2Script.cpk` / `Diva2Sound.cpk` off `disc0:`. Runs without error.
  (`--mount` is what makes that work: `Load_PSP_ELF_PBP` in `Core/PSPLoaders.cpp` mounts the ISO
  on `disc0:`, `umd:` and `umd1:` when booting an ELF or PBP.)

**pkg2zip cannot extract update packages**, so it is not an alternative here. Its PSP path only
recognises `USRDIR/CONTENT/EBOOT.PBP` (a full PSN game, which it converts to an ISO),
`PSP-KEY.EDAT` and `CONTENT.DAT`, and `continue`s past everything else - `PBOOT.PBP` and every
patch file are silently dropped, with no warning that anything was skipped.

## How PPSSPP handles them

Four pieces:

- **`Core/Util/PkgUnpack.cpp`** reads a package: header, item table, both PARAM.SFOs, and the
  decryption. `PkgReader::Open()` gives you a `PkgInfo` with the disc ID, disc version and patch
  version; `InstallPkg()` writes the payload out. Sits next to `PSARUnpack.cpp`, and like it needs
  nothing but the AES already in `ext/libkirk`.
- **`UI/InstallPkgScreen.cpp`** is what opening a `.pkg` gets you, the same way a `.zip` gets
  `InstallZipScreen` - it shows what the update patches, what it'll take up on disk, and where it's
  going. The size is exact rather than an estimate: package contents aren't compressed, so summing
  the item table is the answer. `GameManager::InstallPkgOnThread()` does the work.
- **`GameScreen`** shows an installed update in the info pane, and offers "Delete Game Update" in
  its context menu. Deleting takes the whole `PSP/GAME/<DISC_ID>` folder when the update is all
  that's in it, and only `PBOOT.PBP` when a digital game shares the folder - nothing records what
  an install wrote, so the executable is the only part still identifiable afterwards.
- **`FindGameUpdatePBOOT()` in `Core/PSPLoaders.cpp`** is the boot-time half. Starting a disc looks
  for `ms0:/PSP/GAME/<DISC_ID>/PBOOT.PBP`, and boots that instead of `disc0:/PSP_GAME/SYSDIR/EBOOT.BIN`
  if it's there, leaving the disc mounted.

`PPSSPPHeadless --install-pkg=DIR <file.pkg>` does an install without the UI, which is how the
above got tested. It prints what the package is and installs into DIR exactly (the app picks
`PSP/GAME/<DISC_ID>` itself).

### Install layout

The package's own PS3-style wrapping is stripped: `USRDIR/CONTENT/<x>` and `USRDIR/<x>` both become
`<x>` in the game folder, and the `USRDIR` and `USRDIR/CONTENT` directory entries are dropped
rather than created. The root-level files - `PARAM.SFO`, `PS3LOGO.DAT`, `ICON0.PNG`, `PIC0.PNG`,
`PIC2.PNG` - are store metadata and are **not** installed. Writing that `PARAM.SFO` in particular
would be actively wrong: a folder holding a `PARAM.SFO` and no `EBOOT.PBP` is what PPSSPP
identifies as *save data*, so the update would show up in the savedata list.

### Which updates get used

The update's `DISC_ID` has to match the disc's, or it's ignored with a warning - that's what keeps
an update from being applied to the wrong game.

`DISC_VERSION` is advisory. An update is built against one specific disc revision, and PPSSPP logs
a warning when they differ, but still boots it: refusing outright is a worse failure mode than
letting the user find out, since they installed it deliberately. This is not what a real PSP does.
It comes up in practice - the LittleBigPlanet v2.05 update targets disc version 1.00, and the
common European dump is 1.01, and it works.

There's no setting to turn this off. Anyone who has a `PBOOT.PBP` sitting in a game folder either
installed it here or copied it off a real memory stick, and in both cases booting it is what they
were after.

### Verified

- All packages parse and install through the C++ path, byte-identical to `Tools/pkg.py` - each
  one reports its disc ID and versions and writes its payload without an error.
- Hatsune Miku Project DIVA 2nd (ULJM05681): install the v1.01 update, boot the UMD, PPSSPP boots
  `PBOOT.PBP` and the game reads its CPKs off `disc0:`.
- LittleBigPlanet (UCES01264): install the v2.05 update, boot the v1.01 UMD, and the patched game
  opens `ms0:/PSP/GAME/UCES01264/PATCH.ARC` alongside the disc's own `lbp_archive.arc` - the update
  is actually in use, not just booted.
- `python3 test.py -g --graphics=software`: no failures, so restructuring the disc boot
  path didn't disturb anything.
- Truncated and item-table-corrupted packages are refused with a message rather than crashing.
  Corruption *inside* file data still installs - nothing here verifies the package CMAC, same as
  every other PKG tool.
- The browser listing, install screen and install itself were checked by hand in the app.

## Digital (NP\*) titles

About half of the packages patch a digital title rather than a UMD, and that half is tested too.

The one that settles the question is **Super Robot Taisen Operation Extend (NPJH50521)**, because
it's a real NPUMDIMG `EBOOT.PBP` rather than a decrypted ISO dump - `NPDRM: PSAR ID: 4d55504e`,
mounted on `disc0:` by the NPDRM block device, with the game's own 560 MB EBOOT sitting in the same
folder as the update. All eight of its update revisions were installed and booted in turn, and each
one loads a distinguishably different executable:

```
disc executable  .text 0x419b1c
v1.01 0x41fb8c   v1.02 0x4233dc   v1.03 0x42952c   v1.04 0x42a53c
v1.05 0x42a61c   v1.06 0x42a59c   v1.07 0x42b48c   v1.08 0x42b7dc
```

So **`ISO.BIN.EDAT` does not re-key the PBOOT**, which was the open worry: a digital title's patched
executable is encrypted exactly like a UMD one, and needs nothing PPSSPP doesn't already have.

**`DISC_VERSION` being advisory matters far more than expected.** Most of the pairs hit a mismatch,
because the dumps in circulation are later disc revisions than the updates were built against.
Refusing outright would make most of them unusable. Elminage Original was the one clean
exact-match case, disc 1.01 against an update for 1.01, and it boots without a warning.

### NPDRM `.sprx` modules

Package payloads are full of `\0PSPEDAT` files. That's fine for
*data*: `sceNpDrmEdataSetupKey()` in `Core/HLE/scePspNpDrm_user.cpp` wraps an open file descriptor
with the `0x04100002`/`0x04100001` ioctl pair, and the game reads plaintext.

A few packages wrap **executables** that way - `.sprx` modules the game loads with
`sceKernelLoadModuleNpDrm`. Those need more than the data path does. Until they were handled, God
Eater 2 (NPJH50832) installed cleanly, booted its `PBOOT.PBP`, and then looped forever failing to
load `system.sprx`; Shiren 4 Plus (NPJH50698), which keeps the whole game in one `.sprx` behind a
small loader, failed the same way.

An NPDRM module is two layers, and the loader originally saw only the outer one:

```
+0x00  "\0PSPEDAT" header, 0x90 bytes
       +0x08  u32   key mode; low byte is what sceNpDrmGetFixedKey takes (3 in all of these)
       +0x0C  u16   payload offset (0x90 in everything seen)
       +0x0F  u8    flag bits: 1 = XOR in the licensee key, 2 = XOR in the 16 bytes at 0x40
       +0x10  char[0x30]  content ID, "JP0365-NPJH50698_00-SIREN4PLUS2012MA"
+0x90  a normal "~PSP" PRX: tag 0x407810F0 at 0xD0, decrypt_mode 23 at 0x7C
```

`sceKernelLoadModule()` steps over the EDAT header, and the PRX inside then decrypts. Two keys go
into that, both worked out from JPCSP (`ModuleMgrForUser.sceKernelLoadModuleNpDrm`, `crypto/DRM.java`,
`crypto/PRX.java`, `crypto/KeyVault.java`), and both feed `pspDecryptType5()`, which already had a
slot for each.

**xor2, the per-content key** - `NpDrmDeriveModuleKey()` in `Core/HLE/scePspNpDrm_user.cpp`, four
steps in order:

1. `sceNpDrmGetFixedKey(kirk, key, edat+0x10, 0x01000000 | edat[0x08])` - already in
   `ext/libkirk/amctrl.c`, and identical to JPCSP's `hleNpDrmGetFixedKey`: our `key_363C` is its
   `drmFixedKey`, our `key_357C[0/1/2]` are its `drmEncKey1/2/3`, and its AES-CBC under an all-zero
   IV over one block is our `AES_encrypt`. Nothing had called this function before.
2. If `edat[0x0F] & 1`: XOR the licensee key the game passed to `sceNpDrmSetLicenseeKey()`, which
   this file already kept but never used. The game sets it before it loads the module.
3. If `edat[0x0F] & 2`: XOR the 16 bytes at `edat+0x40`. None seen here use it.
4. AES-128 decrypt the result under `drmModuleKey`, the one constant that had to be added here.
   JPCSP does CBC with a zero IV; over a single block that is a plain `AES_decrypt`.

**xor1, a static key picked by `decrypt_mode`** - the PRX header byte at 0x7C being 23
(`DECRYPT_MODE_SPRX`), which is what these payloads are. JPCSP keys this on the mode rather than on
the tag, and so do we: tag 0x407810F0's table entry has no seed of its own in JPCSP's tables either,
so our table was never wrong, it just had nothing to say about a case selected somewhere else.
`pspDecryptType5()` takes the mode-derived XOR when the mode calls for it and falls back to the tag
table's otherwise - the same precedence JPCSP uses, which leaves every tag that does carry a seed
(the `pauth` ones) exactly as it was.

No new decryption logic was needed. `pspDecryptType5()` is structurally identical to JPCSP's "new
method" for type 5: `expandSeed(pti->key, pti->code, seed)` XORs xor2 over the 0x90-byte scrambled
key buffer as `RoundXOR(buf2, 0, 0x90, xor2, null)` does; `PRXType5::decrypt()` XORs both over the
0x50-byte kirk header and SHA1 as `RoundXOR(buf2, 0x14, 0x50, xor1, xor2)` does, and then xor1 alone
over the 0x60 bytes at `id` as `RoundXOR(buf4, 0x14, 0x60, xor1, null)` does. JPCSP's `RoundXOR` is
`buf[i] ^= key[i & 0xF]`, the same repeating XOR as our `xor[i % 0x10]`.

### The other half of it: KL4E

Decrypting is only half the job. Shiren 4 Plus's `f5psp.sprx` decrypts to bytes that start with
`KL4E`: every one of these modules has `comp_attribute = 0x0201`, i.e. compressed, and
`(comp_attribute & 0xF00) != 0` means KL4E/KL3E rather than gzip. So decryption alone would just
move the failure from "unsupported PRX type" to "decompression failed".

`Core/Util/KL4E.cpp` already handles that - it went in for firmware modules that use the same
compression, and these get it for free. It's worth knowing the two halves are independent, because
each one on its own leaves the module unloadable and the error doesn't say which is missing.

Note that none of this can be checked offline against the hand-decrypted `.sprx` files that
circulate for God Eater 2: their flag byte says the licensee key is part of the derivation, and that
key only exists while the game is running. Those files are still useful as a cross-check of the
installer itself - the `PBOOT.PBP` in such a folder is byte-for-byte what `InstallPkg()` writes, and
the only difference is that the `.sprx` files beside it were decrypted by hand.

### Where that leaves the three module titles

- **God Eater 2 (NPJH50832)** installs, boots, loads its modules and plays. Decryption alone wasn't
  enough for it: it also needed the type-B relocation fix in `ElfReader::LoadRelocations2` (issue
  #8075), where two `lui`s sharing one `addiu` got different high halves, so a callback pointer
  landed 0x48 bytes inside a function.
- **Shiren 4 Plus (NPJH50698)** loads `f5psp.sprx` - the log says `'FDS3PSP' is KL4E-compressed,
  decompressing` - and runs.
- **Tales of the World Radiant Mythology 3 (NPJH50353)** still doesn't reach its modules inside a
  headless boot, so it remains untested rather than known-good.

### Still not tested

- Only God Eater 2 has been played past a title screen. For the rest the runs are short headless
  boots, so "the update is in use" means the patched executable is what loaded and ran - not that a
  patched *asset* was read. LittleBigPlanet's `PATCH.ARC` covers that for a UMD title; there's no
  equivalent observation for a digital one yet.
