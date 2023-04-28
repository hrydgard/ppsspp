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
