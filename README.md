PPSSPP - a fast and portable PSP emulator
=========================================

Created by Henrik Rydgård

Additional code by many contributors, see the Credits screen

Originally released under the GPL 2.0 (and later) in November 2012

No BIOS file required to play, PPSSPP is an "HLE" emulator.  Default settings balance good compatibility and speed.


 Luna version includes various compatibility improvements(hackfixes) :
 - Armored Core and Bleach: Soul Carnival series(freezes),
 - Auditorium(black screen),
 - Bijin Tokei Portable(miniatures),
 - Evangelion Jo(most text in menus, note that it still requires pauth file;p),
 - Heroes Phantasia(flashing/z-fighting),
 - Hokuto no Ken: Raoh Gaiden(depth problem),
 - Nayuta no Kiseki(minor glitches),
 - Patapon 2(menu speed),
 - Resistance Retribution(some of the crashes),
 - Sangokushi IX with Power-Up Kit(black layer),
 - The Warriors(videos),
 - Mighty Flip Champs DX(bad sound speed),
 - Edge(bad sound speed),
 - N+(bad sound speed),
 - and more via included patches.
 (Note: When this list decreases, it just mean PPSSPP already got an official fix.)
 Also includes a really awful hack for MOHH2 multiplayer(MOHH1 works fine without it;p).
 ~ LunaMoo

What's new in 1.7.1
-------------------
* Minor crashfixes, lang updates
* Save bug fixed (#11508)

What's new in 1.7.0
-------------------
* Fix for insta-crash on Galaxy Note 9, some Xiaomi devices, and the new nVidia Shield (#11441)
* Vertex range culling on most platforms, fixes DTM: Race Driver and similar (#11393)
* Major speed boost in some Gundam and Earth Defense Force games (#10973)
* Fix for issues with post processing shaders in GL 3+ (#11182)
* Fixes to sound output switching on Windows (WASAPI) (#11438)
* Detects DirectInput devices when running (#11461)
* Simple Discord integration (#11307)
* New debugging APIs (#10909)
* Task switching made a lot more robust (fixes GPD XD problems) (#11447)
* Texture decoding optimizations (#11350)
* Tons and tons of miscellaneous bugfixes and compatibility fixes

