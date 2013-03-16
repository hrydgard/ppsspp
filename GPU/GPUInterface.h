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

#include "../Globals.h"
#include "GPUState.h"
#include <deque>

class PointerWrap;

enum DisplayListStatus
{
	PSP_GE_LIST_DONE          = 0, // reached finish+end
	PSP_GE_LIST_QUEUED        = 1, // in queue, not stalled
	PSP_GE_LIST_DRAWING       = 2, // drawing
	PSP_GE_LIST_STALL_REACHED = 3, // stalled
	PSP_GE_LIST_END_REACHED   = 4, // reached signal+end, in jpcsp but not in pspsdk?
	PSP_GE_LIST_CANCEL_DONE   = 5, // canceled?
};


// Used for debug
struct FramebufferInfo
{
	u32 fb_address;
	u32 z_address;
	int format;
	u32 width;
	u32 height;
	void* fbo;
};

struct DisplayList
{
	int id;
	u32 startpc;
	u32 pc;
	u32 stall;
	DisplayListStatus status;
	int subIntrBase;
	u16 subIntrToken;
};

class GPUInterface
{
public:
	virtual ~GPUInterface() {}

	// Initialization
	virtual void InitClear() = 0;

	// Draw queue management
	virtual DisplayList* getList(int listid) = 0;
	// TODO: Much of this should probably be shared between the different GPU implementations.
	virtual u32 EnqueueList(u32 listpc, u32 stall, int subIntrBase, bool head) = 0;
	virtual void UpdateStall(int listid, u32 newstall) = 0;
	virtual void DrawSync(int mode) = 0;
	virtual void Continue() = 0;

	virtual void InterruptStart() = 0;
	virtual void InterruptEnd() = 0;

	virtual void PreExecuteOp(u32 op, u32 diff) = 0;
	virtual void ExecuteOp(u32 op, u32 diff) = 0;
	virtual bool InterpretList(DisplayList& list) = 0;
	virtual int  listStatus(int listid) = 0;

	// Framebuffer management
	virtual void SetDisplayFramebuffer(u32 framebuf, u32 stride, int format) = 0;
	virtual void BeginFrame() = 0;  // Can be a good place to draw the "memory" framebuffer for accelerated plugins
	virtual void CopyDisplayToOutput() = 0;

	// Tells the GPU to update the gpuStats structure.
	virtual void UpdateStats() = 0;

	// Invalidate any cached content sourced from the specified range.
	// If size = -1, invalidate everything.
	virtual void InvalidateCache(u32 addr, int size) = 0;
	virtual void InvalidateCacheHint(u32 addr, int size) = 0;

	// Internal hack to avoid interrupts from "PPGe" drawing (utility UI, etc)
	virtual void EnableInterrupts(bool enable) = 0;

	virtual void DeviceLost() = 0;
	virtual void Flush() = 0;
	virtual void DoState(PointerWrap &p) = 0;

	// Called by the window system if the window size changed. This will be reflected in PSPCoreParam.pixel*.
	virtual void Resized() = 0;
	virtual bool FramebufferDirty() = 0;

	// Debugging
	virtual void DumpNextFrame() = 0;
	virtual const std::deque<DisplayList>& GetDisplayLists() = 0;
	virtual DisplayList* GetCurrentDisplayList() = 0;
	virtual bool DecodeTexture(u8* dest, GPUgstate state) = 0;
	virtual std::vector<FramebufferInfo> GetFramebufferList() = 0;
};
