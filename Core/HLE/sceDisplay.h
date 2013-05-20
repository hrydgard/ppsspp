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

void __DisplayInit();
void __DisplayDoState(PointerWrap &p);
void __DisplayShutdown();

void Register_sceDisplay();

// will return true once after every end-of-frame.
bool __DisplayFrameDone();

// Get information about the current framebuffer.
bool __DisplayGetFramebuf(u8 **topaddr, u32 *linesize, u32 *pixelFormat, int mode);

typedef void (*VblankCallback)();
// Listen for vblank events.  Only register during init.
void __DisplayListenVblank(VblankCallback callback);

void __DisplayGetDebugStats(char stats[2048]);
void __DisplayGetFPS(float *out_vps, float *out_fps);
void __DisplayGetAveragedFPS(float *out_vps, float *out_fps);
