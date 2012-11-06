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

#include "sceKernel.h"
#include "sceKernelThread.h"
#include "sceKernelMbx.h"
#include "HLE.h"

struct NativeMbxPacket
{
	u32 next;
	u8 priority;
	u8 padding[3];

	// data follows
};

struct NativeMbx
{
	SceSize size;
	char name[32];
	SceUInt attr;
	int numWaitThreads;
	int numMessages;
	u32 packetListHead;
};

struct Mbx : public KernelObject 
{
	const char *GetName() {return nmb.name;}
	const char *GetTypeName() {return "Mbx";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_MBXID; }
	int GetIDType() const { return SCE_KERNEL_TMID_Mbox; }

	NativeMbx nmb;

	std::vector<SceUID> receiveWaitingThreads;
};

void sceKernelCreateMbx()
{
	const char *name = Memory::GetCharPointer(PARAM(0));
	int memoryPartition = PARAM(1);
	SceUInt attr = PARAM(2);
	int size = PARAM(3);
	int opt = PARAM(4);

	ERROR_LOG(HLE, "sceKernelCreateMbx(%s, %i, %08x, %i, %08x)", name, memoryPartition, attr, size, opt);

	Mbx *m = new Mbx();
	SceUID id = kernelObjects.Create(m);

	m->nmb.size = sizeof(NativeMbx);
	strncpy(m->nmb.name, name, sizeof(m->nmb.name));
	m->nmb.attr = attr;
	m->nmb.numWaitThreads = 0;
	m->nmb.numMessages = 0;
	m->nmb.packetListHead = 0;

	RETURN(id);
}

void sceKernelDeleteMbx()
{
	SceUInt uid = PARAM(0);
	ERROR_LOG(HLE, "sceKernelDeleteMbx(%i)", uid);
	RETURN(kernelObjects.Destroy<Mbx>(uid));
}

void sceKernelSendMbx()
{
	SceUInt uid = PARAM(0);
	u32 packetAddr = PARAM(1);

	ERROR_LOG(HLE, "UNIMPL sceKernelSendMbx(%i, %08x)", uid, packetAddr);
	RETURN(0);
}

void sceKernelReceiveMbx()
{
	SceUInt uid = PARAM(0);
	u32 packetAddrPtr = PARAM(1);
	u32 timeoutPtr = PARAM(2);

	ERROR_LOG(HLE, "UNIMPL sceKernelReceiveMbx(%i, %08x, %08x)", uid, packetAddrPtr, timeoutPtr);
	RETURN(0);
}

void sceKernelReceiveMbxCB()
{
	SceUInt uid = PARAM(0);
	u32 packetAddrPtr = PARAM(1);
	u32 timeoutPtr = PARAM(2);
	__KernelCheckCallbacks();

	ERROR_LOG(HLE, "UNIMPL sceKernelReceiveMbxCB(%i, %08x, %08x)", uid, packetAddrPtr, timeoutPtr);
	RETURN(0);
}

void sceKernelPollMbx()
{
	SceUInt uid = PARAM(0);
	u32 packetAddrPtr = PARAM(1);

	ERROR_LOG(HLE, "UNIMPL sceKernelPollMbx(%i, %08x)", uid, packetAddrPtr);
	RETURN(0);
}

void sceKernelCancelReceiveMbx()
{
	SceUInt uid = PARAM(0);
	int numWaitingThreadsAddr = PARAM(1);

	ERROR_LOG(HLE, "UNIMPL sceKernelCancelReceiveMbx(%i, %i)", uid, numWaitingThreadsAddr);
	RETURN(0);
}

void sceKernelCancelMbx()
{
	SceUInt uid = PARAM(0);
	u32 numSendThreads = PARAM(1);
	u32 numReceiveThreads = PARAM(2);

	ERROR_LOG(HLE, "UNIMPL sceKernelCancelMbx(%i, %i, %i)", uid, numSendThreads, numReceiveThreads);
	RETURN(0);
}

void sceKernelReferMbxStatus()
{
	SceUInt uid = PARAM(0);
	u32 mbxStatusAddr = PARAM(1);

	ERROR_LOG(HLE, "sceKernelReferMbxStatus(%i, %08x)", uid, mbxStatusAddr);
	u32 error;
	Mbx *mp = kernelObjects.Get<Mbx>(uid, error);
	if (mp)
	{
		Memory::WriteStruct(mbxStatusAddr, &mp->nmb);
		RETURN(0);
	}
	else
	{
		RETURN(error);
	}
}
