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
#if defined(_M_IX86) || defined(_M_X64)

#include "ppsspp_config.h"
#ifdef __ANDROID__
#include <sys/stat.h>
#include <fcntl.h>
#elif PPSSPP_PLATFORM(MAC)
#include <sys/sysctl.h>
#endif

#include <memory.h>
#include <set>
#include "base/logging.h"
#include "base/basictypes.h"
#include "file/file_util.h"

#include "Common.h"
#include "CPUDetect.h"
#include "FileUtil.h"
#include "StringUtils.h"

#if defined(_WIN32) && !defined(__MINGW32__)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
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
#include <emmintrin.h>

#define _XCR_XFEATURE_ENABLED_MASK 0
static unsigned long long _xgetbv(unsigned int index)
{
	unsigned int eax, edx;
	__asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(index));
	return ((unsigned long long)edx << 32) | eax;
}

#else
#define _XCR_XFEATURE_ENABLED_MASK 0
#endif

#if !defined(MIPS)

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

#endif
#endif

CPUInfo cpu_info;

CPUInfo::CPUInfo() {
	Detect();
}

static std::vector<int> ParseCPUList(const std::string &filename) {
	std::string data;
	std::vector<int> results;

	if (readFileToString(true, filename.c_str(), data)) {
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

#if PPSSPP_PLATFORM(UWP)
	OS64bit = Mode64bit;  // TODO: Not always accurate!
#elif defined(_WIN32) && defined(_M_IX86)
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
	strcpy(brand_string, cpu_string);

	// Detect family and other misc stuff.
	bool ht = false;
	HTT = ht;
	logical_cpu_count = 1;
	if (max_std_fn >= 1) {
		do_cpuid(cpu_id, 0x00000001);
		int family = ((cpu_id[0] >> 8) & 0xf) + ((cpu_id[0] >> 20) & 0xff);
		int model = ((cpu_id[0] >> 4) & 0xf) + ((cpu_id[0] >> 12) & 0xf0);
		// Detect people unfortunate enough to be running PPSSPP on an Atom
		if (family == 6 && (model == 0x1C || model == 0x26 || model == 0x27 || model == 0x35 || model == 0x36 ||
		                    model == 0x37 || model == 0x4A || model == 0x4D || model == 0x5A || model == 0x5D))
			bAtom = true;

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
				bFMA3 = true;
		}
		if ((cpu_id[2] >> 25) & 1) bAES = true;

		if ((cpu_id[3] >> 24) & 1)
		{
			// We can use FXSAVE.
			bFXSR = true;
		}

		// AVX support requires 3 separate checks:
		//  - Is the AVX bit set in CPUID? (>>28)
		//  - Is the XSAVE bit set in CPUID? ( >>26)
		//  - Is the OSXSAVE bit set in CPUID? ( >>27)
		//  - XGETBV result has the XCR bit set.
		if (((cpu_id[2] >> 28) & 1) && ((cpu_id[2] >> 27) & 1) && ((cpu_id[2] >> 26) & 1))
		{
			if ((_xgetbv(_XCR_XFEATURE_ENABLED_MASK) & 0x6) == 0x6)
			{
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
		if ((cpu_id[2] >> 6) & 1) bSSE4A = true;
		if ((cpu_id[2] >> 16) & 1) bFMA4 = true;
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
	auto getLogicalProcessorInformationEx = (getLogicalProcessorInformationEx_f)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "GetLogicalProcessorInformationEx");
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
			for (auto processor : processors) {
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
	logical_cpu_count /= num_cores;
#elif PPSSPP_PLATFORM(LINUX)
	if (File::Exists("/sys/devices/system/cpu/present")) {
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
	logical_cpu_count /= num_cores;
#elif PPSSPP_PLATFORM(MAC)
	int num = 0;
	size_t sz = sizeof(num);
	if (sysctlbyname("hw.physicalcpu_max", &num, &sz, nullptr, 0) == 0) {
		num_cores = num;
		sz = sizeof(num);
		if (sysctlbyname("hw.logicalcpu_max", &num, &sz, nullptr, 0) == 0) {
			logical_cpu_count = num / num_cores;
		}
	}
#endif
}

// Turn the cpu info into a string we can show
std::string CPUInfo::Summarize()
{
	std::string sum;
	if (num_cores == 1)
		sum = StringFromFormat("%s, %d core", cpu_string, num_cores);
	else
	{
		sum = StringFromFormat("%s, %d cores", cpu_string, num_cores);
		if (HTT) sum += StringFromFormat(" (%i logical threads per physical core)", logical_cpu_count);
	}
	if (bSSE) sum += ", SSE";
	if (bSSE2) sum += ", SSE2";
	if (bSSE3) sum += ", SSE3";
	if (bSSSE3) sum += ", SSSE3";
	if (bSSE4_1) sum += ", SSE4.1";
	if (bSSE4_2) sum += ", SSE4.2";
	if (bSSE4A) sum += ", SSE4A";
	if (HTT) sum += ", HTT";
	if (bAVX) sum += ", AVX";
	if (bAVX2) sum += ", AVX2";
	if (bFMA3) sum += ", FMA3";
	if (bFMA4) sum += ", FMA4";
	if (bAES) sum += ", AES";
	if (bSHA) sum += ", SHA";
	if (bXOP) sum += ", XOP";
	if (bRTM) sum += ", TSX";
	if (bLongMode) sum += ", 64-bit support";
	return sum;
}

#endif // defined(_M_IX86) || defined(_M_X64)
