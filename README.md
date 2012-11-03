PPSSPP - a fast and portable PSP emulator
=========================================
Written by Henrik Rydgård

Released under the GPL 2.0 in November 2012

Official website:
http://www.ppsspp.org/

To contribute, see http://www.ppsspp.org/development.html .

For the latest source code and build instructions, see
http://github.com/hrydgard/ppsspp


BASIC BUILD INSTRUCTIONS

(for more detailed instructions, see http://www.ppsspp.org/development.html )

First of all, after having checked out the source, don't forget to
run:
git submodule init
git submodule update

in order to get the "native" library.

Now, the actual building:

PPSSPP currently has three build systems, for building
for the following platforms:
* Win32: MSVC
* Android: Android.mk + Eclipse project
* SDL (all other platforms): CMake

The ports and build files are located in the following three
subdirectories:

* android/
* SDL/
* Windows/

Please see the README in the directory corresponding to the
platform that you want to build for above.

Windows is separate from SDL because it has some graphical Win-only features.

Long term, the Windows port should be changed to use WX, like Dolphin. At that point,
the SDL port can probably go away as WX works for all Wintel-like platforms such as
MacOSX and Linux.
