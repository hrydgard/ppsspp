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
#include "sceKernelThread.h"
#include "ChunkFile.h"

#define SCE_KERNEL_MPA_THFIFO_S 0x0000
#define SCE_KERNEL_MPA_THPRI_S  0x0100
#define SCE_KERNEL_MPA_THFIFO_R 0x0000
#define SCE_KERNEL_MPA_THPRI_R  0x1000

#define SCE_KERNEL_MPW_FULL 0
#define SCE_KERNEL_MPW_ASAP 1

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

struct MsgPipeWaitingThread
{
	SceUID id;
	u32 bufAddr;
	u32 bufSize;
	u32 freeSize;
	int waitMode;
	u32 transferredBytesAddr;
};

struct MsgPipe : public KernelObject
{
	const char *GetName() {return nmp.name;}
	const char *GetTypeName() {return "MsgPipe";}
	static u32 GetMissingErrorCode() { return SCE_KERNEL_ERROR_UNKNOWN_MPPID; }
	static int GetStaticIDType() { return SCE_KERNEL_TMID_Mpipe; }
	int GetIDType() const { return SCE_KERNEL_TMID_Mpipe; }

	MsgPipe() : buffer(NULL) {}
	~MsgPipe()
	{
		if (buffer != NULL)
			delete [] buffer;
	}

	void AddWaitingThread(std::vector<MsgPipeWaitingThread> &list, SceUID id, u32 addr, u32 size, int waitMode, u32 transferredBytesAddr, bool usePrio)
	{
		MsgPipeWaitingThread thread = { id, addr, size, size, waitMode, transferredBytesAddr };
		if (usePrio)
		{
			for (std::vector<MsgPipeWaitingThread>::iterator it = list.begin(); it != list.end(); it++)
			{
				if (__KernelGetThreadPrio(id) >= __KernelGetThreadPrio((*it).id))
				{
					list.insert(it, thread);
					return;
				}
			}

			list.push_back(thread);
		}
		else
		{
			list.push_back(thread);
		}
	}

	void AddSendWaitingThread(SceUID id, u32 addr, u32 size, int waitMode, u32 transferredBytesAddr)
	{
		bool usePrio = ((nmp.attr & SCE_KERNEL_MPA_THPRI_S) != 0);
		AddWaitingThread(sendWaitingThreads, id, addr, size, waitMode, transferredBytesAddr, usePrio);
	}

	void AddReceiveWaitingThread(SceUID id, u32 addr, u32 size, int waitMode, u32 transferredBytesAddr)
	{
		bool usePrio = ((nmp.attr & SCE_KERNEL_MPA_THPRI_R) != 0);
		AddWaitingThread(receiveWaitingThreads, id, addr, size, waitMode, transferredBytesAddr, usePrio);
	}

	void CheckSendThreads()
	{
		if (sendWaitingThreads.empty())
			return;
		MsgPipeWaitingThread *thread = &sendWaitingThreads.front();
		if ((u32) nmp.freeSize >= thread->bufSize)
		{
			// Put all the data to the buffer
			memcpy(buffer + (nmp.bufSize - nmp.freeSize), Memory::GetPointer(thread->bufAddr), thread->bufSize);
			Memory::Write_U32(thread->bufSize, thread->transferredBytesAddr);
			nmp.freeSize -= thread->bufSize;
			__KernelResumeThreadFromWait(thread->id);
			sendWaitingThreads.erase(sendWaitingThreads.begin());
			CheckReceiveThreads();
		}
		else if (thread->waitMode == SCE_KERNEL_MPW_ASAP && nmp.freeSize != 0)
		{
			// Put as much data as possible into the buffer
			memcpy(buffer + (nmp.bufSize - nmp.freeSize), Memory::GetPointer(thread->bufAddr), nmp.freeSize);
			Memory::Write_U32(nmp.freeSize, thread->transferredBytesAddr);
			nmp.freeSize = 0;
			__KernelResumeThreadFromWait(thread->id);
			receiveWaitingThreads.erase(receiveWaitingThreads.begin());
			CheckReceiveThreads();
		}
	}

	// This function should be only ran when the temporary buffer size is not 0 (otherwise, data is copied directly to the threads)
	void CheckReceiveThreads()
	{
		if (receiveWaitingThreads.empty())
			return;
		MsgPipeWaitingThread *thread = &receiveWaitingThreads.front();
		if ((u32) nmp.bufSize - (u32) nmp.freeSize >= thread->bufSize)
		{
			// Get the needed data from the buffer
			Memory::Memcpy(thread->bufAddr, buffer, thread->bufSize);
			// Put the unused data at the start of the buffer
			memmove(buffer, buffer + thread->bufSize, nmp.bufSize - nmp.freeSize);
			Memory::Write_U32(thread->bufSize, thread->transferredBytesAddr);
			nmp.freeSize += thread->bufSize;
			__KernelResumeThreadFromWait(thread->id);
			receiveWaitingThreads.erase(receiveWaitingThreads.begin());
			CheckSendThreads();
		}
		else if (thread->waitMode == SCE_KERNEL_MPW_ASAP && nmp.freeSize != nmp.bufSize)
		{
			// Get all the data from the buffer
			Memory::Memcpy(thread->bufAddr, buffer, nmp.bufSize - nmp.freeSize);
			Memory::Write_U32(nmp.bufSize - nmp.freeSize, thread->transferredBytesAddr);
			nmp.freeSize = nmp.bufSize;
			__KernelResumeThreadFromWait(thread->id);
			receiveWaitingThreads.erase(receiveWaitingThreads.begin());
			CheckSendThreads();
		}
	}

	virtual void DoState(PointerWrap &p)
	{
		p.Do(nmp);
		MsgPipeWaitingThread mpwt1 = {0}, mpwt2 = {0};
		p.Do(sendWaitingThreads, mpwt1);
		p.Do(receiveWaitingThreads, mpwt2);
		bool hasBuffer = buffer != NULL;
		p.Do(hasBuffer);
		if (hasBuffer)
		{
			if (buffer == NULL)
				buffer = new u8[nmp.bufSize];
			p.DoArray(buffer, nmp.bufSize);
		}
		p.DoMarker("MsgPipe");
	}

	NativeMsgPipe nmp;

	std::vector<MsgPipeWaitingThread> sendWaitingThreads;
	std::vector<MsgPipeWaitingThread> receiveWaitingThreads;

	u8 *buffer;
};

KernelObject *__KernelMsgPipeObject()
{
	return new MsgPipe;
}

void sceKernelCreateMsgPipe()
{
	const char *name = Memory::GetCharPointer(PARAM(0));
	int memoryPartition = PARAM(1);
	SceUInt attr = PARAM(2);
	int size = PARAM(3);
	int opt = PARAM(4);

	MsgPipe *m = new MsgPipe();
	SceUID id = kernelObjects.Create(m);

	m->nmp.size = sizeof(NativeMsgPipe);
	strncpy(m->nmp.name, name, sizeof(m->nmp.name));
	m->nmp.attr = attr;
	m->nmp.bufSize = size;
	m->nmp.freeSize = size;
	m->nmp.numSendWaitThreads = 0;
	m->nmp.numReceiveWaitThreads = 0;

	m->buffer = 0;
	if (size != 0)
	{
		m->buffer = new u8[size];
	}
	
	DEBUG_LOG(HLE, "%d=sceKernelCreateMsgPipe(%s, part=%d, attr=%08x, size=%d, opt=%08x)", id, name, memoryPartition, attr, size, opt);

	RETURN(id);
}

void sceKernelDeleteMsgPipe()
{
	SceUID uid = PARAM(0);
	u32 error;
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m)
	{
		ERROR_LOG(HLE, "sceKernelDeleteMsgPipe(%i) - ERROR %08x", uid, error);
		RETURN(error);
		return;
	}
	for (u32 i = 0; i < m->sendWaitingThreads.size(); i++)
	{
		__KernelResumeThreadFromWait(m->sendWaitingThreads[i].id);
	}
	for (u32 i = 0; i < m->receiveWaitingThreads.size(); i++)
	{
		__KernelResumeThreadFromWait(m->receiveWaitingThreads[i].id);
	}
	DEBUG_LOG(HLE, "sceKernelDeleteMsgPipe(%i)", uid);
	RETURN(kernelObjects.Destroy<MsgPipe>(uid));
}

void __KernelSendMsgPipe(MsgPipe *m, u32 sendBufAddr, u32 sendSize, int waitMode, u32 resultAddr, u32 timeoutPtr, bool cbEnabled, bool pool)
{
	u32 curSendAddr = sendBufAddr;
	if (m->nmp.bufSize == 0)
	{
		while (!m->receiveWaitingThreads.empty())
		{
			MsgPipeWaitingThread *thread = &m->receiveWaitingThreads.front();
			if (thread->freeSize > sendSize)
			{
				Memory::Memcpy(thread->bufAddr + (thread->bufSize - thread->freeSize), Memory::GetPointer(curSendAddr), sendSize);
				thread->freeSize -= sendSize;
				curSendAddr += sendSize;
				sendSize = 0;
				if (thread->waitMode == SCE_KERNEL_MPW_ASAP)
				{
					Memory::Write_U32(thread->bufSize - thread->freeSize, thread->transferredBytesAddr);
					__KernelResumeThreadFromWait(thread->id);
					m->receiveWaitingThreads.erase(m->receiveWaitingThreads.begin());
				}
				break;
			}
			else if (thread->freeSize == sendSize)
			{
				Memory::Memcpy(thread->bufAddr + (thread->bufSize - thread->freeSize), Memory::GetPointer(curSendAddr), sendSize);
				Memory::Write_U32(thread->bufSize, thread->transferredBytesAddr);
				__KernelResumeThreadFromWait(thread->id);
				m->receiveWaitingThreads.erase(m->receiveWaitingThreads.begin());
				curSendAddr += sendSize;
				sendSize = 0;
				break;
			}
			else
			{
				Memory::Memcpy(thread->bufAddr + (thread->bufSize - thread->freeSize), Memory::GetPointer(curSendAddr), thread->freeSize);
				sendSize -= thread->freeSize;
				curSendAddr += thread->freeSize;
				Memory::Write_U32(thread->bufSize, thread->transferredBytesAddr);
				__KernelResumeThreadFromWait(thread->id);
				m->receiveWaitingThreads.erase(m->receiveWaitingThreads.begin());
			}
		}
		// If there is still data to send and (we want to send all of it or we didn't send anything)
		if (sendSize != 0 && (waitMode != SCE_KERNEL_MPW_ASAP || curSendAddr == sendBufAddr))
		{
			if (pool)
			{
				RETURN(SCE_KERNEL_ERROR_MPP_FULL);
				return;
			}
			else
			{
				m->AddSendWaitingThread(__KernelGetCurThread(), curSendAddr, sendSize, waitMode, resultAddr);
				RETURN(0);
				__KernelWaitCurThread(WAITTYPE_MSGPIPE, 0, 0, 0, cbEnabled, "msgpipe waited");
				return;
			}
		}
	}
	else
	{
		if (sendSize <= (u32) m->nmp.freeSize)
		{
			memcpy(m->buffer + (m->nmp.bufSize - m->nmp.freeSize), Memory::GetPointer(sendBufAddr), sendSize);
			m->nmp.freeSize -= sendSize;
			curSendAddr = sendBufAddr + sendSize;
			sendSize = 0;
		}
		else if (waitMode == SCE_KERNEL_MPW_ASAP && m->nmp.freeSize != 0)
		{
			memcpy(m->buffer + (m->nmp.bufSize - m->nmp.freeSize), Memory::GetPointer(sendBufAddr), m->nmp.freeSize);
			curSendAddr = sendBufAddr + m->nmp.freeSize;
			sendSize -= m->nmp.freeSize;
			m->nmp.freeSize = 0;
		}
		else
		{
			if (pool)
			{
				RETURN(SCE_KERNEL_ERROR_MPP_FULL);
				return;
			}
			else
			{
				m->AddSendWaitingThread(__KernelGetCurThread(), curSendAddr, sendSize, waitMode, resultAddr);
				RETURN(0);
				__KernelWaitCurThread(WAITTYPE_MSGPIPE, 0, 0, 0, cbEnabled, "msgpipe waited");
				return;
			}
		}

		if (curSendAddr != sendBufAddr)
		{
			m->CheckReceiveThreads();
		}
	}
	Memory::Write_U32(curSendAddr - sendBufAddr, resultAddr);

	RETURN(0);
}

void sceKernelSendMsgPipe()
{
	SceUInt uid = PARAM(0);
	u32 sendBufAddr = PARAM(1);
	u32 sendSize = PARAM(2);
	int waitMode = PARAM(3);
	u32 resultAddr = PARAM(4);
	u32 timeoutPtr = PARAM(5);

	u32 error;
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m) {
		ERROR_LOG(HLE, "sceKernelSendMsgPipe(%i) - ERROR %08x", uid, error);
		RETURN(error);
		return;
	}

	DEBUG_LOG(HLE, "sceKernelSendMsgPipe(id=%i, addr=%08x, size=%i, mode=%i, result=%08x, timeout=%08x)", uid, sendBufAddr, sendSize, waitMode, resultAddr, timeoutPtr);
	__KernelSendMsgPipe(m, sendBufAddr, sendSize, waitMode, resultAddr, timeoutPtr, false, false);
}

void sceKernelSendMsgPipeCB()
{
	SceUInt uid = PARAM(0);
	u32 sendBufAddr = PARAM(1);
	u32 sendSize = PARAM(2);
	int waitMode = PARAM(3);
	u32 resultAddr = PARAM(4);
	u32 timeoutPtr = PARAM(5);

	u32 error;
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m) {
		ERROR_LOG(HLE, "sceKernelSendMsgPipeCB(%i) - ERROR %08x", uid, error);
		RETURN(error);
		return;
	}

	DEBUG_LOG(HLE, "sceKernelSendMsgPipeCB(id=%i, addr=%08x, size=%i, mode=%i, result=%08x, timeout=%08x)", uid, sendBufAddr, sendSize, waitMode, resultAddr, timeoutPtr);
	__KernelSendMsgPipe(m, sendBufAddr, sendSize, waitMode, resultAddr, timeoutPtr, true, false);
	__KernelCheckCallbacks();
}

void sceKernelTrySendMsgPipe()
{
	SceUInt uid = PARAM(0);
	u32 sendBufAddr = PARAM(1);
	u32 sendSize = PARAM(2);
	int waitMode = PARAM(3);
	u32 resultAddr = PARAM(4);

	u32 error;
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m) {
		ERROR_LOG(HLE, "sceKernelTrySendMsgPipe(%i) - ERROR %08x", uid, error);
		RETURN(error);
		return;
	}

	DEBUG_LOG(HLE, "sceKernelTrySendMsgPipe(id=%i, addr=%08x, size=%i, mode=%i, result=%08x)", uid, sendBufAddr, sendSize, waitMode, resultAddr);
	__KernelSendMsgPipe(m, sendBufAddr, sendSize, waitMode, resultAddr, 0, false, true);
}

void __KernelReceiveMsgPipe(MsgPipe *m, u32 receiveBufAddr, u32 receiveSize, int waitMode, u32 resultAddr, u32 timeoutPtr, bool cbEnabled, bool pool)
{
	u32 curReceiveAddr = receiveBufAddr;
	// MsgPipe buffer size is 0, receiving directly from waiting send threads
	if (m->nmp.bufSize == 0)
	{
		// While they're still sending waiting threads (which can send data)
		while (!m->sendWaitingThreads.empty())
		{
			MsgPipeWaitingThread *thread = &m->sendWaitingThreads.front();
			// Sending thread has more data than we have to receive: retrieve just the amount of data we want
			if (thread->bufSize - thread->freeSize > receiveSize)
			{
				Memory::Memcpy(curReceiveAddr, Memory::GetPointer(thread->bufAddr), receiveSize);
				thread->freeSize += receiveSize;
				// Move still available data at the beginning of the sending thread buffer
				Memory::Memcpy(thread->bufAddr, Memory::GetPointer(thread->bufAddr + receiveSize), thread->bufSize - thread->freeSize);
				curReceiveAddr += receiveSize;
				receiveSize = 0;
				// The sending thread mode is ASAP: we have sent some data so restart it even though its buffer isn't empty
				if (thread->waitMode == SCE_KERNEL_MPW_ASAP)
				{
					Memory::Write_U32(thread->bufSize - thread->freeSize, thread->transferredBytesAddr);
					__KernelResumeThreadFromWait(thread->id);
					m->sendWaitingThreads.erase(m->sendWaitingThreads.begin());
				}
				break;
			}
			// Sending thread wants to send the same amount of data as we want to retrieve: get the data and resume thread
			else if (thread->bufSize - thread->freeSize == receiveSize)
			{
				Memory::Memcpy(curReceiveAddr, Memory::GetPointer(thread->bufAddr), receiveSize);
				Memory::Write_U32(thread->bufSize, thread->transferredBytesAddr);
				__KernelResumeThreadFromWait(thread->id);
				m->sendWaitingThreads.erase(m->sendWaitingThreads.begin());
				curReceiveAddr += receiveSize;
				receiveSize = 0;
				break;
			}
			// Not enough data in the sending thread: get the data available and restart the sending thread, then loop
			else
			{
				Memory::Memcpy(curReceiveAddr, Memory::GetPointer(thread->bufAddr), thread->bufSize - thread->freeSize);
				receiveSize -= thread->bufSize - thread->freeSize;
				curReceiveAddr += thread->bufSize - thread->freeSize;
				Memory::Write_U32(thread->bufSize, thread->transferredBytesAddr);
				__KernelResumeThreadFromWait(thread->id);
				m->sendWaitingThreads.erase(m->sendWaitingThreads.begin());
			}
		}
		// All data hasn't been received and (mode isn't ASAP or nothing was received)
		if (receiveSize != 0 && (waitMode != SCE_KERNEL_MPW_ASAP || curReceiveAddr == receiveBufAddr))
		{
			if (pool)
			{
				RETURN(SCE_KERNEL_ERROR_MPP_EMPTY);
				return;
			}
			else
			{
				m->AddReceiveWaitingThread(__KernelGetCurThread(), curReceiveAddr, receiveSize, waitMode, resultAddr);
				RETURN(0);
				__KernelWaitCurThread(WAITTYPE_MSGPIPE, 0, 0, 0, cbEnabled, "msgpipe waited");
				return;
			}
		}
	}
	// Getting data from the MsgPipe buffer
	else
	{
		// Enough data in the buffer: copy just the needed amount of data
		if (receiveSize <= (u32) m->nmp.bufSize - (u32) m->nmp.freeSize)
		{
			Memory::Memcpy(receiveBufAddr, m->buffer, receiveSize);
			m->nmp.freeSize += receiveSize;
			memmove(m->buffer, m->buffer + receiveSize, m->nmp.bufSize - m->nmp.freeSize);
			curReceiveAddr = receiveBufAddr + receiveSize;
			receiveSize = 0;
		}
		// Else, if mode is ASAP and there's at list 1 available byte of data: copy all the available data
		else if (waitMode == SCE_KERNEL_MPW_ASAP && m->nmp.freeSize != m->nmp.bufSize)
		{
			Memory::Memcpy(receiveBufAddr, m->buffer, m->nmp.bufSize - m->nmp.freeSize);
			receiveSize -= m->nmp.bufSize - m->nmp.freeSize;
			curReceiveAddr = receiveBufAddr + m->nmp.bufSize - m->nmp.freeSize;
			m->nmp.freeSize = m->nmp.bufSize;
		}
		else
		{
			if (pool)
			{
				RETURN(SCE_KERNEL_ERROR_MPP_EMPTY);
				return;
			}
			else
			{
				m->AddReceiveWaitingThread(__KernelGetCurThread(), curReceiveAddr, receiveSize, waitMode, resultAddr);
				RETURN(0);
				__KernelWaitCurThread(WAITTYPE_MSGPIPE, 0, 0, 0, cbEnabled, "msgpipe waited");
				return;
			}
		}

		if (curReceiveAddr != receiveBufAddr)
		{
			m->CheckSendThreads();
		}
	}
	Memory::Write_U32(curReceiveAddr - receiveBufAddr, resultAddr);

	RETURN(0);
}

void sceKernelReceiveMsgPipe()
{
	SceUID uid = PARAM(0);
	u32 receiveBufAddr = PARAM(1);
	u32 receiveSize = PARAM(2);
	int waitMode = PARAM(3);
	u32 resultAddr = PARAM(4);
	u32 timeoutPtr = PARAM(5);

	u32 error;
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m) {
		ERROR_LOG(HLE, "sceKernelReceiveMsgPipe(%i) - ERROR %08x", uid, error);
		RETURN(error);
		return;
	}

	DEBUG_LOG(HLE, "sceKernelReceiveMsgPipe(%i, %08x, %i, %i, %08x, %08x)", uid, receiveBufAddr, receiveSize, waitMode, resultAddr, timeoutPtr);
	__KernelReceiveMsgPipe(m, receiveBufAddr, receiveSize, waitMode, resultAddr, timeoutPtr, false, false);
}

void sceKernelReceiveMsgPipeCB()
{
	SceUID uid = PARAM(0);
	u32 receiveBufAddr = PARAM(1);
	u32 receiveSize = PARAM(2);
	int waitMode = PARAM(3);
	u32 resultAddr = PARAM(4);
	u32 timeoutPtr = PARAM(5);

	u32 error;
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m) {
		ERROR_LOG(HLE, "sceKernelReceiveMsgPipeCB(%i) - ERROR %08x", uid, error);
		RETURN(error);
		return;
	}

	DEBUG_LOG(HLE, "sceKernelReceiveMsgPipeCB(%i, %08x, %i, %i, %08x, %08x)", uid, receiveBufAddr, receiveSize, waitMode, resultAddr, timeoutPtr);
	__KernelReceiveMsgPipe(m, receiveBufAddr, receiveSize, waitMode, resultAddr, timeoutPtr, true, false);
}

void sceKernelTryReceiveMsgPipe()
{
	SceUID uid = PARAM(0);
	u32 receiveBufAddr = PARAM(1);
	u32 receiveSize = PARAM(2);
	int waitMode = PARAM(3);
	u32 resultAddr = PARAM(4);

	u32 error;
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m) {
		ERROR_LOG(HLE, "sceKernelTryReceiveMsgPipe(%i) - ERROR %08x", uid, error);
		RETURN(error);
		return;
	}

	DEBUG_LOG(HLE, "sceKernelTryReceiveMsgPipe(%i, %08x, %i, %i, %08x)", uid, receiveBufAddr, receiveSize, waitMode, resultAddr);
	__KernelReceiveMsgPipe(m, receiveBufAddr, receiveSize, waitMode, resultAddr, 0, false, true);
}

void sceKernelCancelMsgPipe()
{
	SceUID uid = PARAM(0);
	u32 numSendThreadsAddr = PARAM(1);
	u32 numReceiveThreadsAddr = PARAM(2);

	u32 error;
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (!m)
	{
		ERROR_LOG(HLE, "sceKernelCancelMsgPipe(%i) - ERROR %08x", uid, error);
		RETURN(error);
		return;
	}
	if (m->buffer != 0)
	{
		delete [] m->buffer;
	}
	u32 count;
	for (count = 0; count < m->sendWaitingThreads.size(); count++)
	{
		__KernelResumeThreadFromWait(m->sendWaitingThreads[count].id);
	}
	Memory::Write_U32(count, numSendThreadsAddr);
	for (count = 0; count < m->receiveWaitingThreads.size(); count++)
	{
		__KernelResumeThreadFromWait(m->receiveWaitingThreads[count].id);
	}
	Memory::Write_U32(count, numReceiveThreadsAddr);
	DEBUG_LOG(HLE, "sceKernelCancelMsgPipe(%i, %i, %i)", uid, numSendThreadsAddr, numReceiveThreadsAddr);
	RETURN(0);
}

void sceKernelReferMsgPipeStatus()
{
	SceUID uid = PARAM(0);
	u32 msgPipeStatusAddr = PARAM(1);

	DEBUG_LOG(HLE,"sceKernelReferMsgPipeStatus(%i, %08x)", uid, msgPipeStatusAddr);
	u32 error;
	MsgPipe *m = kernelObjects.Get<MsgPipe>(uid, error);
	if (m)
	{
		m->nmp.numSendWaitThreads = (int) m->sendWaitingThreads.size();
		m->nmp.numReceiveWaitThreads = (int) m->receiveWaitingThreads.size();
		Memory::WriteStruct(msgPipeStatusAddr, &m->nmp);
		RETURN(0);
	}
	else
	{
		RETURN(error);
	}
}
