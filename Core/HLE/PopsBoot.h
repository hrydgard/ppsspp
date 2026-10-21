// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <string>

#include "Common/CommonTypes.h"
#include "Core/HLE/sceKernel.h"

class FileLoader;
class PointerWrap;

void __PopsBootInit();
void __PopsBootShutdown();
void __PopsBootDoState(PointerWrap &p);
bool PopsBootActive();

// After the POPS manager is loaded: the user modules of the POPS boot configuration.
void __PopsBootLoadModules();
// After the POPS manager is loaded: have it load the emulator for the game, as the kernel's init does.
// Fails for a manager other than 6.60's.
bool __PopsBootStartInit(FileLoader *pbp, const std::string &gamePath, std::string *error);
// The emulator module is loaded, not yet started.
void __PopsBootPatchEmulator(u32 textAddr, u32 textSize, u32 decodeStub);

// The manager registered its version key function with SysMem.
void __PopsBootSetVersionKeyFunc(u32 func);

void Register_PopsBootCalls();
