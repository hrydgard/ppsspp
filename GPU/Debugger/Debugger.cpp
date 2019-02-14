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

#include "GPU/GPU.h"
#include "GPU/Debugger/Breakpoints.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/Debugger/Stepping.h"

namespace GPUDebug {

static bool active = false;
static bool inited = false;
static BreakNext breakNext = BreakNext::NONE;
static int breakAtCount = -1;

static int primsLastFrame = 0;
static int primsThisFrame = 0;
static int thisFlipNum = 0;

static void Init() {
	if (!inited) {
		GPUBreakpoints::Init();
		Core_ListenStopRequest(&GPUStepping::ForceUnpause);
		inited = true;
	}
}

void SetActive(bool flag) {
	Init();

	active = flag;
	if (!active) {
		breakNext = BreakNext::NONE;
		breakAtCount = -1;
		GPUStepping::ResumeFromStepping();
	}
}

bool IsActive() {
	return active;
}

void SetBreakNext(BreakNext next) {
	SetActive(true);
	breakNext = next;
	breakAtCount = -1;
	if (next == BreakNext::TEX) {
		GPUBreakpoints::AddTextureChangeTempBreakpoint();
	} else if (next == BreakNext::PRIM || next == BreakNext::COUNT) {
		GPUBreakpoints::AddCmdBreakpoint(GE_CMD_PRIM, true);
		GPUBreakpoints::AddCmdBreakpoint(GE_CMD_BEZIER, true);
		GPUBreakpoints::AddCmdBreakpoint(GE_CMD_SPLINE, true);
	} else if (next == BreakNext::CURVE) {
		GPUBreakpoints::AddCmdBreakpoint(GE_CMD_BEZIER, true);
		GPUBreakpoints::AddCmdBreakpoint(GE_CMD_SPLINE, true);
	}
	GPUStepping::ResumeFromStepping();
}

void SetBreakCount(int c, bool relative) {
	if (relative) {
		breakAtCount = primsThisFrame + c;
	} else {
		breakAtCount = c;
	}
}

static bool IsBreakpoint(u32 pc, u32 op) {
	if (breakNext == BreakNext::OP) {
		return true;
	} else if (breakNext == BreakNext::COUNT) {
		return primsThisFrame == breakAtCount;
	} else {
		return GPUBreakpoints::IsBreakpoint(pc, op);
	}
}

void NotifyCommand(u32 pc) {
	if (!active)
		return;
	u32 op = Memory::ReadUnchecked_U32(pc);
	u32 cmd = op >> 24;
	if (thisFlipNum != gpuStats.numFlips) {
		primsLastFrame = primsThisFrame;
		primsThisFrame = 0;
		thisFlipNum = gpuStats.numFlips;
	}
	if (cmd == GE_CMD_PRIM || cmd == GE_CMD_BEZIER || cmd == GE_CMD_SPLINE) {
		primsThisFrame++;
	}

	if (IsBreakpoint(pc, op)) {
		GPUBreakpoints::ClearTempBreakpoints();

		auto info = gpuDebug->DissassembleOp(pc);
		NOTICE_LOG(G3D, "Waiting at %08x, %s", pc, info.desc.c_str());
		GPUStepping::EnterStepping();
	}
}

void NotifyDraw() {
	if (!active)
		return;
	if (breakNext == BreakNext::DRAW) {
		NOTICE_LOG(G3D, "Waiting at a draw");
		GPUStepping::EnterStepping();
	}
}

void NotifyDisplay(u32 framebuf, u32 stride, int format) {
	if (!active)
		return;
	if (breakNext == BreakNext::FRAME) {
		// This should work fine, start stepping at the first op of the new frame.
		breakNext = BreakNext::OP;
	}
}

void NotifyTextureAttachment(u32 texaddr) {
	if (!active)
		return;
}

int PrimsThisFrame() {
	return primsThisFrame;
}

int PrimsLastFrame() {
	return primsLastFrame;
}

}
