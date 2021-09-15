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
#if PPSSPP_ARCH(RISCV64)

#include "Common/Common.h"
#include "Common/CPUDetect.h"
#include "Common/StringUtils.h"
#include "Common/File/FileUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include <cstring>
#include <sstream>

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

int GetCoreCount()
{
	std::string line, marker = "processor\t: ";
	int cores = 1;

	std::string presentData;
	bool presentSuccess = File::ReadFileToString(true, Path(syscpupresentfile), presentData);
	std::istringstream presentFile(presentData);

	if (presentSuccess) {
		int low, high, found;
		std::getline(presentFile, line);
		found = sscanf(line.c_str(), "%d-%d", &low, &high);
		if (found == 1)
			return 1;
		if (found == 2)
			return high - low + 1;
	}

	std::string procdata;
	if (!File::ReadFileToString(true, Path(procfile), procdata))
		return 1;
	std::istringstream file(procdata);
	
	while (std::getline(file, line))
	{
		if (line.find(marker) != std::string::npos)
			++cores;
	}
	
	return cores;
}
#endif

CPUInfo cpu_info;

CPUInfo::CPUInfo() {
	Detect();
}

// Detects the various cpu features
void CPUInfo::Detect()
{
	// Set some defaults here
	HTT = false;
#if PPSSPP_ARCH(RISCV64)
	OS64bit = true;
	CPU64bit = true;
	Mode64bit = true;
#else
	OS64bit = false;
	CPU64bit = false;
	Mode64bit = false;
#endif
	vendor = VENDOR_OTHER;
	logical_cpu_count = 1;
	
	// Get the information about the CPU 
#if !defined(__linux__)
	num_cores = 1;
#else // __linux__
	truncate_cpy(cpu_string, GetCPUString().c_str());
	truncate_cpy(brand_string, GetCPUBrandString().c_str());
	num_cores = GetCoreCount();
#endif
}

// Turn the cpu info into a string we can show
std::string CPUInfo::Summarize()
{
	std::string sum;
	if (num_cores == 1)
		sum = StringFromFormat("%s, %i core", cpu_string, num_cores);
	else
		sum = StringFromFormat("%s, %i cores", cpu_string, num_cores);
	if (CPU64bit) sum += ", 64-bit";

	//TODO: parse "isa : rv64imafdc" from /proc/cpuinfo

	return sum;
}

#endif // PPSSPP_ARCH(RISCV64)
