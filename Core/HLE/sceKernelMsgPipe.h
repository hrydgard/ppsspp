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

#include <map>
#include <vector>

#include "Core/HLE/sceKernel.h"
#include "Core/HLE/ErrorCodes.h"
#include "Core/MemMap.h"

class PointerWrap;

struct NativeMsgPipe {
	SceSize_le size;
	char name[32];
	SceUInt_le attr;
	s32_le bufSize;
	s32_le freeSize;
	s32_le numSendWaitThreads;
	s32_le numReceiveWaitThreads;
};

struct MsgPipeWaitingThread {
	SceUID threadID;
	u32 bufAddr;
	u32 bufSize;
	// Free space at the end for receive, valid/free to read bytes from end for send.
	u32 freeSize;
	s32 waitMode;
	PSPPointer<u32_le> transferredBytes;
	u64 pausedTimeout;

	bool IsStillWaiting(SceUID waitID) const;
	void WriteCurrentTimeout(SceUID waitID) const;
	void Complete(SceUID waitID, int result) const;
	void Cancel(SceUID waitID, int result) const;
	void ReadBuffer(u32 destPtr, u32 len);
	void WriteBuffer(u32 srcPtr, u32 len);

	bool operator ==(const SceUID &otherThreadID) const {
		return threadID == otherThreadID;
	}
};

// Exposed here (rather than kept private to sceKernelMsgPipe.cpp) so the WebSocket debugger can
// read a live object's state directly via kernelObjects.Get<MsgPipe>()/Iterate<MsgPipe>() - see
// HLEKernelObjectSubscriber.cpp. That's a read-only use: nothing outside this file should call
// any of these methods (DoState, the Check*/Sort*/Add* helpers, etc.) or otherwise mutate a
// MsgPipe - it's public here for this file's own use as before, not an invitation to write to it
// from elsewhere. Method bodies stay defined in the .cpp (only the trivial one-liners are
// inline), so this header doesn't need to pull in Memory::/BlockAllocator/CoreTiming.
struct MsgPipe : public KernelObject {
	const char *GetName() override { return nmp.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "MsgPipe"; }
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_MPPID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Mpipe; }
	int GetIDType() const override { return SCE_KERNEL_TMID_Mpipe; }

	MsgPipe();
	~MsgPipe();

	u32 GetUsedSize();
	void AddWaitingThread(std::vector<MsgPipeWaitingThread> &list, SceUID id, u32 addr, u32 size, int waitMode, u32 transferredBytesAddr);
	void AddSendWaitingThread(SceUID id, u32 addr, u32 size, int waitMode, u32 transferredBytesAddr);
	void AddReceiveWaitingThread(SceUID id, u32 addr, u32 size, int waitMode, u32 transferredBytesAddr);
	bool CheckSendThreads();
	bool CheckReceiveThreads();
	void SortThreads(std::vector<MsgPipeWaitingThread> &waitingThreads, bool usePrio);
	void SortReceiveThreads();
	void SortSendThreads();
	void RemoveReceiveWaitingThread(SceUID threadID);
	void RemoveSendWaitingThread(SceUID threadID);
	void DoState(PointerWrap &p) override;

	NativeMsgPipe nmp;

	std::vector<MsgPipeWaitingThread> sendWaitingThreads;
	std::vector<MsgPipeWaitingThread> receiveWaitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	std::map<SceUID, MsgPipeWaitingThread> pausedSendWaits;
	std::map<SceUID, MsgPipeWaitingThread> pausedReceiveWaits;

	u32 buffer;
};

int sceKernelCreateMsgPipe(const char *name, int partition, u32 attr, u32 size, u32 optionsPtr);
int sceKernelDeleteMsgPipe(SceUID uid);
int sceKernelSendMsgPipe(SceUID uid, u32 sendBufAddr, u32 sendSize, u32 waitMode, u32 resultAddr, u32 timeoutPtr);
int sceKernelSendMsgPipeCB(SceUID uid, u32 sendBufAddr, u32 sendSize, u32 waitMode, u32 resultAddr, u32 timeoutPtr);
int sceKernelTrySendMsgPipe(SceUID uid, u32 sendBufAddr, u32 sendSize, u32 waitMode, u32 resultAddr);
int sceKernelReceiveMsgPipe(SceUID uid, u32 receiveBufAddr, u32 receiveSize, u32 waitMode, u32 resultAddr, u32 timeoutPtr);
int sceKernelReceiveMsgPipeCB(SceUID uid, u32 receiveBufAddr, u32 receiveSize, u32 waitMode, u32 resultAddr, u32 timeoutPtr);
int sceKernelTryReceiveMsgPipe(SceUID uid, u32 receiveBufAddr, u32 receiveSize, u32 waitMode, u32 resultAddr);
int sceKernelCancelMsgPipe(SceUID uid, u32 numSendThreadsAddr, u32 numReceiveThreadsAddr);
int sceKernelReferMsgPipeStatus(SceUID uid, u32 statusPtr);

void __KernelMsgPipeInit();
void __KernelMsgPipeDoState(PointerWrap &p);
KernelObject *__KernelMsgPipeObject();
