PPSSPP - a fast and portable PSP emulator
=========================================

Written by Henrik Rydgård

Released under the GPL 2.0 in November 2012

Official website:
http://www.ppsspp.org/

To contribute, see [the development page][ppsspp-devel].

For the latest source code and build instructions, see [our github page][ppsspp-repo].

Basic build instructions
------------------------

(for more detailed instructions, see [the development page][ppsspp-devel])

First of all, after having checked out the source, don't forget to
run:

    git submodule update --init
 
in order to get the "native" library.

Now, the actual building:

PPSSPP currently uses CMake for its build system. In order
to build for most systems, create a `build` directory and
run:

    cmake path/to/ppsspp
    make

You can specify the -G parameter to cmake to choose a generator.
The `NMake Makefiles`, `Visual Studio 11` (projects + sln),
`GNU Makefiles` and `Unix Makefiles` generators have been tested.

Of course in-tree builds are supported, but that makes cleanup
harder to do; with out-of-tree builds you can just remove the
`build` directory.

Building for Linux/BSD/etc
--------------------------

Install the libsdl1.2 (SDL 1.2) development headers. This is called
`libsdl1.2-devel` on Debian/Ubuntu, `SDL-devel` on Fedora/RHEL,
`sdl12` on BSD ports.

Currently the user interface is identical to Android's, operated
with the mouse. A Qt-based interface is planned.

Building for OSX
----------------

Install the Xcode Command Line Tools and, using macports, fink or
homebrew, install the SDL development headers. This is called `sdl`
on homebrew. Just follow the basic build instructions afterwards.

Currently the user interface is identical to Android's, operated
with the mouse. A Qt-based interface is planned.

Building for Android
--------------------

To build for android, first you must set the ANDROID\_NDK environment
variable to point to the path of your NDK install. This is done on
windows cmd with `set ANDROID_NDK=X:\...`, on bourne shells with
`export ANDROID_NDK=/path/to/ndk`, and on C shells with
`setenv ANDROID_NDK /path/to/ndk`.

Create a `build-android` directory and inside it run:

    cmake -DCMAKE_TOOLCHAIN_FILE=path/to/ppsspp/android/android.toolchain.cmake path/to/ppsspp
    make

After `make` finishes, it will have created the needed .so files in
path/to/ppsspp/android/libs/armeabi-v7a. You can now use the build.xml
in the android/ dir to build the final executable, or import the android/
folder as an existing project in Eclipse.

Note that Eclipse won't notice if you have made changes to the C++ code.
Introduce a meaningless change to a random .java file such as a whitespace
to get Eclipse to rebuild the project.

Also note that the `Visual Studio` generators aren't compatible with compilers
other than microsoft's, but `NMake Makefiles` works fine.

[ppsspp-repo]: <https://github.com/hrydgard/ppsspp>
    "https://github.com/hrydgard/ppsspp"
[ppsspp-devel]: <http://www.ppsspp.org/development.html>
    "http://www.ppsspp.org/development.html"
