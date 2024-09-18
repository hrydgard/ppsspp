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

#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceHprm.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"

static u32 sceHprmPeekCurrentKey(u32 keyAddress) {
	DEBUG_LOG(Log::HLE,"0=sceHprmPeekCurrentKey(ptr)");
	Memory::Write_U32(0, keyAddress);
	return 0;
}

// TODO: Might make sense to reflect the headphone status of the host here,
// if the games adjust their sound.
static u32 sceHprmIsHeadphoneExist() {
	DEBUG_LOG(Log::HLE, "sceHprmIsHeadphoneExist()");
	return 0;
}

static u32 sceHprmIsMicrophoneExist() {
	DEBUG_LOG(Log::HLE, "sceHprmIsMicrophoneExist()");
	return 0;
}

static u32 sceHprmIsRemoteExist() {
	DEBUG_LOG(Log::HLE, "sceHprmIsRemoteExist()");
	return 0;
}

static u32 sceHprmRegisterCallback() {
	ERROR_LOG(Log::HLE, "UNIMPL %s", __FUNCTION__);
	return 0;
}

static u32 sceHprmUnregisterCallback() {
	ERROR_LOG(Log::HLE, "UNIMPL %s", __FUNCTION__);
	return 0;
}

static u32 sceHprmPeekLatch(u32 latchAddr) {
	DEBUG_LOG(Log::HLE,"sceHprmPeekLatch latchAddr %08x",latchAddr);
	return 0;
}

static u32 sceHprmReadLatch(u32 latchAddr) {
	DEBUG_LOG(Log::HLE,"sceHprmReadLatch latchAddr %08x",latchAddr);
	return 0;
}

const HLEFunction sceHprm[] = 
{
	{0X089FDFA4, nullptr,                            "sceHprm_089fdfa4",          '?', "" },
	{0X1910B327, &WrapU_U<sceHprmPeekCurrentKey>,    "sceHprmPeekCurrentKey",     'x', "x"},
	{0X208DB1BD, &WrapU_V<sceHprmIsRemoteExist>,     "sceHprmIsRemoteExist",      'x', "" },
	{0X7E69EDA4, &WrapU_V<sceHprmIsHeadphoneExist>,  "sceHprmIsHeadphoneExist",   'x', "" },
	{0X219C58F1, &WrapU_V<sceHprmIsMicrophoneExist>, "sceHprmIsMicrophoneExist",  'x', "" },
	{0XC7154136, &WrapU_V<sceHprmRegisterCallback>,  "sceHprmRegisterCallback",   'x', "" },
	{0xFD7DE6CD, &WrapU_V<sceHprmUnregisterCallback>,"sceHprmUnregisterCallback", 'x', "" },
	{0X444ED0B7, nullptr,                            "sceHprmUnregitserCallback", '?', "" }, // Typo.
	{0X2BCEC83E, &WrapU_U<sceHprmPeekLatch>,         "sceHprmPeekLatch",          'x', "x"},
	{0X40D2F9F0, &WrapU_U<sceHprmReadLatch>,         "sceHprmReadLatch",          'x', "x"},
};

void Register_sceHprm()
{
	RegisterModule("sceHprm", ARRAY_SIZE(sceHprm), sceHprm);
}
