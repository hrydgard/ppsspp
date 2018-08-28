

PPSSPP - a fast and portable PSP emulator
=========================================

Created by Henrik Rydgård

Additional code by many contributors, see the Credits screen

Originally released under the GPL 2.0 (and later) in November 2012

Official website:
https://www.ppsspp.org/

Discord:
https://discord.gg/5NJB6dD

No BIOS file required to play, PPSSPP is an "HLE" emulator.  Default settings balance good compatibility and speed.

To contribute, see [the development page](https://www.ppsspp.org/development.html).  Help testing, investigating, or fixing is always welcome.  See [the list of issues](https://github.com/hrydgard/ppsspp/issues).

For the latest source code, see [our github page](https://github.com/hrydgard/ppsspp).

For build instructions and other development tutorials, see [the wiki](https://github.com/hrydgard/ppsspp/wiki).

For game compatibility, see [community compatibility feedback](http://report.ppsspp.org/games).

This hackish branch add's:
 - frame profiler and simpler frame stats as a normal option,
 - adhoc between multiply instances on same pc,
 - some other ini-only options exposed in UI,
 - functionality of some additional hotkeys: W-Lan, Vol+/-, note(mute on/off), screen(accesses display layout editor),
 - different default settings personalized for my liking,
 - simple UI ~ an option to hide most rarely used options,
 - new awful compat hacks to deal with just as awful regressions and other problems,
 - guardband-culling branch as compat hack(activated for some games listed below),
 - some cwcheat workarounds of mine included in separate import-ready database,
 - a few more post process shaders,
 - texture replacement info from ge debugger(experimental aka usually works, but it might be done badly, available from right click menu),
 - ultra high render resolutions and render screenshots,
 - some mp3 improvements(volume clamping and reverse of a bad hack),
 - separate sas and atrac/mp3 volume sliders,
 - realtime gpu texture scaling by aliaspider(new methods are mostly bad, but xBRZ variants are pretty nice for otherwise un-scallable textures),
 - some untested/optional hack to avoid stutter on NVidia hardware/bad drivers.

 To list some game compatibility improvements here:
 - Armored Core and Bleach: Soul Carnival series(freezes),
 - Auditorium(black screen),
 - Bijin Tokei Portable(miniatures),
 - Driver '76(glitches),
 - DTM Race Driver/TOCA Race Driver/V8 Supercars games(glitches),
 - Evangelion Jo(most text in menus, note that it still requires pauth file;p),
 - Heroes Phantasia(flashing/z-fighting),
 - Hokuto no Ken: Raoh Gaiden(depth problem),
 - Nascar(glitches),
 - Nayuta no Kiseki(minor glitches),
 - Patapon 2(menu speed),
 - Resistance Retribution(some of the crashes),
 - Sangokushi IX with Power-Up Kit(black layer),
 - The Warriors(videos),
 - Mighty Flip Champs DX(bad sound speed),
 - Edge(bad sound speed),
 - N+(bad sound speed),
 - and more via included patches.

 Also includes a really awful hack for MOHH2 multiplayer(MOHH1 works fine without it;p).
 
 ~ LunaMoo

What's new in 1.5.4
-------------------
* Bugfixes and crashfixes!

What's new in 1.5.0
-------------------
* Full Vulkan support, also for Android now. Very fast on supported devices. (#10033, #10049)
* Smarter graphics state management, reduced CPU consumption on all backends (#9899)
* Android: Support for Arabic and other scripts we couldn't support before
* Fix Android widgets, screen scaling (#10145)
* Fixes to video dumping
* Geometry problems fixed in Medal of Honor
* Implement immediate draws, fixing Thrillville (#7459)
* Software rendering improvements, speed and accuracy
* Hardware tesselation of PSP Beziers and Splines (used by a few games)
* Partial sceUsbGps and sceUsbCam support (Android)
* Android "Sustained performance mode" to avoid thermal throttling (#9901)
* Linux controller mapping fixes (#9997)
* Assorted bugfixes and compatibility improvements

Looking for [older news](history.md)?



