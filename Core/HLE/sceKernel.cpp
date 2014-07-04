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

#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/CwCheat.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSInt.h"
#include "Core/MIPS/JitCommon/JitCommon.h"

#include "Common/LogManager.h"
#include "Core/FileSystems/FileSystem.h"
#include "Core/FileSystems/MetaFileSystem.h"
#include "Core/PSPLoaders.h"
#include "Core/CoreTiming.h"
#include "Core/Reporting.h"
#include "Core/SaveState.h"
#include "Core/System.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

#include "__sceAudio.h"
#include "sceAtrac.h"
#include "sceAudio.h"
#include "sceAudiocodec.h"
#include "sceCcc.h"
#include "sceCtrl.h"
#include "sceDisplay.h"
#include "sceFont.h"
#include "sceGe.h"
#include "sceIo.h"
#include "sceJpeg.h"
#include "sceKernel.h"
#include "sceKernelAlarm.h"
#include "sceKernelInterrupt.h"
#include "sceKernelThread.h"
#include "sceKernelMemory.h"
#include "sceKernelModule.h"
#include "sceKernelMutex.h"
#include "sceKernelMbx.h"
#include "sceKernelMsgPipe.h"
#include "sceKernelInterrupt.h"
#include "sceKernelSemaphore.h"
#include "sceKernelEventFlag.h"
#include "sceKernelVTimer.h"
#include "sceKernelTime.h"
#include "sceMp3.h"
#include "sceMpeg.h"
#include "sceNet.h"
#include "sceNetAdhoc.h"
#include "scePower.h"
#include "sceUtility.h"
#include "sceUmd.h"
#include "sceRtc.h"
#include "sceSsl.h"
#include "sceSas.h"
#include "scePsmf.h"
#include "sceImpose.h"
#include "sceUsb.h"
#include "scePspNpDrm_user.h"
#include "sceVaudio.h"
#include "sceHeap.h"
#include "sceDmac.h"
#include "sceMp4.h"

#include "../Util/PPGeDraw.h"

/*
17: [MIPS32 R4K 00000000 ]: Loader: Type: 1 Vaddr: 00000000 Filesz: 2856816 Memsz: 2856816 
18: [MIPS32 R4K 00000000 ]: Loader: Loadable Segment Copied to 0898dab0, size 002b9770
19: [MIPS32 R4K 00000000 ]: Loader: Type: 1 Vaddr: 002b9770 Filesz: 14964 Memsz: 733156 
20: [MIPS32 R4K 00000000 ]: Loader: Loadable Segment Copied to 08c47220, size 000b2fe4
*/

static bool kernelRunning = false;
KernelObjectPool kernelObjects;
KernelStats kernelStats;
u32 registeredExitCbId;

void __KernelInit()
{
	if (kernelRunning)
	{
		ERROR_LOG(SCEKERNEL, "Can't init kernel when kernel is running");
		return;
	}

	__KernelTimeInit();
	__InterruptsInit();
	__KernelMemoryInit();
	__KernelThreadingInit();
	__KernelAlarmInit();
	__KernelVTimerInit();
	__KernelEventFlagInit();
	__KernelMbxInit();
	__KernelMutexInit();
	__KernelSemaInit();
	__KernelMsgPipeInit();
	__IoInit();
	__JpegInit();
	__AudioInit();
	__SasInit();
	__AtracInit();
	__CccInit();
	__DisplayInit();
	__GeInit();
	__PowerInit();
	__UtilityInit();
	__UmdInit();
	__MpegInit();
	__PsmfInit();
	__CtrlInit();
	__RtcInit();
	__SslInit();
	__ImposeInit();
	__UsbInit();
	__FontInit();
	__NetInit();
	__NetAdhocInit();
	__VaudioInit();
	__CheatInit();
	__HeapInit();
	__DmacInit();
	__AudioCodecInit();
	__VideoPmpInit();
	
	SaveState::Init();  // Must be after IO, as it may create a directory
	Reporting::Init();

	// "Internal" PSP libraries
	__PPGeInit();

	kernelRunning = true;
	INFO_LOG(SCEKERNEL, "Kernel initialized.");
}

void __KernelShutdown()
{
	if (!kernelRunning)
	{
		ERROR_LOG(SCEKERNEL, "Can't shut down kernel - not running");
		return;
	}
	kernelObjects.List();
	INFO_LOG(SCEKERNEL, "Shutting down kernel - %i kernel objects alive", kernelObjects.GetCount());
	hleCurrentThreadName = NULL;
	kernelObjects.Clear();

	__AudioCodecShutdown();
	__VideoPmpShutdown();
	__AACShutdown();
	__NetShutdown();
	__NetAdhocShutdown();
	__FontShutdown();

	__Mp3Shutdown();
	__MpegShutdown();
	__PsmfShutdown();
	__PPGeShutdown();

	__CtrlShutdown();
	__UtilityShutdown();
	__GeShutdown();
	__SasShutdown();
	__DisplayShutdown();
	__AtracShutdown();
	__AudioShutdown();
	__IoShutdown();
	__KernelMutexShutdown();
	__KernelThreadingShutdown();
	__KernelMemoryShutdown();
	__InterruptsShutdown();
	__CheatShutdown();
	__KernelModuleShutdown();

	CoreTiming::ClearPendingEvents();
	CoreTiming::UnregisterAllEvents();
	Reporting::Shutdown();

	kernelRunning = false;
}

void __KernelDoState(PointerWrap &p)
{
	{
		auto s = p.Section("Kernel", 1, 2);
		if (!s)
			return;

		p.Do(kernelRunning);
		kernelObjects.DoState(p);

		if (s >= 2)
			p.Do(registeredExitCbId);
	}

	{
		auto s = p.Section("Kernel Modules", 1);
		if (!s)
			return;

		__InterruptsDoState(p);
		// Memory needs to be after kernel objects, which may free kernel memory.
		__KernelMemoryDoState(p);
		__KernelThreadingDoState(p);
		__KernelAlarmDoState(p);
		__KernelVTimerDoState(p);
		__KernelEventFlagDoState(p);
		__KernelMbxDoState(p);
		__KernelModuleDoState(p);
		__KernelMsgPipeDoState(p);
		__KernelMutexDoState(p);
		__KernelSemaDoState(p);
		__KernelTimeDoState(p);
	}

	{
		auto s = p.Section("HLE Modules", 1);
		if (!s)
			return;

		__AtracDoState(p);
		__AudioDoState(p);
		__CccDoState(p);
		__CtrlDoState(p);
		__DisplayDoState(p);
		__FontDoState(p);
		__GeDoState(p);
		__ImposeDoState(p);
		__IoDoState(p);
		__JpegDoState(p);
		__Mp3DoState(p);
		__MpegDoState(p);
		__NetDoState(p);
		__NetAdhocDoState(p);
		__PowerDoState(p);
		__PsmfDoState(p);
		__PsmfPlayerDoState(p);
		__RtcDoState(p);
		__SasDoState(p);
		__SslDoState(p);
		__UmdDoState(p);
		__UtilityDoState(p);
		__UsbDoState(p);
		__VaudioDoState(p);
		__HeapDoState(p);

		__PPGeDoState(p);
		__CheatDoState(p);
		__sceAudiocodecDoState(p);
		__VideoPmpDoState(p);
		__AACDoState(p);
	}

	{
		auto s = p.Section("Kernel Cleanup", 1);
		if (!s)
			return;

		__InterruptsDoStateLate(p);
		__KernelThreadingDoStateLate(p);
		Reporting::DoState(p);
	}
}

bool __KernelIsRunning() {
	return kernelRunning;
}

void sceKernelExitGame()
{
	INFO_LOG(SCEKERNEL, "sceKernelExitGame");
	if (!PSP_CoreParameter().headLess)
		PanicAlert("Game exited");
	__KernelSwitchOffThread("game exited");
	Core_Stop();
}

void sceKernelExitGameWithStatus()
{
	INFO_LOG(SCEKERNEL, "sceKernelExitGameWithStatus");
	if (!PSP_CoreParameter().headLess)
		PanicAlert("Game exited (with status)");
	__KernelSwitchOffThread("game exited");
	Core_Stop();
}

u32 sceKernelDevkitVersion()
{
	int firmwareVersion = g_Config.iFirmwareVersion;
	int major = firmwareVersion / 100;
	int minor = (firmwareVersion / 10) % 10;
	int revision = firmwareVersion % 10;
	int devkitVersion = (major << 24) | (minor << 16) | (revision << 8) | 0x10;
	DEBUG_LOG(SCEKERNEL, "sceKernelDevkitVersion (%i) ", devkitVersion);
	return devkitVersion;
}

u32 sceKernelRegisterKprintfHandler()
{
	ERROR_LOG(SCEKERNEL, "UNIMPL sceKernelRegisterKprintfHandler()");
	return 0;
}
void sceKernelRegisterDefaultExceptionHandler()
{
	ERROR_LOG(SCEKERNEL, "UNIMPL sceKernelRegisterDefaultExceptionHandler()");
	RETURN(0);
}

void sceKernelSetGPO(u32 ledAddr)
{
	// Sets debug LEDs.
	DEBUG_LOG(SCEKERNEL, "sceKernelSetGPO(%02x)", ledAddr);
}

u32 sceKernelGetGPI()
{
	// Always returns 0 on production systems.
	DEBUG_LOG(SCEKERNEL, "0=sceKernelGetGPI()");
	return 0;
}

// #define LOG_CACHE

// Don't even log these by default, they're spammy and we probably won't
// need to emulate them. Useful for invalidating cached textures though,
// and in the future display lists (although hashing takes care of those
// for now).
int sceKernelDcacheInvalidateRange(u32 addr, int size)
{
#ifdef LOG_CACHE
	NOTICE_LOG(CPU,"sceKernelDcacheInvalidateRange(%08x, %i)", addr, size);
#endif
	if (size < 0 || (int) addr + size < 0)
		return SCE_KERNEL_ERROR_ILLEGAL_ADDR;

	if (size > 0)
	{
		if ((addr % 64) != 0 || (size % 64) != 0)
			return SCE_KERNEL_ERROR_CACHE_ALIGNMENT;

		if (addr != 0)
			gpu->InvalidateCache(addr, size, GPU_INVALIDATE_HINT);
	}
	hleEatCycles(190);
	return 0;
}

int sceKernelIcacheInvalidateRange(u32 addr, int size) {
	DEBUG_LOG(CPU, "sceKernelIcacheInvalidateRange(%08x, %i)", addr, size);
	currentMIPS->InvalidateICache(addr, size);
	return 0;
}

int sceKernelDcacheWritebackAll()
{
#ifdef LOG_CACHE
	NOTICE_LOG(CPU,"sceKernelDcacheWritebackAll()");
#endif
	// Some games seem to use this a lot, it doesn't make sense
	// to zap the whole texture cache.
	gpu->InvalidateCache(0, -1, GPU_INVALIDATE_ALL);
	hleEatCycles(3524);
	hleReSchedule("dcache writeback all");
	return 0;
}

int sceKernelDcacheWritebackRange(u32 addr, int size)
{
#ifdef LOG_CACHE
	NOTICE_LOG(CPU,"sceKernelDcacheWritebackRange(%08x, %i)", addr, size);
#endif
	if (size < 0)
		return SCE_KERNEL_ERROR_INVALID_SIZE;

	if (size > 0 && addr != 0) {
		gpu->InvalidateCache(addr, size, GPU_INVALIDATE_HINT);
	}
	hleEatCycles(165);
	return 0;
}
int sceKernelDcacheWritebackInvalidateRange(u32 addr, int size)
{
#ifdef LOG_CACHE
	NOTICE_LOG(CPU,"sceKernelDcacheInvalidateRange(%08x, %i)", addr, size);
#endif
	if (size < 0)
		return SCE_KERNEL_ERROR_INVALID_SIZE;

	if (size > 0 && addr != 0) {
		gpu->InvalidateCache(addr, size, GPU_INVALIDATE_HINT);
	}
	hleEatCycles(165);
	return 0;
}
int sceKernelDcacheWritebackInvalidateAll()
{
#ifdef LOG_CACHE
	NOTICE_LOG(CPU,"sceKernelDcacheInvalidateAll()");
#endif
	gpu->InvalidateCache(0, -1, GPU_INVALIDATE_ALL);
	hleEatCycles(1165);
	hleReSchedule("dcache invalidate all");
	return 0;
}


u32 sceKernelIcacheInvalidateAll()
{
#ifdef LOG_CACHE
	NOTICE_LOG(CPU, "Icache invalidated - should clear JIT someday");
#endif
	return 0;
}


u32 sceKernelIcacheClearAll()
{
#ifdef LOG_CACHE
	NOTICE_LOG(CPU, "Icache cleared - should clear JIT someday");
#endif
	DEBUG_LOG(CPU, "Icache cleared - should clear JIT someday");
	return 0;
}

void KernelObject::GetQuickInfo(char *ptr, int size)
{
	strcpy(ptr, "-");
}

KernelObjectPool::KernelObjectPool()
{
	memset(occupied, 0, sizeof(bool)*maxCount);
	nextID = initialNextID;
}

SceUID KernelObjectPool::Create(KernelObject *obj, int rangeBottom, int rangeTop)
{
	if (rangeTop > maxCount)
		rangeTop = maxCount;
	if (nextID >= rangeBottom && nextID < rangeTop)
		rangeBottom = nextID++;

	for (int i = rangeBottom; i < rangeTop; i++)
	{
		if (!occupied[i])
		{
			occupied[i] = true;
			pool[i] = obj;
			pool[i]->uid = i + handleOffset;
			return i + handleOffset;
		}
	}

	ERROR_LOG_REPORT(SCEKERNEL, "Unable to allocate kernel object, too many objects slots in use.");
	return 0;
}

bool KernelObjectPool::IsValid(SceUID handle)
{
	int index = handle - handleOffset;
	if (index < 0)
		return false;
	if (index >= maxCount)
		return false;

	return occupied[index];
}

void KernelObjectPool::Clear()
{
	for (int i=0; i<maxCount; i++)
	{
		//brutally clear everything, no validation
		if (occupied[i])
			delete pool[i];
		occupied[i]=false;
	}
	memset(pool, 0, sizeof(KernelObject*)*maxCount);
	nextID = initialNextID;
}

KernelObject *&KernelObjectPool::operator [](SceUID handle)
{
	_dbg_assert_msg_(SCEKERNEL, IsValid(handle), "GRABBING UNALLOCED KERNEL OBJ");
	return pool[handle - handleOffset];
}

void KernelObjectPool::List()
{
	for (int i = 0; i < maxCount; i++)
	{
		if (occupied[i])
		{
			char buffer[256];
			if (pool[i])
			{
				pool[i]->GetQuickInfo(buffer,256);
				INFO_LOG(SCEKERNEL, "KO %i: %s \"%s\": %s", i + handleOffset, pool[i]->GetTypeName(), pool[i]->GetName(), buffer);
			}
			else
			{
				strcpy(buffer,"WTF? Zero Pointer");
			}
		}
	}
}

int KernelObjectPool::GetCount()
{
	int count = 0;
	for (int i=0; i<maxCount; i++)
	{
		if (occupied[i])
			count++;
	}
	return count;
}

void KernelObjectPool::DoState(PointerWrap &p)
{
	auto s = p.Section("KernelObjectPool", 1);
	if (!s)
		return;

	int _maxCount = maxCount;
	p.Do(_maxCount);

	if (_maxCount != maxCount)
	{
		p.SetError(p.ERROR_FAILURE);
		ERROR_LOG(SCEKERNEL, "Unable to load state: different kernel object storage.");
		return;
	}

	if (p.mode == p.MODE_READ)
	{
		hleCurrentThreadName = NULL;
		kernelObjects.Clear();
	}

	p.Do(nextID);
	p.DoArray(occupied, maxCount);
	for (int i = 0; i < maxCount; ++i)
	{
		if (!occupied[i])
			continue;

		int type;
		if (p.mode == p.MODE_READ)
		{
			p.Do(type);
			pool[i] = CreateByIDType(type);

			// Already logged an error.
			if (pool[i] == NULL)
				return;

			pool[i]->uid = i + handleOffset;
		}
		else
		{
			type = pool[i]->GetIDType();
			p.Do(type);
		}
		pool[i]->DoState(p);
		if (p.error >= p.ERROR_FAILURE)
			break;
	}
}

KernelObject *KernelObjectPool::CreateByIDType(int type)
{
	// Used for save states.  This is ugly, but what other way is there?
	switch (type)
	{
	case SCE_KERNEL_TMID_Alarm:
		return __KernelAlarmObject();
	case SCE_KERNEL_TMID_EventFlag:
		return __KernelEventFlagObject();
	case SCE_KERNEL_TMID_Mbox:
		return __KernelMbxObject();
	case SCE_KERNEL_TMID_Fpl:
		return __KernelMemoryFPLObject();
	case SCE_KERNEL_TMID_Vpl:
		return __KernelMemoryVPLObject();
	case PPSSPP_KERNEL_TMID_PMB:
		return __KernelMemoryPMBObject();
	case PPSSPP_KERNEL_TMID_Module:
		return __KernelModuleObject();
	case SCE_KERNEL_TMID_Mpipe:
		return __KernelMsgPipeObject();
	case SCE_KERNEL_TMID_Mutex:
		return __KernelMutexObject();
	case SCE_KERNEL_TMID_LwMutex:
		return __KernelLwMutexObject();
	case SCE_KERNEL_TMID_Semaphore:
		return __KernelSemaphoreObject();
	case SCE_KERNEL_TMID_Callback:
		return __KernelCallbackObject();
	case SCE_KERNEL_TMID_Thread:
		return __KernelThreadObject();
	case SCE_KERNEL_TMID_VTimer:
		return __KernelVTimerObject();
	case SCE_KERNEL_TMID_Tlspl:
		return __KernelTlsplObject();
	case PPSSPP_KERNEL_TMID_File:
		return __KernelFileNodeObject();
	case PPSSPP_KERNEL_TMID_DirList:
		return __KernelDirListingObject();

	default:
		ERROR_LOG(COMMON, "Unable to load state: could not find object type %d.", type);
		return NULL;
	}
}

struct SystemStatus {
	SceSize_le size;
	SceUInt_le status;
	SceUInt_le clockPart1;
	SceUInt_le clockPart2;
	SceUInt_le perfcounter1;
	SceUInt_le perfcounter2;
	SceUInt_le perfcounter3;
};

int sceKernelReferSystemStatus(u32 statusPtr) {
	DEBUG_LOG(SCEKERNEL, "sceKernelReferSystemStatus(%08x)", statusPtr);
	if (Memory::IsValidAddress(statusPtr)) {
		SystemStatus status;
		memset(&status, 0, sizeof(SystemStatus));
		status.size = sizeof(SystemStatus);
		// TODO: Fill in the struct!
		Memory::WriteStruct(statusPtr, &status);
	}
	return 0;
}

struct DebugProfilerRegs {
	u32 enable;
	u32 systemck;
	u32 cpuck;
	u32 internal;
	u32 memory;
	u32 copz;
	u32 vfpu;
	u32 sleep;
	u32 bus_access;
	u32 uncached_load;
	u32 uncached_store;
	u32 cached_load;
	u32 cached_store;
	u32 i_miss;
	u32 d_miss;
	u32 d_writeback;
	u32 cop0_inst;
	u32 fpu_inst;
	u32 vfpu_inst;
	u32 local_bus;
};

u32 sceKernelReferThreadProfiler(u32 statusPtr) {
	ERROR_LOG(SCEKERNEL, "FAKE sceKernelReferThreadProfiler()");

	// Can we confirm that the struct above is the right struct?
	// If so, re-enable this code.
	//DebugProfilerRegs regs;
	//memset(&regs, 0, sizeof(regs));
	// TODO: fill the struct.
	//if (Memory::IsValidAddress(statusPtr)) {
	//	Memory::WriteStruct(statusPtr, &regs);
	//}
	return 0;
}

int sceKernelReferGlobalProfiler(u32 statusPtr) {
	ERROR_LOG(SCEKERNEL, "UNIMPL sceKernelReferGlobalProfiler(%08x)", statusPtr);
	// Ignore for now
	return 0;
}

int ThreadManForKernel_446d8de6(const char *threadName, u32 entry, u32 prio, int stacksize, u32 attr, u32 optionAddr)
{
	WARN_LOG(SCEKERNEL,"ThreadManForKernel_446d8de6:Not support this patcher");
	return sceKernelCreateThread(threadName, entry, prio, stacksize,  attr, optionAddr);
}

int ThreadManForKernel_f475845d(SceUID threadToStartID, int argSize, u32 argBlockPtr)
{	
	WARN_LOG(SCEKERNEL,"ThreadManForKernel_f475845d:Not support this patcher");
	return sceKernelStartThread(threadToStartID,argSize,argBlockPtr);
}

int ThreadManForKernel_ceadeb47(u32 usec)
{	
	WARN_LOG(SCEKERNEL,"ThreadManForKernel_ceadeb47:Not support this patcher");
	return sceKernelDelayThread(usec);
}

const HLEFunction ThreadManForUser[] =
{
	{0x55C20A00,&WrapI_CUUU<sceKernelCreateEventFlag>,         "sceKernelCreateEventFlag"},
	{0x812346E4,&WrapU_IU<sceKernelClearEventFlag>,            "sceKernelClearEventFlag"},
	{0xEF9E4C70,&WrapU_I<sceKernelDeleteEventFlag>,            "sceKernelDeleteEventFlag"},
	{0x1fb15a32,&WrapU_IU<sceKernelSetEventFlag>,              "sceKernelSetEventFlag"},
	{0x402FCF22,&WrapI_IUUUU<sceKernelWaitEventFlag>,          "sceKernelWaitEventFlag",               HLE_NOT_IN_INTERRUPT},
	{0x328C546A,&WrapI_IUUUU<sceKernelWaitEventFlagCB>,        "sceKernelWaitEventFlagCB",             HLE_NOT_IN_INTERRUPT},
	{0x30FD48F0,&WrapI_IUUU<sceKernelPollEventFlag>,           "sceKernelPollEventFlag"},
	{0xCD203292,&WrapU_IUU<sceKernelCancelEventFlag>,          "sceKernelCancelEventFlag"},
	{0xA66B0120,&WrapU_IU<sceKernelReferEventFlagStatus>,      "sceKernelReferEventFlagStatus"},

	{0x8FFDF9A2,&WrapI_IIU<sceKernelCancelSema>,               "sceKernelCancelSema"},
	{0xD6DA4BA1,&WrapI_CUIIU<sceKernelCreateSema>,             "sceKernelCreateSema"},
	{0x28b6489c,&WrapI_I<sceKernelDeleteSema>,                 "sceKernelDeleteSema"},
	{0x58b1f937,&WrapI_II<sceKernelPollSema>,                  "sceKernelPollSema"},
	{0xBC6FEBC5,&WrapI_IU<sceKernelReferSemaStatus>,           "sceKernelReferSemaStatus"},
	{0x3F53E640,&WrapI_II<sceKernelSignalSema>,                "sceKernelSignalSema"},
	{0x4E3A1105,&WrapI_IIU<sceKernelWaitSema>,                 "sceKernelWaitSema",                    HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0x6d212bac,&WrapI_IIU<sceKernelWaitSemaCB>,               "sceKernelWaitSemaCB",                  HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},

	{0x60107536,&WrapI_U<sceKernelDeleteLwMutex>,              "sceKernelDeleteLwMutex"},
	{0x19CFF145,&WrapI_UCUIU<sceKernelCreateLwMutex>,          "sceKernelCreateLwMutex"},
	{0x4C145944,&WrapI_IU<sceKernelReferLwMutexStatusByID>,    "sceKernelReferLwMutexStatusByID"},
	// NOTE: LockLwMutex, UnlockLwMutex, and ReferLwMutexStatus are in Kernel_Library, see sceKernelInterrupt.cpp.
	// The below should not be called directly.
	//{0x71040D5C,0,                                             "_sceKernelTryLockLwMutex"},
	//{0x7CFF8CF3,0,                                             "_sceKernelLockLwMutex"},
	//{0x31327F19,0,                                             "_sceKernelLockLwMutexCB"},
	//{0xBEED3A47,0,                                             "_sceKernelUnlockLwMutex"},

	{0xf8170fbe,WrapI_I<sceKernelDeleteMutex>,                 "sceKernelDeleteMutex"},
	{0xB011B11F,WrapI_IIU<sceKernelLockMutex>,                 "sceKernelLockMutex",                   HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0x5bf4dd27,WrapI_IIU<sceKernelLockMutexCB>,               "sceKernelLockMutexCB",                 HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0x6b30100f,WrapI_II<sceKernelUnlockMutex>,                "sceKernelUnlockMutex"},
	{0xb7d098c6,WrapI_CUIU<sceKernelCreateMutex>,              "sceKernelCreateMutex"},
	{0x0DDCD2C9,WrapI_II<sceKernelTryLockMutex>,               "sceKernelTryLockMutex"},
	{0xA9C2CB9A,WrapI_IU<sceKernelReferMutexStatus>,           "sceKernelReferMutexStatus"},
	{0x87D9223C,WrapI_IIU<sceKernelCancelMutex>,               "sceKernelCancelMutex"},

	{0xFCCFAD26,WrapI_I<sceKernelCancelWakeupThread>,"sceKernelCancelWakeupThread"},
	{0x1AF94D03,0,"sceKernelDonateWakeupThread"},
	{0xea748e31,WrapI_UU<sceKernelChangeCurrentThreadAttr>,"sceKernelChangeCurrentThreadAttr"},
	{0x71bc9871,WrapI_II<sceKernelChangeThreadPriority>,"sceKernelChangeThreadPriority"},
	{0x446D8DE6,WrapI_CUUIUU<sceKernelCreateThread>,           "sceKernelCreateThread",                HLE_NOT_IN_INTERRUPT},
	{0x9fa03cd3,WrapI_I<sceKernelDeleteThread>,"sceKernelDeleteThread"},
	{0xBD123D9E,WrapI_U<sceKernelDelaySysClockThread>,         "sceKernelDelaySysClockThread",         HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0x1181E963,WrapI_U<sceKernelDelaySysClockThreadCB>,       "sceKernelDelaySysClockThreadCB",       HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0xceadeb47,WrapI_U<sceKernelDelayThread>,                 "sceKernelDelayThread",                 HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0x68da9e36,WrapI_U<sceKernelDelayThreadCB>,               "sceKernelDelayThreadCB",               HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0xaa73c935,WrapV_I<sceKernelExitThread>,"sceKernelExitThread"},
	{0x809ce29b,WrapV_I<sceKernelExitDeleteThread>,"sceKernelExitDeleteThread"},
	{0x94aa61ee,sceKernelGetThreadCurrentPriority,"sceKernelGetThreadCurrentPriority"},
	{0x293b45b8,WrapI_V<sceKernelGetThreadId>,				   "sceKernelGetThreadId",				   HLE_NOT_IN_INTERRUPT},
	{0x3B183E26,WrapI_I<sceKernelGetThreadExitStatus>,"sceKernelGetThreadExitStatus"},
	{0x52089CA1,WrapI_I<sceKernelGetThreadStackFreeSize>,      "sceKernelGetThreadStackFreeSize"},
	{0xFFC36A14,WrapU_UU<sceKernelReferThreadRunStatus>,"sceKernelReferThreadRunStatus"},
	{0x17c1684e,WrapU_UU<sceKernelReferThreadStatus>,"sceKernelReferThreadStatus"},
	{0x2C34E053,WrapI_I<sceKernelReleaseWaitThread>,"sceKernelReleaseWaitThread"},
	{0x75156e8f,WrapI_I<sceKernelResumeThread>,"sceKernelResumeThread"},
	{0x3ad58b8c,&WrapU_V<sceKernelSuspendDispatchThread>,      "sceKernelSuspendDispatchThread",       HLE_NOT_IN_INTERRUPT},
	{0x27e22ec2,&WrapU_U<sceKernelResumeDispatchThread>,       "sceKernelResumeDispatchThread",        HLE_NOT_IN_INTERRUPT},
	{0x912354a7,&WrapI_I<sceKernelRotateThreadReadyQueue>,"sceKernelRotateThreadReadyQueue"},
	{0x9ACE131E,WrapI_V<sceKernelSleepThread>,                 "sceKernelSleepThread",                 HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0x82826f70,WrapI_V<sceKernelSleepThreadCB>,               "sceKernelSleepThreadCB",               HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0xF475845D,&WrapI_IIU<sceKernelStartThread>,              "sceKernelStartThread",                 HLE_NOT_IN_INTERRUPT},
	{0x9944f31f,WrapI_I<sceKernelSuspendThread>,"sceKernelSuspendThread"},
	{0x616403ba,WrapI_I<sceKernelTerminateThread>,"sceKernelTerminateThread"},
	{0x383f7bcc,WrapI_I<sceKernelTerminateDeleteThread>,"sceKernelTerminateDeleteThread"},
	{0x840E8133,WrapI_IU<sceKernelWaitThreadEndCB>,"sceKernelWaitThreadEndCB"},
	{0xd13bde95,WrapI_V<sceKernelCheckThreadStack>,"sceKernelCheckThreadStack"},

	{0x94416130,WrapU_UUUU<sceKernelGetThreadmanIdList>,"sceKernelGetThreadmanIdList"},
	{0x57CF62DD,WrapU_U<sceKernelGetThreadmanIdType>,"sceKernelGetThreadmanIdType"},
	{0xBC80EC7C,WrapU_UUU<sceKernelExtendThreadStack>, "sceKernelExtendThreadStack"},
	// NOTE: Takes a UID from sceKernelMemory's AllocMemoryBlock and seems thread stack related.
	//{0x28BFD974,0,"ThreadManForUser_28BFD974"},

	{0x82BC5777,WrapU64_V<sceKernelGetSystemTimeWide>,"sceKernelGetSystemTimeWide"},
	{0xdb738f35,WrapI_U<sceKernelGetSystemTime>,"sceKernelGetSystemTime"},
	{0x369ed59d,WrapU_V<sceKernelGetSystemTimeLow>,"sceKernelGetSystemTimeLow"},

	{0x8218B4DD,WrapI_U<sceKernelReferGlobalProfiler>,"sceKernelReferGlobalProfiler"},
	{0x627E6F3A,WrapI_U<sceKernelReferSystemStatus>,"sceKernelReferSystemStatus"},
	{0x64D4540E,WrapU_U<sceKernelReferThreadProfiler>,"sceKernelReferThreadProfiler"},

	//Fifa Street 2 uses alarms
	{0x6652b8ca,WrapI_UUU<sceKernelSetAlarm>,"sceKernelSetAlarm"},
	{0xB2C25152,WrapI_UUU<sceKernelSetSysClockAlarm>,"sceKernelSetSysClockAlarm"},
	{0x7e65b999,WrapI_I<sceKernelCancelAlarm>,"sceKernelCancelAlarm"},
	{0xDAA3F564,WrapI_IU<sceKernelReferAlarmStatus>,"sceKernelReferAlarmStatus"},

	{0xba6b92e2,WrapI_UUU<sceKernelSysClock2USec>,"sceKernelSysClock2USec"},
	{0x110dec9a,WrapI_UU<sceKernelUSec2SysClock>,"sceKernelUSec2SysClock"},
	{0xC8CD158C,WrapU64_U<sceKernelUSec2SysClockWide>,"sceKernelUSec2SysClockWide"},
	{0xE1619D7C,WrapI_UUUU<sceKernelSysClock2USecWide>,"sceKernelSysClock2USecWide"},

	{0x278C0DF5,WrapI_IU<sceKernelWaitThreadEnd>,"sceKernelWaitThreadEnd"},
	{0xd59ead2f,WrapI_I<sceKernelWakeupThread>,"sceKernelWakeupThread"}, //AI Go, audio?

	{0x0C106E53,0,"sceKernelRegisterThreadEventHandler"},
	{0x72F3C145,0,"sceKernelReleaseThreadEventHandler"},
	{0x369EEB6B,0,"sceKernelReferThreadEventHandlerStatus"},

	{0x349d6d6c,sceKernelCheckCallback,                        "sceKernelCheckCallback"},
	{0xE81CAF8F,WrapI_CUU<sceKernelCreateCallback>,            "sceKernelCreateCallback"},
	{0xEDBA5844,WrapI_I<sceKernelDeleteCallback>,              "sceKernelDeleteCallback"},
	{0xC11BA8C4,WrapI_II<sceKernelNotifyCallback>,             "sceKernelNotifyCallback"},
	{0xBA4051D6,WrapI_I<sceKernelCancelCallback>,              "sceKernelCancelCallback"},
	{0x2A3D44FF,WrapI_I<sceKernelGetCallbackCount>,            "sceKernelGetCallbackCount"},
	{0x730ED8BC,WrapI_IU<sceKernelReferCallbackStatus>,        "sceKernelReferCallbackStatus"},

	{0x8125221D,WrapI_CUU<sceKernelCreateMbx>,                 "sceKernelCreateMbx"},
	{0x86255ADA,WrapI_I<sceKernelDeleteMbx>,                   "sceKernelDeleteMbx"},
	{0xE9B3061E,WrapI_IU<sceKernelSendMbx>,                    "sceKernelSendMbx"},
	{0x18260574,WrapI_IUU<sceKernelReceiveMbx>,                "sceKernelReceiveMbx",                  HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0xF3986382,WrapI_IUU<sceKernelReceiveMbxCB>,              "sceKernelReceiveMbxCB",                HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0x0D81716A,WrapI_IU<sceKernelPollMbx>,                    "sceKernelPollMbx"},
	{0x87D4DD36,WrapI_IU<sceKernelCancelReceiveMbx>,           "sceKernelCancelReceiveMbx"},
	{0xA8E8C846,WrapI_IU<sceKernelReferMbxStatus>,             "sceKernelReferMbxStatus"},

	{0x7C0DC2A0,WrapI_CIUUU<sceKernelCreateMsgPipe>,           "sceKernelCreateMsgPipe"},
	{0xF0B7DA1C,WrapI_I<sceKernelDeleteMsgPipe>,               "sceKernelDeleteMsgPipe"},
	{0x876DBFAD,WrapI_IUUUUU<sceKernelSendMsgPipe>,            "sceKernelSendMsgPipe"},
	{0x7C41F2C2,WrapI_IUUUUU<sceKernelSendMsgPipeCB>,          "sceKernelSendMsgPipeCB"},
	{0x884C9F90,WrapI_IUUUU<sceKernelTrySendMsgPipe>,          "sceKernelTrySendMsgPipe"},
	{0x74829B76,WrapI_IUUUUU<sceKernelReceiveMsgPipe>,         "sceKernelReceiveMsgPipe"},
	{0xFBFA697D,WrapI_IUUUUU<sceKernelReceiveMsgPipeCB>,       "sceKernelReceiveMsgPipeCB"},
	{0xDF52098F,WrapI_IUUUU<sceKernelTryReceiveMsgPipe>,       "sceKernelTryReceiveMsgPipe"},
	{0x349B864D,WrapI_IUU<sceKernelCancelMsgPipe>,             "sceKernelCancelMsgPipe"},
	{0x33BE4024,WrapI_IU<sceKernelReferMsgPipeStatus>,         "sceKernelReferMsgPipeStatus"},

	{0x56C039B5,WrapI_CIUUU<sceKernelCreateVpl>,               "sceKernelCreateVpl"},
	{0x89B3D48C,WrapI_I<sceKernelDeleteVpl>,                   "sceKernelDeleteVpl"},
	{0xBED27435,WrapI_IUUU<sceKernelAllocateVpl>,              "sceKernelAllocateVpl",                 HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0xEC0A693F,WrapI_IUUU<sceKernelAllocateVplCB>,            "sceKernelAllocateVplCB",               HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0xAF36D708,WrapI_IUU<sceKernelTryAllocateVpl>,            "sceKernelTryAllocateVpl"},
	{0xB736E9FF,WrapI_IU<sceKernelFreeVpl>,                    "sceKernelFreeVpl"},
	{0x1D371B8A,WrapI_IU<sceKernelCancelVpl>,                  "sceKernelCancelVpl"},
	{0x39810265,WrapI_IU<sceKernelReferVplStatus>,             "sceKernelReferVplStatus"},

	{0xC07BB470,WrapI_CUUUUU<sceKernelCreateFpl>,              "sceKernelCreateFpl"},
	{0xED1410E0,WrapI_I<sceKernelDeleteFpl>,                   "sceKernelDeleteFpl"},
	{0xD979E9BF,WrapI_IUU<sceKernelAllocateFpl>,               "sceKernelAllocateFpl",                 HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0xE7282CB6,WrapI_IUU<sceKernelAllocateFplCB>,             "sceKernelAllocateFplCB",               HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED},
	{0x623AE665,WrapI_IU<sceKernelTryAllocateFpl>,             "sceKernelTryAllocateFpl"},
	{0xF6414A71,WrapI_IU<sceKernelFreeFpl>,                    "sceKernelFreeFpl"},
	{0xA8AA591F,WrapI_IU<sceKernelCancelFpl>,                  "sceKernelCancelFpl"},
	{0xD8199E4C,WrapI_IU<sceKernelReferFplStatus>,             "sceKernelReferFplStatus"},

	{0x20fff560,WrapU_CU<sceKernelCreateVTimer>,               "sceKernelCreateVTimer",                HLE_NOT_IN_INTERRUPT},
	{0x328F9E52,WrapU_I<sceKernelDeleteVTimer>,                "sceKernelDeleteVTimer",                HLE_NOT_IN_INTERRUPT},
	{0xc68d9437,WrapU_I<sceKernelStartVTimer>,                 "sceKernelStartVTimer"},
	{0xD0AEEE87,WrapU_I<sceKernelStopVTimer>,                  "sceKernelStopVTimer"},
	{0xD2D615EF,WrapU_I<sceKernelCancelVTimerHandler>,         "sceKernelCancelVTimerHandler"},
	{0xB3A59970,WrapU_IU<sceKernelGetVTimerBase>,              "sceKernelGetVTimerBase"},
	{0xB7C18B77,WrapU64_I<sceKernelGetVTimerBaseWide>,         "sceKernelGetVTimerBaseWide"},
	{0x034A921F,WrapU_IU<sceKernelGetVTimerTime>,              "sceKernelGetVTimerTime"},
	{0xC0B3FFD2,WrapU64_I<sceKernelGetVTimerTimeWide>,         "sceKernelGetVTimerTimeWide"},
	{0x5F32BEAA,WrapU_IU<sceKernelReferVTimerStatus>,          "sceKernelReferVTimerStatus"},
	{0x542AD630,WrapU_IU<sceKernelSetVTimerTime>,              "sceKernelSetVTimerTime"},
	{0xFB6425C3,WrapU64_IU64<sceKernelSetVTimerTimeWide>,      "sceKernelSetVTimerTimeWide"},
	{0xd8b299ae,WrapU_IUUU<sceKernelSetVTimerHandler>,         "sceKernelSetVTimerHandler"},
	{0x53B00E9A,WrapU_IU64UU<sceKernelSetVTimerHandlerWide>,   "sceKernelSetVTimerHandlerWide"},

	{0x8daff657,WrapI_CUUUUU<sceKernelCreateTlspl>,            "sceKernelCreateTlspl"},
	{0x32bf938e,WrapI_I<sceKernelDeleteTlspl>,                 "sceKernelDeleteTlspl"},
	{0x721067F3,WrapI_IU<sceKernelReferTlsplStatus>,           "sceKernelReferTlsplStatus"},
	// Not completely certain about args.
	{0x4A719FB2,WrapI_I<sceKernelFreeTlspl>,                   "sceKernelFreeTlspl"},
	// Internal.  Takes (uid, &addr) as parameters... probably.
	//{0x65F54FFB,0,                                             "_sceKernelAllocateTlspl"},
	// NOTE: sceKernelGetTlsAddr is in Kernel_Library, see sceKernelInterrupt.cpp.

	// Not sure if these should be hooked up. See below.
	{0x0E927AED, _sceKernelReturnFromTimerHandler, "_sceKernelReturnFromTimerHandler"},
	{0x532A522E, WrapV_I<_sceKernelExitThread>,"_sceKernelExitThread"},


	// Shouldn't hook this up. No games should import this function manually and call it.
	// {0x6E9EA350, _sceKernelReturnFromCallback,"_sceKernelReturnFromCallback"},
};

const HLEFunction ThreadManForKernel[] =
{
	{0xceadeb47, WrapI_U<ThreadManForKernel_ceadeb47>, "ThreadManForKernel_ceadeb47"},
	{0x446d8de6, WrapI_CUUIUU<ThreadManForKernel_446d8de6>, "ThreadManForKernel_446d8de6"},//Not sure right
	{0xf475845d, &WrapI_IIU<ThreadManForKernel_f475845d>, "ThreadManForKernel_f475845d"},//Not sure right
};

void Register_ThreadManForUser()
{
	RegisterModule("ThreadManForUser", ARRAY_SIZE(ThreadManForUser), ThreadManForUser);
}


const HLEFunction LoadExecForUser[] =
{
	{0x05572A5F,&WrapV_V<sceKernelExitGame>, "sceKernelExitGame"}, //()
	{0x4AC57943,&WrapI_I<sceKernelRegisterExitCallback>,"sceKernelRegisterExitCallback"},
	{0xBD2F1094,&WrapI_CU<sceKernelLoadExec>,"sceKernelLoadExec"},
	{0x2AC9954B,&WrapV_V<sceKernelExitGameWithStatus>,"sceKernelExitGameWithStatus"},
	{0x362A956B,&WrapI_V<LoadExecForUser_362A956B>, "LoadExecForUser_362A956B"},
	{0x8ada38d3,0, "LoadExecForUser_8ADA38D3"},
};

void Register_LoadExecForUser()
{
	RegisterModule("LoadExecForUser", ARRAY_SIZE(LoadExecForUser), LoadExecForUser);
}

int LoadExecForKernel_4AC57943(SceUID cbId) 
{
	WARN_LOG(SCEKERNEL,"LoadExecForKernel_4AC57943:Not support this patcher");
	return sceKernelRegisterExitCallback(cbId);//not sure right
}
 
const HLEFunction LoadExecForKernel[] =
{
	{0x4AC57943,&WrapI_I<LoadExecForKernel_4AC57943>,"LoadExecForKernel_4AC57943"},
	{0xa3d5e142,0, "LoadExecForKernel_a3d5e142"}, 
};
 
void Register_LoadExecForKernel()
{
	RegisterModule("LoadExecForKernel", ARRAY_SIZE(LoadExecForKernel), LoadExecForKernel);
}

const HLEFunction SysMemForKernel[] =
{
	{0x636c953b,0, "SysMemForKernel_636c953b"},
	{0xc9805775,0, "SysMemForKernel_c9805775"},
	{0x1c1fbfe7,0, "SysMemForKernel_1c1fbfe7"},
};

const HLEFunction ExceptionManagerForKernel[] =
{
	{0x3FB264FC, 0, "sceKernelRegisterExceptionHandler"},
	{0x5A837AD4, 0, "sceKernelRegisterPriorityExceptionHandler"},
	{0x565C0B0E, sceKernelRegisterDefaultExceptionHandler, "sceKernelRegisterDefaultExceptionHandler"},
	{0x1AA6CFFA, 0, "sceKernelReleaseExceptionHandler"},
	{0xDF83875E, 0, "sceKernelGetActiveDefaultExceptionHandler"},
	{0x291FF031, 0, "sceKernelReleaseDefaultExceptionHandler"},
	{0x15ADC862, 0, "sceKernelRegisterNmiHandler"},
	{0xB15357C9, 0, "sceKernelReleaseNmiHandler"},
};

void Register_SysMemForKernel()
{
	RegisterModule("SysMemForKernel", ARRAY_SIZE(SysMemForKernel), SysMemForKernel);
}

void Register_ExceptionManagerForKernel()
{
	RegisterModule("ExceptionManagerForKernel", ARRAY_SIZE(ExceptionManagerForKernel), ExceptionManagerForKernel);
}

// Seen in some homebrew
const HLEFunction UtilsForKernel[] = {
	{0xC2DF770E, 0, "sceKernelIcacheInvalidateRange"},
	{0x78934841, 0, "sceKernelGzipDecompress"},
	{0xe8db3ce6, 0, "sceKernelDeflateDecompress"},
	{0x840259f1, 0, "sceKernelUtilsSha1Digest"},
	{0x9e5c5086, 0, "sceKernelUtilsMd5BlockInit"},
	{0x61e1e525, 0, "sceKernelUtilsMd5BlockUpdate"},
	{0xb8d24e78, 0, "sceKernelUtilsMd5BlockResult"},
	{0xc8186a58, 0, "sceKernelUtilsMd5Digest"},
	{0x6c6887ee, 0, "UtilsForKernel_6C6887EE"},
	{0x91e4f6a7, 0, "sceKernelLibcClock"},
	{0x27cc57f0, 0, "sceKernelLibcTime"},
	{0x79d1c3fa, 0, "sceKernelDcacheWritebackAll"},
	{0x3ee30821, 0, "sceKernelDcacheWritebackRange"},
	{0x34b9fa9e, 0, "sceKernelDcacheWritebackInvalidateRange"},
	{0xb435dec5, 0, "sceKernelDcacheWritebackInvalidateAll"},
	{0xbfa98062, 0, "sceKernelDcacheInvalidateRange"},
	{0x920f104a, 0, "sceKernelIcacheInvalidateAll"},
	{0xe860e75e, 0, "sceKernelUtilsMt19937Init"},
	{0x06fb8a63, 0, "sceKernelUtilsMt19937UInt"},
};


void Register_UtilsForKernel()
{
	RegisterModule("UtilsForKernel", ARRAY_SIZE(UtilsForKernel), UtilsForKernel);
}

void Register_ThreadManForKernel()
{
	RegisterModule("ThreadManForKernel", ARRAY_SIZE(ThreadManForKernel), ThreadManForKernel);		

}
