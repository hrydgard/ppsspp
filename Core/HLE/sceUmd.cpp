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
#include "sceUmd.h"
#include "sceKernelThread.h"

#define UMD_NOT_PRESENT 0x01
#define UMD_PRESENT		 0x02
#define UMD_CHANGED		 0x04
#define UMD_NOT_READY	 0x08
#define UMD_READY			 0x10
#define UMD_READABLE		0x20


u8 umdActivated = 1;
u32 umdStatus = 0;
u32 umdErrorStat = 0;


#define PSP_UMD_TYPE_GAME 0x10
#define PSP_UMD_TYPE_VIDEO 0x20
#define PSP_UMD_TYPE_AUDIO 0x40

struct PspUmdInfo {
	int type;
};


void __UmdInit() {
	umdActivated = 1;
	umdStatus = 0;
	umdErrorStat = 0;
}

u8 __KernelUmdGetState()
{
	u8 state = UMD_PRESENT;
	if (umdActivated) {
		state |= UMD_READY;
		state |= UMD_READABLE;
	}
	else
		state |= UMD_NOT_READY;
	return state;
}

void __KernelUmdActivate()
{
	umdActivated = 1;
}

void __KernelUmdDeactivate()
{
	umdActivated = 0;
}

// typedef int (*UmdCallback)(int unknown, int event);


//int sceUmdCheckMedium(int a);
void sceUmdCheckMedium()
{
	DEBUG_LOG(HLE,"1=sceUmdCheckMedium(?)");
	//ignore PARAM(0)	
	RETURN(1); //non-zero: disc in drive
}
	
void sceUmdGetDiscInfo()
{
	u32 infoAddr = PARAM(0);
	ERROR_LOG(HLE,"sceUmdGetDiscInfo(%08x)", infoAddr);
	PspUmdInfo info;
	info.type = PSP_UMD_TYPE_GAME;
	if (Memory::IsValidAddress(infoAddr))
	{
		Memory::WriteStruct(infoAddr, &info);
	}
	RETURN(0);
}

void sceUmdActivate()
{
	u32 unknown = PARAM(0);
	const char *name = Memory::GetCharPointer(PARAM(1));
	u32 retVal	= 0;
	__KernelUmdActivate();
	DEBUG_LOG(HLE,"%i=sceUmdActivate(%08x, %s)", retVal, unknown, name);
	u32 notifyArg = UMD_PRESENT | UMD_READABLE;
	__KernelNotifyCallbackType(THREAD_CALLBACK_UMD, -1, notifyArg);
	RETURN(retVal);
}

void sceUmdDeactivate()
{
	DEBUG_LOG(HLE,"sceUmdDeactivate()");
	bool triggerCallback = umdActivated;
	__KernelUmdDeactivate();

	if (triggerCallback) {
		u32 notifyArg = UMD_PRESENT | UMD_READY;
		__KernelNotifyCallbackType(THREAD_CALLBACK_UMD, -1, notifyArg);
	}
	RETURN(0);
}

u32 sceUmdRegisterUMDCallBack(u32 cbId)
{
	DEBUG_LOG(HLE,"0=sceUmdRegisterUMDCallback(id=%i)",PARAM(0));
	return __KernelRegisterCallback(THREAD_CALLBACK_UMD, cbId);
}

u32 sceUmdUnRegisterUMDCallBack(u32 cbId)
{
	DEBUG_LOG(HLE,"0=sceUmdUnRegisterUMDCallBack(id=%i)",PARAM(0));
	return __KernelUnregisterCallback(THREAD_CALLBACK_UMD, cbId);
}

void sceUmdGetDriveStat()
{
	//u32 retVal = PSP_UMD_INITED | PSP_UMD_READY | PSP_UMD_PRESENT;
	u32 retVal = __KernelUmdGetState();
	DEBUG_LOG(HLE,"0x%02x=sceUmdGetDriveStat()",retVal);
	RETURN(retVal);
}

/** 
* Wait for a drive to reach a certain state
*
* @param stat - The drive stat to wait for.
* @return < 0 on error
*
*/
void sceUmdWaitDriveStat()
{
	u32 stat = PARAM(0);
	ERROR_LOG(HLE,"UNIMPL 0=sceUmdWaitDriveStat(stat = %08x)", stat);
	//if ((stat & __KernelUmdGetState()) != stat)
	//	__KernelWaitCurThread(WAITTYPE_UMD, 0, stat, 0, 0);	//__KernelWaitCurThread(WAITTYPE_UMD, 0);
	RETURN(0);
}

void sceUmdWaitDriveStatWithTimer()
{
	u32 stat = PARAM(0);
	ERROR_LOG(HLE,"UNIMPL 0=sceUmdWaitDriveStatWithTimer(stat = %08x)", stat);
	//__KernelWaitCurThread(WAITTYPE_UMD, 0);
	RETURN(0);
}

void sceUmdWaitDriveStatCB()
{
	u32 stat = PARAM(0);
	ERROR_LOG(HLE,"UNIMPL 0=sceUmdWaitDriveStatCB(stat = %08x)", stat);
	//__KernelWaitCurThread(WAITTYPE_UMD, 0);
	RETURN(0);
}

void sceUmdCancelWaitDriveStat()
{
	u32 stat = PARAM(0);
	ERROR_LOG(HLE,"UNIMPL 0=sceUmdCancelWaitDriveStat(stat = %08x)", stat);
	RETURN(0);
}

void sceUmdGetErrorStat()
{
	DEBUG_LOG(HLE,"%i=sceUmdGetErrorStat()", umdErrorStat);
	RETURN(umdErrorStat);
}


const HLEFunction sceUmdUser[] = 
{
	{0xC6183D47,sceUmdActivate,"sceUmdActivate"},
	{0x6B4A146C,sceUmdGetDriveStat,"sceUmdGetDriveStat"},
	{0x46EBB729,sceUmdCheckMedium,"sceUmdCheckMedium"},
	{0xE83742BA,sceUmdDeactivate,"sceUmdDeactivate"},
	{0x8EF08FCE,sceUmdWaitDriveStat,"sceUmdWaitDriveStat"},
	{0x56202973,sceUmdWaitDriveStatWithTimer,"sceUmdWaitDriveStatWithTimer"},
	{0x4A9E5E29,sceUmdWaitDriveStatCB,"sceUmdWaitDriveStatCB"},
	{0x6af9b50a,sceUmdCancelWaitDriveStat,"sceUmdCancelWaitDriveStat"},
	{0x6B4A146C,sceUmdGetDriveStat,"sceUmdGetDriveStat"},
	{0x20628E6F,sceUmdGetErrorStat,"sceUmdGetErrorStat"},
	{0x340B7686,sceUmdGetDiscInfo,"sceUmdGetDiscInfo"},
	{0xAEE7404D,&WrapU_U<sceUmdRegisterUMDCallBack>,"sceUmdRegisterUMDCallBack"},
	{0xBD2BDE07,&WrapU_U<sceUmdUnRegisterUMDCallBack>,"sceUmdUnRegisterUMDCallBack"},
	{0x87533940,0,"sceUmdReplaceProhibit"},	// ??? sounds bogus
};

void Register_sceUmdUser()
{
	RegisterModule("sceUmdUser", ARRAY_SIZE(sceUmdUser), sceUmdUser);
}
