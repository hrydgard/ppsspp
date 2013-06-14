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
#include "Core/HLE/sceKernelThread.h"
#include <list>
#include <string>

class PointerWrap;

enum DisplayListStatus
{
    /** The list has been completed */
    PSP_GE_LIST_COMPLETED = 0,
    /** The list is queued but not executed yet */
    PSP_GE_LIST_QUEUED = 1,
    /** The list is currently being executed */
    PSP_GE_LIST_DRAWING = 2,
    /** The list was stopped because it encountered stall address */
    PSP_GE_LIST_STALLING = 3,
    /** The list is paused because of a signal or sceGeBreak */
    PSP_GE_LIST_PAUSED = 4,
};

enum DisplayListState
{
    /** No state assigned, the list is empty */
    PSP_GE_DL_STATE_NONE = 0,
    /** The list has been queued */
    PSP_GE_DL_STATE_QUEUED = 1,
    /** The list is being executed */
    PSP_GE_DL_STATE_RUNNING = 2,
    /** The list was completed and will be removed */
    PSP_GE_DL_STATE_COMPLETED = 3,
    /** The list has been paused by a signal */
    PSP_GE_DL_STATE_PAUSED = 4,
};

enum SignalBehavior
{
	PSP_GE_SIGNAL_NONE             = 0x00,
	PSP_GE_SIGNAL_HANDLER_SUSPEND  = 0x01,
	PSP_GE_SIGNAL_HANDLER_CONTINUE = 0x02,
	PSP_GE_SIGNAL_HANDLER_PAUSE    = 0x03,
	PSP_GE_SIGNAL_SYNC             = 0x08,
	PSP_GE_SIGNAL_JUMP             = 0x10,
	PSP_GE_SIGNAL_CALL             = 0x11,
	PSP_GE_SIGNAL_RET              = 0x12,
	PSP_GE_SIGNAL_RJUMP            = 0x13,
	PSP_GE_SIGNAL_RCALL            = 0x14,
	PSP_GE_SIGNAL_OJUMP            = 0x15,
	PSP_GE_SIGNAL_OCALL            = 0x16,

	PSP_GE_SIGNAL_RTBP0            = 0x20,
	PSP_GE_SIGNAL_RTBP1            = 0x21,
	PSP_GE_SIGNAL_RTBP2            = 0x22,
	PSP_GE_SIGNAL_RTBP3            = 0x23,
	PSP_GE_SIGNAL_RTBP4            = 0x24,
	PSP_GE_SIGNAL_RTBP5            = 0x25,
	PSP_GE_SIGNAL_RTBP6            = 0x26,
	PSP_GE_SIGNAL_RTBP7            = 0x27,
	PSP_GE_SIGNAL_OTBP0            = 0x28,
	PSP_GE_SIGNAL_OTBP1            = 0x29,
	PSP_GE_SIGNAL_OTBP2            = 0x2A,
	PSP_GE_SIGNAL_OTBP3            = 0x2B,
	PSP_GE_SIGNAL_OTBP4            = 0x2C,
	PSP_GE_SIGNAL_OTBP5            = 0x2D,
	PSP_GE_SIGNAL_OTBP6            = 0x2E,
	PSP_GE_SIGNAL_OTBP7            = 0x2F,
	PSP_GE_SIGNAL_RCBP             = 0x30,
	PSP_GE_SIGNAL_OCBP             = 0x38,
	PSP_GE_SIGNAL_BREAK1           = 0xF0,
	PSP_GE_SIGNAL_BREAK2           = 0xFF,
};

enum GPUState
{
	GPUSTATE_RUNNING = 0,
	GPUSTATE_DONE = 1,
	GPUSTATE_STALL = 2,
	GPUSTATE_INTERRUPT = 3,
	GPUSTATE_ERROR = 4,
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

struct DisplayListStackEntry
{
	u32 pc;
	u32 offsetAddr;
};

struct DisplayList
{
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
	u64 waitTicks;
};

enum GPUInvalidationType {
	// Affects all memory.  Not considered highly.
	GPU_INVALIDATE_ALL,
	// Indicates some memory may have changed.
	GPU_INVALIDATE_HINT,
	// Reliable invalidation (where any hashing, etc. is unneeded, it'll always invalidate.)
	GPU_INVALIDATE_SAFE,
};

class GPUInterface
{
public:
	virtual ~GPUInterface() {}

	static const int DisplayListMaxCount = 64;

	// Initialization
	virtual void InitClear() = 0;

	// Draw queue management
	virtual DisplayList* getList(int listid) = 0;
	// TODO: Much of this should probably be shared between the different GPU implementations.
	virtual u32  EnqueueList(u32 listpc, u32 stall, int subIntrBase, bool head) = 0;
	virtual u32  DequeueList(int listid) = 0;
	virtual u32  UpdateStall(int listid, u32 newstall) = 0;
	virtual u32  DrawSync(int mode) = 0;
	virtual int  ListSync(int listid, int mode) = 0;
	virtual u32  Continue() = 0;
	virtual u32  Break(int mode) = 0;

	virtual void InterruptStart(int listid) = 0;
	virtual void InterruptEnd(int listid) = 0;
	virtual void SyncEnd(WaitType waitType, int listid, bool wokeThreads) = 0;

	virtual void PreExecuteOp(u32 op, u32 diff) = 0;
	virtual void ExecuteOp(u32 op, u32 diff) = 0;
	virtual bool InterpretList(DisplayList& list) = 0;

	// Framebuffer management
	virtual void SetDisplayFramebuffer(u32 framebuf, u32 stride, int format) = 0;
	virtual void BeginFrame() = 0;  // Can be a good place to draw the "memory" framebuffer for accelerated plugins
	virtual void CopyDisplayToOutput() = 0;

	// Tells the GPU to update the gpuStats structure.
	virtual void UpdateStats() = 0;

	// Invalidate any cached content sourced from the specified range.
	// If size = -1, invalidate everything.
	virtual void InvalidateCache(u32 addr, int size, GPUInvalidationType type) = 0;
	// Update either RAM from VRAM, or VRAM from RAM... or even VRAM from VRAM.
	virtual void UpdateMemory(u32 dest, u32 src, int size) = 0;

	// Will cause the texture cache to be cleared at the start of the next frame.
	virtual void ClearCacheNextFrame() = 0;

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
	virtual void GetReportingInfo(std::string &primaryInfo, std::string &fullInfo) = 0;
	virtual const std::list<int>& GetDisplayLists() = 0;
	virtual DisplayList* GetCurrentDisplayList() = 0;
	virtual bool DecodeTexture(u8* dest, GPUgstate state) = 0;
	virtual std::vector<FramebufferInfo> GetFramebufferList() = 0;
};
