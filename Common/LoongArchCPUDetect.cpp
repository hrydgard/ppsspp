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

std::string GetCPUString() {
    //TODO
	std::string cpu_string;
	cpu_string = "Unknown";
	return cpu_string;
}

std::string GetCPUBrandString() {
    //TODO
    std::string brand_string;
    brand_string = "Unknown";
    return brand_string;
}

int GetCoreCount() {	
	// TODO 
	return 4;
}

#endif

CPUInfo cpu_info;

CPUInfo::CPUInfo() {
	Detect();
}

// Detects the various cpu features
void CPUInfo::Detect() {
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

	truncate_cpy(brand_string, "Unknown");
#if !defined(__linux__)
	num_cores = 1;
	logical_cpu_count = 1;
	truncate_cpy(cpu_string, "Unknown");
#else // __linux__
	truncate_cpy(cpu_string, GetCPUString().c_str());
	truncate_cpy(brand_string, GetCPUBrandString().c_str());
	num_cores = GetCoreCount();
#endif
}

std::vector<std::string> CPUInfo::Features() {
	// TODO
	std::vector<std::string> features;



	return features;
}


// Turn the cpu info into a string we can show
std::string CPUInfo::Summarize() {
	std::string sum;
	if (num_cores == 1)
		sum = StringFromFormat("%s, %i core", cpu_string, num_cores);
	else
		sum = StringFromFormat("%s, %i cores", cpu_string, num_cores);

	//TODO: parse /proc/cpuinfo

	return sum;
}

#endif // PPSSPP_ARCH(LOONGARCH64)