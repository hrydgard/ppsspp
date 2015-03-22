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

static int scePowerGetBatteryLifePercent() {
	DEBUG_LOG(HLE, "100=scePowerGetBatteryLifePercent");
	return 100;
}

static int scePowerGetBatteryLifeTime() {
	DEBUG_LOG(HLE, "0=scePowerGetBatteryLifeTime()");
	// 0 means we're on AC power.
	return 0;
}

static int scePowerGetBatteryTemp() {
	DEBUG_LOG(HLE, "0=scePowerGetBatteryTemp()");
	// 0 means celsius temperature of the battery
	return 0;
}

static int scePowerIsPowerOnline() {
	DEBUG_LOG(HLE, "1=scePowerIsPowerOnline");
	return 1;
}

static int scePowerIsBatteryExist() {
	DEBUG_LOG(HLE, "1=scePowerIsBatteryExist");
	return 1;
}

static int scePowerIsBatteryCharging() {
	DEBUG_LOG(HLE, "0=scePowerIsBatteryCharging");
	return 0;
}

static int scePowerGetBatteryChargingStatus() {
	DEBUG_LOG(HLE, "0=scePowerGetBatteryChargingStatus");
	return 0;
}

static int scePowerIsLowBattery() {
	DEBUG_LOG(HLE, "0=scePowerIsLowBattery");
	return 0;
}

static int scePowerRegisterCallback(int slot, int cbId) {
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

static int scePowerUnregisterCallback(int slotId) {
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

static int sceKernelPowerLock(int lockType) {
	DEBUG_LOG(HLE, "0=sceKernelPowerLock(%i)", lockType);
	if (lockType == 0) {
		return 0;
	} else {
		return SCE_KERNEL_ERROR_INVALID_MODE;
	}
}

static int sceKernelPowerUnlock(int lockType) {
	DEBUG_LOG(HLE, "0=sceKernelPowerUnlock(%i)", lockType);
	if (lockType == 0) {
		return 0;
	} else {
		return SCE_KERNEL_ERROR_INVALID_MODE;
	}
}

static int sceKernelPowerTick(int flag) {
	DEBUG_LOG(HLE, "UNIMPL 0=sceKernelPowerTick(%i)", flag);
	return 0;
}

static int __KernelVolatileMemLock(int type, u32 paddr, u32 psize) {
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

static int sceKernelVolatileMemTryLock(int type, u32 paddr, u32 psize) {
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

static int sceKernelVolatileMemUnlock(int type) {
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

static int sceKernelVolatileMemLock(int type, u32 paddr, u32 psize) {
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


static u32 scePowerSetClockFrequency(u32 pllfreq, u32 cpufreq, u32 busfreq) {
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

static u32 scePowerSetCpuClockFrequency(u32 cpufreq) {
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

static u32 scePowerSetBusClockFrequency(u32 busfreq) {
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

static u32 scePowerGetCpuClockFrequencyInt() {
	int cpuFreq = CoreTiming::GetClockFrequencyMHz();
	DEBUG_LOG(HLE,"%i=scePowerGetCpuClockFrequencyInt()", cpuFreq);
	return cpuFreq;
}

static u32 scePowerGetPllClockFrequencyInt() {
	INFO_LOG(HLE,"%i=scePowerGetPllClockFrequencyInt()", pllFreq);
	return pllFreq;
}

static u32 scePowerGetBusClockFrequencyInt() {
	INFO_LOG(HLE,"%i=scePowerGetBusClockFrequencyInt()", busFreq);
	return busFreq;
}

static float scePowerGetCpuClockFrequencyFloat() {
	int cpuFreq = CoreTiming::GetClockFrequencyMHz(); 
	INFO_LOG(HLE, "%f=scePowerGetCpuClockFrequencyFloat()", (float)cpuFreq);
	return (float) cpuFreq;
}

static float scePowerGetPllClockFrequencyFloat() {
	INFO_LOG(HLE, "%f=scePowerGetPllClockFrequencyFloat()", (float)pllFreq);
	return (float) pllFreq;
}

static float scePowerGetBusClockFrequencyFloat() {
	INFO_LOG(HLE, "%f=scePowerGetBusClockFrequencyFloat()", (float)busFreq);
	return (float) busFreq;
}

static int scePowerTick() {
	DEBUG_LOG(HLE, "scePowerTick()");
	// Don't think we need to do anything.
	return 0;
}


static u32 IsPSPNonFat() {
	DEBUG_LOG(HLE, "%d=scePower_a85880d0_IsPSPNonFat()", g_Config.iPSPModel);

	return g_Config.iPSPModel;  
}

static const HLEFunction scePower[] = {
	{0X04B7766E, &WrapI_II<scePowerRegisterCallback>,         "scePowerRegisterCallback",          'i', "ii" },
	{0X2B51FE2F, nullptr,                                     "scePower_2B51FE2F",                 '?', ""   },
	{0X442BFBAC, nullptr,                                     "scePowerGetBacklightMaximum",       '?', ""   },
	{0XEFD3C963, &WrapI_V<scePowerTick>,                      "scePowerTick",                      'i', ""   },
	{0XEDC13FE5, nullptr,                                     "scePowerGetIdleTimer",              '?', ""   },
	{0X7F30B3B1, nullptr,                                     "scePowerIdleTimerEnable",           '?', ""   },
	{0X972CE941, nullptr,                                     "scePowerIdleTimerDisable",          '?', ""   },
	{0X27F3292C, nullptr,                                     "scePowerBatteryUpdateInfo",         '?', ""   },
	{0XE8E4E204, nullptr,                                     "scePower_E8E4E204",                 '?', ""   },
	{0XB999184C, nullptr,                                     "scePowerGetLowBatteryCapacity",     '?', ""   },
	{0X87440F5E, &WrapI_V<scePowerIsPowerOnline>,             "scePowerIsPowerOnline",             'i', ""   },
	{0X0AFD0D8B, &WrapI_V<scePowerIsBatteryExist>,            "scePowerIsBatteryExist",            'i', ""   },
	{0X1E490401, &WrapI_V<scePowerIsBatteryCharging>,         "scePowerIsBatteryCharging",         'i', ""   },
	{0XB4432BC8, &WrapI_V<scePowerGetBatteryChargingStatus>,  "scePowerGetBatteryChargingStatus",  'i', ""   },
	{0XD3075926, &WrapI_V<scePowerIsLowBattery>,              "scePowerIsLowBattery",              'i', ""   },
	{0X78A1A796, nullptr,                                     "scePowerIsSuspendRequired",         '?', ""   },
	{0X94F5A53F, nullptr,                                     "scePowerGetBatteryRemainCapacity",  '?', ""   },
	{0XFD18A0FF, nullptr,                                     "scePowerGetBatteryFullCapacity",    '?', ""   },
	{0X2085D15D, &WrapI_V<scePowerGetBatteryLifePercent>,     "scePowerGetBatteryLifePercent",     'i', ""   },
	{0X8EFB3FA2, &WrapI_V<scePowerGetBatteryLifeTime>,        "scePowerGetBatteryLifeTime",        'i', ""   },
	{0X28E12023, &WrapI_V<scePowerGetBatteryTemp>,            "scePowerGetBatteryTemp",            'i', ""   },
	{0X862AE1A6, nullptr,                                     "scePowerGetBatteryElec",            '?', ""   },
	{0X483CE86B, nullptr,                                     "scePowerGetBatteryVolt",            '?', ""   },
	{0XCB49F5CE, nullptr,                                     "scePowerGetBatteryChargeCycle",     '?', ""   },
	{0X23436A4A, nullptr,                                     "scePowerGetInnerTemp",              '?', ""   },
	{0X0CD21B1F, nullptr,                                     "scePowerSetPowerSwMode",            '?', ""   },
	{0X165CE085, nullptr,                                     "scePowerGetPowerSwMode",            '?', ""   },
	{0XD6D016EF, nullptr,                                     "scePowerLock",                      '?', ""   },
	{0XCA3D34C1, nullptr,                                     "scePowerUnlock",                    '?', ""   },
	{0XDB62C9CF, nullptr,                                     "scePowerCancelRequest",             '?', ""   },
	{0X7FA406DD, nullptr,                                     "scePowerIsRequest",                 '?', ""   },
	{0X2B7C7CF4, nullptr,                                     "scePowerRequestStandby",            '?', ""   },
	{0XAC32C9CC, nullptr,                                     "scePowerRequestSuspend",            '?', ""   },
	{0X2875994B, nullptr,                                     "scePower_2875994B",                 '?', ""   },
	{0X0074EF9B, nullptr,                                     "scePowerGetResumeCount",            '?', ""   },
	{0XDFA8BAF8, &WrapI_I<scePowerUnregisterCallback>,        "scePowerUnregisterCallback",        'i', "i"  },
	{0XDB9D28DD, &WrapI_I<scePowerUnregisterCallback>,        "scePowerUnregitserCallback",        'i', "i"  },
	{0X843FBF43, &WrapU_U<scePowerSetCpuClockFrequency>,      "scePowerSetCpuClockFrequency",      'x', "x"  },
	{0XB8D7B3FB, &WrapU_U<scePowerSetBusClockFrequency>,      "scePowerSetBusClockFrequency",      'x', "x"  },
	{0XFEE03A2F, &WrapU_V<scePowerGetCpuClockFrequencyInt>,   "scePowerGetCpuClockFrequency",      'x', ""   },
	{0X478FE6F5, &WrapU_V<scePowerGetBusClockFrequencyInt>,   "scePowerGetBusClockFrequency",      'x', ""   },
	{0XFDB5BFE9, &WrapU_V<scePowerGetCpuClockFrequencyInt>,   "scePowerGetCpuClockFrequencyInt",   'x', ""   },
	{0XBD681969, &WrapU_V<scePowerGetBusClockFrequencyInt>,   "scePowerGetBusClockFrequencyInt",   'x', ""   },
	{0XB1A52C83, &WrapF_V<scePowerGetCpuClockFrequencyFloat>, "scePowerGetCpuClockFrequencyFloat", 'f', ""   },
	{0X9BADB3EB, &WrapF_V<scePowerGetBusClockFrequencyFloat>, "scePowerGetBusClockFrequencyFloat", 'f', ""   },
	{0X737486F2, &WrapU_UUU<scePowerSetClockFrequency>,       "scePowerSetClockFrequency",         'x', "xxx"},
	{0X34F9C463, &WrapU_V<scePowerGetPllClockFrequencyInt>,   "scePowerGetPllClockFrequencyInt",   'x', ""   },
	{0XEA382A27, &WrapF_V<scePowerGetPllClockFrequencyFloat>, "scePowerGetPllClockFrequencyFloat", 'f', ""   },
	{0XEBD177D6, &WrapU_UUU<scePowerSetClockFrequency>,       "scePower_EBD177D6",                 'x', "xxx"}, // This is also the same as SetClockFrequency
	{0X469989AD, &WrapU_UUU<scePowerSetClockFrequency>,       "scePower_469989ad",                 'x', "xxx"}, // This is also the same as SetClockFrequency
	{0X545A7F3C, nullptr,                                     "scePower_545A7F3C",                 '?', ""   }, // TODO: Supposedly the same as SetClockFrequency also?
	{0XA4E93389, nullptr,                                     "scePower_A4E93389",                 '?', ""   }, // TODO: Supposedly the same as SetClockFrequency also?
	{0XA85880D0, &WrapU_V<IsPSPNonFat>,                       "scePower_a85880d0_IsPSPNonFat",     'x', ""   },
	{0X3951AF53, nullptr,                                     "scePowerWaitRequestCompletion",     '?', ""   },
	{0X0442D852, nullptr,                                     "scePowerRequestColdReset",          '?', ""   },
	{0XBAFA3DF0, nullptr,                                     "scePowerGetCallbackMode",           '?', ""   },
	{0XA9D22232, nullptr,                                     "scePowerSetCallbackMode",           '?', ""   },

	// These seem to be aliases.
	{0X23C31FFE, &WrapI_IUU<sceKernelVolatileMemLock>,        "scePowerVolatileMemLock",           'i', "ixx"},
	{0XFA97A599, &WrapI_IUU<sceKernelVolatileMemTryLock>,     "scePowerVolatileMemTryLock",        'i', "ixx"},
	{0XB3EDD801, &WrapI_I<sceKernelVolatileMemUnlock>,        "scePowerVolatileMemUnlock",         'i', "i"  },
};

//890129c in tyshooter looks bogus
const HLEFunction sceSuspendForUser[] = {
	{0XEADB1BD7, &WrapI_I<sceKernelPowerLock>,                "sceKernelPowerLock",                'i', "i"  }, //(int param) set param to 0
	{0X3AEE7261, &WrapI_I<sceKernelPowerUnlock>,              "sceKernelPowerUnlock",              'i', "i"  }, //(int param) set param to 0
	{0X090CCB3F, &WrapI_I<sceKernelPowerTick>,                "sceKernelPowerTick",                'i', "i"  },

	// There's an extra 4MB that can be allocated, which seems to be "volatile". These functions
	// let you grab it.
	{0XA14F40B2, &WrapI_IUU<sceKernelVolatileMemTryLock>,     "sceKernelVolatileMemTryLock",       'i', "ixx"},
	{0XA569E425, &WrapI_I<sceKernelVolatileMemUnlock>,        "sceKernelVolatileMemUnlock",        'i', "i"  },
	{0X3E0271D3, &WrapI_IUU<sceKernelVolatileMemLock>,        "sceKernelVolatileMemLock",          'i', "ixx"},
};


void Register_scePower() {
	RegisterModule("scePower",ARRAY_SIZE(scePower),scePower);
}

void Register_sceSuspendForUser() {
	RegisterModule("sceSuspendForUser", ARRAY_SIZE(sceSuspendForUser), sceSuspendForUser);
}
