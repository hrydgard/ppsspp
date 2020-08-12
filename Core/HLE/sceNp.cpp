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

// This is pretty much a stub implementation. Doesn't actually do anything, just tries to return values
// to keep games happy anyway.

#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"

#include "Core/HLE/sceNp.h"

static int sceNpInit(u32 poolsize, u32 poolptr)
{
	ERROR_LOG(HLE, "UNIMPL %s(%08x, %08x) at %08x", __FUNCTION__, poolsize, poolptr, currentMIPS->pc);
	return 0;
}

static int sceNpTerm()
{
	// No parameters
	ERROR_LOG(HLE, "UNIMPL %s()", __FUNCTION__);
	return 0;
}

static int sceNpGetContentRatingFlag()
{
	ERROR_LOG(HLE, "UNIMPL %s()", __FUNCTION__);
	return 0;
}

const HLEFunction sceNp[] = {
	{0X857B47D3, &WrapI_UU<sceNpInit>,					"sceNpInit",					'i', "xx" },
	{0X37E1E274, &WrapI_V<sceNpTerm>,					"sceNpTerm",					'i', ""   },
	{0XBB069A87, &WrapI_V<sceNpGetContentRatingFlag>,	"sceNpGetContentRatingFlag",	'i', ""   },
};

void Register_sceNp()
{
	RegisterModule("sceNp", ARRAY_SIZE(sceNp), sceNp);
}

static int sceNpAuthTerm()
{
	// No parameters
	ERROR_LOG(HLE, "UNIMPL %s()", __FUNCTION__);
	return 0;
}

static int sceNpAuthInit(u32 poolSize, u32 stackSize, u32 threadPrio) 
{
	ERROR_LOG(HLE, "UNIMPL %s(%08x, %08x, %08x)", __FUNCTION__, poolSize, stackSize, threadPrio);
	return 0;
}

/* 
"Authenticating matching server usage license" on Patapon 3. Could be waiting for a state change for eternity? probably need to trigger a callback handler?
unknownPtr seems to be a struct where offset: 
	+00: 32-bit is the size of the struct (ie. 36 bytes),
	+04: 32-bit is also a small number (ie. 3) a mode/event/flag enum may be?,
	+08: 32-bit is a pointer to a productId? (ie. "EP9000-UCES01421_00"),
	+0C: 4x 32-bit reserved? all zero
	+1C: 32-bit random data or callback handler? optional handler? seems to be a valid pointer and pointing to a starting point of a function (have a label on the disassembly)
	+20: 32-bit a pointer to a random data (4 to 8-bytes data max? both 2x 32-bit seems to be a valid pointer). optional handler args?
return value >= 0 and <0 seems to be stored at a different location by the game (valid result vs error code?)
*/
static int sceNpAuthCreateStartRequest(u32 unknownPtr)
{
	ERROR_LOG(HLE, "UNIMPL %s(%08x) at %08x", __FUNCTION__, unknownPtr, currentMIPS->pc);
	if (!Memory::IsValidAddress(unknownPtr))
		return hleLogError(HLE, -1, "invalid arg");

	u32* params = (u32*)Memory::GetPointer(unknownPtr);
	INFO_LOG(HLE, "%s - Product ID: %s", __FUNCTION__, Memory::IsValidAddress(params[2]) ? Memory::GetCharPointer(params[2]):"");
	// Forcing callback args to bypass all the proper procedure and attempt to go straight to a fake success, might not works properly tho.
	// 1st Arg usually either an ID returned from Create/AddHandler function or an Event ID if the game expecting a sequence of events.
	// 2nd Arg seems to be used if not a negative number and exits the handler if it's negative (error code?)
	// 3rd Arg seems to be a data (ie. 92 bytes of data?) pointer and tested for null within callback handler (optional callback args?)
	if (params[0] >= 32) {
		// These callback's args seems to be similar to the args of Adhocctl handler? (ie. event/flag, error, handlerArg)
		u32 args[3] = { 0, 0, (params[0] >= 36) ? params[8] : 0 };
		hleEnqueueCall(params[7], 3, args);
	}

	hleDelayResult(0, "give time", 500000);
	return 0;
}

// Used within callback of sceNpAuthCreateStartRequest (arg1 = callback's args[0], arg2 = structPtr?, arg3 = args[1])
static int sceNpAuthGetTicket(u32 arg1, u32 arg2Ptr, u32 arg3)
{
	ERROR_LOG(HLE, "UNIMPL %s(%d, %08x, %d)", __FUNCTION__, arg1, arg2Ptr, arg3);
	return 0;
}

// Used within callback of sceNpAuthCreateStartRequest (arg1 = structPtr?, arg2 = args[1], arg3 = DLCcode? ie. "EP9000-UCES01421_00-DLL001", arg4 = 0)
static int sceNpAuthGetEntitlementById(u32 arg1Ptr, u32 arg2, u32 arg3Ptr, u32 arg4)
{
	ERROR_LOG(HLE, "UNIMPL %s(%08x, %d, %08x, %d)", __FUNCTION__, arg1Ptr, arg2, arg3Ptr, arg4);
	INFO_LOG(HLE, "%s - Product ID: %s", __FUNCTION__, Memory::IsValidAddress(arg3Ptr) ? Memory::GetCharPointer(arg3Ptr) : "");
	return 0;
}

const HLEFunction sceNpAuth[] = {
	{0X4EC1F667, &WrapI_V<sceNpAuthTerm>,						"sceNpAuthTerm",				'i', ""     },
	{0XA1DE86F8, &WrapI_UUU<sceNpAuthInit>,						"sceNpAuthInit",				'i', "xxx"  },
	{0XCD86A656, &WrapI_U<sceNpAuthCreateStartRequest>,			"sceNpAuthCreateStartRequest",	'i', "x"    },
	{0X3F1C1F70, &WrapI_UUU<sceNpAuthGetTicket>,				"sceNpAuthGetTicket",			'i', "xxx"  },
	{0X6900F084, &WrapI_UUUU<sceNpAuthGetEntitlementById>,		"sceNpAuthGetEntitlementById",	'i', "xxxx" },
};

void Register_sceNpAuth()
{
	RegisterModule("sceNpAuth", ARRAY_SIZE(sceNpAuth), sceNpAuth);
}

static int sceNpServiceTerm()
{
	// No parameters
	ERROR_LOG(HLE, "UNIMPL %s()", __FUNCTION__);
	return 0;
}

static int sceNpServiceInit(u32 poolSize, u32 stackSize, u32 threadPrio) 
{
	ERROR_LOG(HLE, "UNIMPL %s(%08x, %08x, %08x)", __FUNCTION__, poolSize, stackSize, threadPrio);
	return 0;
}

const HLEFunction sceNpService[] = {
	{0X00ACFAC3, &WrapI_V<sceNpServiceTerm>,    "sceNpServiceTerm",   'i', ""   },
	{0X0F8F5821, &WrapI_UUU<sceNpServiceInit>,  "sceNpServiceInit",   'i', "xxx"},
};

void Register_sceNpService()
{
	RegisterModule("sceNpService", ARRAY_SIZE(sceNpService), sceNpService);
}

const HLEFunction sceNpCommerce2[] = {
	{0X005B5F20, nullptr,                            "sceNpCommerce2GetProductInfoStart",				'?', ""   },
	{0X0E9956E3, nullptr,                            "sceNpCommerce2Init",								'?', ""   },
	{0X1888A9FE, nullptr,                            "sceNpCommerce2DestroyReq",						'?', ""   },
	{0X1C952DCB, nullptr,                            "sceNpCommerce2GetGameProductInfo",				'?', ""   },
	{0X2B25F6E9, nullptr,                            "sceNpCommerce2CreateSessionStart",				'?', ""   },
	{0X3371D5F1, nullptr,                            "sceNpCommerce2GetProductInfoCreateReq",			'?', ""   },
	{0X4ECD4503, nullptr,                            "sceNpCommerce2CreateSessionCreateReq",			'?', ""   },
	{0X590A3229, nullptr,                            "sceNpCommerce2GetSessionInfo",					'?', ""   },
	{0X6F1FE37F, nullptr,                            "sceNpCommerce2CreateCtx",							'?', ""   },
	{0XA5A34EA4, nullptr,                            "sceNpCommerce2Term",								'?', ""   },
	{0XAA4A1E3D, nullptr,                            "sceNpCommerce2GetProductInfoGetResult",			'?', ""   },
	{0XBC61FFC8, nullptr,                            "sceNpCommerce2CreateSessionGetResult",			'?', ""   },
	{0XC7F32242, nullptr,                            "sceNpCommerce2AbortReq",							'?', ""   },
	{0XF2278B90, nullptr,                            "sceNpCommerce2GetGameSkuInfoFromGameProductInfo",	'?', ""   },
	{0XF297AB9C, nullptr,                            "sceNpCommerce2DestroyCtx",						'?', ""   },
	{0XFC30C19E, nullptr,                            "sceNpCommerce2InitGetProductInfoResult",			'?', ""   },
};

void Register_sceNpCommerce2()
{
	RegisterModule("sceNpCommerce2", ARRAY_SIZE(sceNpCommerce2), sceNpCommerce2);
}
