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


u32 sceNetStrtoul(char *str, u32 strEndAddrPtr, int base) {
	// TODO
	// Wrap_U_CUI?
	// Redirect that to libc
	char* str_end = nullptr;
	u32 res = std::strtoul(str, &str_end, base);

	// str_end - Memory::base;
	return 0;
}

u32 sceNetMemmove(void *dest, void *src, u32 count) {
	// TODO
	auto res = std::memmove(dest, src, count);
	return 0;
}

u32 sceNetStrcpy(char *dest, char *src) {
	// TODO
	auto res = std::strcpy(dest, src);
	return 0;
}

u32 sceNetStrncmp(char *lhs, char *rhs, u32 count) {
	// TODO
	auto res = std::strncmp(lhs, rhs, count);
	return 0;
}

u32 sceNetStrcasecmp(char *lhs, char *rhs) {
	// TODO
	auto res = strcasecmp(lhs, rhs);
	return 0;
}

u32 sceNetStrcmp(char *lhs, char *rhs) {
	// TODO
	auto res = std::strcmp(lhs, rhs);
	return 0;
}

u32 sceNetStrncpy(char *dest, char *src, u32 count) {
	// TODO
	auto res = std::strncpy(dest, src, count);
	return 0;
}

u32 sceNetStrchr(char *str, int ch) {
	// TODO
	auto res = std::strchr(str, ch);
	return 0;
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
