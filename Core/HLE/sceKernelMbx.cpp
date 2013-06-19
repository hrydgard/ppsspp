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
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"
#include "ChunkFile.h"

#define SCE_KERNEL_MBA_THPRI 0x100
#define SCE_KERNEL_MBA_MSPRI 0x400
#define SCE_KERNEL_MBA_ATTR_KNOWN (SCE_KERNEL_MBA_THPRI | SCE_KERNEL_MBA_MSPRI)

const int PSP_MBX_ERROR_DUPLICATE_MSG = 0x800201C9;

struct MbxWaitingThread
{
	SceUID first;
	u32 second;
};
void __KernelMbxTimeout(u64 userdata, int cyclesLate);

static int mbxWaitTimer = -1;

struct NativeMbx
{
	SceSize size;
	char name[KERNELOBJECT_MAX_NAME_LENGTH + 1];
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
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Mbox; }
	int GetIDType() const { return SCE_KERNEL_TMID_Mbox; }

	void AddWaitingThread(SceUID id, u32 addr)
	{
		bool inserted = false;
		if (nmb.attr & SCE_KERNEL_MBA_THPRI)
		{
			for (std::vector<MbxWaitingThread>::iterator it = waitingThreads.begin(); it != waitingThreads.end(); it++)
			{
				if (__KernelGetThreadPrio(id) < __KernelGetThreadPrio((*it).first))
				{
					MbxWaitingThread waiting = {id, addr};
					waitingThreads.insert(it, waiting);
					inserted = true;
					break;
				}
			}
		}
		if (!inserted)
		{
			MbxWaitingThread waiting = {id, addr};
			waitingThreads.push_back(waiting);
		}
	}

	inline void AddInitialMessage(u32 ptr)
	{
		nmb.numMessages++;
		Memory::Write_U32(ptr, ptr);
		nmb.packetListHead = ptr;
	}

	inline void AddFirstMessage(u32 endPtr, u32 ptr)
	{
		nmb.numMessages++;
		Memory::Write_U32(nmb.packetListHead, ptr);
		Memory::Write_U32(ptr, endPtr);
		nmb.packetListHead = ptr;
	}

	inline void AddLastMessage(u32 endPtr, u32 ptr)
	{
		nmb.numMessages++;
		Memory::Write_U32(ptr, endPtr);
		Memory::Write_U32(nmb.packetListHead, ptr);
	}

	inline void AddMessage(u32 beforePtr, u32 afterPtr, u32 ptr)
	{
		nmb.numMessages++;
		Memory::Write_U32(afterPtr, ptr);
		Memory::Write_U32(ptr, beforePtr);
	}

	int ReceiveMessage(u32 receivePtr)
	{
		u32 ptr = nmb.packetListHead;

		if (nmb.numMessages == 991)
		{
			u32 next = Memory::Read_U32(nmb.packetListHead);
			u32 next2 = Memory::Read_U32(next);
			if (next2 == ptr && next != ptr)
			{
				Memory::Write_U32(next, next);
				nmb.packetListHead = next;
			}
			else
				nmb.packetListHead = 0;
		}
		else
		{
			// Check over the linked list and reset the head.
			int c = 0;
			while (true)
			{
				u32 next = Memory::Read_U32(nmb.packetListHead);
				if (!Memory::IsValidAddress(next))
					return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
				if (next == ptr)
				{
					if (nmb.packetListHead != ptr)
					{
						next = Memory::Read_U32(next);
						Memory::Write_U32(next, nmb.packetListHead);
						nmb.packetListHead = next;
						break;
					}
					else
					{
						if (c < nmb.numMessages - 1)
							return PSP_MBX_ERROR_DUPLICATE_MSG;

						nmb.packetListHead = 0;
						break;
					}
				}

				nmb.packetListHead = next;
				c++;
			}
		}

		// Tell the receiver about the message.
		Memory::Write_U32(ptr, receivePtr);
		nmb.numMessages--;

		return 0;
	}

	virtual void DoState(PointerWrap &p)
	{
		p.Do(nmb);
		MbxWaitingThread mwt = {0};
		p.Do(waitingThreads, mwt);
		p.DoMarker("Mbx");
	}

	NativeMbx nmb;

	std::vector<MbxWaitingThread> waitingThreads;
};

void __KernelMbxInit()
{
	mbxWaitTimer = CoreTiming::RegisterEvent("MbxTimeout", __KernelMbxTimeout);
}

void __KernelMbxDoState(PointerWrap &p)
{
	p.Do(mbxWaitTimer);
	CoreTiming::RestoreRegisterEvent(mbxWaitTimer, "MbxTimeout", __KernelMbxTimeout);
	p.DoMarker("sceKernelMbx");
}

KernelObject *__KernelMbxObject()
{
	return new Mbx;
}

bool __KernelUnlockMbxForThread(Mbx *m, MbxWaitingThread &th, u32 &error, int result, bool &wokeThreads)
{
	SceUID waitID = __KernelGetWaitID(th.first, WAITTYPE_MBX, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(th.first, error);

	// The waitID may be different after a timeout.
	if (waitID != m->GetUID())
		return true;

	if (timeoutPtr != 0 && mbxWaitTimer != -1)
	{
		// Remove any event for this thread.
		s64 cyclesLeft = CoreTiming::UnscheduleEvent(mbxWaitTimer, th.first);
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

	SceUID mbxID = __KernelGetWaitID(threadID, WAITTYPE_MBX, error);
	Mbx *m = kernelObjects.Get<Mbx>(mbxID, error);
	if (m)
	{
		// This thread isn't waiting anymore, but we'll remove it from waitingThreads later.
		// The reason is, if it times out, but what it was waiting on is DELETED prior to it
		// actually running, it will get a DELETE result instead of a TIMEOUT.
		// So, we need to remember it or we won't be able to mark it DELETE instead later.

		// TODO: Should numWaitThreads be decreased yet?
		__KernelResumeThreadFromWait(threadID, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
	}
}

void __KernelWaitMbx(Mbx *m, u32 timeoutPtr)
{
	if (timeoutPtr == 0 || mbxWaitTimer == -1)
		return;

	int micro = (int) Memory::Read_U32(timeoutPtr);

	// This seems to match the actual timing.
	if (micro <= 2)
		micro = 10;
	else if (micro <= 209)
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

std::vector<MbxWaitingThread>::iterator __KernelMbxFindPriority(std::vector<MbxWaitingThread> &waiting)
{
	_dbg_assert_msg_(HLE, !waiting.empty(), "__KernelMutexFindPriority: Trying to find best of no threads.");

	std::vector<MbxWaitingThread>::iterator iter, end, best = waiting.end();
	u32 best_prio = 0xFFFFFFFF;
	for (iter = waiting.begin(), end = waiting.end(); iter != end; ++iter)
	{
		u32 iter_prio = __KernelGetThreadPrio(iter->first);
		if (iter_prio < best_prio)
		{
			best = iter;
			best_prio = iter_prio;
		}
	}

	_dbg_assert_msg_(HLE, best != waiting.end(), "__KernelMutexFindPriority: Returning invalid best thread.");
	return best;
}

SceUID sceKernelCreateMbx(const char *name, u32 attr, u32 optAddr)
{
	if (!name)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateMbx(): invalid name", SCE_KERNEL_ERROR_ERROR);
		return SCE_KERNEL_ERROR_ERROR;
	}
	// Accepts 0x000 - 0x0FF, 0x100 - 0x1FF, and 0x400 - 0x4FF.
	if (((attr & ~SCE_KERNEL_MBA_ATTR_KNOWN) & ~0xFF) != 0)
	{
		WARN_LOG_REPORT(HLE, "%08x=sceKernelCreateMbx(): invalid attr parameter: %08x", SCE_KERNEL_ERROR_ILLEGAL_ATTR, attr);
		return SCE_KERNEL_ERROR_ILLEGAL_ATTR;
	}

	Mbx *m = new Mbx();
	SceUID id = kernelObjects.Create(m);

	m->nmb.size = sizeof(NativeMbx);
	strncpy(m->nmb.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	m->nmb.name[KERNELOBJECT_MAX_NAME_LENGTH] = 0;
	m->nmb.attr = attr;
	m->nmb.numWaitThreads = 0;
	m->nmb.numMessages = 0;
	m->nmb.packetListHead = 0;

	DEBUG_LOG(HLE, "%i=sceKernelCreateMbx(%s, %08x, %08x)", id, name, attr, optAddr);

	if (optAddr != 0)
		WARN_LOG_REPORT(HLE, "sceKernelCreateMbx(%s) unsupported options parameter: %08x", name, optAddr);
	if ((attr & ~SCE_KERNEL_MBA_ATTR_KNOWN) != 0)
		WARN_LOG_REPORT(HLE, "sceKernelCreateMbx(%s) unsupported attr parameter: %08x", name, attr);

	return id;
}

int sceKernelDeleteMbx(SceUID id)
{
	u32 error;
	Mbx *m = kernelObjects.Get<Mbx>(id, error);
	if (m)
	{
		DEBUG_LOG(HLE, "sceKernelDeleteMbx(%i)", id);

		bool wokeThreads = false;
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

int sceKernelSendMbx(SceUID id, u32 packetAddr)
{
	u32 error;
	Mbx *m = kernelObjects.Get<Mbx>(id, error);
	if (!m)
	{
		ERROR_LOG(HLE, "sceKernelSendMbx(%i, %08x): invalid mbx id", id, packetAddr);
		return error;
	}

	NativeMbxPacket *addPacket = (NativeMbxPacket*)Memory::GetPointer(packetAddr);
	if (addPacket == 0)
	{
		ERROR_LOG(HLE, "sceKernelSendMbx(%i, %08x): invalid packet address", id, packetAddr);
		return -1;
	}

	// If the queue is empty, maybe someone is waiting.
	// We have to check them first, they might've timed out.
	if (m->nmb.numMessages == 0)
	{
		bool wokeThreads = false;
		std::vector<MbxWaitingThread>::iterator iter;
		while (!wokeThreads && !m->waitingThreads.empty())
		{
			if ((m->nmb.attr & SCE_KERNEL_MBA_THPRI) != 0)
				iter = __KernelMbxFindPriority(m->waitingThreads);
			else
				iter = m->waitingThreads.begin();

			MbxWaitingThread t = *iter;
			__KernelUnlockMbxForThread(m, t, error, 0, wokeThreads);
			m->waitingThreads.erase(iter);

			if (wokeThreads)
			{
				DEBUG_LOG(HLE, "sceKernelSendMbx(%i, %08x): threads waiting, resuming %d", id, packetAddr, t.first);
				Memory::Write_U32(packetAddr, t.second);
				hleReSchedule("mbx sent");

				// We don't need to do anything else, finish here.
				return 0;
			}
		}
	}

	DEBUG_LOG(HLE, "sceKernelSendMbx(%i, %08x): no threads currently waiting, adding message to queue", id, packetAddr);

	if (m->nmb.numMessages == 0)
		m->AddInitialMessage(packetAddr);
	else
	{
		u32 next = m->nmb.packetListHead, prev;
		for (int i = 0, n = m->nmb.numMessages; i < n; i++)
		{
			if (next == packetAddr)
				return PSP_MBX_ERROR_DUPLICATE_MSG;
			if (!Memory::IsValidAddress(next))
				return SCE_KERNEL_ERROR_ILLEGAL_ADDR;

			prev = next;
			next = Memory::Read_U32(next);
		}

		bool inserted = false;
		if (m->nmb.attr & SCE_KERNEL_MBA_MSPRI)
		{
			NativeMbxPacket p;
			for (int i = 0, n = m->nmb.numMessages; i < n; i++)
			{
				Memory::ReadStruct<NativeMbxPacket>(next, &p);
				if (addPacket->priority < p.priority)
				{
					if (i == 0)
						m->AddFirstMessage(prev, packetAddr);
					else
						m->AddMessage(prev, next, packetAddr);
					inserted = true;
					break;
				}

				prev = next;
				next = Memory::Read_U32(next);
			}
		}
		if (!inserted)
			m->AddLastMessage(prev, packetAddr);
	}

	return 0;
}

int sceKernelReceiveMbx(SceUID id, u32 packetAddrPtr, u32 timeoutPtr)
{
	u32 error;
	Mbx *m = kernelObjects.Get<Mbx>(id, error);

	if (!m)
	{
		ERROR_LOG(HLE, "sceKernelReceiveMbx(%i, %08x, %08x): invalid mbx id", id, packetAddrPtr, timeoutPtr);
		return error;
	}

	if (m->nmb.numMessages > 0)
	{
		DEBUG_LOG(HLE, "sceKernelReceiveMbx(%i, %08x, %08x): sending first queue message", id, packetAddrPtr, timeoutPtr);
		return m->ReceiveMessage(packetAddrPtr);
	}
	else
	{
		DEBUG_LOG(HLE, "sceKernelReceiveMbx(%i, %08x, %08x): no message in queue, waiting", id, packetAddrPtr, timeoutPtr);
		__KernelMbxRemoveThread(m, __KernelGetCurThread());
		m->AddWaitingThread(__KernelGetCurThread(), packetAddrPtr);
		__KernelWaitMbx(m, timeoutPtr);
		__KernelWaitCurThread(WAITTYPE_MBX, id, 0, timeoutPtr, false, "mbx waited");
		return 0;
	}
}

int sceKernelReceiveMbxCB(SceUID id, u32 packetAddrPtr, u32 timeoutPtr)
{
	u32 error;
	Mbx *m = kernelObjects.Get<Mbx>(id, error);

	if (!m)
	{
		ERROR_LOG(HLE, "sceKernelReceiveMbxCB(%i, %08x, %08x): invalid mbx id", id, packetAddrPtr, timeoutPtr);
		return error;
	}

	if (m->nmb.numMessages > 0)
	{
		DEBUG_LOG(HLE, "sceKernelReceiveMbxCB(%i, %08x, %08x): sending first queue message", id, packetAddrPtr, timeoutPtr);
		hleCheckCurrentCallbacks();
		return m->ReceiveMessage(packetAddrPtr);
	}
	else
	{
		DEBUG_LOG(HLE, "sceKernelReceiveMbxCB(%i, %08x, %08x): no message in queue, waiting", id, packetAddrPtr, timeoutPtr);
		__KernelMbxRemoveThread(m, __KernelGetCurThread());
		m->AddWaitingThread(__KernelGetCurThread(), packetAddrPtr);
		__KernelWaitMbx(m, timeoutPtr);
		__KernelWaitCurThread(WAITTYPE_MBX, id, 0, timeoutPtr, true, "mbx waited");
		return 0;
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

	if (m->nmb.numMessages > 0)
	{
		DEBUG_LOG(HLE, "sceKernelPollMbx(%i, %08x): sending first queue message", id, packetAddrPtr);
		return m->ReceiveMessage(packetAddrPtr);
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

	u32 count = (u32) m->waitingThreads.size();
	DEBUG_LOG(HLE, "sceKernelCancelReceiveMbx(%i, %08x): cancelling %d threads", id, numWaitingThreadsAddr, count);

	bool wokeThreads = false;
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

	// Should we crash the thread somehow?
	if (!Memory::IsValidAddress(infoAddr))
		return -1;

	for (int i = 0, n = m->nmb.numMessages; i < n; ++i)
		m->nmb.packetListHead = Memory::Read_U32(m->nmb.packetListHead);

	// For whatever reason, it won't write if the size (first member) is 0.
	if (Memory::Read_U32(infoAddr) != 0)
	{
		m->nmb.numWaitThreads = (int) m->waitingThreads.size();
		Memory::WriteStruct<NativeMbx>(infoAddr, &m->nmb);
	}

	return 0;
}
