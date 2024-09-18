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

// Reference : https://stackoverflow.com/questions/6121792/how-to-check-if-a-cpu-supports-the-sse3-instruction-set
#include "ppsspp_config.h"
#if (PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)) && !defined(__EMSCRIPTEN__)

#include "ext/cpu_features/include/cpuinfo_x86.h"

#if defined(CPU_FEATURES_OS_FREEBSD) || defined(CPU_FEATURES_OS_LINUX) || defined(CPU_FEATURES_OS_ANDROID) || defined(CPU_FEATURES_OS_MACOS) || defined(CPU_FEATURES_OS_WINDOWS)
#define USE_CPU_FEATURES 1
#endif

#ifdef __ANDROID__
#include <sys/stat.h>
#include <fcntl.h>
#elif PPSSPP_PLATFORM(MAC)
#include <sys/sysctl.h>
#endif

#include <algorithm>
#include <cstdint>
#include <memory.h>
#include <set>

#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"

#if defined(_WIN32)
#include "Common/CommonWindows.h"

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

#ifdef __MINGW32__
static uint64_t do_xgetbv(unsigned int index) {
	unsigned int eax, edx;
	// This is xgetbv directly, so we can avoid compilers warning we need runtime checks.
	asm(".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(index));
	return ((uint64_t)edx << 32) | eax;
}
#else
#define do_xgetbv _xgetbv
#endif

#else  // _WIN32

#ifdef _M_SSE
#include <emmintrin.h>

static uint64_t do_xgetbv(unsigned int index) {
	unsigned int eax, edx;
	__asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
	return ((uint64_t)edx << 32) | eax;
}
#endif  // _M_SSE

#if !PPSSPP_ARCH(MIPS)

void do_cpuidex(u32 regs[4], u32 cpuid_leaf, u32 ecxval) {
#if defined(__i386__) && defined(__PIC__)
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

#endif // !PPSSPP_ARCH(MIPS)

#endif  // !win32

#ifndef _XCR_XFEATURE_ENABLED_MASK
#define _XCR_XFEATURE_ENABLED_MASK 0
#endif

CPUInfo cpu_info;

CPUInfo::CPUInfo() {
	Detect();
}

#if PPSSPP_PLATFORM(LINUX)
static std::vector<int> ParseCPUList(const std::string &filename) {
	std::string data;
	std::vector<int> results;

	if (File::ReadSysTextFileToString(Path(filename), &data)) {
		std::vector<std::string> ranges;
		SplitString(data, ',', ranges);
		for (auto range : ranges) {
			int low = 0, high = 0;
			int parts = sscanf(range.c_str(), "%d-%d", &low, &high);
			if (parts == 1) {
				high = low;
			}
			for (int i = low; i <= high; ++i) {
				results.push_back(i);
			}
		}
	}

	return results;
}
#endif

// Detects the various cpu features
void CPUInfo::Detect() {
#ifdef USE_CPU_FEATURES
	cpu_features::X86Info info = cpu_features::GetX86Info();
#endif

	memset(this, 0, sizeof(*this));
#if PPSSPP_ARCH(X86)
	Mode64bit = false;
#elif PPSSPP_ARCH(AMD64)
	Mode64bit = true;
	OS64bit = true;
#endif
	num_cores = 1;

#if PPSSPP_PLATFORM(UWP)
	OS64bit = Mode64bit;  // TODO: Not always accurate!
#elif defined(_WIN32) && PPSSPP_ARCH(X86)
	BOOL f64 = false;
	IsWow64Process(GetCurrentProcess(), &f64);
	OS64bit = (f64 == TRUE) ? true : false;
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
#ifdef USE_CPU_FEATURES
	if (info.brand_string[0])
		strcpy(brand_string, info.brand_string);
	else
#endif
		strcpy(brand_string, cpu_string);

#ifdef USE_CPU_FEATURES
	switch (cpu_features::GetX86Microarchitecture(&info)) {
	case cpu_features::INTEL_ATOM_BNL:
	case cpu_features::INTEL_ATOM_SMT:
	case cpu_features::INTEL_ATOM_GMT:
	case cpu_features::INTEL_ATOM_GMT_PLUS:
	case cpu_features::INTEL_ATOM_TMT:
		bAtom = true;
		break;
	default:
		bAtom = false;
		break;
	}

	bPOPCNT = info.features.popcnt;
	bBMI1 = info.features.bmi1;
	bBMI2 = info.features.bmi2;
	bBMI2_fast = bBMI2 && (vendor != VENDOR_AMD || info.family >= 0x19);
	bMOVBE = info.features.movbe;
	bLZCNT = info.features.lzcnt;
	bRTM = info.features.rtm;

	bSSE = info.features.sse;
	bSSE2 = info.features.sse2;
	bSSE3 = info.features.sse3;
	bSSSE3 = info.features.ssse3;
	bSSE4_1 = info.features.sse4_1;
	bSSE4_2 = info.features.sse4_2;
	bSSE4A = info.features.sse4a;
	bAES = info.features.aes;
	bSHA = info.features.sha;
	bF16C = info.features.f16c;
	bAVX = info.features.avx;
	bAVX2 = info.features.avx2;
	bFMA3 = info.features.fma3;
	bFMA4 = info.features.fma4;
#endif

	// Detect family and other misc stuff.
	bool ht = false;
	HTT = ht;
	logical_cpu_count = 1;
	if (max_std_fn >= 1) {
		do_cpuid(cpu_id, 0x00000001);
#ifndef USE_CPU_FEATURES
		int family = ((cpu_id[0] >> 8) & 0xf) + ((cpu_id[0] >> 20) & 0xff);
		int model = ((cpu_id[0] >> 4) & 0xf) + ((cpu_id[0] >> 12) & 0xf0);
		// Detect people unfortunate enough to be running PPSSPP on an Atom
		if (family == 6 && (model == 0x1C || model == 0x26 || model == 0x27 || model == 0x35 || model == 0x36 ||
		                    model == 0x37 || model == 0x4A || model == 0x4D || model == 0x5A || model == 0x5D))
			bAtom = true;
#endif

		logical_cpu_count = (cpu_id[1] >> 16) & 0xFF;
		ht = (cpu_id[3] >> 28) & 1;

#ifndef USE_CPU_FEATURES
		if ((cpu_id[3] >> 25) & 1) bSSE = true;
		if ((cpu_id[3] >> 26) & 1) bSSE2 = true;
		if ((cpu_id[2])       & 1) bSSE3 = true;
		if ((cpu_id[2] >> 9)  & 1) bSSSE3 = true;
		if ((cpu_id[2] >> 19) & 1) bSSE4_1 = true;
		if ((cpu_id[2] >> 20) & 1) bSSE4_2 = true;
		if ((cpu_id[2] >> 28) & 1) {
			bAVX = true;
			if ((cpu_id[2] >> 12) & 1)
				bFMA3 = true;
		}
		if ((cpu_id[2] >> 25) & 1) bAES = true;
#endif

		if ((cpu_id[3] >> 24) & 1)
		{
			// We can use FXSAVE.
			bFXSR = true;
		}

#ifndef USE_CPU_FEATURES
		// AVX support requires 3 separate checks:
		//  - Is the AVX bit set in CPUID? (>>28)
		//  - Is the XSAVE bit set in CPUID? ( >>26)
		//  - Is the OSXSAVE bit set in CPUID? ( >>27)
		//  - XGETBV result has the XCR bit set.
		if (((cpu_id[2] >> 28) & 1) && ((cpu_id[2] >> 27) & 1) && ((cpu_id[2] >> 26) & 1)) {
			if ((do_xgetbv(_XCR_XFEATURE_ENABLED_MASK) & 0x6) == 0x6) {
				bAVX = true;
				if ((cpu_id[2] >> 12) & 1)
					bFMA3 = true;
			}
		}


		// TSX support require check:
		// -- Is the RTM bit set in CPUID? (>>11)
		// -- No need to check HLE bit because legacy processors ignore HLE hints
		// -- See https://software.intel.com/en-us/articles/how-to-detect-new-instruction-support-in-the-4th-generation-intel-core-processor-family
		if (max_std_fn >= 7)
		{
			do_cpuid(cpu_id, 0x00000007);
			// careful; we can't enable AVX2 unless the XSAVE/XGETBV checks above passed
			if ((cpu_id[1] >> 5) & 1)
				bAVX2 = bAVX;
			if ((cpu_id[1] >> 3) & 1)
				bBMI1 = true;
			if ((cpu_id[1] >> 8) & 1)
				bBMI2 = true;
			if ((cpu_id[1] >> 29) & 1)
				bSHA = true;
			if ((cpu_id[1] >> 11) & 1)
				bRTM = true;
		}

		bBMI2_fast = bBMI2 && (vendor != VENDOR_AMD || family >= 0x19);
#endif
	}
	if (max_ex_fn >= 0x80000004) {
#ifndef USE_CPU_FEATURES
		// Extract brand string
		do_cpuid(cpu_id, 0x80000002);
		memcpy(brand_string, cpu_id, sizeof(cpu_id));
		do_cpuid(cpu_id, 0x80000003);
		memcpy(brand_string + 16, cpu_id, sizeof(cpu_id));
		do_cpuid(cpu_id, 0x80000004);
		memcpy(brand_string + 32, cpu_id, sizeof(cpu_id));
#endif
	}
	if (max_ex_fn >= 0x80000001) {
		// Check for more features.
		do_cpuid(cpu_id, 0x80000001);
		if (cpu_id[2] & 1) bLAHFSAHF64 = true;
#ifndef USE_CPU_FEATURES
		if ((cpu_id[2] >> 6) & 1) bSSE4A = true;
		if ((cpu_id[2] >> 16) & 1) bFMA4 = true;
#endif
		if ((cpu_id[2] >> 11) & 1) bXOP = true;
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

	// The above only gets valid info for the active processor.
	// Let's rely on OS APIs for accurate information, if available, below.

#if PPSSPP_PLATFORM(WINDOWS)
#if !PPSSPP_PLATFORM(UWP)
	typedef BOOL (WINAPI *getLogicalProcessorInformationEx_f)(LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Buffer, PDWORD ReturnedLength);
	getLogicalProcessorInformationEx_f getLogicalProcessorInformationEx = nullptr;
	HMODULE kernel32 = GetModuleHandle(L"kernel32.dll");
	if (kernel32)
		getLogicalProcessorInformationEx = (getLogicalProcessorInformationEx_f)GetProcAddress(kernel32, "GetLogicalProcessorInformationEx");
#else
	void *getLogicalProcessorInformationEx = nullptr;
#endif

	if (getLogicalProcessorInformationEx) {
#if !PPSSPP_PLATFORM(UWP)
		DWORD len = 0;
		getLogicalProcessorInformationEx(RelationAll, nullptr, &len);
		auto processors = new uint8_t[len];
		if (getLogicalProcessorInformationEx(RelationAll, (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)processors, &len)) {
			num_cores = 0;
			logical_cpu_count = 0;
			auto p = processors;
			while (p < processors + len) {
				const auto &processor = *(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)p;
				if (processor.Relationship == RelationProcessorCore) {
					num_cores++;
					for (int j = 0; j < processor.Processor.GroupCount; ++j) {
						const auto &mask = processor.Processor.GroupMask[j].Mask;
						for (int i = 0; i < sizeof(mask) * 8; ++i) {
							logical_cpu_count += (mask >> i) & 1;
						}
					}
				}
				p += processor.Size;
			}
		}
		delete [] processors;
#endif
	} else {
		DWORD len = 0;
		const DWORD sz = sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
		GetLogicalProcessorInformation(nullptr, &len);
		std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> processors;
		processors.resize((len + sz - 1) / sz);
		if (GetLogicalProcessorInformation(&processors[0], &len)) {
			num_cores = 0;
			logical_cpu_count = 0;
			for (const auto &processor : processors) {
				if (processor.Relationship == RelationProcessorCore) {
					num_cores++;
					for (int i = 0; i < sizeof(processor.ProcessorMask) * 8; ++i) {
						logical_cpu_count += (processor.ProcessorMask >> i) & 1;
					}
				}
			}
		}
	}

	// This seems to be the count per core.  Hopefully all cores are the same, but we counted each above.
	logical_cpu_count /= std::max(num_cores, 1);
#elif PPSSPP_PLATFORM(LINUX)
	if (File::Exists(Path("/sys/devices/system/cpu/present"))) {
		// This may not count unplugged cores, but at least it's a best guess.
		// Also, this assumes the CPU cores are heterogeneous (e.g. all cores could be active simultaneously.)
		num_cores = 0;
		logical_cpu_count = 0;

		std::set<int> counted_cores;
		auto present = ParseCPUList("/sys/devices/system/cpu/present");
		for (int id : present) {
			logical_cpu_count++;

			if (counted_cores.count(id) == 0) {
				num_cores++;
				counted_cores.insert(id);

				// Also count any thread siblings as counted.
				auto threads = ParseCPUList(StringFromFormat("/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", id));
				for (int mark_id : threads) {
					counted_cores.insert(mark_id);
				}
			}
		}
	}

	// This seems to be the count per core.  Hopefully all cores are the same, but we counted each above.
	logical_cpu_count /= std::max(num_cores, 1);
#elif PPSSPP_PLATFORM(MAC)
	int num = 0;
	size_t sz = sizeof(num);
	if (sysctlbyname("hw.physicalcpu_max", &num, &sz, nullptr, 0) == 0) {
		num_cores = num;
		sz = sizeof(num);
		if (sysctlbyname("hw.logicalcpu_max", &num, &sz, nullptr, 0) == 0) {
			logical_cpu_count = num / std::max(num_cores, 1);
		}
	}
#endif
	if (logical_cpu_count <= 0)
		logical_cpu_count = 1;
}

std::vector<std::string> CPUInfo::Features() {
	std::vector<std::string> features;

	struct Flag {
		bool &flag;
		const char *str;
	};
	const Flag list[] = {
		{ bSSE, "SSE" },
		{ bSSE2, "SSE2" },
		{ bSSE3, "SSE3" },
		{ bSSSE3, "SSSE3" },
		{ bSSE4_1, "SSE4.1" },
		{ bSSE4_2, "SSE4.2" },
		{ bSSE4A, "SSE4A" },
		{ HTT, "HTT" },
		{ bAVX, "AVX" },
		{ bAVX2, "AVX2" },
		{ bFMA3, "FMA3" },
		{ bFMA4, "FMA4" },
		{ bAES, "AES" },
		{ bSHA, "SHA" },
		{ bXOP, "XOP" },
		{ bRTM, "TSX" },
		{ bF16C, "F16C" },
		{ bBMI1, "BMI1" },
		{ bBMI2, "BMI2" },
		{ bPOPCNT, "POPCNT" },
		{ bMOVBE, "MOVBE" },
		{ bLZCNT, "LZCNT" },
		{ bLongMode, "64-bit support" },
	};

	for (auto &item : list) {
		if (item.flag) {
			features.push_back(item.str);
		}
	}

	return features;
}

// Turn the cpu info into a string we can show
std::string CPUInfo::Summarize() {
	std::string sum;
	if (num_cores == 1) {
		sum = StringFromFormat("%s, %d core", cpu_string, num_cores);
	} else {
		sum = StringFromFormat("%s, %d cores", cpu_string, num_cores);
		if (HTT)
			sum += StringFromFormat(" (%i logical threads per physical core)", logical_cpu_count);
	}

	auto features = Features();
	for (std::string &feature : features) {
		sum += ", " + feature;
	}
	return sum;
}

#endif // PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

const char *GetCompilerABI() {
#if PPSSPP_ARCH(ARMV7)
	return "armeabi-v7a";
#elif PPSSPP_ARCH(ARM)
	return "armeabi";
#elif PPSSPP_ARCH(ARM64)
	return "arm64";
#elif PPSSPP_ARCH(X86)
	return "x86";
#elif PPSSPP_ARCH(AMD64)
	return "x86-64";
#elif PPSSPP_ARCH(RISCV64)
    //https://github.com/riscv/riscv-toolchain-conventions#cc-preprocessor-definitions
    //https://github.com/riscv/riscv-c-api-doc/blob/master/riscv-c-api.md#abi-related-preprocessor-definitions
    #if defined(__riscv_float_abi_single)
        return "lp64f";
    #elif defined(__riscv_float_abi_double)
        return "lp64d";
    #elif defined(__riscv_float_abi_quad)
        return "lp64q";
    #elif defined(__riscv_float_abi_soft)
        return "lp64";
    #endif
#else
	return "other";
#endif
}
