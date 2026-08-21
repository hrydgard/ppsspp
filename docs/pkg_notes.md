# The PKG package format (PSP game updates)

A `.pkg` is the NPDRM container Sony distributed downloadable content in - full PSN games, DLC,
themes, and the thing these notes are about: **game updates**. An update package holds a patched
EBOOT (`PBOOT.PBP`) plus whatever data files the patch replaces, and installing it drops them in
`ms0:/PSP/GAME/<DISC_ID>/`. The patched EBOOT then runs with the original UMD (or the original
PSN game) still supplying everything it doesn't override.

`Tools/pkg.py` is a working parser/extractor. PPSSPP itself cannot yet read or use these - see
"What PPSSPP is missing" at the end.

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

The `\0PSPEDAT` files are the PGD-wrapped kind PPSSPP already decrypts at runtime, via
`sceNpDrmEdataSetupKey` in `Core/HLE/scePspNpDrm_user.cpp`.

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

## What PPSSPP is missing

`grep -r PBOOT` over the tree returns nothing, so both halves are unwritten:

1. **Reading and installing a `.pkg`** - unpack it into `ms0:/PSP/GAME/<DISC_ID>/`, next to the
   existing ZIP/ISO install paths in `Core/Util/GameManager.cpp`.
2. **Booting the patch** - when starting a game, notice
   `ms0:/PSP/GAME/<DISC_ID>/PBOOT.PBP` whose inner SFO matches the disc's `DISC_ID` and
   `DISC_VERSION`, and boot that instead with the ISO mounted on `disc0:`. The mounting already
   exists (see `--mount` above); what's missing is the decision.

Only the UMD half of this is proven. The digital NP\* titles patch a PSN `EBOOT.PBP` /
NPUMDIMG rather than a UMD, and `ISO.BIN.EDAT` most likely re-keys it - that path has not been
tested.
