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

#include "HLE.h"
#include "sceKernel.h"
#include "sceKernelMsgPipe.h"

struct NativeMsgPipe
{
	SceSize size;
	char name[32];
	SceUInt attr;
	int bufSize;
	int freeSize;
	int numSendWaitThreads;
	int numReceiveWaitThreads;
};

struct MsgPipe : public KernelObject 
{
	const char *GetName() {return nmp.name;}
	const char *GetTypeName() {return "MsgPipe";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_MPPID; }
	int GetIDType() const { return SCE_KERNEL_TMID_Mpipe; }

	NativeMsgPipe nmp;

	std::vector<SceUID> sendWaitingThreads;
	std::vector<SceUID> receiveWaitingThreads;

	// Ring buffer
	u8 *buffer;
};

void sceKernelCreateMsgPipe()
{
	const char *name = Memory::GetCharPointer(PARAM(0));
	//int memoryPartition = PARAM(1);
	SceUInt attr = PARAM(2);
	int size = PARAM(3);
	//int opt = PARAM(4);

	MsgPipe *m = new MsgPipe();
	SceUID id = kernelObjects.Create(m);

	m->nmp.size = sizeof(NativeMsgPipe);
	strncpy(m->nmp.name, name, sizeof(m->nmp.name));
	m->nmp.attr = attr;
	m->nmp.bufSize = size;
	m->nmp.freeSize = size;
	m->nmp.numSendWaitThreads = 0;
	m->nmp.numReceiveWaitThreads = 0;

	m->buffer = new u8[size];
	RETURN(id);
}

void sceKernelDeleteMsgPipe()
{
	SceUInt uid = PARAM(0);
	u32 error;
	MsgPipe *p = kernelObjects.Get<MsgPipe>(uid, error);
	if (!p)
	{
		ERROR_LOG(HLE, "sceKernelDeleteMsgPipe(%i) - ERROR %08x", uid, error);
		return;
	}
	delete [] p->buffer;
	ERROR_LOG(HLE, "sceKernelDeleteMsgPipe(%i)", uid);
	RETURN(kernelObjects.Destroy<MsgPipe>(uid));
}

void sceKernelSendMsgPipe()
{
	SceUInt uid = PARAM(0);
	u32 sendBufAddr = PARAM(1);
	u32 sendSize = PARAM(2);
	int waitMode = PARAM(3);
	u32 resultAddr = PARAM(4);
	u32 timeoutPtr = PARAM(5);

	ERROR_LOG(HLE, "UNIMPL sceKernelSendMsgPipe(%i, %08x, %i, %i, %08x, %08x)", uid, sendBufAddr, sendSize, waitMode, resultAddr, timeoutPtr);
	RETURN(0);
}

void sceKernelSendMsgPipeCB()
{
	SceUInt uid = PARAM(0);
	u32 sendBufAddr = PARAM(1);
	u32 sendSize = PARAM(2);
	int waitMode = PARAM(3);
	u32 resultAddr = PARAM(4);
	u32 timeoutPtr = PARAM(5);

	ERROR_LOG(HLE, "UNIMPL sceKernelSendMsgPipeCB(%i, %08x, %i, %i, %08x, %08x)", uid, sendBufAddr, sendSize, waitMode, resultAddr, timeoutPtr);
	RETURN(0);
}

void sceKernelTrySendMsgPipe()
{
	SceUInt uid = PARAM(0);
	u32 sendBufAddr = PARAM(1);
	u32 sendSize = PARAM(2);
	int waitMode = PARAM(3);
	u32 resultAddr = PARAM(4);

	ERROR_LOG(HLE, "UNIMPL sceKernelTrySendMsgPipe(%i, %08x, %i, %i, %08x)", uid, sendBufAddr, sendSize, waitMode, resultAddr);
	RETURN(0);
}

void sceKernelReceiveMsgPipe()
{
	SceUInt uid = PARAM(0);
	u32 receiveBufAddr = PARAM(1);
	u32 receiveSize = PARAM(2);
	int waitMode = PARAM(3);
	u32 resultAddr = PARAM(4);
	u32 timeoutPtr = PARAM(5);

	ERROR_LOG(HLE, "UNIMPL sceKernelReceiveMsgPipe(%i, %08x, %i, %i, %08x, %08x)", uid, receiveBufAddr, receiveSize, waitMode, resultAddr, timeoutPtr);
	RETURN(0);
}

void sceKernelReceiveMsgPipeCB()
{
	SceUInt uid = PARAM(0);
	u32 receiveBufAddr = PARAM(1);
	u32 receiveSize = PARAM(2);
	int waitMode = PARAM(3);
	u32 resultAddr = PARAM(4);
	u32 timeoutPtr = PARAM(5);

	ERROR_LOG(HLE, "UNIMPL sceKernelReceiveMsgPipeCB(%i, %08x, %i, %i, %08x, %08x)", uid, receiveBufAddr, receiveSize, waitMode, resultAddr, timeoutPtr);
	RETURN(0);
}

void sceKernelTryReceiveMsgPipe()
{
	SceUInt uid = PARAM(0);
	u32 receiveBufAddr = PARAM(1);
	u32 receiveSize = PARAM(2);
	int waitMode = PARAM(3);
	u32 resultAddr = PARAM(4);

	ERROR_LOG(HLE, "UNIMPL sceKernelTryReceiveMsgPipe(%i, %08x, %i, %i, %08x)", uid, receiveBufAddr, receiveSize, waitMode, resultAddr);
	RETURN(0);
}

void sceKernelCancelMsgPipe()
{
	SceUInt uid = PARAM(0);
	u32 numSendThreads = PARAM(1);
	u32 numReceiveThreads = PARAM(2);

	ERROR_LOG(HLE, "UNIMPL sceKernelCancelMsgPipe(%i, %i, %i)", uid, numSendThreads, numReceiveThreads);
	RETURN(0);
}

void sceKernelReferMsgPipeStatus()
{
	SceUID uid = PARAM(0);
	u32 msgPipeStatusAddr = PARAM(1);

	DEBUG_LOG(HLE,"sceKernelReferMsgPipeStatus(%i, %08x)", uid, msgPipeStatusAddr);
	u32 error;
	MsgPipe *mp = kernelObjects.Get<MsgPipe>(uid, error);
	if (mp)
	{
		Memory::WriteStruct(msgPipeStatusAddr, &mp->nmp);
		RETURN(0);
	}
	else
	{
		RETURN(error);
	}
}

