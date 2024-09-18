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

#include <algorithm>

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Common/Serialize/SerializeMap.h"
#include "Core/Reporting.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceKernel.h"
#include "Core/HLE/sceKernelMsgPipe.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/KernelWaitHelpers.h"

#define SCE_KERNEL_MPA_THFIFO_S 0x0000
#define SCE_KERNEL_MPA_THPRI_S  0x0100
#define SCE_KERNEL_MPA_THFIFO_R 0x0000
#define SCE_KERNEL_MPA_THPRI_R  0x1000
#define SCE_KERNEL_MPA_HIGHMEM  0x4000
#define SCE_KERNEL_MPA_KNOWN    (SCE_KERNEL_MPA_THPRI_S | SCE_KERNEL_MPA_THPRI_R | SCE_KERNEL_MPA_HIGHMEM)

#define SCE_KERNEL_MPW_FULL 0
#define SCE_KERNEL_MPW_ASAP 1

static const u32 MSGPIPE_WAIT_VALUE_SEND = 0;
static const u32 MSGPIPE_WAIT_VALUE_RECV = 1;

// State: the timer for MsgPipe timeouts.
static int waitTimer = -1;

struct NativeMsgPipe
{
	SceSize_le size;
	char name[32];
	SceUInt_le attr;
	s32_le bufSize;
	s32_le freeSize;
	s32_le numSendWaitThreads;
	s32_le numReceiveWaitThreads;
};

struct MsgPipeWaitingThread
{
	SceUID threadID;
	u32 bufAddr;
	u32 bufSize;
	// Free space at the end for receive, valid/free to read bytes from end for send.
	u32 freeSize;
	s32 waitMode;
	PSPPointer<u32_le> transferredBytes;
	u64 pausedTimeout;

	bool IsStillWaiting(SceUID waitID) const
	{
		return HLEKernel::VerifyWait(threadID, WAITTYPE_MSGPIPE, waitID);
	}

	void WriteCurrentTimeout(SceUID waitID) const
	{
		u32 error;
		if (IsStillWaiting(waitID))
		{
			u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
			if (timeoutPtr != 0 && waitTimer != -1)
			{
				// Remove any event for this thread.
				s64 cyclesLeft = CoreTiming::UnscheduleEvent(waitTimer, threadID);
				Memory::Write_U32((u32) cyclesToUs(cyclesLeft), timeoutPtr);
			}
		}
	}

	void Complete(SceUID waitID, int result) const
	{
		if (IsStillWaiting(waitID))
		{
			WriteCurrentTimeout(waitID);
			__KernelResumeThreadFromWait(threadID, result);
		}
	}

	void Cancel(SceUID waitID, int result) const
	{
		Complete(waitID, result);
	}

	void ReadBuffer(u32 destPtr, u32 len)
	{
		Memory::Memcpy(destPtr, bufAddr + bufSize - freeSize, len, "MsgPipeReadBuffer");
		freeSize -= len;
		if (transferredBytes.IsValid())
			*transferredBytes += len;
	}

	void WriteBuffer(u32 srcPtr, u32 len)
	{
		Memory::Memcpy(bufAddr + (bufSize - freeSize), srcPtr, len, "MsgPipeWriteBuffer");
		freeSize -= len;
		if (transferredBytes.IsValid())
			*transferredBytes += len;
	}

	bool operator ==(const SceUID &otherThreadID) const
	{
		return threadID == otherThreadID;
	}
};

static bool __KernelMsgPipeThreadSortPriority(const MsgPipeWaitingThread &thread1, const MsgPipeWaitingThread &thread2)
{
	return __KernelThreadSortPriority(thread1.threadID, thread2.threadID);
}

struct MsgPipe : public KernelObject
{
	const char *GetName() override { return nmp.name; }
	const char *GetTypeName() override { return GetStaticTypeName(); }
	static const char *GetStaticTypeName() { return "MsgPipe"; }
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_MPPID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Mpipe; }
	int GetIDType() const override { return SCE_KERNEL_TMID_Mpipe; }

	MsgPipe() : buffer(0) {}
	~MsgPipe() {
		if (buffer != 0) {
			BlockAllocator *alloc = BlockAllocatorFromAddr(buffer);
			_assert_msg_(alloc != nullptr, "Should always have a valid allocator/address");
			if (alloc)
				alloc->Free(buffer);
		}
	}

	u32 GetUsedSize()
	{
		return (u32)(nmp.bufSize - nmp.freeSize);
	}

	void AddWaitingThread(std::vector<MsgPipeWaitingThread> &list, SceUID id, u32 addr, u32 size, int waitMode, u32 transferredBytesAddr)
	{
		MsgPipeWaitingThread thread = { id, addr, size, size, waitMode, { transferredBytesAddr } };
		// Start out with 0 transferred bytes while waiting.
		// TODO: for receive, it might be a different (partial) number.
		if (thread.transferredBytes.IsValid())
			*thread.transferredBytes = 0;

		list.push_back(thread);
	}

	void AddSendWaitingThread(SceUID id, u32 addr, u32 size, int waitMode, u32 transferredBytesAddr)
	{
		AddWaitingThread(sendWaitingThreads, id, addr, size, waitMode, transferredBytesAddr);
	}

	void AddReceiveWaitingThread(SceUID id, u32 addr, u32 size, int waitMode, u32 transferredBytesAddr)
	{
		AddWaitingThread(receiveWaitingThreads, id, addr, size, waitMode, transferredBytesAddr);
	}

	bool CheckSendThreads()
	{
		SortSendThreads();

		bool wokeThreads = false;
		bool filledSpace = false;
		while (!sendWaitingThreads.empty() && nmp.freeSize > 0)
		{
			MsgPipeWaitingThread *thread = &sendWaitingThreads.front();
			u32 bytesToSend = std::min(thread->freeSize, (u32) nmp.freeSize);

			thread->ReadBuffer(buffer + GetUsedSize(), bytesToSend);
			nmp.freeSize -= bytesToSend;
			filledSpace = true;

			if (thread->waitMode == SCE_KERNEL_MPW_ASAP || thread->freeSize == 0)
			{
				thread->Complete(GetUID(), 0);
				sendWaitingThreads.erase(sendWaitingThreads.begin());
				wokeThreads = true;
				thread = NULL;
			}
			// Unlike receives, we don't do partial sends.  Stop at first blocked thread.
			else
				break;
		}

		if (filledSpace)
			wokeThreads |= CheckReceiveThreads();

		return wokeThreads;
	}

	// This function should be only ran when the temporary buffer size is not 0 (otherwise, data is copied directly to the threads)
	bool CheckReceiveThreads()
	{
		SortReceiveThreads();

		bool wokeThreads = false;
		bool freedSpace = false;
		while (!receiveWaitingThreads.empty() && GetUsedSize() > 0)
		{
			MsgPipeWaitingThread *thread = &receiveWaitingThreads.front();
			// Receive as much as possible, even if it's not enough to wake up.
			u32 bytesToSend = std::min(thread->freeSize, GetUsedSize());

			u8* ptr = Memory::GetPointerWrite(buffer);
			thread->WriteBuffer(buffer, bytesToSend);
			// Put the unused data at the start of the buffer.
			nmp.freeSize += bytesToSend;
			memmove(ptr, ptr + bytesToSend, GetUsedSize());
			freedSpace = true;

			if (thread->waitMode == SCE_KERNEL_MPW_ASAP || thread->freeSize == 0)
			{
				thread->Complete(GetUID(), 0);
				receiveWaitingThreads.erase(receiveWaitingThreads.begin());
				wokeThreads = true;
				thread = NULL;
			}
			// Stop at the first that can't wake up.
			else
				break;
		}

		if (freedSpace)
			wokeThreads |= CheckSendThreads();

		return wokeThreads;
	}

	void SortThreads(std::vector<MsgPipeWaitingThread> &waitingThreads, bool usePrio)
	{
		// Clean up any not waiting at the same time.
		HLEKernel::CleanupWaitingThreads(WAITTYPE_MSGPIPE, GetUID(), waitingThreads);

		if (usePrio)
			std::stable_sort(waitingThreads.begin(), waitingThreads.end(), __KernelMsgPipeThreadSortPriority);
	}

	void SortReceiveThreads()
	{
		bool usePrio = (nmp.attr & SCE_KERNEL_MPA_THPRI_R) != 0;
		SortThreads(receiveWaitingThreads, usePrio);
	}

	void SortSendThreads()
	{
		bool usePrio = (nmp.attr & SCE_KERNEL_MPA_THPRI_S) != 0;
		SortThreads(sendWaitingThreads, usePrio);
	}

	void RemoveReceiveWaitingThread(SceUID threadID)
	{
		HLEKernel::RemoveWaitingThread(receiveWaitingThreads, threadID);
	}

	void RemoveSendWaitingThread(SceUID threadID)
	{
		HLEKernel::RemoveWaitingThread(sendWaitingThreads, threadID);
	}

	void DoState(PointerWrap &p) override
	{
		auto s = p.Section("MsgPipe", 1);
		if (!s)
			return;

		Do(p, nmp);
		MsgPipeWaitingThread mpwt1 = {0}, mpwt2 = {0};
		Do(p, sendWaitingThreads, mpwt1);
		Do(p, receiveWaitingThreads, mpwt2);
		Do(p, pausedSendWaits);
		Do(p, pausedReceiveWaits);
		Do(p, buffer);
	}

	NativeMsgPipe nmp;

	std::vector<MsgPipeWaitingThread> sendWaitingThreads;
	std::vector<MsgPipeWaitingThread> receiveWaitingThreads;
	// Key is the callback id it was for, or if no callback, the thread id.
	std::map<SceUID, MsgPipeWaitingThread> pausedSendWaits;
	std::map<SceUID, MsgPipeWaitingThread> pausedReceiveWaits;

	u32 buffer;
};

KernelObject *__KernelMsgPipeObject()
{
	return new MsgPipe;
}

static void __KernelMsgPipeTimeout(u64 userdata, int cyclesLate)
{
	SceUID threadID = (SceUID) userdata;
	HLEKernel::WaitExecTimeout<MsgPipe, WAITTYPE_MSGPIPE>(threadID);
}

static bool __KernelSetMsgPipeTimeout(u32 timeoutPtr)
{
	if (timeoutPtr == 0 || waitTimer == -1)
		return true;

	int micro = (int) Memory::Read_U32(timeoutPtr);
	if (micro <= 2)
	{
		// Don't wait or reschedule, just timeout immediately.
		return false;
	}

	if (micro <= 210)
		micro = 250;
	CoreTiming::ScheduleEvent(usToCycles(micro), waitTimer, __KernelGetCurThread());
	return true;
}

static int __KernelSendMsgPipe(MsgPipe *m, u32 sendBufAddr, u32 sendSize, int waitMode, u32 resultAddr, u32 timeoutPtr, bool cbEnabled, bool poll, bool &needsResched, bool &needsWait)
{
	u32 curSendAddr = sendBufAddr;
	SceUID uid = m->GetUID();

	// If the buffer size is 0, nothing is buffered and all operations wait.
	if (m->nmp.bufSize == 0)
	{
		m->SortReceiveThreads();

		while (!m->receiveWaitingThreads.empty() && sendSize != 0)
		{
			MsgPipeWaitingThread *thread = &m->receiveWaitingThreads.front();

			u32 bytesToSend = std::min(thread->freeSize, sendSize);
			if (bytesToSend > 0)
			{
				thread->WriteBuffer(curSendAddr, bytesToSend);
				sendSize -= bytesToSend;
				curSendAddr += bytesToSend;

				if (thread->freeSize == 0 || thread->waitMode == SCE_KERNEL_MPW_ASAP)
				{
					thread->Complete(uid, 0);
					m->receiveWaitingThreads.erase(m->receiveWaitingThreads.begin());
					needsResched = true;
					thread = NULL;
				}
			}
		}

		// If there is still data to send and (we want to send all of it or we didn't send anything)
		if (sendSize != 0 && (waitMode != SCE_KERNEL_MPW_ASAP || curSendAddr == sendBufAddr))
		{
			if (poll)
			{
				// Generally, result is not updated in this case.  But for a 0 size buffer in ASAP mode, it is.
				if (Memory::IsValidAddress(resultAddr) && waitMode == SCE_KERNEL_MPW_ASAP)
					Memory::Write_U32(curSendAddr - sendBufAddr, resultAddr);
				return SCE_KERNEL_ERROR_MPP_FULL;
			}
			else
			{
				m->AddSendWaitingThread(__KernelGetCurThread(), curSendAddr, sendSize, waitMode, resultAddr);
				needsWait = true;
				return 0;
			}
		}
	}
	else
	{
		if (sendSize > (u32) m->nmp.bufSize)
		{
			ERROR_LOG(Log::sceKernel, "__KernelSendMsgPipe(%d): size %d too large for buffer", uid, sendSize);
			return SCE_KERNEL_ERROR_ILLEGAL_SIZE;
		}

		u32 bytesToSend = 0;
		// If others are already waiting, space or not, we have to get in line.
		m->SortSendThreads();
		if (m->sendWaitingThreads.empty())
		{
			if (sendSize <= (u32) m->nmp.freeSize)
				bytesToSend = sendSize;
			else if (waitMode == SCE_KERNEL_MPW_ASAP)
				bytesToSend = m->nmp.freeSize;
		}

		if (bytesToSend != 0)
		{
			Memory::Memcpy(m->buffer + (m->nmp.bufSize - m->nmp.freeSize), sendBufAddr, bytesToSend, "MsgPipeSend");
			m->nmp.freeSize -= bytesToSend;
			curSendAddr += bytesToSend;
			sendSize -= bytesToSend;

			if (m->CheckReceiveThreads())
				needsResched = true;
		}
		else if (sendSize != 0)
		{
			if (poll)
				return SCE_KERNEL_ERROR_MPP_FULL;
			else
			{
				m->AddSendWaitingThread(__KernelGetCurThread(), curSendAddr, sendSize, waitMode, resultAddr);
				needsWait = true;
				return 0;
			}
		}
	}

	// We didn't wait, so update the number of bytes transferred now.
	if (Memory::IsValidAddress(resultAddr))
		Memory::Write_U32(curSendAddr - sendBufAddr, resultAddr);

	return 0;
}

static int __KernelReceiveMsgPipe(MsgPipe *m, u32 receiveBufAddr, u32 receiveSize, int waitMode, u32 resultAddr, u32 timeoutPtr, bool cbEnabled, bool poll, bool &needsResched, bool &needsWait)
{
	u32 curReceiveAddr = receiveBufAddr;
	SceUID uid = m->GetUID();

	// MsgPipe buffer size is 0, receiving directly from waiting send threads
	if (m->nmp.bufSize == 0)
	{
		m->SortSendThreads();

		// While they're still sending waiting threads (which can send data)
		while (!m->sendWaitingThreads.empty() && receiveSize != 0)
		{
			MsgPipeWaitingThread *thread = &m->sendWaitingThreads.front();

			// For send threads, "freeSize" is "free to be read".
			u32 bytesToReceive = std::min(thread->freeSize, receiveSize);
			if (bytesToReceive > 0)
			{
				thread->ReadBuffer(curReceiveAddr, bytesToReceive);
				receiveSize -= bytesToReceive;
				curReceiveAddr += bytesToReceive;

				if (thread->freeSize == 0 || thread->waitMode == SCE_KERNEL_MPW_ASAP)
				{
					thread->Complete(uid, 0);
					m->sendWaitingThreads.erase(m->sendWaitingThreads.begin());
					needsResched = true;
					thread = NULL;
				}
			}
		}

		// All data hasn't been received and (mode isn't ASAP or nothing was received)
		if (receiveSize != 0 && (waitMode != SCE_KERNEL_MPW_ASAP || curReceiveAddr == receiveBufAddr))
		{
			if (poll)
			{
				// Generally, result is not updated in this case.  But for a 0 size buffer in ASAP mode, it is.
				if (Memory::IsValidAddress(resultAddr) && waitMode == SCE_KERNEL_MPW_ASAP)
					Memory::Write_U32(curReceiveAddr - receiveBufAddr, resultAddr);
				return SCE_KERNEL_ERROR_MPP_EMPTY;
			}
			else
			{
				m->AddReceiveWaitingThread(__KernelGetCurThread(), curReceiveAddr, receiveSize, waitMode, resultAddr);
				needsWait = true;
				return 0;
			}
		}
	}
	// Getting data from the MsgPipe buffer
	else
	{
		if (receiveSize > (u32) m->nmp.bufSize)
		{
			ERROR_LOG(Log::sceKernel, "__KernelReceiveMsgPipe(%d): size %d too large for buffer", uid, receiveSize);
			return SCE_KERNEL_ERROR_ILLEGAL_SIZE;
		}

		while (m->GetUsedSize() > 0)
		{
			u32 bytesToReceive = std::min(receiveSize, m->GetUsedSize());
			if (bytesToReceive != 0)
			{
				Memory::Memcpy(curReceiveAddr, m->buffer, bytesToReceive, "MsgPipeReceive");
				m->nmp.freeSize += bytesToReceive;
				memmove(Memory::GetPointerWrite(m->buffer), Memory::GetPointer(m->buffer) + bytesToReceive, m->GetUsedSize());
				curReceiveAddr += bytesToReceive;
				receiveSize -= bytesToReceive;

				m->CheckSendThreads();
			}
			else
				break;
		}

		if (receiveSize != 0 && (waitMode != SCE_KERNEL_MPW_ASAP || curReceiveAddr == receiveBufAddr))
		{
			if (poll)
				return SCE_KERNEL_ERROR_MPP_EMPTY;
			else
			{
				m->AddReceiveWaitingThread(__KernelGetCurThread(), curReceiveAddr, receiveSize, waitMode, resultAddr);
				needsWait = true;
				return 0;
			}
		}
	}

	if (Memory::IsValidAddress(resultAddr))
		Memory::Write_U32(curReceiveAddr - receiveBufAddr, resultAddr);

	return 0;
}

static void __KernelMsgPipeBeginCallback(SceUID threadID, SceUID prevCallbackId)
{
	u32 error;
	u32 waitValue = __KernelGetWaitValue(threadID, error);
	u32 timeoutPtr = __KernelGetWaitTimeoutPtr(threadID, error);
	SceUID uid = __KernelGetWaitID(threadID, WAITTYPE_MSGPIPE, error);
	MsgPipe *ko = uid == 0 ? NULL : kernelObjects.Get<MsgPipe>(uid, error);

	switch (waitValue)
	{
	case MSGPIPE_WAIT_VALUE_SEND:
		if (ko)
		{
			auto result = HLEKernel::WaitBeginCallback<MsgPipeWaitingThread>(threadID, prevCallbackId, waitTimer, ko->sendWaitingThreads, ko->pausedSendWaits, timeoutPtr != 0);
			if (result == HLEKernel::WAIT_CB_SUCCESS)
				DEBUG_LOG(Log::sceKernel, "sceKernelSendMsgPipeCB: Suspending wait for callback");
			else if (result == HLEKernel::WAIT_CB_BAD_WAIT_DATA)
				ERROR_LOG_REPORT(Log::sceKernel, "sceKernelSendMsgPipeCB: wait not found to pause for callback");
		}
		else
			WARN_LOG_REPORT(Log::sceKernel, "sceKernelSendMsgPipeCB: beginning callback with bad wait id?");
		break;

	case MSGPIPE_WAIT_VALUE_RECV:
		if (ko)
		{
			auto result = HLEKernel::WaitBeginCallback<MsgPipeWaitingThread>(threadID, prevCallbackId, waitTimer, ko->receiveWaitingThreads, ko->pausedReceiveWaits, timeoutPtr != 0);
			if (result == HLEKernel::WAIT_CB_SUCCESS)
				DEBUG_LOG(Log::sceKernel, "sceKernelReceiveMsgPipeCB: Suspending wait for callback");
			else if (result == HLEKernel::WAIT_CB_BAD_WAIT_DATA)
				ERROR_LOG_REPORT(Log::sceKernel, "sceKernelReceiveMsgPipeCB: wait not found to pause for callback");
		}
		else
			WARN_LOG_REPORT(Log::sceKernel, "sceKernelReceiveMsgPipeCB: beginning callback with bad wait id?");
		break;

	default:
		ERROR_LOG_REPORT(Log::sceKernel, "__KernelMsgPipeBeginCallback: Unexpected wait value");
	}
}

static bool __KernelCheckResumeMsgPipeSend(MsgPipe *m, MsgPipeWaitingThread &waitInfo, u32 &error, int result, bool &wokeThreads)
{
	if (!waitInfo.IsStillWaiting(m->GetUID()))
		return true;

	bool needsResched = false;
	bool needsWait = false;

	result = __KernelSendMsgPipe(m, waitInfo.bufAddr, waitInfo.bufSize, waitInfo.waitMode, waitInfo.transferredBytes.ptr, 0, true, false, needsResched, needsWait);

	if (needsResched)
		hleReSchedule(true, "msgpipe data sent");

	// Could not wake up.  May have sent some stuff.
	if (needsWait)
		return false;

	waitInfo.Complete(m->GetUID(), result);
	wokeThreads = true;
	return true;
}

static bool __KernelCheckResumeMsgPipeReceive(MsgPipe *m, MsgPipeWaitingThread &waitInfo, u32 &error, int result, bool &wokeThreads)
{
	if (!waitInfo.IsStillWaiting(m->GetUID()))
		return true;

	bool needsResched = false;
	bool needsWait = false;

	result = __KernelReceiveMsgPipe(m, waitInfo.bufAddr, waitInfo.bufSize, waitInfo.waitMode, waitInfo.transferredBytes.ptr, 0, true, false, needsResched, needsWait);

	if (needsResched)
		hleReSchedule(true, "msgpipe data received");

	if (needsWait)
		return false;

	waitInfo.Complete(m->GetUID(), result);
	wokeThreads = true;
	return true;
}

static void __KernelMsgPipeEndCallback(SceUID threadID, SceUID prevCallbackId) {
	u32 error;
	u32 waitValue = __KernelGetWaitValue(threadID, error);
	SceUID uid = __KernelGetWaitID(threadID, WAITTYPE_MSGPIPE, error);
	MsgPipe *ko = uid == 0 ? NULL : kernelObjects.Get<MsgPipe>(uid, error);

	if (ko == NULL) {
		ERROR_LOG_REPORT(Log::sceKernel, "__KernelMsgPipeEndCallback: Invalid object");
		return;
	}

	switch (waitValue) {
	case MSGPIPE_WAIT_VALUE_SEND:
		{
			MsgPipeWaitingThread dummy;
			auto result = HLEKernel::WaitEndCallback<MsgPipe, WAITTYPE_MSGPIPE, MsgPipeWaitingThread>(threadID, prevCallbackId, waitTimer, __KernelCheckResumeMsgPipeSend, dummy, ko->sendWaitingThreads, ko->pausedSendWaits);
			if (result == HLEKernel::WAIT_CB_RESUMED_WAIT) {
				DEBUG_LOG(Log::sceKernel, "sceKernelSendMsgPipeCB: Resuming wait from callback");
			} else if (result == HLEKernel::WAIT_CB_TIMED_OUT) {
				// It was re-added to the the waiting threads list, but it timed out.  Let's remove it.
				ko->RemoveSendWaitingThread(threadID);
			}
		}
		break;

	case MSGPIPE_WAIT_VALUE_RECV:
		{
			MsgPipeWaitingThread dummy;
			auto result = HLEKernel::WaitEndCallback<MsgPipe, WAITTYPE_MSGPIPE, MsgPipeWaitingThread>(threadID, prevCallbackId, waitTimer, __KernelCheckResumeMsgPipeReceive, dummy, ko->receiveWaitingThreads, ko->pausedReceiveWaits);
			if (result == HLEKernel::WAIT_CB_RESUMED_WAIT) {
				DEBUG_LOG(Log::sceKernel, "sceKernelReceiveMsgPipeCB: Resuming wait from callback");
			} else if (result == HLEKernel::WAIT_CB_TIMED_OUT) {
				// It was re-added to the the waiting threads list, but it timed out.  Let's remove it.
				ko->RemoveReceiveWaitingThread(threadID);
			}
		}
		break;

	default:
		ERROR_LOG_REPORT(Log::sceKernel, "__KernelMsgPipeEndCallback: Unexpected wait value");
	}
}

void __KernelMsgPipeInit()
{
	waitTimer = CoreTiming::RegisterEvent("MsgPipeTimeout", __KernelMsgPipeTimeout);

	__KernelRegisterWaitTypeFuncs(WAITTYPE_MSGPIPE, __KernelMsgPipeBeginCallback, __KernelMsgPipeEndCallback);
}

void __KernelMsgPipeDoState(PointerWrap &p)
{
	auto s = p.Section("sceKernelMsgPipe", 1);
	if (!s)
		return;

	Do(p, waitTimer);
	CoreTiming::RestoreRegisterEvent(waitTimer, "MsgPipeTimeout", __KernelMsgPipeTimeout);
}

int sceKernelCreateMsgPipe(const char *name, int partition, u32 attr, u32 size, u32 optionsPtr) {
	if (!name)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_NO_MEMORY, "invalid name");
	if (partition < 1 || partition > 9 || partition == 7)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ARGUMENT, "invalid partition %d", partition);

	BlockAllocator *allocator = BlockAllocatorFromID(partition);
	if (allocator == nullptr)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_PERM, "invalid partition %d", partition);

	if ((attr & ~SCE_KERNEL_MPA_KNOWN) >= 0x100)
		return hleLogWarning(Log::sceKernel, SCE_KERNEL_ERROR_ILLEGAL_ATTR, "invalid attr parameter: %08x", attr);

	u32 memBlockPtr = 0;
	if (size != 0) {
		// We ignore the upalign to 256.
		u32 allocSize = size;
		memBlockPtr = allocator->Alloc(allocSize, (attr & SCE_KERNEL_MPA_HIGHMEM) != 0, StringFromFormat("MsgPipe/%s", name).c_str());
		if (memBlockPtr == (u32)-1)
			return hleLogError(Log::sceKernel, SCE_KERNEL_ERROR_NO_MEMORY, "failed to allocate %i bytes for buffer", size);
	}

	MsgPipe *m = new MsgPipe();
	SceUID id = kernelObjects.Create(m);

	m->nmp.size = sizeof(NativeMsgPipe);
	strncpy(m->nmp.name, name, KERNELOBJECT_MAX_NAME_LENGTH);
	m->nmp.name[KERNELOBJECT_MAX_NAME_LENGTH] = 0;
	m->nmp.attr = attr;
	m->nmp.bufSize = size;
	m->nmp.freeSize = size;
	m->nmp.numSendWaitThreads = 0;
	m->nmp.numReceiveWaitThreads = 0;

	m->buffer = memBlockPtr;
	
	DEBUG_LOG(Log::sceKernel, "%d=sceKernelCreateMsgPipe(%s, part=%d, attr=%08x, size=%d, opt=%08x)", id, name, partition, attr, size, optionsPtr);

	if (optionsPtr != 0)
	{
		u32 optionsSize = Memory::Read_U32(optionsPtr);
		if (optionsSize > 4)
			WARN_LOG_REPORT(Log::sceKernel, "sceKernelCreateMsgPipe(%s) unsupported options parameter, size = %d", name, optionsSize);
	}

	return id;
}

int sceKernelDeleteMsgPipe(SceUID uid)
{
	hleEatCycles(900);

	u32 error;
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m)
	{
		ERROR_LOG(Log::sceKernel, "sceKernelDeleteMsgPipe(%i) - ERROR %08x", uid, error);
		return error;
	}

	hleEatCycles(3100);
	if (!m->sendWaitingThreads.empty() || !m->receiveWaitingThreads.empty())
		hleEatCycles(4000);

	for (size_t i = 0; i < m->sendWaitingThreads.size(); i++)
		m->sendWaitingThreads[i].Cancel(uid, SCE_KERNEL_ERROR_WAIT_DELETE);
	for (size_t i = 0; i < m->receiveWaitingThreads.size(); i++)
		m->receiveWaitingThreads[i].Cancel(uid, SCE_KERNEL_ERROR_WAIT_DELETE);

	DEBUG_LOG(Log::sceKernel, "sceKernelDeleteMsgPipe(%i)", uid);
	return kernelObjects.Destroy<MsgPipe>(uid);
}

static int __KernelValidateSendMsgPipe(SceUID uid, u32 sendBufAddr, u32 sendSize, int waitMode, u32 resultAddr, bool tryMode = false)
{
	if (sendSize & 0x80000000)
	{
		ERROR_LOG(Log::sceKernel, "__KernelSendMsgPipe(%d): illegal size %d", uid, sendSize);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}

	if (sendSize != 0 && !Memory::IsValidAddress(sendBufAddr))
	{
		ERROR_LOG(Log::sceKernel, "__KernelSendMsgPipe(%d): bad buffer address %08x (should crash?)", uid, sendBufAddr);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}

	if (waitMode != SCE_KERNEL_MPW_ASAP && waitMode != SCE_KERNEL_MPW_FULL)
	{
		ERROR_LOG(Log::sceKernel, "__KernelSendMsgPipe(%d): invalid wait mode %d", uid, waitMode);
		return SCE_KERNEL_ERROR_ILLEGAL_MODE;
	}

	if (!tryMode)
	{
		if (!__KernelIsDispatchEnabled())
		{
			WARN_LOG(Log::sceKernel, "__KernelSendMsgPipe(%d): dispatch disabled", uid);
			return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
		}
		if (__IsInInterrupt())
		{
			WARN_LOG(Log::sceKernel, "__KernelSendMsgPipe(%d): in interrupt", uid);
			return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
		}
	}

	return 0;
}

static int __KernelSendMsgPipe(MsgPipe *m, u32 sendBufAddr, u32 sendSize, int waitMode, u32 resultAddr, u32 timeoutPtr, bool cbEnabled, bool poll)
{
	hleEatCycles(2400);

	bool needsResched = false;
	bool needsWait = false;

	int result = __KernelSendMsgPipe(m, sendBufAddr, sendSize, waitMode, resultAddr, timeoutPtr, cbEnabled, poll, needsResched, needsWait);

	if (needsResched)
		hleReSchedule(cbEnabled, "msgpipe data sent");

	if (needsWait)
	{
		if (__KernelSetMsgPipeTimeout(timeoutPtr))
			__KernelWaitCurThread(WAITTYPE_MSGPIPE, m->GetUID(), MSGPIPE_WAIT_VALUE_SEND, timeoutPtr, cbEnabled, "msgpipe send waited");
		else
			result = SCE_KERNEL_ERROR_WAIT_TIMEOUT;
	}
	return result;
}

int sceKernelSendMsgPipe(SceUID uid, u32 sendBufAddr, u32 sendSize, u32 waitMode, u32 resultAddr, u32 timeoutPtr)
{
	u32 error = __KernelValidateSendMsgPipe(uid, sendBufAddr, sendSize, waitMode, resultAddr);
	if (error != 0) {
		return error;
	}
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m) {
		ERROR_LOG(Log::sceKernel, "sceKernelSendMsgPipe(%i) - ERROR %08x", uid, error);
		return error;
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelSendMsgPipe(id=%i, addr=%08x, size=%i, mode=%i, result=%08x, timeout=%08x)", uid, sendBufAddr, sendSize, waitMode, resultAddr, timeoutPtr);
	return __KernelSendMsgPipe(m, sendBufAddr, sendSize, waitMode, resultAddr, timeoutPtr, false, false);
}

int sceKernelSendMsgPipeCB(SceUID uid, u32 sendBufAddr, u32 sendSize, u32 waitMode, u32 resultAddr, u32 timeoutPtr)
{
	u32 error = __KernelValidateSendMsgPipe(uid, sendBufAddr, sendSize, waitMode, resultAddr);
	if (error != 0) {
		return error;
	}
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m) {
		ERROR_LOG(Log::sceKernel, "sceKernelSendMsgPipeCB(%i) - ERROR %08x", uid, error);
		return error;
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelSendMsgPipeCB(id=%i, addr=%08x, size=%i, mode=%i, result=%08x, timeout=%08x)", uid, sendBufAddr, sendSize, waitMode, resultAddr, timeoutPtr);
	// TODO: Verify callback behavior.
	hleCheckCurrentCallbacks();
	return __KernelSendMsgPipe(m, sendBufAddr, sendSize, waitMode, resultAddr, timeoutPtr, true, false);
}

int sceKernelTrySendMsgPipe(SceUID uid, u32 sendBufAddr, u32 sendSize, u32 waitMode, u32 resultAddr)
{
	u32 error = __KernelValidateSendMsgPipe(uid, sendBufAddr, sendSize, waitMode, resultAddr, true);
	if (error != 0) {
		return error;
	}
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m) {
		ERROR_LOG(Log::sceKernel, "sceKernelTrySendMsgPipe(%i) - ERROR %08x", uid, error);
		return error;
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelTrySendMsgPipe(id=%i, addr=%08x, size=%i, mode=%i, result=%08x)", uid, sendBufAddr, sendSize, waitMode, resultAddr);
	return __KernelSendMsgPipe(m, sendBufAddr, sendSize, waitMode, resultAddr, 0, false, true);
}

static int __KernelValidateReceiveMsgPipe(SceUID uid, u32 receiveBufAddr, u32 receiveSize, int waitMode, u32 resultAddr, bool tryMode = false)
{
	if (receiveSize & 0x80000000)
	{
		ERROR_LOG(Log::sceKernel, "__KernelReceiveMsgPipe(%d): illegal size %d", uid, receiveSize);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}

	if (receiveSize != 0 && !Memory::IsValidAddress(receiveBufAddr))
	{
		ERROR_LOG(Log::sceKernel, "__KernelReceiveMsgPipe(%d): bad buffer address %08x (should crash?)", uid, receiveBufAddr);
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;
	}

	if (waitMode != SCE_KERNEL_MPW_ASAP && waitMode != SCE_KERNEL_MPW_FULL)
	{
		ERROR_LOG(Log::sceKernel, "__KernelReceiveMsgPipe(%d): invalid wait mode %d", uid, waitMode);
		return SCE_KERNEL_ERROR_ILLEGAL_MODE;
	}

	if (!tryMode)
	{
		if (!__KernelIsDispatchEnabled())
		{
			WARN_LOG(Log::sceKernel, "__KernelReceiveMsgPipe(%d): dispatch disabled", uid);
			return SCE_KERNEL_ERROR_CAN_NOT_WAIT;
		}
		if (__IsInInterrupt())
		{
			WARN_LOG(Log::sceKernel, "__KernelReceiveMsgPipe(%d): in interrupt", uid);
			return SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
		}
	}

	return 0;
}

static int __KernelReceiveMsgPipe(MsgPipe *m, u32 receiveBufAddr, u32 receiveSize, int waitMode, u32 resultAddr, u32 timeoutPtr, bool cbEnabled, bool poll)
{
	bool needsResched = false;
	bool needsWait = false;

	int result = __KernelReceiveMsgPipe(m, receiveBufAddr, receiveSize, waitMode, resultAddr, timeoutPtr, cbEnabled, poll, needsResched, needsWait);

	if (needsResched)
		hleReSchedule(cbEnabled, "msgpipe data received");

	if (needsWait)
	{
		if (__KernelSetMsgPipeTimeout(timeoutPtr))
			__KernelWaitCurThread(WAITTYPE_MSGPIPE, m->GetUID(), MSGPIPE_WAIT_VALUE_RECV, timeoutPtr, cbEnabled, "msgpipe receive waited");
		else
			return SCE_KERNEL_ERROR_WAIT_TIMEOUT;
	}
	return result;
}

int sceKernelReceiveMsgPipe(SceUID uid, u32 receiveBufAddr, u32 receiveSize, u32 waitMode, u32 resultAddr, u32 timeoutPtr)
{
	u32 error = __KernelValidateReceiveMsgPipe(uid, receiveBufAddr, receiveSize, waitMode, resultAddr);
	if (error != 0) {
		return error;
	}
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m) {
		ERROR_LOG(Log::sceKernel, "sceKernelReceiveMsgPipe(%i) - ERROR %08x", uid, error);
		return error;
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelReceiveMsgPipe(%i, %08x, %i, %i, %08x, %08x)", uid, receiveBufAddr, receiveSize, waitMode, resultAddr, timeoutPtr);
	return __KernelReceiveMsgPipe(m, receiveBufAddr, receiveSize, waitMode, resultAddr, timeoutPtr, false, false);
}

int sceKernelReceiveMsgPipeCB(SceUID uid, u32 receiveBufAddr, u32 receiveSize, u32 waitMode, u32 resultAddr, u32 timeoutPtr)
{
	u32 error = __KernelValidateReceiveMsgPipe(uid, receiveBufAddr, receiveSize, waitMode, resultAddr);
	if (error != 0) {
		return error;
	}
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m) {
		ERROR_LOG(Log::sceKernel, "sceKernelReceiveMsgPipeCB(%i) - ERROR %08x", uid, error);
		return error;
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelReceiveMsgPipeCB(%i, %08x, %i, %i, %08x, %08x)", uid, receiveBufAddr, receiveSize, waitMode, resultAddr, timeoutPtr);
	// TODO: Verify callback behavior.
	hleCheckCurrentCallbacks();
	return __KernelReceiveMsgPipe(m, receiveBufAddr, receiveSize, waitMode, resultAddr, timeoutPtr, true, false);
}

int sceKernelTryReceiveMsgPipe(SceUID uid, u32 receiveBufAddr, u32 receiveSize, u32 waitMode, u32 resultAddr)
{
	u32 error = __KernelValidateReceiveMsgPipe(uid, receiveBufAddr, receiveSize, waitMode, resultAddr, true);
	if (error != 0) {
		return error;
	}
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m) {
		ERROR_LOG(Log::sceKernel, "sceKernelTryReceiveMsgPipe(%i) - ERROR %08x", uid, error);
		return error;
	}

	DEBUG_LOG(Log::sceKernel, "sceKernelTryReceiveMsgPipe(%i, %08x, %i, %i, %08x)", uid, receiveBufAddr, receiveSize, waitMode, resultAddr);
	return __KernelReceiveMsgPipe(m, receiveBufAddr, receiveSize, waitMode, resultAddr, 0, false, true);
}

int sceKernelCancelMsgPipe(SceUID uid, u32 numSendThreadsAddr, u32 numReceiveThreadsAddr)
{
	hleEatCycles(900);

	u32 error;
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m)
	{
		ERROR_LOG(Log::sceKernel, "sceKernelCancelMsgPipe(%i) - ERROR %08x", uid, error);
		return error;
	}

	hleEatCycles(1100);
	if (!m->sendWaitingThreads.empty() || !m->receiveWaitingThreads.empty())
		hleEatCycles(4000);

	if (Memory::IsValidAddress(numSendThreadsAddr))
		Memory::Write_U32((u32) m->sendWaitingThreads.size(), numSendThreadsAddr);
	if (Memory::IsValidAddress(numReceiveThreadsAddr))
		Memory::Write_U32((u32) m->receiveWaitingThreads.size(), numReceiveThreadsAddr);

	for (size_t i = 0; i < m->sendWaitingThreads.size(); i++)
		m->sendWaitingThreads[i].Cancel(uid, SCE_KERNEL_ERROR_WAIT_CANCEL);
	m->sendWaitingThreads.clear();
	for (size_t i = 0; i < m->receiveWaitingThreads.size(); i++)
		m->receiveWaitingThreads[i].Cancel(uid, SCE_KERNEL_ERROR_WAIT_CANCEL);
	m->receiveWaitingThreads.clear();

	// And now the entire buffer is free.
	m->nmp.freeSize = m->nmp.bufSize;

	DEBUG_LOG(Log::sceKernel, "sceKernelCancelMsgPipe(%i, %i, %i)", uid, numSendThreadsAddr, numReceiveThreadsAddr);
	return 0;
}

int sceKernelReferMsgPipeStatus(SceUID uid, u32 statusPtr) {
	u32 error;
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (m) {
		auto status = PSPPointer<NativeMsgPipe>::Create(statusPtr);
		if (!status.IsValid()) {
			return hleLogError(Log::sceKernel, -1, "invalid address");
		}

		// Clean up any that have timed out.
		m->SortReceiveThreads();
		m->SortSendThreads();

		m->nmp.numSendWaitThreads = (int) m->sendWaitingThreads.size();
		m->nmp.numReceiveWaitThreads = (int) m->receiveWaitingThreads.size();
		if (status->size != 0) {
			*status = m->nmp;
			status.NotifyWrite("MsgPipeStatus");
		}
		return hleLogSuccessI(Log::sceKernel, 0);
	} else {
		return hleLogError(Log::sceKernel, error, "bad message pipe");
	}
}
