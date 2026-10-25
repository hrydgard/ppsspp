# FFmpeg (vendored)

This is the part of FFmpeg 3.0.2 that PPSSPP uses, copied in from
[hrydgard/ppsspp-ffmpeg](https://github.com/hrydgard/ppsspp-ffmpeg) at commit `b87f7c6`, which was
the `ffmpeg/` submodule up to this point. It's LGPL 2.1 or later (see `COPYING.LGPLv2.1` and
`LICENSE.md`); nothing GPL or nonfree is enabled.

That commit is upstream FFmpeg 3.0.2 plus these PPSSPP patches:

- `libavcodec/avcodec.h`, `avpacket.c`, `frame_thread_encoder.c`, `utils.c`: backport of upstream
  860086ee1688, replacing the deprecated `av_dup_packet` with `av_packet_make_refcounted`.
- `libavutil/common.h`: FreeBSD build fixes.
- `libavformat/utils.c`: fix for crashes in `ff_seek_frame_binary`.
- `libavcodec/fft.h`: avoids non-PIC-compatible relocations on ARM64.
- `libavcodec/atrac3plus.c`: backport of upstream de5102fd92de.
- `libavutil/mem.h`: removes an incorrect attribute.

Since vendoring, one more (marked `PPSSPP:` in the code): the H.264 decoder only does 8-bit video,
which is all the PSP has. `h264_ps.c` rejects a sequence parameter set with any other bit depth,
and `h264dsp.c`, `h264pred.c`, `h264qpel.c`, `h264idct.c`, `h264chroma.c` and `h264_mb.c` no
longer build their 9, 10, 12, 14 and 16-bit variants of every function. Those were about 250 KB
of code, which the linker kept because the DSP init functions pick a variant by bit depth at run
time.

Two files are PPSSPP's own: `libavutil/avconfig.h` and `libavutil/ffversion.h`, which FFmpeg's
configure and `version.sh` would otherwise generate.

## How it's built

There's no configure step. Everything needed to build this lives in `ext/ffmpeg-build`:

- `config.h` is kept by hand, in place of the one configure would write. It's private to the
  FFmpeg build; PPSSPP's own code only sees the headers here. All assembly and architecture-specific
  code is off (every `ARCH_` and SIMD `HAVE_` is 0), so the only differences between platforms are
  OS and compiler ones (Windows/UWP/POSIX, MSVC/GCC-compatible, Linux, the BSDs and Apple), sorted out
  by a few preprocessor checks at the top. Components are enabled by their `CONFIG_..._DECODER` (and
  so on) lines.
- `sources.mk` lists the `.c` files to compile, relative to this directory. Every build system uses
  it: CMake (`ext/ffmpeg-build/CMakeLists.txt`) reads it, `android/jni/Android.mk`,
  `libretro/Makefile` and `libretro/jni/Android.mk` include it, and `update_vcxproj.py` copies it
  into `ext/ffmpeg-build/ffmpeg.vcxproj` and `UWP/ffmpeg_UWP/ffmpeg_UWP.vcxproj`.
- Every build compiles the files with `HAVE_AV_CONFIG_H`, `_ISOC99_SOURCE`, `_FILE_OFFSET_BITS=64`
  and `_LARGEFILE_SOURCE` defined, and with `ext/ffmpeg-build` ahead of this directory on the include
  path, as FFmpeg's own build does.

What's enabled is what PPSSPP uses:

- the H.264, AAC, MP3, ATRAC3 and ATRAC3+ decoders (ATRAC only when FFmpeg is forced for audio),
  and the H.264 parser;
- the MPEG-PS, PMP, OMA, AVI, WAV, MP3, AAC, PCM and raw H.264/H.263/MPEG-4/MPEG video demuxers.
  PSMF video is opened by probing, and only MPEG-PS is needed to pick the container, but the raw
  H.264 demuxer is what identifies the video stream's codec within it. Without it, the stream
  comes out as codec `none`. The others are cheap and keep probing as it was;
- for AVI dumping, the FFV1 encoder (lossless), the AVI muxer and the file protocol.

The set was worked out by running FFmpeg 3.0.2's own configure with just these enabled, and taking
its `CONFIG_` values and the files its build compiled. That's what accounts for the rest of the
`CONFIG_` lines, such as the select rules that pull in shared code.

One MSVC quirk: FFmpeg leaves calls to disabled components in `if (CONFIG_...)` blocks and relies on
the compiler dropping them. MSVC only does that when optimizing, and not with `/Zc:inline`, so the
MSVC builds of FFmpeg are optimized in Debug too, and turn `/Zc:inline` off. GCC and Clang drop
them even at `-O0`.

## Which files are here

Only the files some build actually reads: the 160 in `sources.mk`, plus every header and
`#include`d template they pull in. A header nothing includes isn't here. The includes were found
from the compilers' dependency output (GCC on Linux, MSVC for desktop and UWP, and the NDK's Clang
for arm64 and armv7), and then checked by following every `#include "..."` in the copied files to
one that's missing. All the ones that turned up are behind a disabled condition: the architecture
subdirectories (`x86/`, `arm/`, `aarch64/`, `mips/`, `ppc/` and so on), hardware acceleration
(`d3d11va.h`, `qsv.h`, `vdpau.h`), networking (`network.h`), OS/2 threads, and the subtitle demuxers.
That's 391 files of FFmpeg's and about 5.8 MB.

libswresample isn't built: PPSSPP converts the audio decoders' output to stereo s16 itself (in
`Core/HW/SimpleAudioDec.cpp`), with the same rounding. Two of its headers are still here because
`libavcodec/opus.h` includes one, for `imdct15.c`.

`sources.mk` is also missing 62 files that configure's build would compile for these components
but that nothing links: they're never pulled out of the static library. Most are libavutil's crypto
and hashing (AES, SHA-512, MD5, Camellia and so on), the alternative DCT, IDCT and FFT
implementations, and libavformat's SDP and dump helpers. That was taken from the GCC link map; on
another platform a file one of them is needed for would show up as an unresolved symbol.

## Adding a component

1. Turn its `CONFIG_` line on in `ext/ffmpeg-build/config.h`, along with what configure would
   select for it. Running FFmpeg 3.0.2's configure with it enabled shows both.
2. Add the `.c` files it needs to `ext/ffmpeg-build/sources.mk`. FFmpeg's own `Makefile` in each
   library lists them, under `OBJS-$(CONFIG_...)`. Some may be among the 62 left out above. Then
   run `ext/ffmpeg-build/update_vcxproj.py`.
3. Copy in whatever files it needs that aren't here yet, from the upstream `n3.0.2` tag (or from
   `hrydgard/ppsspp-ffmpeg`). A missing `.c` shows up as a missing file, and a missing component
   as an unresolved `ff_..._decoder` (or similar) at link time.

## The tradeoffs

**Vendored, not a submodule.** The submodule also held prebuilt libraries for every platform. It was
about 1.5 GB checked out and 770 MB of history, so CI had to fetch it sparsely. The sources alone
are small next to PPSSPP's own history. They've only had a handful of PPSSPP patches since 2016,
and now a new codec is one change to one repository. The cost: patches no longer sit as commits on
top of upstream history, so they're listed above, and moving to a newer FFmpeg is one large diff
here rather than a submodule bump. The submodule's history stays in `hrydgard/ppsspp-ffmpeg`, and
older PPSSPP commits still point at it, so bisecting across the change works (the two never share a
path). Checking out a newer commit leaves an old `ffmpeg/` checkout behind as an untracked
directory, which nothing uses and can be deleted.

**Only the needed files, not the whole source tree.** All of `lib*/` and `compat/` would be about
3100 files, 44 MB, and 10 MB in git, nearly all of it never compiled. The cost is step 3 above when
adding a component.

**Only the components PPSSPP uses.** Fewer files make for a faster build, each having a startup
cost. The first vendored copy kept everything the old prebuilt libraries had enabled: 277 files to
compile, 594 vendored. Then, in steps:

- Dropping the decoders nothing asks for (H.263, MPEG-4, MPEG-2, MJPEG, AAC-LATM, PCM), the parsers
  other than H.264, and the HuffYUV, PCM and MPEG-4 encoders: 229 and 506, and the GCC -O2 libretro
  core about 1.8 MB smaller (0.7 MB of code, the rest zero-initialized tables). The MPEG-4 encoder
  alone, with the MPEG-video encoding code it brings along, was 24 of those files. Without it, AVI
  dumps are always FFV1; lossy footage is better captured from the output with a screen recorder.
  Dropping FFV1 as well would save only another 0.16 MB.
- Leaving out the files nothing links, and the H.264 decoder's high bit depths: 167 and 405, and
  another 0.35 MB off the core.
- Replacing libswresample: 160 and 391.

The demuxers were measured too: all of them together, beyond what PSMF needs, are 20 files and
under 0.1 MB, so they stay.

**No assembly.** The Android and Linux prebuilt libraries were already built with `--disable-asm`,
so they lose nothing, but the Windows ones had x86 SIMD code. On x64, H.264 decoding takes about 45%
longer than with the old Windows library (about 1.15 ms against 0.78 ms per 480x272 frame on one
thread), which is still far faster than any PSP video needs. Decoded output was compared against the
old Windows x64 library: H.264 (four game videos, about a thousand frames), MP3 and ATRAC3 come out
bit-identical; ATRAC3+ (float code that had SIMD versions) differs by at most 2.4e-7, about a
hundredth of a 16-bit step.

**No configure.** A hand-kept `config.h` means no Perl, shell or toolchain probing at build time,
and the same file works for CMake, MSBuild, ndk-build and the libretro Makefiles. It has to be
checked by hand on a new platform, though: `-Werror=implicit-function-declaration` (GCC and Clang)
catches most of what a wrong `HAVE_` would break.

**Upgrading FFmpeg.** 3.0 registers components with `CONFIG_`-gated `REGISTER_` macros, which is
what makes the hand-kept `config.h` workable. From 4.x on, configure also generates the component
lists (`libavcodec/codec_list.c` and friends), so an upgrade means writing those by hand too, along
with a new `config.h` and file list. PPSSPP's own code already works with newer FFmpeg (it builds
against the system's with `-DUSE_SYSTEM_FFMPEG=ON`).
