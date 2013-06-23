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
#include "HLE.h"
#include "../MIPS/MIPS.h"
#include "../CoreTiming.h"
#include "ChunkFile.h"

#include "scePower.h"
#include "sceKernelThread.h"

const int PSP_POWER_ERROR_TAKEN_SLOT = 0x80000020;
const int PSP_POWER_ERROR_SLOTS_FULL = 0x80000022;
const int PSP_POWER_ERROR_PRIVATE_SLOT = 0x80000023;
const int PSP_POWER_ERROR_EMPTY_SLOT = 0x80000025;
const int PSP_POWER_ERROR_INVALID_CB = 0x80000100;
const int PSP_POWER_ERROR_INVALID_SLOT = 0x80000102;

const int PSP_POWER_CB_AC_POWER = 0x00001000;
const int PSP_POWER_CB_BATTERY_EXIST = 0x00000080;
const int PSP_POWER_CB_BATTERY_FULL = 0x00000064;

const int POWER_CB_AUTO = -1;

const int PSP_MODEL_FAT	= 0;
const int PSP_MODEL_SLIM = 1;

const int numberOfCBPowerSlots = 16;
const int numberOfCBPowerSlotsPrivate = 32;

static bool volatileMemLocked;
static int powerCbSlots[numberOfCBPowerSlots];

// this should belong here on in CoreTiming?
static int pllFreq = 222;
static int busFreq = 111;

void __PowerInit() {
	memset(powerCbSlots, 0, sizeof(powerCbSlots));
	volatileMemLocked = false;
}

void __PowerDoState(PointerWrap &p) {
	p.DoArray(powerCbSlots, ARRAY_SIZE(powerCbSlots));
	p.Do(volatileMemLocked);
	p.DoMarker("scePower");
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
		return PSP_POWER_ERROR_PRIVATE_SLOT;
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
		__KernelRegisterCallback(THREAD_CALLBACK_POWER, cbId);

		int arg = PSP_POWER_CB_AC_POWER | PSP_POWER_CB_BATTERY_EXIST | PSP_POWER_CB_BATTERY_FULL;
		__KernelNotifyCallbackType(THREAD_CALLBACK_POWER, cbId, arg);
	}
	return retval;
}

int scePowerUnregisterCallback(int slotId) {
	DEBUG_LOG(HLE, "0=scePowerUnregisterCallback(%i)", slotId);

	if (slotId < 0 || slotId >= numberOfCBPowerSlotsPrivate) {
		return PSP_POWER_ERROR_INVALID_SLOT;
	}
	if (slotId >= numberOfCBPowerSlots) {
		return PSP_POWER_ERROR_PRIVATE_SLOT;
	}

	if (powerCbSlots[slotId] != 0) {
		int cbId = powerCbSlots[slotId];
		DEBUG_LOG(HLE, "0=scePowerUnregisterCallback(%i) (cbid = %i)", slotId, cbId);
		__KernelUnregisterCallback(THREAD_CALLBACK_POWER, cbId);
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

#define ERROR_POWER_VMEM_IN_USE 0x802b0200

int sceKernelVolatileMemTryLock(int type, int paddr, int psize) {
	if (!volatileMemLocked) {
		INFO_LOG(HLE,"sceKernelVolatileMemTryLock(%i, %08x, %i) - success", type, paddr, psize);
		volatileMemLocked = true;
	} else {
		ERROR_LOG(HLE, "sceKernelVolatileMemTryLock(%i, %08x, %i) - already locked!", type, paddr, psize);
		return ERROR_POWER_VMEM_IN_USE;
	}

	// Volatile RAM is always at 0x08400000 and is of size 0x00400000.
	// It's always available in the emu.
	// TODO: Should really reserve this properly!
	Memory::Write_U32(0x08400000, paddr);
	Memory::Write_U32(0x00400000, psize);

	return 0;
}

int sceKernelVolatileMemUnlock(int type) {
	if (volatileMemLocked) {
		INFO_LOG(HLE,"sceKernelVolatileMemUnlock(%i)", type);
		volatileMemLocked = false;
	} else {
		ERROR_LOG(HLE, "sceKernelVolatileMemUnlock(%i) FAILED - not locked", type);
	}
	return 0;
}

int sceKernelVolatileMemLock(int type, int paddr, int psize) {
	return sceKernelVolatileMemTryLock(type, paddr, psize);
}


u32 scePowerSetClockFrequency(u32 pllfreq, u32 cpufreq, u32 busfreq) {
	CoreTiming::SetClockFrequencyMHz(cpufreq);
	pllFreq = pllfreq;
	busFreq = busfreq;
	INFO_LOG(HLE,"scePowerSetClockFrequency(%i,%i,%i)", pllfreq, cpufreq, busfreq);
	return 0;
}

u32 scePowerSetCpuClockFrequency(u32 cpufreq) {
	CoreTiming::SetClockFrequencyMHz(cpufreq);
	DEBUG_LOG(HLE,"scePowerSetCpuClockFrequency(%i)", cpufreq);
	return 0;
}

u32 scePowerSetBusClockFrequency(u32 busfreq) {
	busFreq = busfreq;
	DEBUG_LOG(HLE,"scePowerSetBusClockFrequency(%i)", busfreq);
	return 0;
}

u32 scePowerGetCpuClockFrequency() {
	int cpuFreq = CoreTiming::GetClockFrequencyMHz();
	DEBUG_LOG(HLE,"%i=scePowerGetCpuClockFrequency()", cpuFreq);
	return cpuFreq;
}

u32 scePowerGetBusClockFrequency() {
	INFO_LOG(HLE,"%i=scePowerGetBusClockFrequency()", busFreq);
	return busFreq;
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
	return PSP_MODEL_FAT;  
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
	{0xFEE03A2F,WrapU_V<scePowerGetCpuClockFrequency>,"scePowerGetCpuClockFrequency"},
	{0x478FE6F5,WrapU_V<scePowerGetBusClockFrequency>,"scePowerGetBusClockFrequency"},
	{0xFDB5BFE9,WrapU_V<scePowerGetCpuClockFrequencyInt>,"scePowerGetCpuClockFrequencyInt"},
	{0xBD681969,WrapU_V<scePowerGetBusClockFrequencyInt>,"scePowerGetBusClockFrequencyInt"},
	{0xB1A52C83,WrapF_V<scePowerGetCpuClockFrequencyFloat>,"scePowerGetCpuClockFrequencyFloat"},
	{0x9BADB3EB,WrapF_V<scePowerGetBusClockFrequencyFloat>,"scePowerGetBusClockFrequencyFloat"},
	{0x737486F2,WrapU_UUU<scePowerSetClockFrequency>,"scePowerSetClockFrequency"},
	{0x34f9c463,WrapU_V<scePowerGetPllClockFrequencyInt>,"scePowerGetPllClockFrequencyInt"},
	{0xea382a27,WrapF_V<scePowerGetPllClockFrequencyFloat>,"scePowerGetPllClockFrequencyFloat"},
	{0xebd177d6,WrapU_UUU<scePowerSetClockFrequency>,"scePower_EBD177D6"}, // This is also the same as SetClockFrequency
	{0x469989ad,WrapU_UUU<scePowerSetClockFrequency>,"scePower_469989ad"},  // This is also the same as SetClockFrequency
	{0xa85880d0,WrapU_V<IsPSPNonFat>,"scePower_a85880d0_IsPSPNonFat"},
};

//890129c in tyshooter looks bogus
const HLEFunction sceSuspendForUser[] = {
	{0xEADB1BD7,&WrapI_I<sceKernelPowerLock>,"sceKernelPowerLock"}, //(int param) set param to 0
	{0x3AEE7261,&WrapI_I<sceKernelPowerUnlock>,"sceKernelPowerUnlock"},//(int param) set param to 0
	{0x090ccb3f,&WrapI_I<sceKernelPowerTick>,"sceKernelPowerTick"},

	// There's an extra 4MB that can be allocated, which seems to be "volatile". These functions
	// let you grab it.
	{0xa14f40b2,&WrapI_III<sceKernelVolatileMemTryLock>,"sceKernelVolatileMemTryLock"},
	{0xa569e425,&WrapI_I<sceKernelVolatileMemUnlock>,"sceKernelVolatileMemUnlock"},
	{0x3e0271d3,&WrapI_III<sceKernelVolatileMemLock>,"sceKernelVolatileMemLock"}, //when "acquiring mem pool" (fired up)
};


void Register_scePower() {
	RegisterModule("scePower",ARRAY_SIZE(scePower),scePower);
}

void Register_sceSuspendForUser() {
	RegisterModule("sceSuspendForUser", ARRAY_SIZE(sceSuspendForUser), sceSuspendForUser);
}
