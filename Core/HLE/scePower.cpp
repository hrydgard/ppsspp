// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.
#include <map>
#include <vector>
#include "Common/ChunkFile.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/CoreTiming.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/Config.h"

#include "Core/HLE/scePower.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/HLE/sceKernelInterrupt.h"

struct VolatileWaitingThread {
	SceUID threadID;
	u32 addrPtr;
	u32 sizePtr;
};

const int PSP_POWER_ERROR_TAKEN_SLOT = 0x80000020;
const int PSP_POWER_ERROR_SLOTS_FULL = 0x80000022;
const int PSP_POWER_ERROR_EMPTY_SLOT = 0x80000025;
const int PSP_POWER_ERROR_INVALID_CB = 0x80000100;
const int PSP_POWER_ERROR_INVALID_SLOT = 0x80000102;

const int PSP_POWER_CB_AC_POWER = 0x00001000;
const int PSP_POWER_CB_BATTERY_EXIST = 0x00000080;
const int PSP_POWER_CB_BATTERY_FULL = 0x00000064;

const int POWER_CB_AUTO = -1;

// These are the callback slots for user mode applications.
const int numberOfCBPowerSlots = 16;

// These are the callback slots for kernel mode applications.
const int numberOfCBPowerSlotsPrivate = 32;

static bool volatileMemLocked;
static int powerCbSlots[numberOfCBPowerSlots];
static std::vector<VolatileWaitingThread> volatileWaitingThreads;

// Should this belong here, or in CoreTiming?
static int pllFreq = 222;
static int busFreq = 111;

void __PowerInit() {
	memset(powerCbSlots, 0, sizeof(powerCbSlots));
	volatileMemLocked = false;
	volatileWaitingThreads.clear();

	if (g_Config.iLockedCPUSpeed > 0) {
		CoreTiming::SetClockFrequencyMHz(g_Config.iLockedCPUSpeed);
		pllFreq = g_Config.iLockedCPUSpeed;
		busFreq = g_Config.iLockedCPUSpeed / 2;
	} else {
		pllFreq = 222;
		busFreq = 111;
	}
}

void __PowerDoState(PointerWrap &p) {
	auto s = p.Section("scePower", 1);
	if (!s)
		return;

	p.DoArray(powerCbSlots, ARRAY_SIZE(powerCbSlots));
	p.Do(volatileMemLocked);
	p.Do(volatileWaitingThreads);
}

int scePowerGetBatteryLifePercent() {
	DEBUG_LOG(HLE, "100=scePowerGetBatteryLifePercent");
	return 100;
}

int scePowerGetBatteryLifeTime() {
	DEBUG_LOG(HLE, "0=scePowerGetBatteryLifeTime()");
	// 0 means we're on AC power.
	return 0;
}

int scePowerGetBatteryTemp() {
	DEBUG_LOG(HLE, "0=scePowerGetBatteryTemp()");
	// 0 means celsius temperature of the battery
	return 0;
}

int scePowerIsPowerOnline() {
	DEBUG_LOG(HLE, "1=scePowerIsPowerOnline");
	return 1;
}

int scePowerIsBatteryExist() {
	DEBUG_LOG(HLE, "1=scePowerIsBatteryExist");
	return 1;
}

int scePowerIsBatteryCharging() {
	DEBUG_LOG(HLE, "0=scePowerIsBatteryCharging");
	return 0;
}

int scePowerGetBatteryChargingStatus() {
	DEBUG_LOG(HLE, "0=scePowerGetBatteryChargingStatus");
	return 0;
}

int scePowerIsLowBattery() {
	DEBUG_LOG(HLE, "0=scePowerIsLowBattery");
	return 0;
}

int scePowerRegisterCallback(int slot, int cbId) {
	DEBUG_LOG(HLE, "0=scePowerRegisterCallback(%i, %i)", slot, cbId);

	if (slot < -1 || slot >= numberOfCBPowerSlotsPrivate) {
		return PSP_POWER_ERROR_INVALID_SLOT;
	}
	if (slot >= numberOfCBPowerSlots) {
		return SCE_KERNEL_ERROR_PRIV_REQUIRED;
	}
	// TODO: If cbId is invalid return PSP_POWER_ERROR_INVALID_CB.
	if (cbId == 0) {
		return PSP_POWER_ERROR_INVALID_CB;
	}

	int retval = -1;

	if (slot == POWER_CB_AUTO) { // -1 signifies auto select of bank
		for (int i=0; i < numberOfCBPowerSlots; i++) {
			if (powerCbSlots[i] == 0 && retval == -1) { // found an empty slot
				powerCbSlots[i] = cbId;
				retval = i;
			}
		}
		if (retval == -1) {
			return PSP_POWER_ERROR_SLOTS_FULL;
		}
	} else {
		if (powerCbSlots[slot] == 0) {
			powerCbSlots[slot] = cbId;
			retval = 0;
		} else {
			return PSP_POWER_ERROR_TAKEN_SLOT;
		}
	}
	if (retval >= 0) {
		int arg = PSP_POWER_CB_AC_POWER | PSP_POWER_CB_BATTERY_EXIST | PSP_POWER_CB_BATTERY_FULL;
		__KernelNotifyCallback(cbId, arg);
	}
	return retval;
}

int scePowerUnregisterCallback(int slotId) {
	DEBUG_LOG(HLE, "0=scePowerUnregisterCallback(%i)", slotId);

	if (slotId < 0 || slotId >= numberOfCBPowerSlotsPrivate) {
		return PSP_POWER_ERROR_INVALID_SLOT;
	}
	if (slotId >= numberOfCBPowerSlots) {
		return SCE_KERNEL_ERROR_PRIV_REQUIRED;
	}

	if (powerCbSlots[slotId] != 0) {
		int cbId = powerCbSlots[slotId];
		DEBUG_LOG(HLE, "0=scePowerUnregisterCallback(%i) (cbid = %i)", slotId, cbId);
		powerCbSlots[slotId] = 0;
	} else {
		return PSP_POWER_ERROR_EMPTY_SLOT;
	}

	return 0;
}

int sceKernelPowerLock(int lockType) {
	DEBUG_LOG(HLE, "0=sceKernelPowerLock(%i)", lockType);
	if (lockType == 0) {
		return 0;
	} else {
		return SCE_KERNEL_ERROR_INVALID_MODE;
	}
}

int sceKernelPowerUnlock(int lockType) {
	DEBUG_LOG(HLE, "0=sceKernelPowerUnlock(%i)", lockType);
	if (lockType == 0) {
		return 0;
	} else {
		return SCE_KERNEL_ERROR_INVALID_MODE;
	}
}

int sceKernelPowerTick(int flag) {
	DEBUG_LOG(HLE, "UNIMPL 0=sceKernelPowerTick(%i)", flag);
	return 0;
}

int __KernelVolatileMemLock(int type, u32 paddr, u32 psize) {
	if (type != 0) {
		return SCE_KERNEL_ERROR_INVALID_MODE;
	}
	if (volatileMemLocked) {
		return SCE_KERNEL_ERROR_POWER_VMEM_IN_USE;
	}

	// Volatile RAM is always at 0x08400000 and is of size 0x00400000.
	// It's always available in the emu.
	// TODO: Should really reserve this properly!
	if (Memory::IsValidAddress(paddr)) {
		Memory::Write_U32(0x08400000, paddr);
	}
	if (Memory::IsValidAddress(psize)) {
		Memory::Write_U32(0x00400000, psize);
	}
	volatileMemLocked = true;

	return 0;
}

int sceKernelVolatileMemTryLock(int type, u32 paddr, u32 psize) {
	u32 error = __KernelVolatileMemLock(type, paddr, psize);

	switch (error) {
	case 0:
		// HACK: This fixes Crash Tag Team Racing.
		// Should only wait 1200 cycles though according to Unknown's testing,
		// and with that it's still broken. So it's not this, unfortunately.
		// Leaving it in for the 0.9.8 release anyway.
		hleEatCycles(500000);
		DEBUG_LOG(HLE, "sceKernelVolatileMemTryLock(%i, %08x, %08x) - success", type, paddr, psize);
		break;

	case SCE_KERNEL_ERROR_POWER_VMEM_IN_USE:
		ERROR_LOG(HLE, "sceKernelVolatileMemTryLock(%i, %08x, %08x) - already locked!", type, paddr, psize);
		break;

	default:
		ERROR_LOG_REPORT(HLE, "%08x=sceKernelVolatileMemTryLock(%i, %08x, %08x) - error", type, paddr, psize, error);
		break;
	}

	return error;
}

int sceKernelVolatileMemUnlock(int type) {
	if (type != 0) {
		ERROR_LOG_REPORT(HLE, "sceKernelVolatileMemUnlock(%i) - invalid mode", type);
		return SCE_KERNEL_ERROR_INVALID_MODE;
	}
	if (volatileMemLocked) {
		volatileMemLocked = false;

		// Wake someone, always fifo.
		bool wokeThreads = false;
		u32 error;
		while (!volatileWaitingThreads.empty() && !volatileMemLocked) {
			VolatileWaitingThread waitInfo = volatileWaitingThreads.front();
			volatileWaitingThreads.erase(volatileWaitingThreads.begin());

			int waitID = __KernelGetWaitID(waitInfo.threadID, WAITTYPE_VMEM, error);
			// If they were force-released, just skip.
			if (waitID == 1 && __KernelVolatileMemLock(0, waitInfo.addrPtr, waitInfo.sizePtr) == 0) {
				__KernelResumeThreadFromWait(waitInfo.threadID, 0);
				wokeThreads = true;
			}
		}

		if (wokeThreads) {
			INFO_LOG(HLE, "sceKernelVolatileMemUnlock(%i) handed over to another thread", type);
			hleReSchedule("volatile mem unlocked");
		} else {
			DEBUG_LOG(HLE, "sceKernelVolatileMemUnlock(%i)", type);
		}
	} else {
		ERROR_LOG_REPORT(HLE, "sceKernelVolatileMemUnlock(%i) FAILED - not locked", type);
		// I guess it must use a sema.
		return SCE_KERNEL_ERROR_SEMA_OVF;
	}
	return 0;
}

int sceKernelVolatileMemLock(int type, u32 paddr, u32 psize) {
	u32 error = 0;

	// If dispatch is disabled or in an interrupt, don't check, just return an error.
	// But still write the addr and size (some games require this to work, and it's testably true.)
	if (!__KernelIsDispatchEnabled()) {
		error = SCE_KERNEL_ERROR_CAN_NOT_WAIT;
	} else if (__IsInInterrupt()) {
		error = SCE_KERNEL_ERROR_ILLEGAL_CONTEXT;
	} else {
		error = __KernelVolatileMemLock(type, paddr, psize);
	}

	switch (error) {
	case 0:
		// HACK: This fixes Crash Tag Team Racing.
		// Should only wait 1200 cycles though according to Unknown's testing,
		// and with that it's still broken. So it's not this, unfortunately.
		// Leaving it in for the 0.9.8 release anyway.
		hleEatCycles(500000);
		DEBUG_LOG(HLE, "sceKernelVolatileMemLock(%i, %08x, %08x) - success", type, paddr, psize);
		break;

	case SCE_KERNEL_ERROR_POWER_VMEM_IN_USE:
		{
			WARN_LOG(HLE, "sceKernelVolatileMemLock(%i, %08x, %08x) - already locked, waiting", type, paddr, psize);
			const VolatileWaitingThread waitInfo = { __KernelGetCurThread(), paddr, psize };
			volatileWaitingThreads.push_back(waitInfo);
			__KernelWaitCurThread(WAITTYPE_VMEM, 1, 0, 0, false, "volatile mem waited");
		}
		break;

	case SCE_KERNEL_ERROR_CAN_NOT_WAIT:
		{
			WARN_LOG(HLE, "sceKernelVolatileMemLock(%i, %08x, %08x): dispatch disabled", type, paddr, psize);
			Memory::Write_U32(0x08400000, paddr);
			Memory::Write_U32(0x00400000, psize);
		}
		break;

	case SCE_KERNEL_ERROR_ILLEGAL_CONTEXT:
		{
			WARN_LOG(HLE, "sceKernelVolatileMemLock(%i, %08x, %08x): in interrupt", type, paddr, psize);
			Memory::Write_U32(0x08400000, paddr);
			Memory::Write_U32(0x00400000, psize);
		}
		break;

	default:
		ERROR_LOG_REPORT(HLE, "%08x=sceKernelVolatileMemLock(%i, %08x, %08x) - error", type, paddr, psize, error);
		break;
	}

	return error;
}


u32 scePowerSetClockFrequency(u32 pllfreq, u32 cpufreq, u32 busfreq) {
	if (g_Config.iLockedCPUSpeed > 0) {
		INFO_LOG(HLE,"scePowerSetClockFrequency(%i,%i,%i): locked by user config at %i, %i, %i", pllfreq, cpufreq, busfreq, g_Config.iLockedCPUSpeed, g_Config.iLockedCPUSpeed, busFreq);
	}
	else {
		if (cpufreq == 0 || cpufreq > 333) {
			WARN_LOG(HLE,"scePowerSetClockFrequency(%i,%i,%i): invalid frequency", pllfreq, cpufreq, busfreq);
			return SCE_KERNEL_ERROR_INVALID_VALUE;
		}
		// TODO: More restrictions.
		CoreTiming::SetClockFrequencyMHz(cpufreq);
		pllFreq = pllfreq;
		busFreq = busfreq;
		INFO_LOG(HLE,"scePowerSetClockFrequency(%i,%i,%i)", pllfreq, cpufreq, busfreq);
	}
	return 0;
}

u32 scePowerSetCpuClockFrequency(u32 cpufreq) {
	if(g_Config.iLockedCPUSpeed > 0) {
		DEBUG_LOG(HLE,"scePowerSetCpuClockFrequency(%i): locked by user config at %i", cpufreq, g_Config.iLockedCPUSpeed);
	}
	else {
		if (cpufreq == 0 || cpufreq > 333) {
			WARN_LOG(HLE,"scePowerSetCpuClockFrequency(%i): invalid frequency", cpufreq);
			return SCE_KERNEL_ERROR_INVALID_VALUE;
		}
		CoreTiming::SetClockFrequencyMHz(cpufreq);
		DEBUG_LOG(HLE,"scePowerSetCpuClockFrequency(%i)", cpufreq);
	}
	return 0;
}

u32 scePowerSetBusClockFrequency(u32 busfreq) {
	if(g_Config.iLockedCPUSpeed > 0) {
		DEBUG_LOG(HLE,"scePowerSetBusClockFrequency(%i): locked by user config at %i", busfreq, busFreq);
	}
	else {
		if (busfreq == 0 || busfreq > 111) {
			WARN_LOG(HLE,"scePowerSetBusClockFrequency(%i): invalid frequency", busfreq);
			return SCE_KERNEL_ERROR_INVALID_VALUE;
		}
		// TODO: It seems related to other frequencies, though.
		busFreq = busfreq;
		DEBUG_LOG(HLE,"scePowerSetBusClockFrequency(%i)", busfreq);
	}
	return 0;
}

u32 scePowerGetCpuClockFrequencyInt() {
	int cpuFreq = CoreTiming::GetClockFrequencyMHz();
	DEBUG_LOG(HLE,"%i=scePowerGetCpuClockFrequencyInt()", cpuFreq);
	return cpuFreq;
}

u32 scePowerGetPllClockFrequencyInt() {
	INFO_LOG(HLE,"%i=scePowerGetPllClockFrequencyInt()", pllFreq);
	return pllFreq;
}

u32 scePowerGetBusClockFrequencyInt() {
	INFO_LOG(HLE,"%i=scePowerGetBusClockFrequencyInt()", busFreq);
	return busFreq;
}

float scePowerGetCpuClockFrequencyFloat() {
	int cpuFreq = CoreTiming::GetClockFrequencyMHz(); 
	INFO_LOG(HLE, "%f=scePowerGetCpuClockFrequencyFloat()", (float)cpuFreq);
	return (float) cpuFreq;
}

float scePowerGetPllClockFrequencyFloat() {
	INFO_LOG(HLE, "%f=scePowerGetPllClockFrequencyFloat()", (float)pllFreq);
	return (float) pllFreq;
}

float scePowerGetBusClockFrequencyFloat() {
	INFO_LOG(HLE, "%f=scePowerGetBusClockFrequencyFloat()", (float)busFreq);
	return (float) busFreq;
}

int scePowerTick() {
	DEBUG_LOG(HLE, "scePowerTick()");
	// Don't think we need to do anything.
	return 0;
}


u32 IsPSPNonFat() {
	DEBUG_LOG(HLE, "%d=scePower_a85880d0_IsPSPNonFat()", g_Config.iPSPModel);

	return g_Config.iPSPModel;  
}

static const HLEFunction scePower[] = {
	{0x04B7766E,&WrapI_II<scePowerRegisterCallback>,"scePowerRegisterCallback"},
	{0x2B51FE2F,0,"scePower_2B51FE2F"},
	{0x442BFBAC,0,"scePowerGetBacklightMaximum"},
	{0xEFD3C963,&WrapI_V<scePowerTick>,"scePowerTick"},
	{0xEDC13FE5,0,"scePowerGetIdleTimer"},
	{0x7F30B3B1,0,"scePowerIdleTimerEnable"},
	{0x972CE941,0,"scePowerIdleTimerDisable"},
	{0x27F3292C,0,"scePowerBatteryUpdateInfo"},
	{0xE8E4E204,0,"scePower_E8E4E204"},
	{0xB999184C,0,"scePowerGetLowBatteryCapacity"},
	{0x87440F5E,&WrapI_V<scePowerIsPowerOnline>,"scePowerIsPowerOnline"},
	{0x0AFD0D8B,&WrapI_V<scePowerIsBatteryExist>,"scePowerIsBatteryExist"},
	{0x1E490401,&WrapI_V<scePowerIsBatteryCharging>,"scePowerIsBatteryCharging"},
	{0xB4432BC8,&WrapI_V<scePowerGetBatteryChargingStatus>,"scePowerGetBatteryChargingStatus"},
	{0xD3075926,&WrapI_V<scePowerIsLowBattery>,"scePowerIsLowBattery"},
	{0x78A1A796,0,"scePowerIsSuspendRequired"},
	{0x94F5A53F,0,"scePowerGetBatteryRemainCapacity"},
	{0xFD18A0FF,0,"scePowerGetBatteryFullCapacity"},
	{0x2085D15D,&WrapI_V<scePowerGetBatteryLifePercent>,"scePowerGetBatteryLifePercent"},
	{0x8EFB3FA2,&WrapI_V<scePowerGetBatteryLifeTime>,"scePowerGetBatteryLifeTime"},
	{0x28E12023,&WrapI_V<scePowerGetBatteryTemp>,"scePowerGetBatteryTemp"},
	{0x862AE1A6,0,"scePowerGetBatteryElec"},
	{0x483CE86B,0,"scePowerGetBatteryVolt"},
	{0xcb49f5ce,0,"scePowerGetBatteryChargeCycle"},
	{0x23436A4A,0,"scePowerGetInnerTemp"},
	{0x0CD21B1F,0,"scePowerSetPowerSwMode"},
	{0x165CE085,0,"scePowerGetPowerSwMode"},
	{0xD6D016EF,0,"scePowerLock"},
	{0xCA3D34C1,0,"scePowerUnlock"},
	{0xDB62C9CF,0,"scePowerCancelRequest"},
	{0x7FA406DD,0,"scePowerIsRequest"},
	{0x2B7C7CF4,0,"scePowerRequestStandby"},
	{0xAC32C9CC,0,"scePowerRequestSuspend"},
	{0x2875994B,0,"scePower_2875994B"},
	{0x0074EF9B,0,"scePowerGetResumeCount"},
	{0xDFA8BAF8,WrapI_I<scePowerUnregisterCallback>,"scePowerUnregisterCallback"},
	{0xDB9D28DD,WrapI_I<scePowerUnregisterCallback>,"scePowerUnregitserCallback"},	
	{0x843FBF43,WrapU_U<scePowerSetCpuClockFrequency>,"scePowerSetCpuClockFrequency"},
	{0xB8D7B3FB,WrapU_U<scePowerSetBusClockFrequency>,"scePowerSetBusClockFrequency"},
	{0xFEE03A2F,WrapU_V<scePowerGetCpuClockFrequencyInt>,"scePowerGetCpuClockFrequency"},
	{0x478FE6F5,WrapU_V<scePowerGetBusClockFrequencyInt>,"scePowerGetBusClockFrequency"},
	{0xFDB5BFE9,WrapU_V<scePowerGetCpuClockFrequencyInt>,"scePowerGetCpuClockFrequencyInt"},
	{0xBD681969,WrapU_V<scePowerGetBusClockFrequencyInt>,"scePowerGetBusClockFrequencyInt"},
	{0xB1A52C83,WrapF_V<scePowerGetCpuClockFrequencyFloat>,"scePowerGetCpuClockFrequencyFloat"},
	{0x9BADB3EB,WrapF_V<scePowerGetBusClockFrequencyFloat>,"scePowerGetBusClockFrequencyFloat"},
	{0x737486F2,WrapU_UUU<scePowerSetClockFrequency>,"scePowerSetClockFrequency"},
	{0x34f9c463,WrapU_V<scePowerGetPllClockFrequencyInt>,"scePowerGetPllClockFrequencyInt"},
	{0xea382a27,WrapF_V<scePowerGetPllClockFrequencyFloat>,"scePowerGetPllClockFrequencyFloat"},
	{0xebd177d6,WrapU_UUU<scePowerSetClockFrequency>,"scePower_EBD177D6"}, // This is also the same as SetClockFrequency
	{0x469989ad,WrapU_UUU<scePowerSetClockFrequency>,"scePower_469989ad"}, // This is also the same as SetClockFrequency
	{0x545a7f3c,0,"scePower_545A7F3C"}, // TODO: Supposedly the same as SetClockFrequency also?
	{0xa4e93389,0,"scePower_A4E93389"}, // TODO: Supposedly the same as SetClockFrequency also?
	{0xa85880d0,WrapU_V<IsPSPNonFat>,"scePower_a85880d0_IsPSPNonFat"},
	{0x3951af53,0,"scePowerWaitRequestCompletion"},
	{0x0442d852,0,"scePowerRequestColdReset"},
	{0xbafa3df0,0,"scePowerGetCallbackMode"},
	{0xa9d22232,0,"scePowerSetCallbackMode"},

	// These seem to be aliases.
	{0x23c31ffe,&WrapI_IUU<sceKernelVolatileMemLock>,"scePowerVolatileMemLock"},
	{0xfa97a599,&WrapI_IUU<sceKernelVolatileMemTryLock>,"scePowerVolatileMemTryLock"},
	{0xb3edd801,&WrapI_I<sceKernelVolatileMemUnlock>,"scePowerVolatileMemUnlock"},
};

//890129c in tyshooter looks bogus
const HLEFunction sceSuspendForUser[] = {
	{0xEADB1BD7,&WrapI_I<sceKernelPowerLock>,"sceKernelPowerLock"}, //(int param) set param to 0
	{0x3AEE7261,&WrapI_I<sceKernelPowerUnlock>,"sceKernelPowerUnlock"},//(int param) set param to 0
	{0x090ccb3f,&WrapI_I<sceKernelPowerTick>,"sceKernelPowerTick"},

	// There's an extra 4MB that can be allocated, which seems to be "volatile". These functions
	// let you grab it.
	{0xa14f40b2,&WrapI_IUU<sceKernelVolatileMemTryLock>,"sceKernelVolatileMemTryLock"},
	{0xa569e425,&WrapI_I<sceKernelVolatileMemUnlock>,"sceKernelVolatileMemUnlock"},
	{0x3e0271d3,&WrapI_IUU<sceKernelVolatileMemLock>,"sceKernelVolatileMemLock"}, //when "acquiring mem pool" (fired up)
};


void Register_scePower() {
	RegisterModule("scePower",ARRAY_SIZE(scePower),scePower);
}

void Register_sceSuspendForUser() {
	RegisterModule("sceSuspendForUser", ARRAY_SIZE(sceSuspendForUser), sceSuspendForUser);
}
