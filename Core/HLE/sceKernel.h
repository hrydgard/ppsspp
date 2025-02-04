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
#include <string>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/Swap.h"

#include "Core/HLE/ErrorCodes.h"

class PointerWrap;

// If you add to this, make sure to check KernelObjectPool::CreateByIDType().
enum TMIDPurpose {
	SCE_KERNEL_TMID_Thread             = 1,
	SCE_KERNEL_TMID_Semaphore          = 2,
	SCE_KERNEL_TMID_EventFlag          = 3,
	SCE_KERNEL_TMID_Mbox               = 4,
	SCE_KERNEL_TMID_Vpl                = 5,
	SCE_KERNEL_TMID_Fpl                = 6,
	SCE_KERNEL_TMID_Mpipe              = 7,
	SCE_KERNEL_TMID_Callback           = 8,
	SCE_KERNEL_TMID_ThreadEventHandler = 9,
	SCE_KERNEL_TMID_Alarm              = 10,
	SCE_KERNEL_TMID_VTimer             = 11,
	SCE_KERNEL_TMID_Mutex              = 12,
	SCE_KERNEL_TMID_LwMutex            = 13,
	SCE_KERNEL_TMID_Tlspl              = 14,
	SCE_KERNEL_TMID_SleepThread        = 64,
	SCE_KERNEL_TMID_DelayThread        = 65,
	SCE_KERNEL_TMID_SuspendThread      = 66,
	SCE_KERNEL_TMID_DormantThread      = 67,
	// This is kept for old savestates.  Not the real value.
	SCE_KERNEL_TMID_Tlspl_v0           = 0x1001,

	// Not official, but need ids for save states.
	PPSSPP_KERNEL_TMID_Module          = 0x100001,
	PPSSPP_KERNEL_TMID_PMB             = 0x100002,
	PPSSPP_KERNEL_TMID_File            = 0x100003,
	PPSSPP_KERNEL_TMID_DirList         = 0x100004,
	PPSSPP_KERNEL_TMID_Heap            = 0x100005,
};

typedef int SceUID;
typedef unsigned int SceSize;
typedef int SceSSize;
typedef unsigned char SceUChar;
typedef unsigned int SceUInt;
typedef int SceMode;
typedef s64 SceOff;
typedef u64 SceIores;

typedef s32_le SceUID_le;
typedef u32_le SceSize_le;
typedef s32_le SceSSize_le;
typedef u32_le SceUInt_le;
typedef s32_le SceMode_le;
typedef s64_le SceOff_le;
typedef s64_le SceIores_le;

struct SceKernelLoadExecParam
{
	SceSize_le size;    // Size of the structure
	SceSize_le args;    // Size of the arg string
	u32_le argp;      // Pointer to the arg string
	u32_le keyp; // Encryption key? Not yet used
};

void __KernelInit();
void __KernelShutdown();
void __KernelDoState(PointerWrap &p);
bool __KernelIsRunning();
bool __KernelLoadExec(const char *filename, SceKernelLoadExecParam *param);

// For crash reporting.
std::string __KernelStateSummary();

int sceKernelLoadExec(const char *filename, u32 paramPtr);

void sceKernelExitGame();
void sceKernelExitGameWithStatus();

u32 sceKernelDevkitVersion();

u32 sceKernelRegisterKprintfHandler();
int sceKernelRegisterDefaultExceptionHandler();

u32 sceKernelFindModuleByName(const char *name);

void sceKernelSetGPO(u32 ledAddr);
u32 sceKernelGetGPI();
int sceKernelDcacheInvalidateRange(u32 addr, int size);
int sceKernelDcacheWritebackAll();
int sceKernelDcacheWritebackRange(u32 addr, int size);
int sceKernelDcacheWritebackInvalidateRange(u32 addr, int size);
int sceKernelDcacheWritebackInvalidateAll();
int sceKernelGetThreadStackFreeSize(SceUID threadID);
u32 sceKernelIcacheInvalidateAll();
u32 sceKernelIcacheClearAll();
int sceKernelIcacheInvalidateRange(u32 addr, int size);

#define KERNELOBJECT_MAX_NAME_LENGTH 31

class KernelObjectPool;

class KernelObject {
	friend class KernelObjectPool;
	u32 uid;
public:
	virtual ~KernelObject() {}
	SceUID GetUID() const {return uid;}
	virtual const char *GetTypeName() {return "[BAD KERNEL OBJECT TYPE]";}
	virtual const char *GetName() {return "[UNKNOWN KERNEL OBJECT]";}
	virtual int GetIDType() const = 0;
	virtual void GetQuickInfo(char *ptr, int size);

	// Implement the following in all subclasses:
	// static u32 GetMissingErrorCode()
	// static int GetStaticIDType()

	virtual void DoState(PointerWrap &p) {
		_dbg_assert_msg_(false, "Unable to save state: bad kernel object.");
	}
};

// TODO: Delete the "occupied" array, rely on non-zero pool entries?
class KernelObjectPool {
public:
	KernelObjectPool();
	~KernelObjectPool() {}

	// Allocates a UID within the range and inserts the object into the map.
	SceUID Create(KernelObject *obj, int rangeBottom = initialNextID, int rangeTop = 0x7fffffff);

	void DoState(PointerWrap &p);
	static KernelObject *CreateByIDType(int type);

	template <class T>
	u32 Destroy(SceUID handle) {
		u32 error;
		if (Get<T>(handle, error)) {
			int index = handle - handleOffset;
			occupied[index] = false;
			delete pool[index];
			pool[index] = nullptr;
		}
		return error;
	};

	bool IsValid(SceUID handle) const {
		int index = handle - handleOffset;
		if (index < 0 || index >= maxCount)
			return false;
		else
			return occupied[index];
	}

	template <class T>
	T* Get(SceUID handle, u32 &outError) {
		if (handle < handleOffset || handle >= handleOffset+maxCount || !occupied[handle-handleOffset]) {
			// Tekken 6 spams 0x80020001 gets wrong with no ill effects, also on the real PSP
			if (handle != 0 && (u32)handle != 0x80020001) {
				WARN_LOG(Log::sceKernel, "Kernel: Bad %s handle %d (%08x)", T::GetStaticTypeName(), handle, handle);
			}
			outError = T::GetMissingErrorCode();
			return 0;
		} else {
			// Previously we had a dynamic_cast here, but since RTTI was disabled traditionally,
			// it just acted as a static cast and everything worked. This means that we will never
			// see the Wrong type object error below, but we'll just have to live with that danger.
			T* t = static_cast<T*>(pool[handle - handleOffset]);
			if (t == nullptr || t->GetIDType() != T::GetStaticIDType()) {
				WARN_LOG(Log::sceKernel, "Kernel: Wrong object type for %d (%08x), was %s, should have been %s", handle, handle, t ? t->GetTypeName() : "null", T::GetStaticTypeName());
				outError = T::GetMissingErrorCode();
				return 0;
			}
			outError = SCE_KERNEL_ERROR_OK;
			return t;
		}
	}

	// ONLY use this when you KNOW the handle is valid.
	template <class T>
	T *GetFast(SceUID handle) {
		const SceUID realHandle = handle - handleOffset;
		_dbg_assert_(realHandle >= 0 && realHandle < maxCount && occupied[realHandle]);
		return static_cast<T *>(pool[realHandle]);
	}

	template <class T, typename ArgT>
	void Iterate(bool func(T *, ArgT), ArgT arg) {
		int type = T::GetStaticIDType();
		for (int i = 0; i < maxCount; i++) {
			if (!occupied[i])
				continue;
			T *t = static_cast<T *>(pool[i]);
			if (t->GetIDType() == type) {
				if (!func(t, arg))
					break;
			}
		}
	}

	int ListIDType(int type, SceUID_le *uids, int count) const {
		int total = 0;
		for (int i = 0; i < maxCount; i++) {
			if (!occupied[i]) {
				continue;
			}
			if (pool[i]->GetIDType() == type) {
				if (total < count) {
					*uids++ = pool[i]->GetUID();
				}
				++total;
			}
		}
		return total;
	}

	bool GetIDType(SceUID handle, int *type) const {
		if (handle < handleOffset || handle >= handleOffset+maxCount || !occupied[handle-handleOffset]) {
			ERROR_LOG(Log::sceKernel, "Kernel: Bad object handle %i (%08x)", handle, handle);
			return false;
		}
		KernelObject *t = pool[handle - handleOffset];
		*type = t->GetIDType();
		return true;
	}

	void List();
	void Clear();
	int GetCount() const;

	enum {
		maxCount = 4096,
		handleOffset = 0x100,
		initialNextID = 0x10
	};
private:
	KernelObject *pool[maxCount];
	bool occupied[maxCount];
	int nextID;
};

extern KernelObjectPool kernelObjects;

typedef std::pair<int, int> KernelStatsSyscall;

struct KernelStats {
	void Reset() {
		ResetFrame();
	}
	void ResetFrame() {
		msInSyscalls = 0;
		slowestSyscallTime = 0;
		slowestSyscallName = 0;
		summedMsInSyscalls.clear();
		summedSlowestSyscallTime = 0;
		summedSlowestSyscallName = 0;
	}

	double msInSyscalls;
	double slowestSyscallTime;
	const char *slowestSyscallName;
	std::map<KernelStatsSyscall, double> summedMsInSyscalls;
	double summedSlowestSyscallTime;
	const char *summedSlowestSyscallName;
};

extern KernelStats kernelStats;
extern u32 registeredExitCbId;

extern u32 g_GPOBits;
extern u32 g_GPIBits;

void Register_ThreadManForUser();
void Register_ThreadManForKernel();
void Register_LoadExecForUser();
void Register_LoadExecForKernel();
void Register_UtilsForKernel();

// returns nullptr if not found.
const char *KernelErrorToString(u32 err);
