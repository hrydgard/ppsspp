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
		GPUStepping::ResumeFromStepping();
	}
}

bool IsActive() {
	return active;
}

void SetBreakNext(BreakNext next) {
	SetActive(true);
	breakNext = next;
	if (next == BreakNext::TEX) {
		GPUBreakpoints::AddTextureChangeTempBreakpoint();
	} else if (next == BreakNext::PRIM) {
		GPUBreakpoints::AddCmdBreakpoint(GE_CMD_PRIM, true);
		GPUBreakpoints::AddCmdBreakpoint(GE_CMD_BEZIER, true);
		GPUBreakpoints::AddCmdBreakpoint(GE_CMD_SPLINE, true);
	} else if (next == BreakNext::CURVE) {
		GPUBreakpoints::AddCmdBreakpoint(GE_CMD_BEZIER, true);
		GPUBreakpoints::AddCmdBreakpoint(GE_CMD_SPLINE, true);
	}
	GPUStepping::ResumeFromStepping();
}

void NotifyCommand(u32 pc) {
	if (!active)
		return;
	u32 op = Memory::ReadUnchecked_U32(pc);
	if (breakNext == BreakNext::OP || GPUBreakpoints::IsBreakpoint(pc, op)) {
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

}
