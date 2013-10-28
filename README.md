PPSSPP - a fast and portable PSP emulator
=========================================

Written by Henrik Rydgård

Originally released under the GPL 2.0 (and later) in November 2012

Official website:
http://www.ppsspp.org/

To contribute, see [the development page][ppsspp-devel].

For the latest source code, see [our github page][ppsspp-repo].

For build instructions and other development tutorials, see the [wiki](https://github.com/hrydgard/ppsspp/wiki).


What's new in 0.9.5
-------------------
* Many, many emulation fixes:
  - bezier/spline curve support, fixing LocoRoco and others
  - stencil clear emulation, fixing Final Fantasy IV text
* Performance improvements in some games
* Post-processing shaders like FXAA, scanlines, vignette
* More solid save states (we will try to keep them working from now on. Save states only upgrade forward,
  not backward to older versions though).
* Change render resolution independently of window size
* Massive debugger improvements
* Win32 menu bar is now translatable
* Multiple UI bugs were fixed, and the UI instantly changes when a new language is selected
* Win32: Ability to store PPSSPP's config files and memory stick files in places other than the same directory
* Android-x86 support
* Unofficial port for modified Xbox 360 consoles
* Atrac3+ plugin no longer required. Symbian now supports Atrac3+ audio.
* Symbian audio and ffmpeg is now threaded for more consistent media processing.
* Haptic feedback support for mobile devices.
* Accurate system information for mobile devices.
* Qt audio has been fixed.
* Analog controller support for Blackberry.
