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
	Memory::WriteOrException_U32(0, keyAddress);
	return hleLogDebug(Log::HLE, 0);
}

// TODO: Might make sense to reflect the headphone status of the host here,
// if the games adjust their sound.
static u32 sceHprmIsHeadphoneExist() {
	return hleLogDebug(Log::HLE, 0);
}

static u32 sceHprmIsMicrophoneExist() {
	return hleLogDebug(Log::HLE, 0);
}

static u32 sceHprmIsRemoteExist() {
	return hleLogDebug(Log::HLE, 0);
}

static u32 sceHprmRegisterCallback() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static u32 sceHprmUnregisterCallback() {
	return hleLogError(Log::HLE, 0, "UNIMPL");
}

static u32 sceHprmPeekLatch(u32 latchAddr) {
	return hleLogDebug(Log::HLE,0, "latchAddr %08x", latchAddr);
}

static u32 sceHprmReadLatch(u32 latchAddr) {
	// Real hardware/JPCSP: 4 x u32 output struct, dummied out to all-zero (nothing held/pressed).
	for (int i = 0; i < 4; i++)
		Memory::WriteOrException_U32(0, latchAddr + i * 4);
	return hleLogDebug(Log::HLE, 0, "latchAddr %08x", latchAddr);
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
	RegisterHLEModule("sceHprm", ARRAY_SIZE(sceHprm), sceHprm);
}

// Kernel-mode variant library used by VSH modules (e.g. vshbridge). NID 0xE9B776BE is the
// firmware 6.60+ alias of sceHprmReadLatch, listed as nid=0xE9B776BE version=660 alongside the
// 0x40D2F9F0 version=150 NID in JPCSP's sceHprm.java - both resolve to the same function there.
static u32 sceHprm_driver_DC895B2B() {
	return hleLogWarning(Log::sceMisc, 0, "UNIMPL");
}

const HLEFunction sceHprm_driver[] =
{
	{0XE9B776BE, &WrapU_U<sceHprmReadLatch>, "sceHprmReadLatch", 'x', "x"},
	// Purpose unknown - JPCSP names it after its NID too, and returns 0. Present so the VSH's
	// one startup call resolves instead of trapping.
	{0XDC895B2B, &WrapU_V<sceHprm_driver_DC895B2B>, "sceHprm_driver_DC895B2B", 'x', ""},
};

void Register_sceHprm_driver()
{
	RegisterHLEModule("sceHprm_driver", ARRAY_SIZE(sceHprm_driver), sceHprm_driver);
}
