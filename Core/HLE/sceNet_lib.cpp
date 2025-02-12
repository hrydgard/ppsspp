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

#include <cstring>


// This is one of the firmware modules (pspnet.prx), the official PSP games can't call these funcs


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
	u32 res = std::strlen(str);

	return hleLogDebug(Log::sceNet, res);
}

s32 sceNetMemcmp(u32 lhsPtr, u32 rhsPtr, u32 count) {
	// Redirect that to libc
	s32 res = std::memcmp(Memory::GetPointer(lhsPtr), Memory::GetPointer(rhsPtr), count);

	return hleLogDebug(Log::sceNet, res);
}


const HLEFunction sceNet_lib[] = {
	{0X1858883D, nullptr,                        "sceNetRand",                  'i', ""       },
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
	RegisterModule("sceNet_lib", ARRAY_SIZE(sceNet_lib), sceNet_lib);
}
