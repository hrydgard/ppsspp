// Copyright (c) 2018- PPSSPP Project.

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

#include "Common/CommonTypes.h"

namespace GPUDebug {

enum class BreakNext {
	NONE,
	OP,
	DRAW,
	TEX,
	NONTEX,
	FRAME,
	VSYNC,
	PRIM,
	CURVE,
	COUNT,
};

void SetActive(bool flag);
bool IsActive();

void SetBreakNext(BreakNext next);
void SetBreakCount(int c, bool relative = false);

// While debugging is active, these may block.
bool NotifyCommand(u32 pc);
void NotifyDraw();
void NotifyDisplay(u32 framebuf, u32 stride, int format);
void NotifyBeginFrame();

int PrimsThisFrame();
int PrimsLastFrame();

bool SetRestrictPrims(const char *rule);
const char *GetRestrictPrims();

}
