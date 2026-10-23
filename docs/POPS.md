# PS1 EBOOTs (POPS)

PS1 games on the PSP are EBOOT.PBPs run by the firmware's PS1 emulator. PPSSPP runs the firmware's own
modules for them, so it needs an installed 6.60 or 6.61 firmware (`flash0:/kd/popsman.prx`); without
one, booting a PS1 EBOOT fails with a message saying so. `--unpack-updater DIR EBOOT.PBP` installs one from an
official updater.

The code is `Core/HLE/PopsBoot.cpp` (the boot, the ME, the patches) and `Core/HW/PopsMMIO.cpp` (the
hardware registers the manager uses). The patches are for the 6.60 and 6.61 `popsman.prx` and `pops_0Xg.prx`,
whose code is the same (and the same at the patched places for the PSP-1000 and PSP-2000 models). The
manager's is checked when it's loaded. Offsets below are from the module's base.

## The boot

On hardware the kernel reboots into its POPS configuration, which starts the POPS manager
(`popsman.prx`) and a few user modules, and the kernel's init then calls `scePopsManLoadModule(path, 0)`,
which loads `pops_0Xg.prx` (the emulator) for the PSP model. `Load_PSP_POPS` does the same without the
reboot: it loads the manager, the user modules (`libfont_hv`, `pafmini`, `libpspvmc`) and a helper thread
that makes the init's call. `InitForKernel` reports a memory stick boot in the POPS key configuration.

The emulator draws through the GE and plays through the Media Engine. The manager drives both directly:

- **GE.** The emulator keeps one display list stalled and the manager extends it each frame by writing
  the stall register (`0xBD40010C`), then waits for the FINISH interrupt. These map onto the HLE GPU's
  current list.
- **ME.** A PS1 boot always gets the emulated Media Engine (`Core/MIPS/MeCore.cpp`), which runs the
  manager's code as the PSP does. The manager copies a bootstrap into the boot page (`0xBFC00040`),
  releases the ME's reset through `sceSysreg_driver` and waits for an acknowledgement in the mailbox
  (`0xBFC007F0`). Sony's own ME firmware isn't booted for it. The ME loop (+2F88) sets up the audio block
  (`0xBE000000`) and then, once per sample, sleeps in `halt` until the FIFO has room (interrupt source 12,
  a level: the loop never enables interrupts nor acks them), calls the emulator's mixer (on the ME) and
  writes the sample. The FIFO plays at 44.1kHz in emulated time, and its samples are mixed into the
  output (`MeMMIO::AudioMix`). The loop's calls to the cache functions do nothing on the ME.

Neither the lock the emulator shares with the ME (pops +5C) nor its SPU interrupt pacing is patched: with
two CPUs they should work as on hardware. Vib-Ribbon, which streams its stage music through the SPU's
capture buffers and asserts when those interrupts come at the wrong PS1 time, hasn't been tried.

Unresolved imports remain, mostly `sceSyscon_driver` and power management. The manager carries on
without them.

## Rendering

The emulator keeps the PS1's 1024x512 VRAM at `0x04000000` as one 16-bit framebuffer, and reads and writes
it outside where it draws. It's a fixed framebuffer: created whole, from memory, and uploaded without
filtering. Its 24-bit video is unpacked three bytes per pixel into `0x04100000`, which is kept at 1x. It
draws sprites on pixel centers, which above 1x read their neighbours' texels, so those are moved back half a
pixel. And it builds video frames by ORing passes together, so draws whose logic op is done in the shader
are drawn one at a time.

## Store and converted games

A store game's disc image is a PGD. The manager's license check (+266C) needs the game's license and
activation, so it's replaced: the version key comes from the PGD header, as CFW's popcorn plugin does.
The MACs are checked with the real algorithms (`sceAmctrl_driver`, on libkirk).

A converted EBOOT (popstation and similar) has a plain disc image of DEFLATE blocks and no MACs. As popcorn
does, the emulator's decoder calls go to an inflate, its CD audio index check takes the full-length branch,
and the two MAC checks (+0A28, +0AB8) pass.

## Disc images

A PS1 disc image boots as the converted EBOOT it would make (`Core/FileLoaders/PS1EbootFileLoader.cpp`),
which never exists as a whole. It takes a cue sheet (with any number of files, AUDIO tracks, INDEX 00
and PREGAP), a CloneCD `.ccd` with its `.img`, an Alcohol 120% `.mds` with its `.mdf` (subchannel data
skipped), a CD CHD (whose audio is big-endian), or a bare raw (2352-byte sector) `.bin`, `.img` or
`.iso`, or a 2048-byte `.iso`, around whose user data Mode 2 Form 1 sectors are made (with no EDC/ECC).
A bare image that a cue sheet next to it names, or that has a `.ccd` or `.mds` of the same name, is read
through that. The disc ID is the one SYSTEM.CNF boots. The game browser shows each disc once, and a zip
or 7z with one in it installs it (several files into a folder of their own).

It's laid out as popstation lays out a PSISOIMG: the disc ID at +0x400, the TOC at +0x800, a block
index at +0x4000 and the blocks from +0x100000, each of 16 sectors. The manager doesn't read the disc
image of one whose blocks are its own uncompressed size (0x9300), so each block is a stored DEFLATE
block, which the inflate above decodes: 0x9305 bytes, so reads of the image map straight onto its files.
There are no icons, pictures or DATA.PSP, and none of the STARTDAT that follows a popstation image. The
EBOOT is the only file in `host0:`.

For any EBOOT, also as popcorn does: the manual's name check passes, and an ICON0 the emulator can't use
(anything but an 80x80 PNG, such as a missing one or a PSP game's 144x80 one, which crashes it) reads as a
blank 80x80 one.

## Testing

```
PPSSPPHeadless --memstick=MEMSTICK --graphics=vulkan --timeout-wall=60 --timeout-emulated=60 \
    --press=30:0x8,40:0x4000 --screenshot-at=55:shot.png --dump-audio=out.wav EBOOT.PBP
```

The HOME button opens the emulator's menu. Savestates work.

## Not done

- Other firmware versions. CFW's popsloader runs POPS from 3.02 to 6.60, and its tables show what differs
  between them: the offsets patched here, and the kernel NIDs the modules import.
- The anti-LibCrypt word some PAL games need in a converted image (PSISOIMG + 0x12B0). ARK-4's popcorn has
  a table of them by disc ID, switched off. Converting tools can patch the image instead.
- ARK-4's per-game CONFIG.BIN next to the EBOOT, copied over the image's config (PSISOIMG + 0x420).
- Multi-disc games from disc images (PSTITLEIMG, which has up to five PSISOIMGs), and booting an image
  straight from an archive.
- A byte popcorn changes in reads of converted images (0x41B, from 0x27 to 0x55 when 0x41B-0x41E read
  27 19 22 41) without knowing why.
