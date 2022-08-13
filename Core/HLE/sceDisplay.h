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

#include "Core/MemMap.h"

void __DisplayInit();
void __DisplayDoState(PointerWrap &p);
void __DisplayShutdown();

void Register_sceDisplay();

// Get information about the current framebuffer.
bool __DisplayGetFramebuf(PSPPointer<u8> *topaddr, u32 *linesize, u32 *pixelFormat, int mode);
void __DisplaySetFramebuf(u32 topaddr, int linesize, int pixelformat, int sync);

// Call this when resuming to avoid a small speedup burst
void __DisplaySetWasPaused();

void Register_sceDisplay_driver();
void __DisplayWaitForVblanks(const char* reason, int vblanks, bool callbacks = false);
