# PPSSPP Agent instructions

These rules apply to this repository by default.

Ignore the folder ai_instructions in the root directory, it's old stuff from contributors.

## General instructions

1. Keep style changes minimal unless requested. Follow existing code patterns and conventions.
2. Keep cross-platform parity in mind when changing shared code. See below for more multiplatform tips

## Core Safety Checks

1. For HLE, CPU, GPU, timing, threading, and memory changes, call out regression risks explicitly.
2. Consider savestate compatibility when changing serialized state.

## Build and Validation

To verify that things build on Linux/Mac, use ./b.sh --debug. For Windows, use the Visual Studio solution in the Windows subdirectory.
Do not run unit test (I will add instructions for how to run them later).

## Multiplatform considerations

The emulator has multiple platform-specific entry points. Some of these will be merged or removed in the future, but are all
still there. To verify that a change works, technically we need to compile for all these systems, but in practice we'll
just compile locally and test the platform we are currently on, and let CI handle the cross platform considerations.

System_-prefixed wrapper functions implement kind of a platform wrapper for some functionality, and are implemented in
the following list of files for each system. If we change one, we need to change them all.

Windows/main.cpp
ios/main.cpp
SDL/SDLMain.cpp
UWP/PPSSPP_UWPMain.cpp
Qt/main.cpp
android/jni/app-android.cpp
libretro/libretro.cpp

## Headless and unittest builds

We have additional PPSSPPHeadless and unit test builds (/headless and /unittest), that have their own separate
main functions (and also stub out most of the System_ functions as needed). Take these into account
when making cross platform changes.

New unit tests are added by listing them in availableTests in unittest.cpp. If they are large, put them in
separate files in the unittest subdirectory. Remember to update both CMakeLists.txt and the visual studio project.
