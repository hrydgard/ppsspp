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

// TODO: when a thread is being resumed (message received or cancellation), sceKernelReceiveMbx() always returns 0

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

void __KernelMbxTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID)userdata;

	u32 error;
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	if (timeoutPtr != 0)
		Memory::Write_U32(0, timeoutPtr);

	__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
	// TODO: waitingThreads (but not here.)
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
		for (size_t i = 0; i < m->waitingThreads.size(); i++)
		{
			Memory::Write_U32(0, m->waitingThreads[i].second);
			__KernelResumeThreadFromWait(m->waitingThreads[i].first, SCE_KERNEL_ERROR_WAIT_DELETE);
		}

		if (!m->waitingThreads.empty())
			hleReSchedule("mbx deleted");
		m->waitingThreads.clear();
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
		Memory::Write_U32(packetAddr, m->waitingThreads.front().second);
		__KernelResumeThreadFromWait(m->waitingThreads.front().first);
		DEBUG_LOG(HLE, "sceKernelSendMbx(%i, %08x): threads waiting, resuming %d", id, packetAddr, m->waitingThreads.front().first);
		m->waitingThreads.erase(m->waitingThreads.begin());
		RETURN(0);
		__KernelReSchedule();
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
	__KernelCheckCallbacks();

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
		RETURN(0);
	}
	else
	{
		DEBUG_LOG(HLE, "sceKernelReceiveMbxCB(%i, %08x, %08x): no message in queue, waiting", id, packetAddrPtr, timeoutPtr);
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
	for (size_t i = 0; i < m->waitingThreads.size(); i++)
	{
		Memory::Write_U32(0, m->waitingThreads[i].second);
		__KernelResumeThreadFromWait(m->waitingThreads[i].first, SCE_KERNEL_ERROR_WAIT_CANCEL);
	}
	m->waitingThreads.clear();

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

	SceKernelMbxInfo *info = (SceKernelMbxInfo*)Memory::GetPointer(infoAddr);
	DEBUG_LOG(HLE, "sceKernelReferMbxStatus(%i, %08x)", id, infoAddr);
	if (info)
	{
		info->size = m->nmb.size;
		strncpy(info->name, m->nmb.name, 32);
		info->attr = m->nmb.attr;
		info->numWaitThreads = m->waitingThreads.size();
		info->numMessage = m->messageQueue.size();
		// Fill the 'next' parameter of packets which we don't use by default but could be used by a game
		if (m->messageQueue.size() != 0)
		{
			info->topPacketAddr = m->messageQueue[0];
			for (u32 i = 0; i < m->messageQueue.size() - 1; i++)
			{
				Memory::Write_U32(m->messageQueue[i + 1], Memory::Read_U32(m->messageQueue[i]));
			}
			Memory::Write_U32(m->messageQueue[m->messageQueue.size() - 1], 0);
		}
		else
		{
			info->topPacketAddr = 0;
		}
	}
	else
	{
		return -1;
	}

	return 0;
}

