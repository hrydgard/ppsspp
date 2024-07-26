Build instructions
==================
See https://github.com/hrydgard/ppsspp/wiki/Build-instructions .

Also, for general information, see https://www.ppsspp.org/docs .

SDL2 Game Controller Support Notes
==================================

For SDL2 game controller support, at least SDL 2.0.4 is required.

Under the assets directory is the SDL2 game controller database: gamecontrollerdb.txt. This file contains many known control pad mappings for Windows, Linux and MAC OS. PPSSPPSDL will load this file at start-up and work out how to assign control pad buttons for your control pad.

Hot plugging of control pads is also supported.

If you control pad has a "Guide" or "Home" button then when pressed, this will trigger the emulator pause menu, thus allowing you to exit the emulator if you wish or load another game from your library.

WSL notes
=========
Works fine on WSL2 with OpenGL, at least. For more notes, see https://www.ppsspp.org/docs/development/wsl/ .

Running with valgrind
=====================

Here's an example where we both use suppressions, and generate new ones (that you can then take from suppressions.log and simplify and copy to valgrind-wsl2.supp):

> valgrind --suppressions=SDL/valgrind-wsl2.supp --gen-suppressions=all --log-file=suppressions.log build/PPSSPPSDL

Running with ASAN
=================

./b.sh --sanitize

build/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL

Running with UBSAN
==================

./b.sh --sanitize --sanitizeub

UBSAN_OPTIONS=print_stacktrack=true build/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL
