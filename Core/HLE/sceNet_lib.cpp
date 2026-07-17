// Copyright (c) 2025- PPSSPP Project.

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


#include "Core/HLE/sceNet_lib.h"

#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/HLE.h"
#include "Common/Serialize/SerializeFuncs.h"

#include <cstring>


// This is one of the firmware modules (pspnet.prx), the official PSP games can't call these funcs



// Struct name source: I made it up
struct SceNetRngContext {
	u32 state[521];
	s32 index;

	void Shuffle() {
		for (int i = 0; i < 32; ++i) {
			state[i] ^= state[i + 489];
		}
		for (int i = 32; i < 521; ++i) {
			state[i] ^= state[i - 32];
		}
	}

	void Init(u32 seed) {
		uint32_t acc = 0;

		for (int i = 0; i < 17; ++i) {
			for (int j = 0; j < 32; ++j) {
				seed = seed * 0x5D588B65u + 1;
				acc = (acc >> 1) | (seed & 0x80000000);
			}
			state[i] = acc;
		}

		state[16] = (state[16] << 23) ^ (state[0] >> 9) ^ state[15];

		for (int i = 17; i < 521; i++) {
			state[i] = (state[i - 17] << 23) ^ (state[i - 16] >> 9) ^ state[i - 1];
		}

		Shuffle();
		Shuffle();
		Shuffle();

		index = 520;
	}

	u32 Next() {
		++index;

		if (index > 520) {
			Shuffle();
			index = 0;
		}

		return state[index];
	}

	void DoState(PointerWrap& p) {
		Do(p, state);
		Do(p, index);
	}
};


static SceNetRngContext rng_context;


void __NetLibDoState(PointerWrap& p) {
	auto s = p.Section("sceNet_lib", 0, 1);
	if (!s) {
		return;
	}
	Do(p, rng_context);
}


void InitNetLibRngContext(u32 seed) {
	INFO_LOG(Log::sceNet, "Initialized the sceNet_lib rng context, seed=%d", seed);
	rng_context.Init(seed);
}


u32 sceNetRand() {
	return hleLogDebug(Log::sceNet, rng_context.Next());
}

u32 sceNetStrtoul(const char *str, u32 strEndAddrPtr, int base) {
	// Redirect that to libc
	char* str_end = nullptr;
	u32 res = std::strtoul(str, &str_end, base);

	// Remap the pointer
	u32 psp_str_end = Memory::GetAddressFromHostPointer(str_end);
	Memory::Write_U32(psp_str_end, strEndAddrPtr);

	return hleLogDebug(Log::sceNet, res);
}

u32 sceNetMemmove(void* dest, u32 srcPtr, u32 count) {
	// Redirect that to libc
	void* host_ptr = std::memmove(
		dest, Memory::GetPointer(srcPtr), count
	);

	// Remap the pointer
	u32 res = Memory::GetAddressFromHostPointer(host_ptr);
	return hleLogDebug(Log::sceNet, res);
}

u32 sceNetStrcpy(void* dest, const char *src) {
	// Redirect that to libc
	char* host_ptr = std::strcpy(static_cast<char*>(dest), src);

	// Remap the pointer
	u32 res = Memory::GetAddressFromHostPointer(host_ptr);
	return hleLogDebug(Log::sceNet, res);
}

s32 sceNetStrncmp(const char *lhs, const char *rhs, u32 count) {
	// Redirect that to libc
	s32 res = std::strncmp(lhs, rhs, count);

	return hleLogDebug(Log::sceNet, res);
}

s32 sceNetStrcasecmp(const char *lhs, const char *rhs) {
	// Redirect that to eh... what is this, a libc extension?
	s32 res = strcasecmp(lhs, rhs);

	return hleLogDebug(Log::sceNet, res);
}

s32 sceNetStrcmp(const char *lhs, const char *rhs) {
	// Redirect that to libc
	s32 res = std::strcmp(lhs, rhs);

	return hleLogDebug(Log::sceNet, res);
}

u32 sceNetStrncpy(void *dest, const char *src, u32 count) {
	// Redirect that to libc
	char* host_ptr = std::strncpy(static_cast<char*>(dest), src, count);

	// Remap the pointer
	u32 res = Memory::GetAddressFromHostPointer(host_ptr);
	return hleLogDebug(Log::sceNet, res);
}

u32 sceNetStrchr(void *str, int ch) {
	// For some reason it doesn't build for me if I make 'str' a const char *
	// At the same time I can't make it char *, because then WrapU_CI won't work

	// Redirect that to libc
	char* host_ptr = std::strchr(static_cast<char*>(str), ch);

	// Remap the pointer
	u32 res = Memory::GetAddressFromHostPointer(host_ptr);
	return hleLogDebug(Log::sceNet, res);
}

u32 sceNetStrlen(const char* str) {
	// Redirect that to libc
	u32 res = (u32)std::strlen(str);

	return hleLogDebug(Log::sceNet, res);
}

s32 sceNetMemcmp(u32 lhsPtr, u32 rhsPtr, u32 count) {
	// Redirect that to libc
	s32 res = std::memcmp(Memory::GetPointer(lhsPtr), Memory::GetPointer(rhsPtr), count);

	return hleLogDebug(Log::sceNet, res);
}


const HLEFunction sceNet_lib[] = {
	{0X1858883D, &WrapU_V<sceNetRand>,           "sceNetRand",                  'i', ""       },
	{0X2A73ADDC, &WrapU_CUI<sceNetStrtoul>,      "sceNetStrtoul",               'i', "sxi"    },
	{0X4753D878, &WrapU_VUU<sceNetMemmove>,      "sceNetMemmove",               'i', "xxx"    },
	{0X80C9F02A, &WrapU_VC<sceNetStrcpy>,        "sceNetStrcpy",                'i', "xs"     },
	{0X94DCA9F0, &WrapI_CCU<sceNetStrncmp>,      "sceNetStrncmp",               'i', "ssx"    },
	{0X9CFBC7E3, &WrapI_CC<sceNetStrcasecmp>,    "sceNetStrcasecmp",            'i', "ss"     },
	{0XA0F16ABD, &WrapI_CC<sceNetStrcmp>,        "sceNetStrcmp",                'i', "ss"     },
	{0XB5CE388A, &WrapU_VCU<sceNetStrncpy>,      "sceNetStrncpy",               'i', "xsx"    },
	{0XBCBE14CF, &WrapU_VI<sceNetStrchr>,        "sceNetStrchr",                'i', "si"     },
	{0XCF705E46, nullptr,                        "sceNetSprintf",               'i', ""       },
	{0XD8722983, &WrapU_C<sceNetStrlen>,         "sceNetStrlen",                'i', "s"      },
	{0XE0A81C7C, &WrapI_UUU<sceNetMemcmp>,       "sceNetMemcmp",                'i', "xxx"    },
};


void Register_sceNet_lib() {
	RegisterHLEModule("sceNet_lib", ARRAY_SIZE(sceNet_lib), sceNet_lib);
}
