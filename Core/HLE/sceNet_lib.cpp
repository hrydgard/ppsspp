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


// NOTE: I stole it from SysclibForKernel
static int sprintf_impl(u32 dst, int limit, u32 fmt, int paramOffset) {
	if (!Memory::IsValidNullTerminatedString(fmt)) {
		ERROR_LOG(Log::sceNet, "net sprintf bad fmt");
		return 0;
	}

	VERBOSE_LOG(Log::sceNet, "net sprintf fmt: %s", Memory::GetCharPointerUnchecked(fmt));
	VERBOSE_LOG(Log::sceNet, "net sprintf a0-a4, t0-t4: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x",
		currentMIPS->r[MIPS_REG_A0],
		currentMIPS->r[MIPS_REG_A1],
		currentMIPS->r[MIPS_REG_A2],
		currentMIPS->r[MIPS_REG_A3],
		currentMIPS->r[MIPS_REG_T0],
		currentMIPS->r[MIPS_REG_T1],
		currentMIPS->r[MIPS_REG_T2],
		currentMIPS->r[MIPS_REG_T3]
	);

	bool processing_specifier = false;
	std::string specifier = "";
	int bytes_to_read = 0;
	int arg_idx = paramOffset;
	std::string result = "";
	for (const char* c = Memory::GetCharPointerUnchecked(fmt); *c != '\0'; c++) {
		if (!processing_specifier) {
			if (*c == '%') {
				specifier = "%";
				processing_specifier = true;
				bytes_to_read = 0;
			}
			else {
				result.append(1, *c);
			}
		}
		else {
			specifier.append(1, *c);

			// going by https://cplusplus.com/reference/cstdio/printf/#compatibility
			// no idea what the kernel module really supports as of writing this
			switch (*c) {
			case '%':
			{
				result.append(specifier);
				processing_specifier = false;
				break;
			}
			case 's':
			{
				// consume 4 bytes from arguments
				u32 val = 0;
				if (arg_idx <= 1) {
					val = currentMIPS->r[MIPS_REG_A2 + arg_idx];
				}
				else if (arg_idx <= 5) {
					val = currentMIPS->r[MIPS_REG_T0 + arg_idx - 2];
				}
				else {
					int stack_idx = arg_idx - 6;
					u32 stack_cur = currentMIPS->r[MIPS_REG_SP] + stack_idx * 4;

					if (!Memory::IsValidAddress(stack_cur)) {
						ERROR_LOG(Log::sceNet, "net sprintf bad stack pointer %08x", stack_cur);
						return 0;
					}
					val = Memory::Read_U32(stack_cur);
					VERBOSE_LOG(Log::sceNet, "net sprintf fetching %08x from sp + %u", val, stack_idx * 4);
				}
				arg_idx++;

				if (!Memory::IsValidNullTerminatedString(val)) {
					ERROR_LOG(Log::sceNet, "net sprintf bad string reference at %08x", val);
					return 0;
				}
				result.append(Memory::GetCharPointerUnchecked(val));
				processing_specifier = false;
				break;
			}
			case 'd':
			case 'i':
			case 'u':
			case 'o':
			case 'x':
			case 'X':
			case 'f':
			case 'e':
			case 'E':
			case 'g':
			case 'G':
			case 'c':
			case 'p':
			case 'n':
			{
				u64 val = 0;
				if (bytes_to_read == 0) {
					bytes_to_read = 4;
				}
				int read_cnt = 0;
				while (bytes_to_read != 0) {
					u32 val_from_arg = 0;
					if (arg_idx <= 1) {
						val_from_arg = currentMIPS->r[MIPS_REG_A2 + arg_idx];
					}
					else if (arg_idx <= 5) {
						val_from_arg = currentMIPS->r[MIPS_REG_T0 + arg_idx - 2];
					}
					else {
						int stack_idx = arg_idx - 6;
						u32 stack_cur = currentMIPS->r[MIPS_REG_SP] + stack_idx * 4;

						if (!Memory::IsValidAddress(stack_cur)) {
							ERROR_LOG(Log::sceNet, "net sprintf bad stack pointer %08x", stack_cur);
							return 0;
						}
						val_from_arg = Memory::Read_U32(stack_cur);
						DEBUG_LOG(Log::sceNet, "net sprintf fetching %08x from sp + %u", val_from_arg, stack_idx * 4);
					}
					arg_idx++;

					val = val | ((u64)val_from_arg << (read_cnt * 32));

					bytes_to_read = bytes_to_read - 4;
					read_cnt++;
				}
				char buf[128] = { 0 };
				snprintf(buf, sizeof(buf), specifier.c_str(), val);
				buf[sizeof(buf) - 1] = '\0';
				result.append(buf);
				processing_specifier = false;
				break;
			}
			case 'h':
			{
				// allegrex calling convention is 4 bytes aligned
				bytes_to_read = 4;
				break;
			}
			case 'l':
			{
				bytes_to_read = bytes_to_read + 4;
				break;
			}
			}
		}
	}

	const size_t retval = result.size();

	// Implement the snprintf length check.
	if (limit != 0 && result.length() >= limit) {
		result.resize(limit - 1);
	}

	VERBOSE_LOG(Log::sceNet, "net sprintf result string has length %d (retval: %d), content:", (int)result.length(), (int)retval);
	VERBOSE_LOG(Log::sceNet, "%s", result.c_str());
	// Since this is a sprintf function and not an actual printf, we don't log to the Sprintf log.
	// INFO_LOG(Log::Printf, "%s", result.c_str());
	if (!Memory::IsValidRange(dst, (u32)result.length() + 1)) {
		ERROR_LOG(Log::sceNet, "net sprintf result string is too long or dst is invalid");
		return 0;
	}
	memcpy((char*)Memory::GetPointerUnchecked(dst), result.c_str(), (int)result.length() + 1);
	return (int)retval;
}

static int sceNetSprintf(u32 dst, u32 fmt) {
	DEBUG_LOG(Log::sceNet, "Untested: sceNetSprintf(dst=%08x, fmt=%08x)", dst, fmt);
	return hleLogDebug(Log::sceNet, sprintf_impl(dst, 0, fmt, 0));
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
	{0XCF705E46, &WrapI_UU<sceNetSprintf>,       "sceNetSprintf",               'i', "xx"     },
	{0XD8722983, &WrapU_C<sceNetStrlen>,         "sceNetStrlen",                'i', "s"      },
	{0XE0A81C7C, &WrapI_UUU<sceNetMemcmp>,       "sceNetMemcmp",                'i', "xxx"    },
};


void Register_sceNet_lib() {
	RegisterHLEModule("sceNet_lib", ARRAY_SIZE(sceNet_lib), sceNet_lib);
}
