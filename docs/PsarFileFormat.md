# The PSAR archive format (PSP firmware updaters)

An official PSP firmware updater carries the whole firmware image in a PSAR archive. PPSSPP can
walk one and extract the files out of it without emulating the updater, which is what
`Core/Util/PSARUnpack.cpp` does. The main use is getting `flash0:/font` - the PGF system fonts -
out of an updater the user already has, since most UMDs carry one.

This document is what was learned reading 43 different firmware versions, from 1.50 to 6.61. It's
a description of the format rather than of the code; the code is the same shape but has the error
handling.

## Where an updater lives

The archive turns up in three shapes, all of them the same bytes once you find them:

| Shape | Where | Notes |
| --- | --- | --- |
| Downloaded updater | `EBOOT.PBP`, subfile 7 (`DATA.PSAR`) | The one you get from Sony's site |
| Disc updater | `PSP_GAME/SYSDIR/UPDATE/DATA.BIN` | Not wrapped in a PBP - the bare archive |
| Loose | any file starting with `PSAR` | Whatever someone already pulled out |

A disc updater directory also holds `EBOOT.BIN` (the updater executable, a bare `~PSP` PRX, of no
use to us) and `PARAM.SFO`. That SFO is worth knowing about: its `TITLE` reads
`"PSP™ Update ver 3.95"`, so **the firmware version can be read without decrypting anything**,
which is cheap enough to check a whole game library with. `DISC_ID` is `MSTKUPDATE` and `CATEGORY`
is `MG`.

Of 589 discs scanned across two libraries, 473 carried an updater.

## Archive header

```
+0x00  u32   "PSAR" magic (0x52415350)
+0x04  u8    version. 1 = "oldschool" (see below), 2 and 3 are the common ones
+0x08  u32   total length of the records
+0x20  u32   0x2C333333 if this archive is already decrypted, otherwise part of the ciphertext
```

The length at +0x08 matters: archives have a few bytes of padding after the last record (16 and 32
bytes in the two checked), and a walk that goes by file size instead will try to decode that
padding as a record and report a spurious failure at the very end.

The marker at +0x20 only appears in archives some other tool has already decrypted. In an original
updater those bytes are ciphertext and will not match.

## Records

The archive is a flat sequence of records starting at +0x10. There is no index - you find entry
*n+1* by decoding entry *n* and adding up its sizes. Each record is:

```
[ 0x150 bytes  encryption header ]     <- absent if the archive is pre-decrypted
[ 0x110 bytes  entry description  ]
[ variable     file contents      ]    <- has its own 0x150 header, same as above
```

Call the header size *overhead*: 0x150 normally, 0 for a pre-decrypted archive.

### Decoding a block

Every encrypted block - entry descriptions and file contents alike - is decoded the same way:

1. **Demangle.** The 0x130 bytes at block+0x20 are AES-128-CBC encrypted on top of everything
   else. Decrypt them with KIRK command 7 (`KIRK_CMD_DECRYPT_IV_0`), key seed **0x55**, IV zero,
   writing the result back over block+0x20. This step is skipped for version 1 archives.

   The point of it is that it hides the PRX tag: you cannot tell how to decrypt the block until
   you have done this.

2. **PRX decrypt.** The block is now an ordinary PRX blob, with its tag at block+0xD0. Run it
   through the normal PRX decrypter (`pspDecryptPRX`). The tag official updaters use is
   **0x0E000000**; `0x06000000` also appears in the wild.

   Note that `0x0E000000`'s key is stored *unscrambled*, unlike almost every other key in
   PPSSPP's tag table, so it needs the `kirk7` pass applied before use - that's what the
   `scrambleKey` flag on `TAG_INFO` is for.

One wrinkle worth knowing: the decrypter reads a little past the end of the block and **uses what
it finds there**, so a decoder should hand it about 16 bytes of slack from the archive. The last
record ends flush with the end of the archive and has no slack to give; zero-filling it there is
fine, but zero-filling it everywhere breaks every archive that does have those bytes.

### Entry description

0x110 bytes once decoded:

```
+0x004  char[]  entry name, NUL padded (see "Naming" below)
+0x100  u32     always zero. If it isn't, you've lost your place in the archive
+0x104  u32     size of the contents block that follows, including its 0x150 header
+0x108  u32     uncompressed size of the file. 0 means this entry is a directory
```

A directory entry still has a contents block to skip over - advance by `+0x104` either way.

### The first two records

The first record (at +0x10) is not a file. Decoded, it holds a text string at +0x10 whose last
comma-separated field is the firmware version, e.g. `...,6.61`. That's the authoritative version
for choosing decryption keys further down.

There is a second record after it whose length isn't recorded anywhere. In practice it is
`overhead + 100`, or `overhead + 144` on 2.7x, or `overhead + u16 at +0x90 of the first record`.
Try them in that order and take the first that decodes. Round the result up to a multiple of 16
before advancing. Version 1 archives don't have this record at all.

## Contents

The contents block decodes to a compressed stream. Sniff the first bytes:

| Magic | Format |
| --- | --- |
| `78 9C` | zlib |
| `KL4E` | KL4E - Sony's own, not implemented in PPSSPP |
| `KL3E` | KL3E - likewise |
| `2RLZ` | LZR |

Across all 43 firmware versions examined, **every file in every archive was zlib**. KL4E does turn
up on the PSP, but a layer further in: two kernel modules (`memlmd_01g.prx` and `loadexec_01g.prx`)
are themselves KL4E-compressed *inside* their own PRX, which is a separate problem from this one.

## Naming

This is the part that changed most across firmware generations, and the part most likely to catch
out an implementation tested against only one updater.

### 1.x and 2.x - real paths

Entries are named outright: `flash0:/vsh/resource/opening_plugin.rco`, `flash1:/registry`,
`ipl:/psp_nandipl.bin`. Nothing else is needed. Note `ipl:` - it is not enough to recognise
`flash0:` and `flash1:`; better to treat any `<device>:/` shape as a real path.

### 3.x - grouped short names

Entries are named `<group>:<5 digits>`, where the group is `com` for files every model gets, or
`01g`, `02g`, ... for one model's. `<group>:00000` is that group's **file list**, and the rest of
that group's entries are looked up in it by their number alone (entry `com:00004` is key `00004`).

### 5.x and 6.x - flat short names with per-model lists

Entries are a bare 5-digit number. The low numbers are file lists, one per PSP model
(`00001` = 01g, `00002` = 02g, ...) and the rest are files that any of the lists may name.

**Which numbers are lists is not fixed.** 6.61 uses 1-11; 6.00 has real files at `00010`-`00012`.
Since a list always decrypts successfully (the PRX layer inside validates a hash), the robust rule
is: try to decrypt a candidate as a list, and if that fails, treat it as an ordinary file.

### File lists

A list decodes to plain text, one `shortname<separator>path` per line. Both the separator and the
path style depend on the generation:

| Generation | Separator | Path written as |
| --- | --- | --- |
| 3.x | `\|` | `flash0/font/ltn0.pgf` |
| 5.x, 6.x | `,` | `flash0:/font/ltn0.pgf` |

Worth normalising to one form early - otherwise a filter like `flash0:/font/` silently matches
nothing on a 3.x archive.

### List encryption

The lists are encrypted on top of everything else, with **DES** - the tables in the reference
implementations are DES's own: a 56-entry PC-1, a 48-entry PC-2, eight 64-entry 4-bit S-boxes, a
32-entry P permutation, and textbook IP/FP bit-shuffling constants.

It is DES-CBC decrypt, with the key assembled from two 32-bit words as `(high << 32) | low` in
big-endian byte order, and an IV alongside them. There is one such set per firmware series, and
which one applies is chosen from the version string in the archive's first record:

| Firmware | Key set |
| --- | --- |
| 1.x, 2.x, 3.0 - 3.7 | 0 |
| 3.8, 3.9 | 1 |
| 4.x | 2 |
| 5.x | 3 |
| 6.x | 4 |

A sixth set exists that nothing selects by version. The values themselves are in `kTableKeys` in
`Core/Util/PSARUnpack.cpp` and aren't repeated here.

Underneath the DES layer is an ordinary PRX blob, so run the result through `pspDecryptPRX` as
well; what comes out of *that* is the text.

A good check while implementing: after the DES pass but before the PRX pass, the u32 at +0xD0
should be a recognisable PRX tag. A wrong key gives a random value there, so that single word
tells you whether the cipher is right before anything else has to work.

## What the archives look like in practice

Three structural eras, visible in the shape of the output:

| Firmware | Naming | Directory entries | Fonts |
| --- | --- | --- | --- |
| 1.50 - 3.52 | real paths | 0-16 | 17-20 |
| 3.71 - 4.05 | `com:`/`NNg:` groups | 50 | 21 |
| 5.01 - 6.61 | flat + per-model lists | 16-17 | 21 |

A 6.61 archive holds 435 records: 411 files, 17 directories and 7 file lists. Among the files are
295 `~PSP` modules, 61 PRF files, 7 encrypted XMB indices (`PSPsysGP`), and 21 files in
`flash0:/font` - 19 of them PGF, plus `gb3s1518.bwfon` and `imagefont.bin`.

The "Fonts" column above counts files in `flash0:/font`, not PGFs specifically.

## Gotchas, collected

- Walk by the length at +0x08, not the file size, or the trailing padding decodes as a bad record.
- Stop when fewer than `overhead + 0x110` bytes remain rather than when the position passes the
  end - the two differ by exactly one spurious record.
- Give the block decrypter its 16 bytes of slack, but don't require them for the last record.
- Don't assume which entry numbers are file lists; find out by trying.
- Don't assume there are file lists at all - 1.x and 2.x have none.
- `ipl:` is a device too.
- A 3.x path has no colon; a 6.x path does.

## See also

- `Core/Util/PSARUnpack.{cpp,h}` - the implementation, and the only consumer of most of this.
- `Core/ELF/PrxDecrypter.cpp` - the PRX layer every block goes through.
- `ext/libkirk` - `kirk7()` is the demangle step.
- `PPSSPPHeadless --unpack-updater=DIR <updater|disc|PSAR>` unpacks one from the command line;
  `--unpack-updater-model=01g..12g` picks a model.
