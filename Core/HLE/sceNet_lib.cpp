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
	// WrapU_CUI?

	// Redirect that to libc
	char* str_end = nullptr;
	u32 res = std::strtoul(str, &str_end, base);

	// Remap the pointer
	u32 psp_str_end = Memory::GetAddressFromHostPointer(str_end);
	Memory::Write_U32(psp_str_end, strEndAddrPtr);

	return res;
}

u32 sceNetMemmove(void* dest, u32 srcPtr, u32 count) {
	// WrapU_VUU?

	// Redirect that to libc
	void* host_ptr = std::memmove(
		dest, Memory::GetPointer(srcPtr), count
	);

	// Remap the pointer
	u32 res = Memory::GetAddressFromHostPointer(host_ptr);
	return res;
}

u32 sceNetStrcpy(void* dest, const char *src) {
	// WrapU_VC?

	// Redirect that to libc
	char* host_ptr = std::strcpy(static_cast<char*>(dest), src);

	// Remap the pointer
	u32 res = Memory::GetAddressFromHostPointer(host_ptr);
	return res;
}

s32 sceNetStrncmp(const char *lhs, const char *rhs, u32 count) {
	// WrapI_CCU?

	// Redirect that to libc
	s32 res = std::strncmp(lhs, rhs, count);

	return res;
}

s32 sceNetStrcasecmp(const char *lhs, const char *rhs) {
	// WrapI_CC?

	// Redirect that to eh... what is this, a libc extension?
	s32 res = strcasecmp(lhs, rhs);

	return res;
}

s32 sceNetStrcmp(const char *lhs, const char *rhs) {
	// WrapI_CC?

	// Redirect that to libc
	s32 res = std::strcmp(lhs, rhs);

	return res;
}

u32 sceNetStrncpy(void *dest, const char *src, u32 count) {
	// WrapU_VCU?

	// Redirect that to libc
	char* host_ptr = std::strncpy(static_cast<char*>(dest), src, count);

	// Remap the pointer
	u32 res = Memory::GetAddressFromHostPointer(host_ptr);
	return res;
}

u32 sceNetStrchr(char *str, int ch) {
	// For some reason doesn't build for me if I make 'str' a const char

	// Wrap_CI

	// Redirect that to libc
	char* host_ptr = std::strchr(str, ch);

	// Remap the pointer
	u32 res = Memory::GetAddressFromHostPointer(host_ptr);
	return res;
}


const HLEFunction sceNet_lib[] = {
	{0X1858883D, nullptr,           "sceNetRand",                  'i', ""       },
	{0X2A73ADDC, nullptr,           "sceNetStrtoul",               'i', ""       },
	{0X4753D878, nullptr,           "sceNetMemmove",               'i', ""       },
	{0X80C9F02A, nullptr,           "sceNetStrcpy",                'i', ""       },
	{0X94DCA9F0, nullptr,           "sceNetStrncmp",               'i', ""       },
	{0X9CFBC7E3, nullptr,           "sceNetStrcasecmp",            'i', ""       },
	{0XA0F16ABD, nullptr,           "sceNetStrcmp",                'i', ""       },
	{0XB5CE388A, nullptr,           "sceNetStrncpy",               'i', ""       },
	{0XBCBE14CF, nullptr,           "sceNetStrchr",                'i', ""       },
	{0XCF705E46, nullptr,           "sceNetSprintf",               'i', ""       },
	{0XD8722983, nullptr,           "sceNetStrlen",                'i', ""       },
	{0XE0A81C7C, nullptr,           "sceNetMemcmp",                'i', ""       },
};


void Register_sceNet_lib() {
	RegisterModule("sceNet_lib", ARRAY_SIZE(sceNet_lib), sceNet_lib);
}
