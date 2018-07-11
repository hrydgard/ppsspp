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
#include "Core/MemMapHelpers.h"
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
#include "sceUsbGps.h"
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
	__UsbGpsInit();
	
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
	__NetAdhocShutdown();
	__NetShutdown();
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
	SaveState::Shutdown();

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
		__UsbGpsDoState(p);

		// IMPORTANT! Add new sections last!
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
	__KernelSwitchOffThread("game exited");
	Core_Stop();
}

void sceKernelExitGameWithStatus()
{
	INFO_LOG(SCEKERNEL, "sceKernelExitGameWithStatus");
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

	DEBUG_LOG(SCEKERNEL, "%08x=sceKernelDevkitVersion()", devkitVersion);
	return devkitVersion;
}

u32 sceKernelRegisterKprintfHandler()
{
	ERROR_LOG(SCEKERNEL, "UNIMPL sceKernelRegisterKprintfHandler()");
	return 0;
}

int sceKernelRegisterDefaultExceptionHandler()
{
	ERROR_LOG(SCEKERNEL, "UNIMPL sceKernelRegisterDefaultExceptionHandler()");
	return 0;
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
	// Note that this doesn't actually fully invalidate all with such a large range.
	currentMIPS->InvalidateICache(0, 0x3FFFFFFF);
	return 0;
}

u32 sceKernelIcacheClearAll()
{
#ifdef LOG_CACHE
	NOTICE_LOG(CPU, "Icache cleared - should clear JIT someday");
#endif
	DEBUG_LOG(CPU, "Icache cleared - should clear JIT someday");
	// Note that this doesn't actually fully invalidate all with such a large range.
	currentMIPS->InvalidateICache(0, 0x3FFFFFFF);
	return 0;
}

void KernelObject::GetQuickInfo(char *ptr, int size)
{
	strcpy(ptr, "-");
}

KernelObjectPool::KernelObjectPool() {
	memset(occupied, 0, sizeof(bool)*maxCount);
	nextID = initialNextID;
}

SceUID KernelObjectPool::Create(KernelObject *obj, int rangeBottom, int rangeTop) {
	if (rangeTop > maxCount)
		rangeTop = maxCount;
	if (nextID >= rangeBottom && nextID < rangeTop)
		rangeBottom = nextID++;

	for (int i = rangeBottom; i < rangeTop; i++) {
		if (!occupied[i]) {
			occupied[i] = true;
			pool[i] = obj;
			pool[i]->uid = i + handleOffset;
			return i + handleOffset;
		}
	}

	ERROR_LOG_REPORT(SCEKERNEL, "Unable to allocate kernel object, too many objects slots in use.");
	return 0;
}

bool KernelObjectPool::IsValid(SceUID handle) const {
	int index = handle - handleOffset;
	if (index < 0 || index >= maxCount)
		return false;
	else
		return occupied[index];
}

void KernelObjectPool::Clear() {
	for (int i = 0; i < maxCount; i++) {
		// brutally clear everything, no validation
		if (occupied[i])
			delete pool[i];
		pool[i] = nullptr;
		occupied[i] = false;
	}
	nextID = initialNextID;
}

void KernelObjectPool::List() {
	for (int i = 0; i < maxCount; i++) {
		if (occupied[i]) {
			char buffer[256];
			if (pool[i]) {
				pool[i]->GetQuickInfo(buffer, 256);
				INFO_LOG(SCEKERNEL, "KO %i: %s \"%s\": %s", i + handleOffset, pool[i]->GetTypeName(), pool[i]->GetName(), buffer);
			} else {
				strcpy(buffer, "WTF? Zero Pointer");
			}
		}
	}
}

int KernelObjectPool::GetCount() const {
	int count = 0;
	for (int i = 0; i < maxCount; i++) {
		if (occupied[i])
			count++;
	}
	return count;
}

void KernelObjectPool::DoState(PointerWrap &p) {
	auto s = p.Section("KernelObjectPool", 1);
	if (!s)
		return;

	int _maxCount = maxCount;
	p.Do(_maxCount);

	if (_maxCount != maxCount) {
		p.SetError(p.ERROR_FAILURE);
		ERROR_LOG(SCEKERNEL, "Unable to load state: different kernel object storage.");
		return;
	}

	if (p.mode == p.MODE_READ) {
		hleCurrentThreadName = nullptr;
		kernelObjects.Clear();
	}

	p.Do(nextID);
	p.DoArray(occupied, maxCount);
	for (int i = 0; i < maxCount; ++i) {
		if (!occupied[i])
			continue;

		int type;
		if (p.mode == p.MODE_READ) {
			p.Do(type);
			pool[i] = CreateByIDType(type);

			// Already logged an error.
			if (pool[i] == nullptr)
				return;

			pool[i]->uid = i + handleOffset;
		} else {
			type = pool[i]->GetIDType();
			p.Do(type);
		}
		pool[i]->DoState(p);
		if (p.error >= p.ERROR_FAILURE)
			break;
	}
}

KernelObject *KernelObjectPool::CreateByIDType(int type) {
	// Used for save states.  This is ugly, but what other way is there?
	switch (type) {
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
	case SCE_KERNEL_TMID_Tlspl_v0:
		return __KernelTlsplObject();
	case PPSSPP_KERNEL_TMID_File:
		return __KernelFileNodeObject();
	case PPSSPP_KERNEL_TMID_DirList:
		return __KernelDirListingObject();
	case SCE_KERNEL_TMID_ThreadEventHandler:
		return __KernelThreadEventHandlerObject();

	default:
		ERROR_LOG(SAVESTATE, "Unable to load state: could not find object type %d.", type);
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

static int sceKernelReferSystemStatus(u32 statusPtr) {
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

static u32 sceKernelReferThreadProfiler(u32 statusPtr) {
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

static int sceKernelReferGlobalProfiler(u32 statusPtr) {
	ERROR_LOG(SCEKERNEL, "UNIMPL sceKernelReferGlobalProfiler(%08x)", statusPtr);
	// Ignore for now
	return 0;
}

const HLEFunction ThreadManForUser[] =
{
	{0X55C20A00, &WrapI_CUUU<sceKernelCreateEventFlag>,              "sceKernelCreateEventFlag",                  'i', "sxxx"    },
	{0X812346E4, &WrapU_IU<sceKernelClearEventFlag>,                 "sceKernelClearEventFlag",                   'x', "ix"      },
	{0XEF9E4C70, &WrapU_I<sceKernelDeleteEventFlag>,                 "sceKernelDeleteEventFlag",                  'x', "i"       },
	{0X1FB15A32, &WrapU_IU<sceKernelSetEventFlag>,                   "sceKernelSetEventFlag",                     'x', "ix"      },
	{0X402FCF22, &WrapI_IUUUU<sceKernelWaitEventFlag>,               "sceKernelWaitEventFlag",                    'i', "ixxpp",  HLE_NOT_IN_INTERRUPT },
	{0X328C546A, &WrapI_IUUUU<sceKernelWaitEventFlagCB>,             "sceKernelWaitEventFlagCB",                  'i', "ixxpp",  HLE_NOT_IN_INTERRUPT },
	{0X30FD48F0, &WrapI_IUUU<sceKernelPollEventFlag>,                "sceKernelPollEventFlag",                    'i', "ixxp"    },
	{0XCD203292, &WrapU_IUU<sceKernelCancelEventFlag>,               "sceKernelCancelEventFlag",                  'x', "ixp"     },
	{0XA66B0120, &WrapU_IU<sceKernelReferEventFlagStatus>,           "sceKernelReferEventFlagStatus",             'x', "ix"      },

	{0X8FFDF9A2, &WrapI_IIU<sceKernelCancelSema>,                    "sceKernelCancelSema",                       'i', "iix"     },
	{0XD6DA4BA1, &WrapI_CUIIU<sceKernelCreateSema>,                  "sceKernelCreateSema",                       'i', "sxiix"   },
	{0X28B6489C, &WrapI_I<sceKernelDeleteSema>,                      "sceKernelDeleteSema",                       'i', "i"       },
	{0X58B1F937, &WrapI_II<sceKernelPollSema>,                       "sceKernelPollSema",                         'i', "ii"      },
	{0XBC6FEBC5, &WrapI_IU<sceKernelReferSemaStatus>,                "sceKernelReferSemaStatus",                  'i', "ix"      },
	{0X3F53E640, &WrapI_II<sceKernelSignalSema>,                     "sceKernelSignalSema",                       'i', "ii"      },
	{0X4E3A1105, &WrapI_IIU<sceKernelWaitSema>,                      "sceKernelWaitSema",                         'i', "iix",    HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0X6D212BAC, &WrapI_IIU<sceKernelWaitSemaCB>,                    "sceKernelWaitSemaCB",                       'i', "iix",    HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },

	{0X60107536, &WrapI_U<sceKernelDeleteLwMutex>,                   "sceKernelDeleteLwMutex",                    'i', "x"       },
	{0X19CFF145, &WrapI_UCUIU<sceKernelCreateLwMutex>,               "sceKernelCreateLwMutex",                    'i', "xsxix"   },
	{0X4C145944, &WrapI_IU<sceKernelReferLwMutexStatusByID>,         "sceKernelReferLwMutexStatusByID",           'i', "ix"      },
	// NOTE: LockLwMutex, UnlockLwMutex, and ReferLwMutexStatus are in Kernel_Library, see sceKernelInterrupt.cpp.
	// The below should not be called directly.
	//{0x71040D5C, nullptr,                                            "_sceKernelTryLockLwMutex",                  '?', ""        },
	//{0x7CFF8CF3, nullptr,                                            "_sceKernelLockLwMutex",                     '?', ""        },
	//{0x31327F19, nullptr,                                            "_sceKernelLockLwMutexCB",                   '?', ""        },
	//{0xBEED3A47, nullptr,                                            "_sceKernelUnlockLwMutex",                   '?', ""        },

	{0XF8170FBE, &WrapI_I<sceKernelDeleteMutex>,                     "sceKernelDeleteMutex",                      'i', "i"       },
	{0XB011B11F, &WrapI_IIU<sceKernelLockMutex>,                     "sceKernelLockMutex",                        'i', "iix",    HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0X5BF4DD27, &WrapI_IIU<sceKernelLockMutexCB>,                   "sceKernelLockMutexCB",                      'i', "iix",    HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0X6B30100F, &WrapI_II<sceKernelUnlockMutex>,                    "sceKernelUnlockMutex",                      'i', "ii"      },
	{0XB7D098C6, &WrapI_CUIU<sceKernelCreateMutex>,                  "sceKernelCreateMutex",                      'i', "sxix"    },
	{0X0DDCD2C9, &WrapI_II<sceKernelTryLockMutex>,                   "sceKernelTryLockMutex",                     'i', "ii"      },
	{0XA9C2CB9A, &WrapI_IU<sceKernelReferMutexStatus>,               "sceKernelReferMutexStatus",                 'i', "ix"      },
	{0X87D9223C, &WrapI_IIU<sceKernelCancelMutex>,                   "sceKernelCancelMutex",                      'i', "iix"     },

	{0XFCCFAD26, &WrapI_I<sceKernelCancelWakeupThread>,              "sceKernelCancelWakeupThread",               'i', "i"       },
	{0X1AF94D03, nullptr,                                            "sceKernelDonateWakeupThread",               '?', ""        },
	{0XEA748E31, &WrapI_UU<sceKernelChangeCurrentThreadAttr>,        "sceKernelChangeCurrentThreadAttr",          'i', "xx"      },
	{0X71BC9871, &WrapI_II<sceKernelChangeThreadPriority>,           "sceKernelChangeThreadPriority",             'i', "ii"      },
	{0X446D8DE6, &WrapI_CUUIUU<sceKernelCreateThread>,               "sceKernelCreateThread",                     'i', "sxxixx", HLE_NOT_IN_INTERRUPT },
	{0X9FA03CD3, &WrapI_I<sceKernelDeleteThread>,                    "sceKernelDeleteThread",                     'i', "i"       },
	{0XBD123D9E, &WrapI_U<sceKernelDelaySysClockThread>,             "sceKernelDelaySysClockThread",              'i', "P",      HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0X1181E963, &WrapI_U<sceKernelDelaySysClockThreadCB>,           "sceKernelDelaySysClockThreadCB",            'i', "P",      HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0XCEADEB47, &WrapI_U<sceKernelDelayThread>,                     "sceKernelDelayThread",                      'i', "x",      HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0X68DA9E36, &WrapI_U<sceKernelDelayThreadCB>,                   "sceKernelDelayThreadCB",                    'i', "x",      HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0XAA73C935, &WrapV_I<sceKernelExitThread>,                      "sceKernelExitThread",                       'v', "i"       },
	{0X809CE29B, &WrapV_I<sceKernelExitDeleteThread>,                "sceKernelExitDeleteThread",                 'v', "i"       },
	{0x94aa61ee, &WrapI_V<sceKernelGetThreadCurrentPriority>,        "sceKernelGetThreadCurrentPriority",         'i', ""        },
	{0X293B45B8, &WrapI_V<sceKernelGetThreadId>,                     "sceKernelGetThreadId",                      'i', "",       HLE_NOT_IN_INTERRUPT },
	{0X3B183E26, &WrapI_I<sceKernelGetThreadExitStatus>,             "sceKernelGetThreadExitStatus",              'i', "i"       },
	{0X52089CA1, &WrapI_I<sceKernelGetThreadStackFreeSize>,          "sceKernelGetThreadStackFreeSize",           'i', "i"       },
	{0XFFC36A14, &WrapU_UU<sceKernelReferThreadRunStatus>,           "sceKernelReferThreadRunStatus",             'x', "xx"      },
	{0X17C1684E, &WrapU_UU<sceKernelReferThreadStatus>,              "sceKernelReferThreadStatus",                'x', "xx"      },
	{0X2C34E053, &WrapI_I<sceKernelReleaseWaitThread>,               "sceKernelReleaseWaitThread",                'i', "i"       },
	{0X75156E8F, &WrapI_I<sceKernelResumeThread>,                    "sceKernelResumeThread",                     'i', "i"       },
	{0X3AD58B8C, &WrapU_V<sceKernelSuspendDispatchThread>,           "sceKernelSuspendDispatchThread",            'x', "",       HLE_NOT_IN_INTERRUPT },
	{0X27E22EC2, &WrapU_U<sceKernelResumeDispatchThread>,            "sceKernelResumeDispatchThread",             'x', "x",      HLE_NOT_IN_INTERRUPT },
	{0X912354A7, &WrapI_I<sceKernelRotateThreadReadyQueue>,          "sceKernelRotateThreadReadyQueue",           'i', "i"       },
	{0X9ACE131E, &WrapI_V<sceKernelSleepThread>,                     "sceKernelSleepThread",                      'i', "",       HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0X82826F70, &WrapI_V<sceKernelSleepThreadCB>,                   "sceKernelSleepThreadCB",                    'i', "",       HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0XF475845D, &WrapI_IIU<sceKernelStartThread>,                   "sceKernelStartThread",                      'i', "iix",    HLE_NOT_IN_INTERRUPT },
	{0X9944F31F, &WrapI_I<sceKernelSuspendThread>,                   "sceKernelSuspendThread",                    'i', "i"       },
	{0X616403BA, &WrapI_I<sceKernelTerminateThread>,                 "sceKernelTerminateThread",                  'i', "i"       },
	{0X383F7BCC, &WrapI_I<sceKernelTerminateDeleteThread>,           "sceKernelTerminateDeleteThread",            'i', "i"       },
	{0X840E8133, &WrapI_IU<sceKernelWaitThreadEndCB>,                "sceKernelWaitThreadEndCB",                  'i', "ix"      },
	{0XD13BDE95, &WrapI_V<sceKernelCheckThreadStack>,                "sceKernelCheckThreadStack",                 'i', ""        },

	{0X94416130, &WrapU_UUUU<sceKernelGetThreadmanIdList>,           "sceKernelGetThreadmanIdList",               'x', "xxxx"    },
	{0X57CF62DD, &WrapU_U<sceKernelGetThreadmanIdType>,              "sceKernelGetThreadmanIdType",               'x', "x"       },
	{0XBC80EC7C, &WrapU_UUU<sceKernelExtendThreadStack>,             "sceKernelExtendThreadStack",                'x', "xxx"     },
	// NOTE: Takes a UID from sceKernelMemory's AllocMemoryBlock and seems thread stack related.
	//{0x28BFD974, nullptr,                                           "ThreadManForUser_28BFD974",                  '?', ""        },

	{0X82BC5777, &WrapU64_V<sceKernelGetSystemTimeWide>,             "sceKernelGetSystemTimeWide",                'X', ""        },
	{0XDB738F35, &WrapI_U<sceKernelGetSystemTime>,                   "sceKernelGetSystemTime",                    'i', "x"       },
	{0X369ED59D, &WrapU_V<sceKernelGetSystemTimeLow>,                "sceKernelGetSystemTimeLow",                 'x', ""        },

	{0X8218B4DD, &WrapI_U<sceKernelReferGlobalProfiler>,             "sceKernelReferGlobalProfiler",              'i', "x"       },
	{0X627E6F3A, &WrapI_U<sceKernelReferSystemStatus>,               "sceKernelReferSystemStatus",                'i', "x"       },
	{0X64D4540E, &WrapU_U<sceKernelReferThreadProfiler>,             "sceKernelReferThreadProfiler",              'x', "x"       },

	//Fifa Street 2 uses alarms
	{0X6652B8CA, &WrapI_UUU<sceKernelSetAlarm>,                      "sceKernelSetAlarm",                         'i', "xxx"     },
	{0XB2C25152, &WrapI_UUU<sceKernelSetSysClockAlarm>,              "sceKernelSetSysClockAlarm",                 'i', "xxx"     },
	{0X7E65B999, &WrapI_I<sceKernelCancelAlarm>,                     "sceKernelCancelAlarm",                      'i', "i"       },
	{0XDAA3F564, &WrapI_IU<sceKernelReferAlarmStatus>,               "sceKernelReferAlarmStatus",                 'i', "ix"      },

	{0XBA6B92E2, &WrapI_UUU<sceKernelSysClock2USec>,                 "sceKernelSysClock2USec",                    'i', "xxx"     },
	{0X110DEC9A, &WrapI_UU<sceKernelUSec2SysClock>,                  "sceKernelUSec2SysClock",                    'i', "xx"      },
	{0XC8CD158C, &WrapU64_U<sceKernelUSec2SysClockWide>,             "sceKernelUSec2SysClockWide",                'X', "x"       },
	{0XE1619D7C, &WrapI_UUUU<sceKernelSysClock2USecWide>,            "sceKernelSysClock2USecWide",                'i', "xxxx"    },

	{0X278C0DF5, &WrapI_IU<sceKernelWaitThreadEnd>,                  "sceKernelWaitThreadEnd",                    'i', "ix"      },
	{0XD59EAD2F, &WrapI_I<sceKernelWakeupThread>,                    "sceKernelWakeupThread",                     'i', "i"       }, //AI Go, audio?

	{0x0C106E53, &WrapI_CIUUU<sceKernelRegisterThreadEventHandler>,  "sceKernelRegisterThreadEventHandler",       'i', "sixxx",  },
	{0x72F3C145, &WrapI_I<sceKernelReleaseThreadEventHandler>,       "sceKernelReleaseThreadEventHandler",        'i', "i"       },
	{0x369EEB6B, &WrapI_IU<sceKernelReferThreadEventHandlerStatus>,  "sceKernelReferThreadEventHandlerStatus",    'i', "ip"      },

	{0x349d6d6c, &sceKernelCheckCallback,                            "sceKernelCheckCallback",                    'i', ""        },
	{0XE81CAF8F, &WrapI_CUU<sceKernelCreateCallback>,                "sceKernelCreateCallback",                   'i', "sxx"     },
	{0XEDBA5844, &WrapI_I<sceKernelDeleteCallback>,                  "sceKernelDeleteCallback",                   'i', "i"       },
	{0XC11BA8C4, &WrapI_II<sceKernelNotifyCallback>,                 "sceKernelNotifyCallback",                   'i', "ii"      },
	{0XBA4051D6, &WrapI_I<sceKernelCancelCallback>,                  "sceKernelCancelCallback",                   'i', "i"       },
	{0X2A3D44FF, &WrapI_I<sceKernelGetCallbackCount>,                "sceKernelGetCallbackCount",                 'i', "i"       },
	{0X730ED8BC, &WrapI_IU<sceKernelReferCallbackStatus>,            "sceKernelReferCallbackStatus",              'i', "ip"      },

	{0X8125221D, &WrapI_CUU<sceKernelCreateMbx>,                     "sceKernelCreateMbx",                        'i', "sxx"     },
	{0X86255ADA, &WrapI_I<sceKernelDeleteMbx>,                       "sceKernelDeleteMbx",                        'i', "i"       },
	{0XE9B3061E, &WrapI_IU<sceKernelSendMbx>,                        "sceKernelSendMbx",                          'i', "ix"      },
	{0X18260574, &WrapI_IUU<sceKernelReceiveMbx>,                    "sceKernelReceiveMbx",                       'i', "ixx",    HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0XF3986382, &WrapI_IUU<sceKernelReceiveMbxCB>,                  "sceKernelReceiveMbxCB",                     'i', "ixx",    HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0X0D81716A, &WrapI_IU<sceKernelPollMbx>,                        "sceKernelPollMbx",                          'i', "ix"      },
	{0X87D4DD36, &WrapI_IU<sceKernelCancelReceiveMbx>,               "sceKernelCancelReceiveMbx",                 'i', "ix"      },
	{0XA8E8C846, &WrapI_IU<sceKernelReferMbxStatus>,                 "sceKernelReferMbxStatus",                   'i', "ix"      },

	{0X7C0DC2A0, &WrapI_CIUUU<sceKernelCreateMsgPipe>,               "sceKernelCreateMsgPipe",                    'i', "sixxx"   },
	{0XF0B7DA1C, &WrapI_I<sceKernelDeleteMsgPipe>,                   "sceKernelDeleteMsgPipe",                    'i', "i"       },
	{0X876DBFAD, &WrapI_IUUUUU<sceKernelSendMsgPipe>,                "sceKernelSendMsgPipe",                      'i', "ixxxxx"  },
	{0X7C41F2C2, &WrapI_IUUUUU<sceKernelSendMsgPipeCB>,              "sceKernelSendMsgPipeCB",                    'i', "ixxxxx"  },
	{0X884C9F90, &WrapI_IUUUU<sceKernelTrySendMsgPipe>,              "sceKernelTrySendMsgPipe",                   'i', "ixxxx"   },
	{0X74829B76, &WrapI_IUUUUU<sceKernelReceiveMsgPipe>,             "sceKernelReceiveMsgPipe",                   'i', "ixxxxx"  },
	{0XFBFA697D, &WrapI_IUUUUU<sceKernelReceiveMsgPipeCB>,           "sceKernelReceiveMsgPipeCB",                 'i', "ixxxxx"  },
	{0XDF52098F, &WrapI_IUUUU<sceKernelTryReceiveMsgPipe>,           "sceKernelTryReceiveMsgPipe",                'i', "ixxxx"   },
	{0X349B864D, &WrapI_IUU<sceKernelCancelMsgPipe>,                 "sceKernelCancelMsgPipe",                    'i', "ixx"     },
	{0X33BE4024, &WrapI_IU<sceKernelReferMsgPipeStatus>,             "sceKernelReferMsgPipeStatus",               'i', "ix"      },

	{0X56C039B5, &WrapI_CIUUU<sceKernelCreateVpl>,                   "sceKernelCreateVpl",                        'i', "sixxx"   },
	{0X89B3D48C, &WrapI_I<sceKernelDeleteVpl>,                       "sceKernelDeleteVpl",                        'i', "i"       },
	{0XBED27435, &WrapI_IUUU<sceKernelAllocateVpl>,                  "sceKernelAllocateVpl",                      'i', "ixxx",   HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0XEC0A693F, &WrapI_IUUU<sceKernelAllocateVplCB>,                "sceKernelAllocateVplCB",                    'i', "ixxx",   HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0XAF36D708, &WrapI_IUU<sceKernelTryAllocateVpl>,                "sceKernelTryAllocateVpl",                   'i', "ixx"     },
	{0XB736E9FF, &WrapI_IU<sceKernelFreeVpl>,                        "sceKernelFreeVpl",                          'i', "ix"      },
	{0X1D371B8A, &WrapI_IU<sceKernelCancelVpl>,                      "sceKernelCancelVpl",                        'i', "ix"      },
	{0X39810265, &WrapI_IU<sceKernelReferVplStatus>,                 "sceKernelReferVplStatus",                   'i', "ix"      },

	{0XC07BB470, &WrapI_CUUUUU<sceKernelCreateFpl>,                  "sceKernelCreateFpl",                        'i', "sxxxxx"  },
	{0XED1410E0, &WrapI_I<sceKernelDeleteFpl>,                       "sceKernelDeleteFpl",                        'i', "i"       },
	{0XD979E9BF, &WrapI_IUU<sceKernelAllocateFpl>,                   "sceKernelAllocateFpl",                      'i', "ixx",    HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0XE7282CB6, &WrapI_IUU<sceKernelAllocateFplCB>,                 "sceKernelAllocateFplCB",                    'i', "ixx",    HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED },
	{0X623AE665, &WrapI_IU<sceKernelTryAllocateFpl>,                 "sceKernelTryAllocateFpl",                   'i', "ix"      },
	{0XF6414A71, &WrapI_IU<sceKernelFreeFpl>,                        "sceKernelFreeFpl",                          'i', "ix"      },
	{0XA8AA591F, &WrapI_IU<sceKernelCancelFpl>,                      "sceKernelCancelFpl",                        'i', "ix"      },
	{0XD8199E4C, &WrapI_IU<sceKernelReferFplStatus>,                 "sceKernelReferFplStatus",                   'i', "ix"      },

	{0X20FFF560, &WrapU_CU<sceKernelCreateVTimer>,                   "sceKernelCreateVTimer",                     'x', "sx",     HLE_NOT_IN_INTERRUPT },
	{0X328F9E52, &WrapU_I<sceKernelDeleteVTimer>,                    "sceKernelDeleteVTimer",                     'x', "i",      HLE_NOT_IN_INTERRUPT },
	{0XC68D9437, &WrapU_I<sceKernelStartVTimer>,                     "sceKernelStartVTimer",                      'x', "i"       },
	{0XD0AEEE87, &WrapU_I<sceKernelStopVTimer>,                      "sceKernelStopVTimer",                       'x', "i"       },
	{0XD2D615EF, &WrapU_I<sceKernelCancelVTimerHandler>,             "sceKernelCancelVTimerHandler",              'x', "i"       },
	{0XB3A59970, &WrapU_IU<sceKernelGetVTimerBase>,                  "sceKernelGetVTimerBase",                    'x', "ix"      },
	{0XB7C18B77, &WrapU64_I<sceKernelGetVTimerBaseWide>,             "sceKernelGetVTimerBaseWide",                'X', "i"       },
	{0X034A921F, &WrapU_IU<sceKernelGetVTimerTime>,                  "sceKernelGetVTimerTime",                    'x', "ix"      },
	{0XC0B3FFD2, &WrapU64_I<sceKernelGetVTimerTimeWide>,             "sceKernelGetVTimerTimeWide",                'X', "i"       },
	{0X5F32BEAA, &WrapU_IU<sceKernelReferVTimerStatus>,              "sceKernelReferVTimerStatus",                'x', "ix"      },
	{0X542AD630, &WrapU_IU<sceKernelSetVTimerTime>,                  "sceKernelSetVTimerTime",                    'x', "ix"      },
	{0XFB6425C3, &WrapU64_IU64<sceKernelSetVTimerTimeWide>,          "sceKernelSetVTimerTimeWide",                'X', "iX"      },
	{0XD8B299AE, &WrapU_IUUU<sceKernelSetVTimerHandler>,             "sceKernelSetVTimerHandler",                 'x', "ixxx"    },
	{0X53B00E9A, &WrapU_IU64UU<sceKernelSetVTimerHandlerWide>,       "sceKernelSetVTimerHandlerWide",             'x', "iXxx"    },

	{0X8DAFF657, &WrapI_CUUUUU<sceKernelCreateTlspl>,                "sceKernelCreateTlspl",                      'i', "sxxxxx"  },
	{0X32BF938E, &WrapI_I<sceKernelDeleteTlspl>,                     "sceKernelDeleteTlspl",                      'i', "i"       },
	{0X721067F3, &WrapI_IU<sceKernelReferTlsplStatus>,               "sceKernelReferTlsplStatus",                 'i', "ix"      },
	// Not completely certain about args.
	{0X4A719FB2, &WrapI_I<sceKernelFreeTlspl>,                       "sceKernelFreeTlspl",                        'i', "i"       },
	// Internal.  Takes (uid, &addr) as parameters... probably.
	//{0x65F54FFB, nullptr,                                            "_sceKernelAllocateTlspl",                   'v', ""        },
	// NOTE: sceKernelGetTlsAddr is in Kernel_Library, see sceKernelInterrupt.cpp.

	// Not sure if these should be hooked up. See below.
	{0x0E927AED, &_sceKernelReturnFromTimerHandler,                  "_sceKernelReturnFromTimerHandler",          'v', ""        },
	{0X532A522E, &WrapV_I<_sceKernelExitThread>,                     "_sceKernelExitThread",                      'v', "i"       },

	// Shouldn't hook this up. No games should import this function manually and call it.
	// {0x6E9EA350, _sceKernelReturnFromCallback,"_sceKernelReturnFromCallback"},
};

const HLEFunction ThreadManForKernel[] =
{
	{0xCEADEB47, &WrapI_U<sceKernelDelayThread>,                     "sceKernelDelayThread",                      'i', "x",      HLE_NOT_IN_INTERRUPT | HLE_NOT_DISPATCH_SUSPENDED | HLE_KERNEL_SYSCALL },
	{0x446D8DE6, &WrapI_CUUIUU<sceKernelCreateThread>,               "sceKernelCreateThread",                     'i', "sxxixx", HLE_NOT_IN_INTERRUPT | HLE_KERNEL_SYSCALL },
	{0xF475845D, &WrapI_IIU<sceKernelStartThread>,                   "sceKernelStartThread",                      'i', "iix",    HLE_NOT_IN_INTERRUPT | HLE_KERNEL_SYSCALL },
};

void Register_ThreadManForUser()
{
	RegisterModule("ThreadManForUser", ARRAY_SIZE(ThreadManForUser), ThreadManForUser);
}


const HLEFunction LoadExecForUser[] =
{
	{0X05572A5F, &WrapV_V<sceKernelExitGame>,                        "sceKernelExitGame",                         'v', ""        },
	{0X4AC57943, &WrapI_I<sceKernelRegisterExitCallback>,            "sceKernelRegisterExitCallback",             'i', "i"       },
	{0XBD2F1094, &WrapI_CU<sceKernelLoadExec>,                       "sceKernelLoadExec",                         'i', "sx"      },
	{0X2AC9954B, &WrapV_V<sceKernelExitGameWithStatus>,              "sceKernelExitGameWithStatus",               'v', ""        },
	{0X362A956B, &WrapI_V<LoadExecForUser_362A956B>,                 "LoadExecForUser_362A956B",                  'i', ""        },
	{0X8ADA38D3, nullptr,                                            "LoadExecForUser_8ADA38D3",                  '?', ""        },
};

void Register_LoadExecForUser()
{
	RegisterModule("LoadExecForUser", ARRAY_SIZE(LoadExecForUser), LoadExecForUser);
}
 
const HLEFunction LoadExecForKernel[] =
{
	{0x4AC57943, &WrapI_I<sceKernelRegisterExitCallback>,            "sceKernelRegisterExitCallback",             'i', "i",      HLE_KERNEL_SYSCALL },
	{0XA3D5E142, nullptr,                                            "LoadExecForKernel_a3d5e142",                '?', ""        },
	{0X28D0D249, &WrapI_CU<sceKernelLoadExec>,                       "sceKernelLoadExec_28D0D249",                'i', "sx"      },
};
 
void Register_LoadExecForKernel()
{
	RegisterModule("LoadExecForKernel", ARRAY_SIZE(LoadExecForKernel), LoadExecForKernel);
}

const HLEFunction ExceptionManagerForKernel[] =
{
	{0X3FB264FC, nullptr,                                            "sceKernelRegisterExceptionHandler",         '?', ""        },
	{0X5A837AD4, nullptr,                                            "sceKernelRegisterPriorityExceptionHandler", '?', ""        },
	{0x565C0B0E, &WrapI_V<sceKernelRegisterDefaultExceptionHandler>, "sceKernelRegisterDefaultExceptionHandler",  'i', "",       HLE_KERNEL_SYSCALL },
	{0X1AA6CFFA, nullptr,                                            "sceKernelReleaseExceptionHandler",          '?', ""        },
	{0XDF83875E, nullptr,                                            "sceKernelGetActiveDefaultExceptionHandler", '?', ""        },
	{0X291FF031, nullptr,                                            "sceKernelReleaseDefaultExceptionHandler",   '?', ""        },
	{0X15ADC862, nullptr,                                            "sceKernelRegisterNmiHandler",               '?', ""        },
	{0XB15357C9, nullptr,                                            "sceKernelReleaseNmiHandler",                '?', ""        },
};

void Register_ExceptionManagerForKernel()
{
	RegisterModule("ExceptionManagerForKernel", ARRAY_SIZE(ExceptionManagerForKernel), ExceptionManagerForKernel);
}

// Seen in some homebrew
const HLEFunction UtilsForKernel[] = {
	{0xC2DF770E, WrapI_UI<sceKernelIcacheInvalidateRange>,           "sceKernelIcacheInvalidateRange",            '?', "",       HLE_KERNEL_SYSCALL },
	{0X78934841, nullptr,                                            "sceKernelGzipDecompress",                   '?', ""        },
	{0XE8DB3CE6, nullptr,                                            "sceKernelDeflateDecompress",                '?', ""        },
	{0X840259F1, nullptr,                                            "sceKernelUtilsSha1Digest",                  '?', ""        },
	{0X9E5C5086, nullptr,                                            "sceKernelUtilsMd5BlockInit",                '?', ""        },
	{0X61E1E525, nullptr,                                            "sceKernelUtilsMd5BlockUpdate",              '?', ""        },
	{0XB8D24E78, nullptr,                                            "sceKernelUtilsMd5BlockResult",              '?', ""        },
	{0XC8186A58, nullptr,                                            "sceKernelUtilsMd5Digest",                   '?', ""        },
	{0X6C6887EE, nullptr,                                            "UtilsForKernel_6C6887EE",                   '?', ""        },
	{0X91E4F6A7, nullptr,                                            "sceKernelLibcClock",                        '?', ""        },
	{0X27CC57F0, nullptr,                                            "sceKernelLibcTime",                         '?', ""        },
	{0X79D1C3FA, nullptr,                                            "sceKernelDcacheWritebackAll",               '?', ""        },
	{0X3EE30821, nullptr,                                            "sceKernelDcacheWritebackRange",             '?', ""        },
	{0X34B9FA9E, nullptr,                                            "sceKernelDcacheWritebackInvalidateRange",   '?', ""        },
	{0XB435DEC5, nullptr,                                            "sceKernelDcacheWritebackInvalidateAll",     '?', ""        },
	{0XBFA98062, nullptr,                                            "sceKernelDcacheInvalidateRange",            '?', ""        },
	{0X920F104A, nullptr,                                            "sceKernelIcacheInvalidateAll",              '?', ""        },
	{0XE860E75E, nullptr,                                            "sceKernelUtilsMt19937Init",                 '?', ""        },
	{0X06FB8A63, nullptr,                                            "sceKernelUtilsMt19937UInt",                 '?', ""        },
};


void Register_UtilsForKernel()
{
	RegisterModule("UtilsForKernel", ARRAY_SIZE(UtilsForKernel), UtilsForKernel);
}

void Register_ThreadManForKernel()
{
	RegisterModule("ThreadManForKernel", ARRAY_SIZE(ThreadManForKernel), ThreadManForKernel);		
}
