// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "../MIPS/MIPSCodeUtils.h"
#include "../MIPS/MIPSInt.h"

#include "../FileSystems/FileSystem.h"
#include "../FileSystems/MetaFileSystem.h"
#include "../PSPLoaders.h"
#include "../../Core/CoreTiming.h"
#include "../../Core/System.h"


#include "__sceAudio.h"
#include "sceAudio.h"
#include "sceDisplay.h"
#include "sceGe.h"
#include "sceIo.h"
#include "sceKernel.h"
#include "sceKernelAlarm.h"
#include "sceKernelCallback.h"
#include "sceKernelInterrupt.h"
#include "sceKernelThread.h"
#include "sceKernelMemory.h"
#include "sceKernelMutex.h"
#include "sceKernelMbx.h"
#include "sceKernelMsgPipe.h"
#include "sceKernelInterrupt.h"
#include "sceKernelSemaphore.h"
#include "sceKernelEventFlag.h"
#include "sceKernelVTimer.h"
#include "sceKernelTime.h"
#include "sceUtility.h"
#include "sceUmd.h"

extern MetaFileSystem pspFileSystem;

/*
17: [MIPS32 R4K 00000000 ]: Loader: Type: 1 Vaddr: 00000000 Filesz: 2856816 Memsz: 2856816 
18: [MIPS32 R4K 00000000 ]: Loader: Loadable Segment Copied to 0898dab0, size 002b9770
19: [MIPS32 R4K 00000000 ]: Loader: Type: 1 Vaddr: 002b9770 Filesz: 14964 Memsz: 733156 
20: [MIPS32 R4K 00000000 ]: Loader: Loadable Segment Copied to 08c47220, size 000b2fe4
*/

static bool kernelRunning = false;

void __KernelInit()
{
	if (kernelRunning)
	{
		ERROR_LOG(HLE, "Can't init kernel when kernel is running");
		return;
	}

	__KernelMemoryInit();
	__KernelThreadingInit();
	__IoInit();
	__AudioInit();
	__DisplayInit();
	__InterruptsInit();
	__GeInit();
	__UtilityInit();
	__UmdInit();

	kernelRunning = true;
	INFO_LOG(HLE, "Kernel initialized.");
}

void __KernelShutdown()
{
	if (!kernelRunning)
	{
		ERROR_LOG(HLE, "Can't shut down kernel - not running");
		return;
	}
	kernelObjects.List();
	INFO_LOG(HLE, "Shutting down kernel - %i kernel objects alive", kernelObjects.GetCount());
	kernelObjects.Clear();

	__GeShutdown();
	__AudioShutdown();
	__IoShutdown();
	__InterruptsShutdown();
	__KernelThreadingShutdown();
	__KernelMemoryShutdown();

	CoreTiming::ClearPendingEvents();
	CoreTiming::UnregisterAllEvents();

	kernelRunning = false;
}

void sceKernelExitGame()
{
	INFO_LOG(HLE,"sceKernelExitGame");
	if (PSP_CoreParameter().headLess)
		exit(0);
	else
		PanicAlert("Game exited");
	Core_Stop();
	RETURN(0);
}

void sceKernelRegisterExitCallback()
{
	u32 cbid = PARAM(0);
	ERROR_LOG(HLE,"UNIMPL sceKernelRegisterExitCallback(%i)", cbid);
	RETURN(0);
}

// TODO: What?
void sceKernelDevkitVersion()
{
	ERROR_LOG(HLE,"unimpl sceKernelDevkitVersion");
	RETURN(1);
}


//////////////////////////////////////////////////////////////////////////
// DEBUG
//////////////////////////////////////////////////////////////////////////


void sceKernelRegisterKprintfHandler()
{
	ERROR_LOG(HLE,"UNIMPL sceKernelRegisterKprintfHandler()");
	RETURN(0);
}
void sceKernelRegisterDefaultExceptionHandler()
{
	ERROR_LOG(HLE,"UNIMPL sceKernelRegisterDefaultExceptionHandler()");
	RETURN(0);
}

void sceKernelSetGPO()
{
	// Sets debug LEDs.
	INFO_LOG(HLE,"sceKernelSetGPO(%02x)", PARAM(0));
}

void sceKernelGetGPI()
{
	// Always returns 0 on production systems.
	INFO_LOG(HLE,"0=sceKernelGetGPI()");
	RETURN(0);
}

void sceKernelDcacheWritebackAll()
{
	//RETURN(0);
}
void sceKernelDcacheWritebackRange()
{
	//RETURN(0);
}
void sceKernelDcacheWritebackInvalidateRange()
{
	//RETURN(0);
}
void sceKernelDcacheWritebackInvalidateAll()
{
	// RETURN(0)
}

KernelObjectPool kernelObjects;

KernelObjectPool::KernelObjectPool()
{
	memset(occupied, 0, sizeof(bool)*maxCount);
}

SceUID KernelObjectPool::Create(KernelObject *obj)
{
	for (int i=0; i<maxCount; i++)
	{
		if (!occupied[i])
		{
			occupied[i]=true;
			pool[i] = obj;
			pool[i]->uid = i + handleOffset;
			return i + handleOffset;
		}
	}
	_dbg_assert_(HLE, 0);
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
}
KernelObject *&KernelObjectPool::operator [](SceUID handle)
{
	_dbg_assert_msg_(HLE, IsValid(handle), "GRABBING UNALLOCED KERNEL OBJ");
	return pool[handle - handleOffset];
}

void KernelObjectPool::List()
{
	for (int i=0; i<maxCount; i++)
	{
		if (occupied[i])
		{
			char buffer[256];
			if (pool[i])
			{
				pool[i]->GetQuickInfo(buffer,256);
			}
			else
			{
				strcpy(buffer,"WTF? Zero Pointer");
			}
			INFO_LOG(HLE, "KO %i: %s \"%s\": %s", i + handleOffset, pool[i]->GetTypeName(), pool[i]->GetName(), buffer);
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


void sceKernelIcacheInvalidateAll()
{
	DEBUG_LOG(CPU, "Icache cleared - should clear JIT someday");
	RETURN(0);
}


const HLEFunction ThreadManForUser[] =
{
	{0x55C20A00,sceKernelCreateEventFlag, "sceKernelCreateEventFlag"},
	{0x812346E4,sceKernelClearEventFlag,	"sceKernelClearEventFlag"},
	{0xEF9E4C70,sceKernelDeleteEventFlag, "sceKernelDeleteEventFlag"},
	{0x1fb15a32,sceKernelSetEventFlag,		"sceKernelSetEventFlag"},
	{0x402FCF22,sceKernelWaitEventFlag,	 "sceKernelWaitEventFlag"},
	{0x328C546A,sceKernelWaitEventFlagCB, "sceKernelWaitEventFlagCB"},
	{0x30FD48F0,sceKernelPollEventFlag,	 "sceKernelPollEventFlag"},
	{0xCD203292,sceKernelCancelEventFlag, "sceKernelCancelEventFlag"},
	{0xA66B0120,sceKernelReferEventFlagStatus,"sceKernelReferEventFlagStatus"},

	{0x8FFDF9A2,sceKernelCancelSema,		"sceKernelCancelSema"},
	{0xD6DA4BA1,sceKernelCreateSema,		"sceKernelCreateSema"},
	{0x28b6489c,sceKernelDeleteSema,		"sceKernelDeleteSema"},
	{0x58b1f937,sceKernelPollSema,			"sceKernelPollSema"},
	{0xBC6FEBC5,sceKernelReferSemaStatus,"sceKernelReferSemaStatus"},
	{0x3F53E640,sceKernelSignalSema,		"sceKernelSignalSema"},
	{0x4E3A1105,sceKernelWaitSema,			"sceKernelWaitSema"},
	{0x6d212bac,sceKernelWaitSemaCB,		 "sceKernelWaitSemaCB"},

	{0x60107536,0,"sceKernelDeleteLwMutex"},
	{0x19CFF145,0,"sceKernelCreateLwMutex"},
	{0xf8170fbe,&WrapU_U<sceKernelDeleteMutex>,"sceKernelDeleteMutex"},
	{0xB011B11F,&WrapU_UUU<sceKernelLockMutex>,"sceKernelLockMutex"},
	{0x5bf4dd27,&WrapU_UUU<sceKernelLockMutexCB>,"sceKernelLockMutexCB"},
	{0x6b30100f,&WrapU_UU<sceKernelUnlockMutex>,"sceKernelUnlockMutex"},
	{0xb7d098c6,&WrapU_CUU<sceKernelCreateMutex>,"sceKernelCreateMutex"},
	// NOTE: LockLwMutex and UnlockLwMutex are in Kernel_Library, see sceKernelInterrupt.cpp.

	{0xFCCFAD26,0,"sceKernelCancelWakeupThread"},
	{0xea748e31,sceKernelChangeCurrentThreadAttr,"sceKernelChangeCurrentThreadAttr"},
	{0x71bc9871,sceKernelChangeThreadPriority,"sceKernelChangeThreadPriority"},
	{0x446D8DE6,sceKernelCreateThread,"sceKernelCreateThread"},
	{0x9fa03cd3,sceKernelDeleteThread,"sceKernelDeleteThread"},
	{0xBD123D9E,0,"sceKernelDelaySysClockThread"},
	{0x1181E963,0,"sceKernelDelaySysClockThreadCB"},
	{0xceadeb47,sceKernelDelayThread,"sceKernelDelayThread"},
	{0x68da9e36,sceKernelDelayThreadCB,"sceKernelDelayThreadCB"},
	{0xaa73c935,sceKernelExitThread,"sceKernelExitThread"},
	{0x809ce29b,sceKernelExitDeleteThread,"sceKernelExitDeleteThread"},
	{0x94aa61ee,0,"sceKernelGetThreadCurrentPriority"},
	{0x293b45b8,sceKernelGetThreadId,"sceKernelGetThreadId"},
	{0x3B183E26,sceKernelGetThreadExitStatus,"sceKernelGetThreadExitStatus"},
	{0x52089CA1,sceKernelGetThreadStackFreeSize,"sceKernelGetThreadStackFreeSize"},
	{0xFFC36A14,0,"sceKernelReferThreadRunStatus"},
	{0x17c1684e,sceKernelReferThreadStatus,"sceKernelReferThreadStatus"},
	{0x2C34E053,0,"sceKernelReleaseWaitThread"},
	{0x75156e8f,sceKernelResumeThread,"sceKernelResumeThread"},
	{0x27e22ec2,0,"sceKernelResumeDispatchThread"},
	{0x912354a7,sceKernelRotateThreadReadyQueue,"sceKernelRotateThreadReadyQueue"},
	{0x9ACE131E,sceKernelSleepThread,"sceKernelSleepThread"},
	{0x82826f70,sceKernelSleepThreadCB,"sceKernelSleepThreadCB"},
	{0xF475845D,sceKernelStartThread,"sceKernelStartThread"},
	{0x9944f31f,sceKernelSuspendThread,"sceKernelSuspendThread"},
	{0x3ad58b8c,0,"sceKernelSuspendDispatchThread"},
	{0x616403ba,0,"sceKernelTerminateThread"},
	{0x383f7bcc,sceKernelTerminateDeleteThread,"sceKernelTerminateDeleteThread"},
	{0x840E8133,sceKernelWaitThreadEndCB,"sceKernelWaitThreadEndCB"},
	{0xd13bde95,sceKernelCheckThreadStack,"sceKernelCheckThreadStack"},

	{0x94416130,0,"sceKernelGetThreadmanIdList"},
	{0x57CF62DD,sceKernelGetThreadmanIdType,"sceKernelGetThreadmanIdType"},

	{0x20fff560,sceKernelCreateVTimer,"sceKernelCreateVTimer"},
	{0x328F9E52,0,"sceKernelDeleteVTimer"},
	{0xc68d9437,sceKernelStartVTimer,"sceKernelStartVTimer"},
	{0xD0AEEE87,0,"sceKernelStopVTimer"},
	{0xD2D615EF,0,"sceKernelCancelVTimerHandler"},
	{0xB3A59970,0,"sceKernelGetVTimerBase"},
	{0xB7C18B77,0,"sceKernelGetVTimerBaseWide"},
	{0x034A921F,0,"sceKernelGetVTimerTime"},
	{0xC0B3FFD2,0,"sceKernelGetVTimerTimeWide"},
	{0x5F32BEAA,0,"sceKernelReferVTimerStatus"},
	{0x542AD630,0,"sceKernelSetVTimerTime"},
	{0xFB6425C3,0,"sceKernelSetVTimerTimeWide"},
	{0xd8b299ae,sceKernelSetVTimerHandler,"sceKernelSetVTimerHandler"},
	{0x53B00E9A,0,"sceKernelSetVTimerHandlerWide"},

	{0x82BC5777,sceKernelGetSystemTimeWide,"sceKernelGetSystemTimeWide"},
	{0xdb738f35,sceKernelGetSystemTime,"sceKernelGetSystemTime"},
	{0x369ed59d,sceKernelGetSystemTimeLow,"sceKernelGetSystemTimeLow"},

	{0x8218B4DD,0,"sceKernelReferGlobalProfiler"},
	{0x627E6F3A,0,"sceKernelReferSystemStatus"},
	{0x64D4540E,0,"sceKernelReferThreadProfiler"},

	//Fifa Street 2 uses alarms
	{0x6652b8ca,sceKernelSetAlarm,"sceKernelSetAlarm"},
	{0xB2C25152,sceKernelSetSysClockAlarm,"sceKernelSetSysClockAlarm"},
	{0x7e65b999,sceKernelCancelAlarm,"sceKernelCancelAlarm"},
	{0xDAA3F564,sceKernelReferAlarmStatus,"sceKernelReferAlarmStatus"},

	{0xba6b92e2,sceKernelSysClock2USec,"sceKernelSysClock2USec"},
	{0x110DEC9A,0,"sceKernelUSec2SysClock"},
	{0xC8CD158C,0,"sceKernelUSec2SysClockWide"},
	{0xE1619D7C,sceKernelSysClock2USecWide,"sceKernelSysClock2USecWide"},

	{0x110dec9a,sceKernelUSec2SysClock,"sceKernelUSec2SysClock"},
	{0x278C0DF5,sceKernelWaitThreadEnd,"sceKernelWaitThreadEnd"},
	{0xd59ead2f,sceKernelWakeupThread,"sceKernelWakeupThread"}, //AI Go, audio?

	{0x0C106E53,0,"sceKernelRegisterThreadEventHandler"},
	{0x72F3C145,0,"sceKernelReleaseThreadEventHandler"},
	{0x369EEB6B,0,"sceKernelReferThreadEventHandlerStatus"},

	{0x349d6d6c,sceKernelCheckCallback,"sceKernelCheckCallback"},
	{0xE81CAF8F,sceKernelCreateCallback,"sceKernelCreateCallback"},
	{0xEDBA5844,sceKernelDeleteCallback,"sceKernelDeleteCallback"},
	{0xC11BA8C4,sceKernelNotifyCallback,"sceKernelNotifyCallback"},
	{0xBA4051D6,sceKernelCancelCallback,"sceKernelCancelCallback"},
	{0x2A3D44FF,sceKernelGetCallbackCount,"sceKernelGetCallbackCount"},
	{0x349D6D6C,sceKernelCheckCallback,"sceKernelCheckCallback"},
	{0x730ED8BC,sceKernelReferCallbackStatus,"sceKernelReferCallbackStatus"},

	{0x8125221D,sceKernelCreateMbx,"sceKernelCreateMbx"},
	{0x86255ADA,sceKernelDeleteMbx,"sceKernelDeleteMbx"},
	{0xE9B3061E,sceKernelSendMbx,"sceKernelSendMbx"},
	{0x18260574,sceKernelReceiveMbx,"sceKernelReceiveMbx"},
	{0xF3986382,sceKernelReceiveMbxCB,"sceKernelReceiveMbxCB"},
	{0x0D81716A,sceKernelPollMbx,"sceKernelPollMbx"},
	{0x87D4DD36,sceKernelCancelReceiveMbx,"sceKernelCancelReceiveMbx"},
	{0xA8E8C846,sceKernelReferMbxStatus,"sceKernelReferMbxStatus"},

	{0x7C0DC2A0,sceKernelCreateMsgPipe,"sceKernelCreateMsgPipe"},
	{0xF0B7DA1C,sceKernelDeleteMsgPipe,"sceKernelDeleteMsgPipe"},
	{0x876DBFAD,sceKernelSendMsgPipe,"sceKernelSendMsgPipe"},
	{0x7C41F2C2,sceKernelSendMsgPipeCB,"sceKernelSendMsgPipeCB"},
	{0x884C9F90,sceKernelTrySendMsgPipe,"sceKernelTrySendMsgPipe"},
	{0x74829B76,sceKernelReceiveMsgPipe,"sceKernelReceiveMsgPipe"},
	{0xFBFA697D,sceKernelReceiveMsgPipeCB,"sceKernelReceiveMsgPipeCB"},
	{0xDF52098F,sceKernelTryReceiveMsgPipe,"sceKernelTryReceiveMsgPipe"},
	{0x349B864D,sceKernelCancelMsgPipe,"sceKernelCancelMsgPipe"},
	{0x33BE4024,sceKernelReferMsgPipeStatus,"sceKernelReferMsgPipeStatus"},

	{0x56C039B5,sceKernelCreateVpl,"sceKernelCreateVpl"},
	{0x89B3D48C,sceKernelDeleteVpl,"sceKernelDeleteVpl"},
	{0xBED27435,sceKernelAllocateVpl,"sceKernelAllocateVpl"},
	{0xEC0A693F,sceKernelAllocateVplCB,"sceKernelAllocateVplCB"},
	{0xAF36D708,sceKernelTryAllocateVpl,"sceKernelTryAllocateVpl"},
	{0xB736E9FF,sceKernelFreeVpl,"sceKernelFreeVpl"},
	{0x1D371B8A,sceKernelCancelVpl,"sceKernelCancelVpl"},
	{0x39810265,sceKernelReferVplStatus,"sceKernelReferVplStatus"},

	{0xC07BB470,sceKernelCreateFpl,"sceKernelCreateFpl"},
	{0xED1410E0,sceKernelDeleteFpl,"sceKernelDeleteFpl"},
	{0xD979E9BF,sceKernelAllocateFpl,"sceKernelAllocateFpl"},
	{0xE7282CB6,sceKernelAllocateFplCB,"sceKernelAllocateFplCB"},
	{0x623AE665,sceKernelTryAllocateFpl,"sceKernelTryAllocateFpl"},
	{0xF6414A71,sceKernelFreeFpl,"sceKernelFreeFpl"},
	{0xA8AA591F,sceKernelCancelFpl,"sceKernelCancelFpl"},
	{0xD8199E4C,sceKernelReferFplStatus,"sceKernelReferFplStatus"},

	{0x0E927AED, _sceKernelReturnFromTimerHandler, "_sceKernelReturnFromTimerHandler"},
	{0x532A522E, _sceKernelExitThread,"_sceKernelExitThread"},

	// Shouldn't hook this up. No games should import this function manually and call it.
	// {0x6E9EA350, _sceKernelReturnFromCallback,"_sceKernelReturnFromCallback"},
};

void Register_ThreadManForUser()
{
	RegisterModule("ThreadManForUser", ARRAY_SIZE(ThreadManForUser), ThreadManForUser);
}


const HLEFunction LoadExecForUser[] =
{
	{0x05572A5F,sceKernelExitGame, "sceKernelExitGame"}, //()
	{0x4AC57943,sceKernelRegisterExitCallback,"sceKernelRegisterExitCallback"},
	{0xBD2F1094,sceKernelLoadExec,"sceKernelLoadExec"},
	{0x2AC9954B,0,"sceKernelExitGameWithStatus"},
};

void Register_LoadExecForUser()
{
	RegisterModule("LoadExecForUser", ARRAY_SIZE(LoadExecForUser), LoadExecForUser);
}



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

void Register_ExceptionManagerForKernel()
{
	RegisterModule("ExceptionManagerForKernel", ARRAY_SIZE(ExceptionManagerForKernel), ExceptionManagerForKernel);
}
