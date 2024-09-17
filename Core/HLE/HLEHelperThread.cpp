// Copyright (c) 2014- PPSSPP Project.

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

#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/CoreTiming.h"
#include "Core/MemMapHelpers.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLEHelperThread.h"
#include "Core/HLE/KernelWaitHelpers.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelMemory.h"
#include "Core/MIPS/MIPSCodeUtils.h"

HLEHelperThread::HLEHelperThread() : id_(0), entry_(0) {
}

HLEHelperThread::HLEHelperThread(const char *threadName, const u32 instructions[], u32 instrCount, u32 prio, int stacksize) {
	u32 instrBytes = instrCount * sizeof(u32);
	u32 totalBytes = instrBytes + sizeof(u32) * 2;
	AllocEntry(totalBytes);
	Memory::Memcpy(entry_, instructions, instrBytes, "HelperMIPS");

	// Just to simplify things, we add the return here.
	Memory::Write_U32(MIPS_MAKE_JR_RA(), entry_ + instrBytes + 0);
	Memory::Write_U32(MIPS_MAKE_NOP(), entry_ + instrBytes + 4);

	Create(threadName, prio, stacksize);
}

HLEHelperThread::HLEHelperThread(const char *threadName, const char *module, const char *func, u32 prio, int stacksize) {
	const u32 bytes = sizeof(u32) * 2;
	AllocEntry(bytes);
	Memory::Write_U32(MIPS_MAKE_JR_RA(), entry_ + 0);
	Memory::Write_U32(MIPS_MAKE_SYSCALL(module, func), entry_ + 4);

	Create(threadName, prio, stacksize);
}

HLEHelperThread::~HLEHelperThread() {
	if (id_ > 0)
		__KernelDeleteThread(id_, SCE_KERNEL_ERROR_THREAD_TERMINATED, "helper deleted");
	if (entry_)
		kernelMemory.Free(entry_);
}

void HLEHelperThread::AllocEntry(u32 size) {
	entry_ = kernelMemory.Alloc(size, false, "HLEHelper");
	Memory::Memset(entry_, 0, size, "HLEHelperClear");
	currentMIPS->InvalidateICache(entry_, size);
}

void HLEHelperThread::Create(const char *threadName, u32 prio, int stacksize) {
	id_ = __KernelCreateThreadInternal(threadName, __KernelGetCurThreadModuleId(), entry_, prio, stacksize, 0x00001000);
}

void HLEHelperThread::DoState(PointerWrap &p) {
	auto s = p.Section("HLEHelperThread", 1);
	if (!s) {
		return;
	}

	Do(p, id_);
	Do(p, entry_);
}

void HLEHelperThread::Start(u32 a0, u32 a1) {
	__KernelStartThread(id_, a0, a1, true);
}

void HLEHelperThread::Terminate() {
	__KernelStopThread(id_, SCE_KERNEL_ERROR_THREAD_TERMINATED, "helper terminated");
}

bool HLEHelperThread::Stopped() {
	return KernelIsThreadDormant(id_);
}

void HLEHelperThread::ChangePriority(u32 prio) {
	KernelChangeThreadPriority(id_, prio);
}

void HLEHelperThread::Resume(WaitType waitType, SceUID uid, int result) {
	bool res = HLEKernel::ResumeFromWait(id_, waitType, uid, result);
	if (!res) {
		ERROR_LOG(Log::HLE, "Failed to wake helper thread from wait");
	}
}

void HLEHelperThread::Forget() {
	id_ = 0;
	entry_ = 0;
}
