# Game patches installed on a PSP (DNAS)

Some early games shipped their updates and DLC as an "installer" package that the PSP's install
utility turns into a module only that PSP can load. Killzone: Liberation's Chapter 5 DLC is the known
case (issues #12343, #12344). PPSSPP installs the EU Killzone download itself when the game boots,
and can also load a patch that was installed on a real PSP, given that PSP's Fuse ID.

## What the download is

`PSP/GAME/UCES00279/` (UCUS98646 for the US game) gets two files:

- `EBOOT.PBP`, 8.5 MB, `CATEGORY=MG`, `BOOTABLE=0`, title "Killzone: Liberation Installer". Its
  `DATA.PSP` is the encrypted package the install utility unpacks. The XMB refuses to start it.
- `CHAPTER5.BIN`, 68 MB, the chapter's data. The patched game module reads it as it is.

## Installing it on a PSP

The install is done by the game's own boot program (`disc0:/PSP_GAME/SYSDIR/EBOOT.BIN`), which
imports `sceUtilityInstall*`. It only installs when started with the argument `-i`, and then only if
`ms0:/PSP/GAME/UCES00279/EBOOT.PBP` is larger than 1 MB. The game passes `-i` itself, after the
online download in its menus (from `kzl.killzone.com`, long gone), so there's no way to trigger it
from inside the game today.

Starting the boot program with `-i` from PSPLink works. `modexec` doesn't (the LoadExec is refused),
but a tiny kernel-mode PRX calling the CFW's `sctrlKernelLoadExecVSHDisc` (`SystemCtrlForKernel`,
NID `0x577AF198`) with `key = "game"` and the argument block `"disc0:/PSP_GAME/SYSDIR/EBOOT.BIN\0-i\0"`
brings up the system's install screen. Afterwards the folder holds `KZL.PRX` (8.6 MB, the patch),
and the 8.5 MB `EBOOT.PBP` has been replaced by a 10 KB one for the XMB.

The Fuse ID is the two words at `0xBC100090` and `0xBC100094`, which only kernel code can read.
PSP tools show it as 12 hex digits, the `0x94` word first.

## Installing it in PPSSPP

`InstallDrmBBPackage()` in `Core/PSPLoaders.cpp` runs when one of the games it lists boots (those
`g_kirk2Results` covers) and `PSP/GAME/<DISC_ID>/EBOOT.PBP` is over 1 MB. It does what psheet does, with no console in the
picture: it writes the package's files next to it and replaces the installer with the package's own
`EBOOT.PBP`. It only does this if it can decrypt every `.PRX` in the package, so otherwise the download is
left alone.

The package is the PBP's `DATA.PSP`, read by `pspReadDrmBBPackage()` (layout from psheet's
`bbox_access.c` functions). Everything in it uses sceDrmBB MAC and cipher type 1, so known keys only:

- The whole PBP is MACed with a fixed key from psheet's data segment, in its last 16 bytes.
- `DATA.PSP` +0x00: a 0x50-byte header MACed (at +0x50) with the secure install ID, which means the
  ID can be recovered from the MAC (`bbmac_getkey`). The EU and US downloads use the same one. Its
  bytes 0x10-0x50 decrypt with the fixed key (header key: bytes 0-0x10, seed 0) to two 16-byte keys,
  then version 1, flags, 1, 0, the parameter table's offset and count, and the file table's offset.
- Parameters are (id, value) pairs, decrypted with the first key and the secure install ID. `0x01010100`
  is the number of files.
- File entries are 16 bytes (u16 name offset, u16 name length, u32 data offset, u32 size, relative
  to the file table), and their names and data are decrypted with the second key and the secure install ID.
  The seed is always the 16-byte block number.

A `.PRX` comes out as a type 3 module whose first step (the 0x50 bytes at 0x80-0xB0, 0xC0-0xD0 and
0x12C-0x13C, with the KIRK header at 0xEC) is encrypted for KIRK command 2. The install sends the
first 0x400 bytes through `sceMesgIns_driver_D062B635` (uofw's `sceUtilsPrepareGetLoadModuleRLength`),
which runs command 2 in place and re-encrypts that block for the console. The rest of the module is
the same before and after. Command 2's key is unknown, but its result is the same on every PSP, so
`g_kirk2Results` in `PrxDecrypter.cpp` stores it for each module we know, keyed by the 16 bytes at
0xEC. `pspDecryptType3()` uses it in place of the first step.

To get the result for another package, run `--re-decrypt` on it, which then writes the module as
the package has it to `<out>.module`. Then either decrypt a PSP
install of it with KIRK command 3 and that PSP's Fuse ID, or run `sceMesgIns_driver_D062B635` on its
first 0x400 bytes on a PSP and decrypt that output with command 3 there.

## Loading it

The boot program calls `sceKernelLoadModuleDNAS(path, secureId, 0, 0)` with a 16-byte secure
install ID from its data (`81A19A6F0FA9CFBAE378F73FFD504C7F` for UCES00279), and treats any negative
result as "no patch", falling back to `disc0:/PSP_GAME/USRDIR/KZL.PRX`. Two layers:

1. **A PGD** (key index 1, DRM type 0), opened by `amctrl` with the secure install ID and the "flag 1"
   DNAS key. Its MAC and cipher use KIRK commands 5 and 8, which are AES under the per-console key
   with seed 1.
2. **A type 3 PRX**, decrypt mode 18, tag `0x89742B04`. This is type 5 with the first step done by
   KIRK command 3 instead of 7: a small command 3 block whose header is at `0xEC` and whose 0x50-byte
   body is the usual kirk header + SHA1 pieces, XORed with the tag's key and the secure install ID.
   The install produced that block with KIRK command 2, re-encrypting under the per-console key with
   seed 0. Command 2's own key (AES keyslot 3) isn't known, which is why the download can't be
   installed here.

The per-console keys come from the Fuse ID: the key mesh on psdevwiki's Kirk page, and the final
step from the KIRK ROM's `aes_set_perconsole_key` (uofw `src/kirk/kirk-rom.c`). The type 3 layout is
JPCSP's `crypto/PRX.java`. The tag's key and XOR are in its `KeyVault.java`, and uofw's `mesgled.h`
documents the decrypt mode.

In PPSSPP:

- `kirk_individual_key()`, `kirk_CMD3/5/8` in `ext/libkirk/kirk_engine.c`.
- `pspDecryptType3()` and `pspUnwrapDNASPGD()` in `Core/ELF/PrxDecrypter.cpp`.
- `sceKernelLoadModuleDNAS()` in `Core/HLE/sceKernelModule.cpp` takes the Fuse ID from the `FuseID`
  setting (`[SystemParam]` in `ppsspp.ini`) or `--fuse-id`.

A wrong Fuse ID fails the PGD's MAC, so the game just runs unpatched. A module PPSSPP installed has
no PGD and needs no Fuse ID.

To decrypt one offline:

```
PPSSPPHeadless --re-decrypt KZL.PRX --re-decrypt-out KZL.elf \
    --re-decrypt-key 81a19a6f0fa9cfbae378f73ffd504c7f --fuse-id <12 hex digits>
```

`--re-decrypt` also takes the download's `EBOOT.PBP`, and decrypts the `.PRX` in it.
