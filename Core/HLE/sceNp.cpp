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
// to keep games happy anyway. So, no ATRAC3 music until someone has reverse engineered Atrac3+.

#include "HLE.h"

#include "sceNp.h"

int sceNp_857B47D3()
{
	// No parameters
	ERROR_LOG(HLE, "UNIMPL sceNp_857B47D3()");
	return 0;
}

int sceNp_37E1E274()
{
	// No parameters
	ERROR_LOG(HLE, "UNIMPL sceNp_37E1E274()");
	return 0;
}

const HLEFunction sceNp[] = {
	{0x857B47D3, &WrapI_V<sceNp_857B47D3>, "sceNp_857B47D3"},
	{0x37E1E274, &WrapI_V<sceNp_37E1E274>, "sceNp_37E1E274"},
};

void Register_sceNp()
{
	RegisterModule("sceNp", ARRAY_SIZE(sceNp), sceNp);
}

const HLEFunction sceNpAuth[] = {
	{0x4EC1F667, 0, "sceNpAuth_4EC1F667"},
	{0xA1DE86F8, 0, "sceNpAuth_A1DE86F8"},
};

void Register_sceNpAuth()
{
	RegisterModule("sceNpAuth", ARRAY_SIZE(sceNpAuth), sceNpAuth);
}

const HLEFunction sceNpService[] = {
	{0x00ACFAC3, 0, "sceNpService_00ACFAC3"},
	{0x0F8F5821, 0, "sceNpService_0F8F5821"},
};

void Register_sceNpService()
{
	RegisterModule("sceNpService", ARRAY_SIZE(sceNpService), sceNpService);
}

const HLEFunction sceNpCommerce2[] = {
	{0x005B5F20, 0, "sceNpCommerce2_005B5F20"},
	{0x0e9956e3, 0, "sceNpCommerce2_0e9956e3"},
	{0x1888a9fe, 0, "sceNpCommerce2_1888a9fe"},
	{0x1c952dcb, 0, "sceNpCommerce2_1c952dcb"},
	{0x2b25f6e9, 0, "sceNpCommerce2_2b25f6e9"},
	{0x3371d5f1, 0, "sceNpCommerce2_3371d5f1"},
	{0x4ecd4503, 0, "sceNpCommerce2_4ecd4503"},
	{0x590a3229, 0, "sceNpCommerce2_590a3229"},
	{0x6f1fe37f, 0, "sceNpCommerce2_6f1fe37f"},
	{0xa5a34ea4, 0, "sceNpCommerce2_a5a34ea4"},
	{0xaa4a1e3d, 0, "sceNpCommerce2_aa4a1e3d"},
	{0xbc61ffc8, 0, "sceNpCommerce2_bc61ffc8"},
	{0xc7f32242, 0, "sceNpCommerce2_c7f32242"},
	{0xf2278b90, 0, "sceNpCommerce2_f2278b90"},
	{0xf297ab9c, 0, "sceNpCommerce2_f297ab9c"},
	{0xfc30c19e, 0, "sceNpCommerce2_fc30c19e"},
};

void Register_sceNpCommerce2()
{
	RegisterModule("sceNpCommerce2", ARRAY_SIZE(sceNpCommerce2), sceNpCommerce2);
}