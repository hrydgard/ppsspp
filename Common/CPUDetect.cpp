// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#ifdef ANDROID
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include <memory.h>
#include "base/logging.h"
#include "base/basictypes.h"

#include "Common.h"
#include "CPUDetect.h"
#include "StringUtils.h"

#ifdef _WIN32
#define _interlockedbittestandset workaround_ms_header_bug_platform_sdk6_set
#define _interlockedbittestandreset workaround_ms_header_bug_platform_sdk6_reset
#define _interlockedbittestandset64 workaround_ms_header_bug_platform_sdk6_set64
#define _interlockedbittestandreset64 workaround_ms_header_bug_platform_sdk6_reset64
#include <intrin.h>
#undef _interlockedbittestandset
#undef _interlockedbittestandreset
#undef _interlockedbittestandset64
#undef _interlockedbittestandreset64

void do_cpuidex(u32 regs[4], u32 cpuid_leaf, u32 ecxval) {
	__cpuidex((int *)regs, cpuid_leaf, ecxval);
}
void do_cpuid(u32 regs[4], u32 cpuid_leaf) {
	__cpuid((int *)regs, cpuid_leaf);
}
#else

#ifdef _M_SSE
#include <xmmintrin.h>
#endif

#if defined __FreeBSD__
#include <sys/types.h>
#include <machine/cpufunc.h>

void do_cpuidex(u32 regs[4], u32 cpuid_leaf, u32 ecxval) {
	__cpuidex((int *)regs, cpuid_leaf, ecxval);
}
void do_cpuid(u32 regs[4], u32 cpuid_leaf) {
	__cpuid((int *)regs, cpuid_leaf);
}
#elif !defined(MIPS)

void do_cpuidex(u32 regs[4], u32 cpuid_leaf, u32 ecxval) {
#ifdef ANDROID
	// Use the /dev/cpu/%i/cpuid interface
	int f = open("/dev/cpu/0/cpuid", O_RDONLY);
	if (f) {
		lseek64(f, ((uint64_t)ecxval << 32) | cpuid_leaf, SEEK_SET);
		read(f, (void *)regs, 16);
		close(f);
	} else {
		ELOG("CPUID %08x failed!", cpuid_leaf);
	}
#elif defined(__i386__) && defined(__PIC__)
	asm (
		"xchgl %%ebx, %1;\n\t"
		"cpuid;\n\t"
		"xchgl %%ebx, %1;\n\t"
		:"=a" (regs[0]), "=r" (regs[1]), "=c" (regs[2]), "=d" (regs[3])
		:"a" (cpuid_leaf), "c" (ecxval));
#else
	asm (
		"cpuid;\n\t"
		:"=a" (regs[0]), "=b" (regs[1]), "=c" (regs[2]), "=d" (regs[3])
		:"a" (cpuid_leaf), "c" (ecxval));
#endif
}
void do_cpuid(u32 regs[4], u32 cpuid_leaf)
{
	do_cpuidex(regs, cpuid_leaf, 0);
}

#endif
#endif

CPUInfo cpu_info;

CPUInfo::CPUInfo() {
	Detect();
}

// Detects the various cpu features
void CPUInfo::Detect() {
	memset(this, 0, sizeof(*this));
#ifdef _M_IX86
	Mode64bit = false;
#elif defined (_M_X64)
	Mode64bit = true;
	OS64bit = true;
#endif
	num_cores = 1;

#ifdef _WIN32
#ifdef _M_IX86
	BOOL f64 = false;
	IsWow64Process(GetCurrentProcess(), &f64);
	OS64bit = (f64 == TRUE) ? true : false;
#endif
#endif

	// Set obvious defaults, for extra safety
	if (Mode64bit) {
		bSSE = true;
		bSSE2 = true;
		bLongMode = true;
	}

	// Assume CPU supports the CPUID instruction. Those that don't can barely
	// boot modern OS:es anyway.
	u32 cpu_id[4];
	memset(cpu_string, 0, sizeof(cpu_string));

	// Detect CPU's CPUID capabilities, and grab cpu string
	do_cpuid(cpu_id, 0x00000000);
	u32 max_std_fn = cpu_id[0];  // EAX
	*((int *)cpu_string) = cpu_id[1];
	*((int *)(cpu_string + 4)) = cpu_id[3];
	*((int *)(cpu_string + 8)) = cpu_id[2];
	do_cpuid(cpu_id, 0x80000000);
	u32 max_ex_fn = cpu_id[0];
	if (!strcmp(cpu_string, "GenuineIntel"))
		vendor = VENDOR_INTEL;
	else if (!strcmp(cpu_string, "AuthenticAMD"))
		vendor = VENDOR_AMD;
	else
		vendor = VENDOR_OTHER;

	// Set reasonable default brand string even if brand string not available.
	strcpy(brand_string, cpu_string);

	// Detect family and other misc stuff.
	bool ht = false;
	HTT = ht;
	logical_cpu_count = 1;
	if (max_std_fn >= 1) {
		do_cpuid(cpu_id, 0x00000001);
		logical_cpu_count = (cpu_id[1] >> 16) & 0xFF;
		ht = (cpu_id[3] >> 28) & 1;

		if ((cpu_id[3] >> 25) & 1) bSSE = true;
		if ((cpu_id[3] >> 26) & 1) bSSE2 = true;
		if ((cpu_id[2])       & 1) bSSE3 = true;
		if ((cpu_id[2] >> 9)  & 1) bSSSE3 = true;
		if ((cpu_id[2] >> 19) & 1) bSSE4_1 = true;
		if ((cpu_id[2] >> 20) & 1) bSSE4_2 = true;
		if ((cpu_id[2] >> 28) & 1) {
			bAVX = true;
			if ((cpu_id[2] >> 12) & 1)
				bFMA = true;
		}
		if ((cpu_id[2] >> 25) & 1) bAES = true;
	}
	if (max_ex_fn >= 0x80000004) {
		// Extract brand string
		do_cpuid(cpu_id, 0x80000002);
		memcpy(brand_string, cpu_id, sizeof(cpu_id));
		do_cpuid(cpu_id, 0x80000003);
		memcpy(brand_string + 16, cpu_id, sizeof(cpu_id));
		do_cpuid(cpu_id, 0x80000004);
		memcpy(brand_string + 32, cpu_id, sizeof(cpu_id));
	}
	if (max_ex_fn >= 0x80000001) {
		// Check for more features.
		do_cpuid(cpu_id, 0x80000001);
		if (cpu_id[2] & 1) bLAHFSAHF64 = true;
		// CmpLegacy (bit 2) is deprecated.
		if ((cpu_id[3] >> 29) & 1) bLongMode = true;
	}

	num_cores = (logical_cpu_count == 0) ? 1 : logical_cpu_count;

	if (max_ex_fn >= 0x80000008) {
		// Get number of cores. This is a bit complicated. Following AMD manual here.
		do_cpuid(cpu_id, 0x80000008);
		int apic_id_core_id_size = (cpu_id[2] >> 12) & 0xF;
		if (apic_id_core_id_size == 0) {
			if (ht) {
				// 0x0B is the preferred method on Core i series processors.
				// Inspired by https://github.com/D-Programming-Language/druntime/blob/23b0d1f41e27638bda2813af55823b502195a58d/src/core/cpuid.d#L562.
				bool hasLeafB = false;
				if (vendor == VENDOR_INTEL && max_std_fn >= 0x0B) {
					do_cpuidex(cpu_id, 0x0B, 0);
					if (cpu_id[1] != 0) {
						logical_cpu_count = cpu_id[1] & 0xFFFF;
						do_cpuidex(cpu_id, 0x0B, 1);
						int totalThreads = cpu_id[1] & 0xFFFF;
						num_cores = totalThreads / logical_cpu_count;
						hasLeafB = true;
					}
				}
				// Old new mechanism for modern Intel CPUs.
				if (!hasLeafB && vendor == VENDOR_INTEL) {
					do_cpuid(cpu_id, 0x00000004);
					int cores_x_package = ((cpu_id[0] >> 26) & 0x3F) + 1;
					HTT = (cores_x_package < logical_cpu_count);
					cores_x_package = ((logical_cpu_count % cores_x_package) == 0) ? cores_x_package : 1;
					num_cores = (cores_x_package > 1) ? cores_x_package : num_cores;
					logical_cpu_count /= cores_x_package;
				}
			}
		} else {
			// Use AMD's new method.
			num_cores = (cpu_id[2] & 0xFF) + 1;
		}
	}
}

// Turn the cpu info into a string we can show
std::string CPUInfo::Summarize()
{
	std::string sum;
	if (num_cores == 1)
		sum = StringFromFormat("%s, %i core", cpu_string, num_cores);
	else
	{
		sum = StringFromFormat("%s, %i cores", cpu_string, num_cores);
		if (HTT) sum += StringFromFormat(" (%i logical threads per physical core)", logical_cpu_count);
	}
	if (bSSE) sum += ", SSE";
	if (bSSE2) sum += ", SSE2";
	if (bSSE3) sum += ", SSE3";
	if (bSSSE3) sum += ", SSSE3";
	if (bSSE4_1) sum += ", SSE4.1";
	if (bSSE4_2) sum += ", SSE4.2";
	if (HTT) sum += ", HTT";
	if (bAVX) sum += ", AVX";
	if (bAVX) sum += ", FMA";
	if (bAES) sum += ", AES";
	if (bLongMode) sum += ", 64-bit support";
	return sum;
}
