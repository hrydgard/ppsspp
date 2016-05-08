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

static int sceNp_857B47D3()
{
	// No parameters
	ERROR_LOG(HLE, "UNIMPL sceNp_857B47D3()");
	return 0;
}

static int sceNp_37E1E274()
{
	// No parameters
	ERROR_LOG(HLE, "UNIMPL sceNp_37E1E274()");
	return 0;
}

const HLEFunction sceNp[] = {
	{0X857B47D3, &WrapI_V<sceNp_857B47D3>,           "sceNp_857B47D3",          'i', ""   },
	{0X37E1E274, &WrapI_V<sceNp_37E1E274>,           "sceNp_37E1E274",          'i', ""   },
};

void Register_sceNp()
{
	RegisterModule("sceNp", ARRAY_SIZE(sceNp), sceNp);
}

static int sceNpAuth_4EC1F667()
{
	// No parameters
	ERROR_LOG(HLE, "UNIMPL sceNpAuth_4EC1F667()");
	return 0;
}

static int sceNpAuth_A1DE86F8(u32 poolSize, u32 stackSize, u32 threadPrio)
{
	ERROR_LOG(HLE, "UNIMPL sceNpAuth_A1DE86F8(%08x, %08x, %08x)",poolSize, stackSize, threadPrio);
	return 0;
}

const HLEFunction sceNpAuth[] = {
	{0X4EC1F667, &WrapI_V<sceNpAuth_4EC1F667>,       "sceNpAuth_4EC1F667",      'i', ""   },
	{0XA1DE86F8, &WrapI_UUU<sceNpAuth_A1DE86F8>,     "sceNpAuth_A1DE86F8",      'i', "xxx"},
};

void Register_sceNpAuth()
{
	RegisterModule("sceNpAuth", ARRAY_SIZE(sceNpAuth), sceNpAuth);
}

static int sceNpService_00ACFAC3()
{
	// No parameters
	ERROR_LOG(HLE, "UNIMPL sceNpService_00ACFAC3()");
	return 0;
}

static int sceNpService_0F8F5821(u32 poolSize, u32 stackSize, u32 threadPrio)
{
	ERROR_LOG(HLE, "UNIMPL sceNpService_0F8F5821(%08x, %08x, %08x)",poolSize, stackSize, threadPrio);
	return 0;
}

const HLEFunction sceNpService[] = {
	{0X00ACFAC3, &WrapI_V<sceNpService_00ACFAC3>,    "sceNpService_00ACFAC3",   'i', ""   },
	{0X0F8F5821, &WrapI_UUU<sceNpService_0F8F5821>,  "sceNpService_0F8F5821",   'i', "xxx"},
};

void Register_sceNpService()
{
	RegisterModule("sceNpService", ARRAY_SIZE(sceNpService), sceNpService);
}

const HLEFunction sceNpCommerce2[] = {
	{0X005B5F20, nullptr,                            "sceNpCommerce2_005B5F20", '?', ""   },
	{0X0E9956E3, nullptr,                            "sceNpCommerce2_0e9956e3", '?', ""   },
	{0X1888A9FE, nullptr,                            "sceNpCommerce2_1888a9fe", '?', ""   },
	{0X1C952DCB, nullptr,                            "sceNpCommerce2_1c952dcb", '?', ""   },
	{0X2B25F6E9, nullptr,                            "sceNpCommerce2_2b25f6e9", '?', ""   },
	{0X3371D5F1, nullptr,                            "sceNpCommerce2_3371d5f1", '?', ""   },
	{0X4ECD4503, nullptr,                            "sceNpCommerce2_4ecd4503", '?', ""   },
	{0X590A3229, nullptr,                            "sceNpCommerce2_590a3229", '?', ""   },
	{0X6F1FE37F, nullptr,                            "sceNpCommerce2_6f1fe37f", '?', ""   },
	{0XA5A34EA4, nullptr,                            "sceNpCommerce2_a5a34ea4", '?', ""   },
	{0XAA4A1E3D, nullptr,                            "sceNpCommerce2_aa4a1e3d", '?', ""   },
	{0XBC61FFC8, nullptr,                            "sceNpCommerce2_bc61ffc8", '?', ""   },
	{0XC7F32242, nullptr,                            "sceNpCommerce2_c7f32242", '?', ""   },
	{0XF2278B90, nullptr,                            "sceNpCommerce2_f2278b90", '?', ""   },
	{0XF297AB9C, nullptr,                            "sceNpCommerce2_f297ab9c", '?', ""   },
	{0XFC30C19E, nullptr,                            "sceNpCommerce2_fc30c19e", '?', ""   },
};

void Register_sceNpCommerce2()
{
	RegisterModule("sceNpCommerce2", ARRAY_SIZE(sceNpCommerce2), sceNpCommerce2);
}