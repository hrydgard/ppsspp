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
#include "../../Core/CoreTiming.h"

#define SCE_KERNEL_MBA_THPRI 0x100
#define SCE_KERNEL_MBA_MSPRI 0x400
#define SCE_KERNEL_MBA_ATTR_KNOWN (SCE_KERNEL_MBA_THPRI | SCE_KERNEL_MBA_MSPRI)

typedef std::pair<SceUID, u32> MbxWaitingThread;
void __KernelMbxTimeout(u64 userdata, int cyclesLate);

bool mbxInitComplete = false;
int mbxWaitTimer = 0;

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

	void AddWaitingThread(SceUID id, u32 addr)
	{
		if (nmb.attr & SCE_KERNEL_MBA_THPRI)
		{
			for (std::vector<MbxWaitingThread>::iterator it = waitingThreads.begin(); it != waitingThreads.end(); it++)
			{
				if (__KernelGetThreadPrio(id) >= __KernelGetThreadPrio((*it).first))
				{
					waitingThreads.insert(it, std::make_pair(id, addr));
					break;
				}
			}
		}
		else
		{
			waitingThreads.push_back(std::make_pair(id, addr));
		}
	}

	NativeMbx nmb;

	std::vector<MbxWaitingThread> waitingThreads;
	std::vector<u32> messageQueue;
};

void __KernelMbxInit()
{
	mbxWaitTimer = CoreTiming::RegisterEvent("MbxTimeout", &__KernelMbxTimeout);

	mbxInitComplete = true;
}

bool __KernelUnlockMbxForThread(Mbx *m, MbxWaitingThread &th, u32 &error, int result, bool &wokeThreads)
{
	SceUID waitID = __KernelGetWaitID(th.first, WAITTYPE_MBX, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(th.first, error);

	// The waitID may be different after a timeout.
	if (waitID != m->GetUID())
		return true;

	if (result == 0)
		m->nmb.numWaitThreads--;
	else
	{
		// Null it out since nothing was received.
		if (Memory::IsValidAddress(th.second))
			Memory::Write_U32(0, th.second);
	}

	if (timeoutPtr != 0 && mbxWaitTimer != 0)
	{
		// Remove any event for this thread.
		u64 cyclesLeft = CoreTiming::UnscheduleEvent(mbxWaitTimer, th.first);
		Memory::Write_U32((u32) cyclesToUs(cyclesLeft), timeoutPtr);
	}

	__KernelResumeThreadFromWait(th.first, result);
	wokeThreads = true;
	return true;
}

void __KernelMbxTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID)userdata;

	u32 error;
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	if (timeoutPtr != 0)
		Memory::Write_U32(0, timeoutPtr);

	SceUID mbxID = __KernelGetWaitID(threadID, WAITTYPE_SEMA, error);
	Mbx *m = kernelObjects.Get<Mbx>(mbxID, error);
	if (m)
	{
		// This thread isn't waiting anymore, but we'll remove it from waitingThreads later.
		// The reason is, if it times out, but what it was waiting on is DELETED prior to it
		// actually running, it will get a DELETE result instead of a TIMEOUT.
		// So, we need to remember it or we won't be able to mark it DELETE instead later.
		m->nmb.numWaitThreads--;
	}

	__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
}

void __KernelWaitMbx(Mbx *m, u32 timeoutPtr)
{
	if (timeoutPtr == 0 || mbxWaitTimer == 0)
		return;

	int micro = (int) Memory::Read_U32(timeoutPtr);

	// TODO: test timing.
	if (micro <= 3)
		micro = 15;
	else if (micro <= 249)
		micro = 250;

	// This should call __KernelMbxTimeout() later, unless we cancel it.
	CoreTiming::ScheduleEvent(usToCycles(micro), mbxWaitTimer, __KernelGetCurThread());
}

void __KernelMbxRemoveThread(Mbx *m, SceUID threadID)
{
	for (size_t i = 0; i < m->waitingThreads.size(); i++)
	{
		MbxWaitingThread *t = &m->waitingThreads[i];
		if (t->first == threadID)
		{
			m->waitingThreads.erase(m->waitingThreads.begin() + i);
			break;
		}
	}
}

SceUID sceKernelCreateMbx(const char *name, u32 attr, u32 optAddr)
{
	if (!mbxInitComplete)
		__KernelMbxInit();

	if (!name)
	{
		WARN_LOG(HLE, "%08x=%s(): invalid name", SCE_KERNEL_ERROR_ERROR, __FUNCTION__);
		return SCE_KERNEL_ERROR_ERROR;
	}
	// Accepts 0x000 - 0x0FF, 0x100 - 0x1FF, and 0x400 - 0x4FF.
	if (((attr & ~SCE_KERNEL_MBA_ATTR_KNOWN) & ~0xFF) != 0)
	{
		WARN_LOG(HLE, "%08x=%s(): invalid attr parameter: %08x", SCE_KERNEL_ERROR_ILLEGAL_ATTR, __FUNCTION__, attr);
		return SCE_KERNEL_ERROR_ILLEGAL_ATTR;
	}

	Mbx *m = new Mbx();
	SceUID id = kernelObjects.Create(m);

	m->nmb.size = sizeof(NativeMbx);
	strncpy(m->nmb.name, name, 31);
	m->nmb.name[31] = 0;
	m->nmb.attr = attr;
	m->nmb.numWaitThreads = 0;
	m->nmb.numMessages = 0;
	m->nmb.packetListHead = 0;

	DEBUG_LOG(HLE, "%i=sceKernelCreateMbx(%s, %08x, %08x)", id, name, attr, optAddr);

	if (optAddr != 0)
		WARN_LOG(HLE, "%s(%s) unsupported options parameter: %08x", __FUNCTION__, name, optAddr);
	if ((attr & ~SCE_KERNEL_MBA_ATTR_KNOWN) != 0)
		WARN_LOG(HLE, "%s(%s) unsupported attr parameter: %08x", __FUNCTION__, name, attr);

	return id;
}

int sceKernelDeleteMbx(SceUID id)
{
	u32 error;
	Mbx *m = kernelObjects.Get<Mbx>(id, error);
	if (m)
	{
		DEBUG_LOG(HLE, "sceKernelDeleteMbx(%i)", id);

		bool wokeThreads;
		for (size_t i = 0; i < m->waitingThreads.size(); i++)
			__KernelUnlockMbxForThread(m, m->waitingThreads[i], error, SCE_KERNEL_ERROR_WAIT_DELETE, wokeThreads);
		m->waitingThreads.clear();

		if (wokeThreads)
			hleReSchedule("mbx deleted");
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelDeleteMbx(%i): invalid mbx id", id);
	}
	return kernelObjects.Destroy<Mbx>(id);
}

void sceKernelSendMbx(SceUID id, u32 packetAddr)
{
	u32 error;
	Mbx *m = kernelObjects.Get<Mbx>(id, error);
	NativeMbxPacket *addPacket = (NativeMbxPacket*)Memory::GetPointer(packetAddr);
	if (addPacket == 0)
	{
		ERROR_LOG(HLE, "sceKernelSendMbx(%i, %08x): invalid packet address", id, packetAddr);
		RETURN(-1);
		return;
	}

	if (!m)
	{
		ERROR_LOG(HLE, "sceKernelSendMbx(%i, %08x): invalid mbx id", id, packetAddr);
		RETURN(error);
		return;
	}

	if (m->waitingThreads.empty())
	{
		DEBUG_LOG(HLE, "sceKernelSendMbx(%i, %08x): no threads currently waiting, adding message to queue", id, packetAddr);
		if (m->nmb.attr & SCE_KERNEL_MBA_MSPRI)
		{
			for (std::vector<u32>::iterator it = m->messageQueue.begin(); it != m->messageQueue.end(); it++)
			{
				NativeMbxPacket *p = (NativeMbxPacket*)Memory::GetPointer(*it);
				if (addPacket->priority >= p->priority)
				{
					m->messageQueue.insert(it, packetAddr);
					break;
				}
			}
		}
		else
		{
			m->messageQueue.push_back(packetAddr);
		}
		RETURN(0);
	}
	else if (m->messageQueue.empty())
	{
		//__KernelUnlockMbxForThread(m, m->waitingThreads[i], error, SCE_KERNEL_ERROR_WAIT_DELETE, wokeThreads);
		Memory::Write_U32(packetAddr, m->waitingThreads.front().second);
		__KernelResumeThreadFromWait(m->waitingThreads.front().first, 0);
		DEBUG_LOG(HLE, "sceKernelSendMbx(%i, %08x): threads waiting, resuming %d", id, packetAddr, m->waitingThreads.front().first);
		m->waitingThreads.erase(m->waitingThreads.begin());
		RETURN(0);
		hleReSchedule("mbx sent");
	}
	else
	{
		ERROR_LOG(HLE, "sceKernelSendMbx(%i, %08x): WTF!? thread waiting while there is a message in the queue?", id, packetAddr);
		RETURN(-1);
	}
}

void sceKernelReceiveMbx(SceUID id, u32 packetAddrPtr, u32 timeoutPtr)
{
	u32 error;
	Mbx *m = kernelObjects.Get<Mbx>(id, error);

	if (!m)
	{
		ERROR_LOG(HLE, "sceKernelReceiveMbx(%i, %08x, %08x): invalid mbx id", id, packetAddrPtr, timeoutPtr);
		RETURN(error);
		return;
	}

	if (!m->messageQueue.empty())
	{
		DEBUG_LOG(HLE, "sceKernelReceiveMbx(%i, %08x, %08x): sending first queue message", id, packetAddrPtr, timeoutPtr);
		Memory::Write_U32(m->messageQueue.front(), packetAddrPtr);
		m->messageQueue.erase(m->messageQueue.begin());
		RETURN(0);
	}
	else
	{
		DEBUG_LOG(HLE, "sceKernelReceiveMbx(%i, %08x, %08x): no message in queue, waiting", id, packetAddrPtr, timeoutPtr);
		__KernelMbxRemoveThread(m, __KernelGetCurThread());
		m->AddWaitingThread(__KernelGetCurThread(), packetAddrPtr);
		RETURN(0);
		__KernelWaitMbx(m, timeoutPtr);
		__KernelWaitCurThread(WAITTYPE_MBX, id, 0, timeoutPtr, false);
	}
}

void sceKernelReceiveMbxCB(SceUID id, u32 packetAddrPtr, u32 timeoutPtr)
{
	u32 error;
	Mbx *m = kernelObjects.Get<Mbx>(id, error);

	if (!m)
	{
		ERROR_LOG(HLE, "sceKernelReceiveMbxCB(%i, %08x, %08x): invalid mbx id", id, packetAddrPtr, timeoutPtr);
		RETURN(error);
		return;
	}

	if (!m->messageQueue.empty())
	{
		DEBUG_LOG(HLE, "sceKernelReceiveMbxCB(%i, %08x, %08x): sending first queue message", id, packetAddrPtr, timeoutPtr);
		Memory::Write_U32(m->messageQueue.front(), packetAddrPtr);
		m->messageQueue.erase(m->messageQueue.begin());
		hleCheckCurrentCallbacks();
		RETURN(0);
	}
	else
	{
		DEBUG_LOG(HLE, "sceKernelReceiveMbxCB(%i, %08x, %08x): no message in queue, waiting", id, packetAddrPtr, timeoutPtr);
		__KernelMbxRemoveThread(m, __KernelGetCurThread());
		m->AddWaitingThread(__KernelGetCurThread(), packetAddrPtr);
		RETURN(0);
		__KernelWaitMbx(m, timeoutPtr);
		__KernelWaitCurThread(WAITTYPE_MBX, id, 0, timeoutPtr, true);
	}
}

int sceKernelPollMbx(SceUID id, u32 packetAddrPtr)
{
	u32 error;
	Mbx *m = kernelObjects.Get<Mbx>(id, error);

	if (!m)
	{
		ERROR_LOG(HLE, "sceKernelPollMbx(%i, %08x): invalid mbx id", id, packetAddrPtr);
		return error;
	}

	if (!m->messageQueue.empty())
	{
		DEBUG_LOG(HLE, "sceKernelPollMbx(%i, %08x): sending first queue message", id, packetAddrPtr);
		Memory::Write_U32(m->messageQueue.front(), packetAddrPtr);
		m->messageQueue.erase(m->messageQueue.begin());
		return 0;
	}
	else
	{
		DEBUG_LOG(HLE, "SCE_KERNEL_ERROR_MBOX_NOMSG=sceKernelPollMbx(%i, %08x): no message in queue", id, packetAddrPtr);
		return SCE_KERNEL_ERROR_MBOX_NOMSG;
	}
}

int sceKernelCancelReceiveMbx(SceUID id, u32 numWaitingThreadsAddr)
{
	u32 error;
	Mbx *m = kernelObjects.Get<Mbx>(id, error);

	if (!m)
	{
		ERROR_LOG(HLE, "sceKernelCancelReceiveMbx(%i, %08x): invalid mbx id", id, numWaitingThreadsAddr);
		return error;
	}

	u32 count = m->waitingThreads.size();
	DEBUG_LOG(HLE, "sceKernelCancelReceiveMbx(%i, %08x): cancelling %d threads", id, numWaitingThreadsAddr, count);

	bool wokeThreads;
	for (size_t i = 0; i < m->waitingThreads.size(); i++)
		__KernelUnlockMbxForThread(m, m->waitingThreads[i], error, SCE_KERNEL_ERROR_WAIT_CANCEL, wokeThreads);
	m->waitingThreads.clear();

	if (wokeThreads)
		hleReSchedule("mbx canceled");

	if (numWaitingThreadsAddr)
		Memory::Write_U32(count, numWaitingThreadsAddr);
	return 0;
}

int sceKernelReferMbxStatus(SceUID id, u32 infoAddr)
{
	u32 error;
	Mbx *m = kernelObjects.Get<Mbx>(id, error);
	if (!m)
	{
		ERROR_LOG(HLE, "sceKernelReferMbxStatus(%i, %08x): invalid mbx id", id, infoAddr);
		return error;
	}

	// TODO: Is there a correct error code?
	if (!Memory::IsValidAddress(infoAddr))
		return -1;

	SceKernelMbxInfo info;
	memcpy(&info, &m->nmb, sizeof(SceKernelMbxInfo));
	info.numMessage = m->messageQueue.size();
	info.numWaitThreads = m->waitingThreads.size();

	if (!m->messageQueue.empty())
	{
		info.topPacketAddr = m->messageQueue[0];

		// TODO: Do this when sending messages too?
		// Fill in the next ptrs in a loop (0 => 1, 1 => 0 for 2.)
		for (int dest = 0, src = 1, n = m->messageQueue.size(); dest < n; ++dest, ++src)
			Memory::Write_U32(m->messageQueue[src % n], m->messageQueue[dest]);
	}
	else
		info.topPacketAddr = 0;

	Memory::WriteStruct<SceKernelMbxInfo>(infoAddr, &info);

	return 0;
}

