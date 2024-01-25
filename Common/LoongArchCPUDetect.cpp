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

#include "ppsspp_config.h"
#if PPSSPP_ARCH(LOONGARCH64)

#include "ext/cpu_features/include/cpuinfo_loongarch.h"

#if defined(CPU_FEATURES_OS_LINUX)
#define USE_CPU_FEATURES 1
#endif

#include <cstring>
#include <set>
#include <sstream>
#include <sys/auxv.h>
#include <vector>
#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "Common/StringUtils.h"
#include "Common/File/FileUtil.h"
#include "Common/Data/Encoding/Utf8.h"

// Only Linux platforms have /proc/cpuinfo
#if defined(__linux__)
const char procfile[] = "/proc/cpuinfo";
// https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-devices-system-cpu
const char syscpupresentfile[] = "/sys/devices/system/cpu/present";

class LoongArchCPUInfoParser {
public:
	LoongArchCPUInfoParser();

	int ProcessorCount();
	int TotalLogicalCount();

private:
	std::vector<std::vector<std::string>> cores_;
};

LoongArchCPUInfoParser::LoongArchCPUInfoParser() {
	std::string procdata, line;
	if (!File::ReadSysTextFileToString(Path(procfile), &procdata))
		return;

	std::istringstream file(procdata);
	int index = -1;
	while (std::getline(file, line)) {
		if (line.length() == 0) {
			index = -1;
		} else {
			if (index == -1) {
				index = (int)cores_.size();
				cores_.push_back(std::vector<std::string>());
			}
			cores_[index].push_back(line);
		}
	}
}

int LoongArchCPUInfoParser::ProcessorCount() {
	static const char * const marker = "core";
	std::set<std::string> coreIndex;
	for (auto core : cores_) {
		for (auto line : core) {
			if (line.find(marker) != line.npos)
				coreIndex.insert(line);
		}
	}

	return (int)coreIndex.size();
}

int LoongArchCPUInfoParser::TotalLogicalCount() {
	std::string presentData, line;
	bool presentSuccess = File::ReadSysTextFileToString(Path(syscpupresentfile), &presentData);
	if (presentSuccess) {
		std::istringstream presentFile(presentData);

		int low, high, found;
		std::getline(presentFile, line);
		found = sscanf(line.c_str(), "%d-%d", &low, &high);
		if (found == 1){
			return 1;
		}
		if (found == 2){
			return high - low + 1;
		}
	}else{
		return 1;
	}
}

#endif

static bool ExtensionSupported(unsigned long v, unsigned int i) {
	// https://github.com/torvalds/linux/blob/master/arch/loongarch/include/uapi/asm/hwcap.h
	unsigned long mask = 1 << i;
	return v & mask;
}

CPUInfo cpu_info;

CPUInfo::CPUInfo() {
	Detect();
}

// Detects the various cpu features
void CPUInfo::Detect()
{
	// Set some defaults here
	HTT = false;
#if PPSSPP_ARCH(LOONGARCH64)
	OS64bit = true;
	CPU64bit = true;
	Mode64bit = true;
#else
	OS64bit = false;
	CPU64bit = false;
	Mode64bit = false;
#endif
	vendor = VENDOR_OTHER;
	truncate_cpy(brand_string, "Loongson");
	truncate_cpy(cpu_string, "Loongson");
#if !defined(__linux__)
	num_cores = 1;
	logical_cpu_count = 1;
	truncate_cpy(brand_string, "Unknown");
	truncate_cpy(cpu_string, "Unknown");
#else // __linux__
	LoongArchCPUInfoParser parser;
	num_cores = parser.ProcessorCount();
	logical_cpu_count = parser.TotalLogicalCount() / num_cores;
	if (logical_cpu_count <= 0)
		logical_cpu_count = 1;
#endif

	unsigned long hwcap = getauxval(AT_HWCAP);
	LOONGARCH_CPUCFG    = true;
	LOONGARCH_LAM       = ExtensionSupported(hwcap, 1);
	LOONGARCH_UAL       = ExtensionSupported(hwcap, 2);
	LOONGARCH_FPU       = ExtensionSupported(hwcap, 3);
	LOONGARCH_LSX       = ExtensionSupported(hwcap, 4);
	LOONGARCH_LASX      = ExtensionSupported(hwcap, 5);
	LOONGARCH_CRC32     = ExtensionSupported(hwcap, 6);
	LOONGARCH_COMPLEX   = ExtensionSupported(hwcap, 7);
	LOONGARCH_CRYPTO    = ExtensionSupported(hwcap, 8);
	LOONGARCH_LVZ       = ExtensionSupported(hwcap, 9);
	LOONGARCH_LBT_X86   = ExtensionSupported(hwcap, 10);
	LOONGARCH_LBT_ARM   = ExtensionSupported(hwcap, 11);
	LOONGARCH_LBT_MIPS  = ExtensionSupported(hwcap, 12);
	LOONGARCH_PTW       = ExtensionSupported(hwcap, 13);

#ifdef USE_CPU_FEATURES
	cpu_features::LoongArchInfo info = cpu_features::GetLoongArchInfo();
	LOONGARCH_CPUCFG    = true;
	LOONGARCH_LAM       = info.features.LAM;
	LOONGARCH_UAL       = info.features.UAL;
	LOONGARCH_FPU       = info.features.FPU;
	LOONGARCH_LSX       = info.features.LSX;
	LOONGARCH_LASX      = info.features.LASX;
	LOONGARCH_CRC32     = info.features.CRC32;
	LOONGARCH_COMPLEX   = info.features.COMPLEX;
	LOONGARCH_CRYPTO    = info.features.CRYPTO;
	LOONGARCH_LVZ       = info.features.LVZ;
	LOONGARCH_LBT_X86   = info.features.LBT_X86;
	LOONGARCH_LBT_ARM   = info.features.LBT_ARM;
	LOONGARCH_LBT_MIPS  = info.features.LBT_MIPS;
	LOONGARCH_PTW       = info.features.PTW;
#endif
}

std::vector<std::string> CPUInfo::Features() {
std::vector<std::string> features;

	struct Flag {
		bool &flag;
		const char *str;
	};
	const Flag list[] = {
		{ LOONGARCH_CPUCFG, "Identify CPU Features" },
		{ LOONGARCH_LAM, "Atomic Memory Access Instructions" },
		{ LOONGARCH_UAL, "Non-Aligned Memory Access" },
		{ LOONGARCH_FPU, "Basic Floating-Point Instructions" },
		{ LOONGARCH_LSX, "Loongson SIMD eXtension" },
		{ LOONGARCH_LASX, "Loongson Advanced SIMD eXtension" },
		{ LOONGARCH_CRC32, "Cyclic Redundancy Check Instructions" },
		{ LOONGARCH_COMPLEX, "Complex Vector Operation Instructions" },
		{ LOONGARCH_CRYPTO, "Encryption And Decryption Vector Instructions" },
		{ LOONGARCH_LVZ, "Virtualization" },
		{ LOONGARCH_LBT_X86, "X86 Binary Translation Extension" },
		{ LOONGARCH_LBT_ARM, "ARM Binary Translation Extension" },
		{ LOONGARCH_LBT_MIPS, "MIPS Binary Translation Extension" },
		{ LOONGARCH_PTW, "Page Table Walker" },
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
	if (num_cores == 1)
		sum = StringFromFormat("%s, %i core", cpu_string, num_cores);
	else
		sum = StringFromFormat("%s, %i cores", cpu_string, num_cores);

	auto features = Features();
	for (std::string &feature : features) {
		sum += ", " + feature;
	}
	return sum;
}

#endif // PPSSPP_ARCH(LOONGARCH64)
