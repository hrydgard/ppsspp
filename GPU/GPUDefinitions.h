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


#include <cstring>

#include "Common/Common.h"
#include "Core/MemMap.h"


// X11, sigh.
#ifdef None
#undef None
#endif

// NOTE: This seems a bit insane, but these are two very similar enums!

// These are return values from DrawSync(), and also sceGeDrawSync().
// So these are frozen and "official".
enum DisplayListDrawSyncStatus {
	// The list has been completed
	PSP_GE_LIST_COMPLETED = 0,
	// The list is queued but not executed yet
	PSP_GE_LIST_QUEUED = 1,
	// The list is currently being executed
	PSP_GE_LIST_DRAWING = 2,
	// The list was stopped because it encountered stall address
	PSP_GE_LIST_STALLING = 3,
	// The list is paused because of a signal or sceGeBreak
	PSP_GE_LIST_PAUSED = 4,
};

// These are states, stored on the lists! not sure if these are exposed, or if their values can be changed?
enum DisplayListState {
	// No state assigned, the list is empty
	PSP_GE_DL_STATE_NONE = 0,
	// The list has been queued
	PSP_GE_DL_STATE_QUEUED = 1,
	// The list is being executed (or stalled?)
	PSP_GE_DL_STATE_RUNNING = 2,
	// The list was completed and will be removed
	PSP_GE_DL_STATE_COMPLETED = 3,
	// The list has been paused by a signal
	PSP_GE_DL_STATE_PAUSED = 4,
};

enum GPUInvalidationType {
	// Affects all memory.  Not considered highly.
	GPU_INVALIDATE_ALL,
	// Indicates some memory may have changed.
	GPU_INVALIDATE_HINT,
	// Reliable invalidation (where any hashing, etc. is unneeded, it'll always invalidate.)
	GPU_INVALIDATE_SAFE,
	// Forced invalidation for when the texture hash may not catch changes.
	GPU_INVALIDATE_FORCE,
};

enum class DLResult {
	Done,  // Or stall
	Error,
	DebugBreak,  // used for stepping, breakpoints
};
enum SignalBehavior {
	PSP_GE_SIGNAL_NONE = 0x00,
	PSP_GE_SIGNAL_HANDLER_SUSPEND = 0x01,
	PSP_GE_SIGNAL_HANDLER_CONTINUE = 0x02,
	PSP_GE_SIGNAL_HANDLER_PAUSE = 0x03,
	PSP_GE_SIGNAL_SYNC = 0x08,
	PSP_GE_SIGNAL_JUMP = 0x10,
	PSP_GE_SIGNAL_CALL = 0x11,
	PSP_GE_SIGNAL_RET = 0x12,
	PSP_GE_SIGNAL_RJUMP = 0x13,
	PSP_GE_SIGNAL_RCALL = 0x14,
	PSP_GE_SIGNAL_OJUMP = 0x15,
	PSP_GE_SIGNAL_OCALL = 0x16,

	PSP_GE_SIGNAL_RTBP0 = 0x20,
	PSP_GE_SIGNAL_RTBP1 = 0x21,
	PSP_GE_SIGNAL_RTBP2 = 0x22,
	PSP_GE_SIGNAL_RTBP3 = 0x23,
	PSP_GE_SIGNAL_RTBP4 = 0x24,
	PSP_GE_SIGNAL_RTBP5 = 0x25,
	PSP_GE_SIGNAL_RTBP6 = 0x26,
	PSP_GE_SIGNAL_RTBP7 = 0x27,
	PSP_GE_SIGNAL_OTBP0 = 0x28,
	PSP_GE_SIGNAL_OTBP1 = 0x29,
	PSP_GE_SIGNAL_OTBP2 = 0x2A,
	PSP_GE_SIGNAL_OTBP3 = 0x2B,
	PSP_GE_SIGNAL_OTBP4 = 0x2C,
	PSP_GE_SIGNAL_OTBP5 = 0x2D,
	PSP_GE_SIGNAL_OTBP6 = 0x2E,
	PSP_GE_SIGNAL_OTBP7 = 0x2F,
	PSP_GE_SIGNAL_RCBP = 0x30,
	PSP_GE_SIGNAL_OCBP = 0x38,
	PSP_GE_SIGNAL_BREAK1 = 0xF0,
	PSP_GE_SIGNAL_BREAK2 = 0xFF,
};

enum GPURunState {
	GPUSTATE_RUNNING = 0,
	GPUSTATE_DONE = 1,
	GPUSTATE_STALL = 2,
	GPUSTATE_INTERRUPT = 3,
	GPUSTATE_ERROR = 4,
};

enum GPUSyncType {
	GPU_SYNC_DRAW,
	GPU_SYNC_LIST,
};

enum class WriteStencil {
	NEEDS_CLEAR = 1,
	STENCIL_IS_ZERO = 2,
	IGNORE_ALPHA = 4,
};
ENUM_CLASS_BITOPS(WriteStencil);

enum class GPUCopyFlag {
	NONE = 0,
	FORCE_SRC_MATCH_MEM = 1,
	FORCE_DST_MATCH_MEM = 2,
	// Note: implies src == dst and FORCE_SRC_MATCH_MEM.
	MEMSET = 4,
	DEPTH_REQUESTED = 8,
	DEBUG_NOTIFIED = 16,
	DISALLOW_CREATE_VFB = 32,
};
ENUM_CLASS_BITOPS(GPUCopyFlag);

struct DisplayListStackEntry {
	u32 pc;
	u32 offsetAddr;
	u32 baseAddr;
};

struct DisplayList {
	int id;
	u32 startpc;
	u32 pc;
	u32 stall;
	DisplayListState state;
	SignalBehavior signal;
	int subIntrBase;
	u16 subIntrToken;
	DisplayListStackEntry stack[32];
	int stackptr;
	bool interrupted;
	u64 waitUntilTicks;
	bool interruptsEnabled;
	bool pendingInterrupt;
	bool started;
	PSPPointer<u32_le> context;
	u32 offsetAddr;
	bool bboxResult;
	u32 stackAddr;

	u32 padding;  // Android x86-32 does not round the structure size up to the closest multiple of 8 like the other platforms.
};

namespace Draw {
class DrawContext;
}

enum DrawType {
	DRAW_UNKNOWN,
	DRAW_PRIM,
	DRAW_SPLINE,
	DRAW_BEZIER,
};

enum {
	FLAG_FLUSHBEFOREONCHANGE = 2,
	FLAG_EXECUTE = 4,
	FLAG_EXECUTEONCHANGE = 8,
	FLAG_READS_PC = 16,
	FLAG_WRITES_PC = 32,
	FLAG_DIRTYONCHANGE = 64,  // NOTE: Either this or FLAG_EXECUTE*, not both!
};

struct TransformedVertex {
	union {
		struct {
			float x, y, z, pos_w;     // in case of morph, preblend during decode
		};
		float pos[4];
	};
	union {
		struct {
			float u; float v; float uv_w;   // scaled by uscale, vscale, if there
		};
		float uv[3];
	};
	float fog;
	union {
		u8 color0[4];   // prelit
		u32 color0_32;
	};
	union {
		u8 color1[4];   // prelit
		u32 color1_32;
	};

	void CopyFromWithOffset(const TransformedVertex &other, float xoff, float yoff) {
		this->x = other.x + xoff;
		this->y = other.y + yoff;
		memcpy(&this->z, &other.z, sizeof(*this) - sizeof(float) * 2);
	}
};
