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
