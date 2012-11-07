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
#include "../MIPS/MIPS.h"
#include "../CoreTiming.h"

#include "scePower.h"

static bool volatileMemLocked;

void scePowerGetBatteryLifePercent()
{
	DEBUG_LOG(HLE, "100=scePowerGetBatteryLifePercent");
	RETURN(100);
}
void scePowerIsPowerOnline()
{
	DEBUG_LOG(HLE, "1=scePowerIsPowerOnline");
	RETURN( 1);
}
void scePowerIsBatteryExist()
{
	DEBUG_LOG(HLE, "1=scePowerIsBatteryExist");
	RETURN( 1);
}
void scePowerIsBatteryCharging()
{
	DEBUG_LOG(HLE, "0=scePowerIsBatteryCharging");
	RETURN( 0);
}
void scePowerGetBatteryChargingStatus()
{
	DEBUG_LOG(HLE, "0=scePowerGetBatteryChargingStatus");
	RETURN( 0);
}
void scePowerIsLowBattery()
{
	DEBUG_LOG(HLE, "0=scePowerIsLowBattery");
	RETURN( 0);
}

void scePowerRegisterCallback()
{
	DEBUG_LOG(HLE,"0=scePowerRegisterCallback() UNIMPL");
	RETURN (0);
}
void sceKernelPowerLock()
{
	DEBUG_LOG(HLE,"UNIMPL 0=sceKernelPowerLock()");
	RETURN (0);
}
void sceKernelPowerUnlock()
{
	DEBUG_LOG(HLE,"UNIMPL 0=sceKernelPowerUnlock()");
	RETURN (0);
}
void sceKernelPowerTick()
{
	DEBUG_LOG(HLE,"UNIMPL 0=sceKernelPowerTick()");
	RETURN (0);
}

#define ERROR_POWER_VMEM_IN_USE 0x802b0200

void sceKernelVolatileMemTryLock()
{
  int type = PARAM(0);
  int paddr = PARAM(1);
  int psize = PARAM(2);

  if (!volatileMemLocked)
  {
    INFO_LOG(HLE,"sceKernelVolatileMemTryLock(%i, %08x, %i) - success", type, paddr, psize);
    volatileMemLocked = true;
  }
  else
  {
    ERROR_LOG(HLE, "sceKernelVolatileMemTryLock - already locked!");
    RETURN(ERROR_POWER_VMEM_IN_USE);
    return;
  }

  // Volatile RAM is always at 0x08400000 and is of size 0x00400000.
  // It's always available in the emu.
  Memory::Write_U32(0x08400000, paddr);
  Memory::Write_U32(0x00400000, psize);

  RETURN(0);
}

void sceKernelVolatileMemUnlock()
{
  INFO_LOG(HLE,"sceKernelVolatileMemUnlock()");
  // TODO: sanity check
  volatileMemLocked = false;
}

void sceKernelVolatileMemLock()
{
  sceKernelVolatileMemTryLock();
}


void scePowerSetClockFrequency(u32 cpufreq, u32 busfreq, u32 gpufreq)
{
  CoreTiming::SetClockFrequencyMHz(cpufreq);

  INFO_LOG(HLE,"scePowerSetClockFrequency(%i,%i,%i)", cpufreq, busfreq, gpufreq);
}

void scePowerGetCpuClockFrequencyInt() {
  INFO_LOG(HLE,"scePowerGetCpuClockFrequencyInt()");
  RETURN(CoreTiming::GetClockFrequencyMHz());
}

static const HLEFunction scePower[] = 
{
  {0x04B7766E,scePowerRegisterCallback,"scePowerRegisterCallback"},
  {0x2B51FE2F,0,"scePower_2B51FE2F"},
  {0x442BFBAC,0,"scePowerGetBacklightMaximum"},
  {0xEFD3C963,0,"scePowerTick"},
  {0xEDC13FE5,0,"scePowerGetIdleTimer"},
  {0x7F30B3B1,0,"scePowerIdleTimerEnable"},
  {0x972CE941,0,"scePowerIdleTimerDisable"},
  {0x27F3292C,0,"scePowerBatteryUpdateInfo"},
  {0xE8E4E204,0,"scePower_E8E4E204"},
  {0xB999184C,0,"scePowerGetLowBatteryCapacity"},
  {0x87440F5E,scePowerIsPowerOnline,"scePowerIsPowerOnline"},
  {0x0AFD0D8B,scePowerIsBatteryExist,"scePowerIsBatteryExist"},
  {0x1E490401,scePowerIsBatteryCharging,"scePowerIsBatteryCharging"},
  {0xB4432BC8,scePowerGetBatteryChargingStatus,"scePowerGetBatteryChargingStatus"},
  {0xD3075926,scePowerIsLowBattery,"scePowerIsLowBattery"},
  {0x78A1A796,0,"scePowerIsSuspendRequired"},
  {0x94F5A53F,0,"scePowerGetBatteryRemainCapacity"},
  {0xFD18A0FF,0,"scePowerGetBatteryFullCapacity"},
  {0x2085D15D,scePowerGetBatteryLifePercent,"scePowerGetBatteryLifePercent"},
  {0x8EFB3FA2,0,"scePowerGetBatteryLifeTime"},
  {0x28E12023,0,"scePowerGetBatteryTemp"},
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
  {0x04B7766E,0,"scePowerRegisterCallback"},
  {0xDFA8BAF8,0,"scePowerUnregisterCallback"},
  {0xDB9D28DD,0,"scePowerUnregitserCallback"},  //haha
  {0x843FBF43,0,"scePowerSetCpuClockFrequency"},
  {0xB8D7B3FB,0,"scePowerSetBusClockFrequency"},
  {0xFEE03A2F,0,"scePowerGetCpuClockFrequency"},
  {0x478FE6F5,0,"scePowerGetBusClockFrequency"},
  {0xFDB5BFE9,scePowerGetCpuClockFrequencyInt,"scePowerGetCpuClockFrequencyInt"},
  {0xBD681969,0,"scePowerGetBusClockFrequencyInt"},
  {0xB1A52C83,0,"scePowerGetCpuClockFrequencyFloat"},
  {0x9BADB3EB,0,"scePowerGetBusClockFrequencyFloat"},
  {0x737486F2,&WrapV_UUU<scePowerSetClockFrequency>,"scePowerSetClockFrequency"},
  {0x34f9c463,0,"scePowerGetPllClockFrequencyInt"},
  {0xea382a27,0,"scePowerGetPllClockFrequencyFloat"},
  {0xebd177d6,0,"scePower_driver_EBD177D6"},
	{0x469989ad,0,"scePower_469989ad"},
	{0xa85880d0,0,"scePower_a85880d0"},
};

//890129c in tyshooter looks bogus
const HLEFunction sceSuspendForUser[] =
{
  {0xEADB1BD7,sceKernelPowerLock,"sceKernelPowerLock"}, //(int param) set param to 0
  {0x3AEE7261,sceKernelPowerUnlock,"sceKernelPowerUnlock"},//(int param) set param to 0
  {0x090ccb3f,sceKernelPowerTick,"sceKernelPowerTick"},

  // There's an extra 4MB that can be allocated, which seems to be "volatile". These functions
  // let you grab it.
  {0xa14f40b2,sceKernelVolatileMemTryLock,"sceKernelVolatileMemTryLock"},
  {0xa569e425,sceKernelVolatileMemUnlock,"sceKernelVolatileMemUnlock"},
  {0x3e0271d3,sceKernelVolatileMemLock,"sceKernelVolatileMemLock"}, //when "acquiring mem pool" (fired up)
};


void Register_scePower() {
  RegisterModule("scePower",ARRAY_SIZE(scePower),scePower);
}

void Register_sceSuspendForUser() {
  RegisterModule("sceSuspendForUser", ARRAY_SIZE(sceSuspendForUser), sceSuspendForUser);
}
