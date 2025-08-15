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
#if PPSSPP_ARCH(MIPS) || PPSSPP_ARCH(MIPS64)

#include "Common/CommonTypes.h"
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
	std::string line, marker = "Hardware\t: ";
	std::string cpu_string = "Unknown";

	std::string procdata;
	if (!File::ReadSysTextFileToString(procfile, &procdata))
		return cpu_string;
	std::istringstream file(procdata);
	
	while (std::getline(file, line))
	{
		if (line.find(marker) != std::string::npos)
		{
			cpu_string = line.substr(marker.length());
			cpu_string.pop_back(); // Drop the new-line character
		}
	}

	return cpu_string;
}

unsigned char GetCPUImplementer()
{
	std::string line, marker = "CPU implementer\t: ";
	unsigned char implementer = 0;

	std::string procdata;
	if (!File::ReadSysTextFileToString(procfile, &procdata))
		return 0;
	std::istringstream file(procdata);

	while (std::getline(file, line))
	{
		if (line.find(marker) != std::string::npos)
		{
			line = line.substr(marker.length());
			sscanf(line.c_str(), "0x%02hhx", &implementer);
			break;
		}
	}

	return implementer;
}

unsigned short GetCPUPart()
{
	std::string line, marker = "CPU part\t: ";
	unsigned short part = 0;

	std::string procdata;
	if (!File::ReadSysTextFileToString(procfile, &procdata))
		return 0;
	std::istringstream file(procdata);

	while (std::getline(file, line))
	{
		if (line.find(marker) != std::string::npos)
		{
			line = line.substr(marker.length());
			sscanf(line.c_str(), "0x%03hx", &part);
			break;
		}
	}

	return part;
}

bool CheckCPUASE(const std::string& ase)
{
	std::string line, marker = "ASEs implemented\t: ";

	std::string procdata;
	if (!File::ReadSysTextFileToString(procfile, &procdata))
		return false;
	std::istringstream file(procdata);
	
	while (std::getline(file, line))
	{
		if (line.find(marker) != std::string::npos)
		{
			std::stringstream line_stream(line);
			std::string token;
			while (std::getline(line_stream, token, ' '))
			{
				if (token == ase)
					return true;
			}
		}
	}
	
	return false;
}

int GetCoreCount()
{
	std::string line, marker = "processor\t: ";
	int cores = 1;

	std::string presentData;
	bool presentSuccess = File::ReadSysTextFileToString(syscpupresentfile, &presentData);
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
	if (!File::ReadSysTextFileToString(procfile, &procdata))
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
#if PPSSPP_ARCH(MIPS64)
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
	// Hardcode this for now
	bXBurst1 = false;
	bXBurst2 = false;
	num_cores = 1;
#else // __linux__
	strncpy(cpu_string, GetCPUString().c_str(), sizeof(cpu_string));
	bXBurst1 = CheckCPUASE("mxu");
	bXBurst2 = CheckCPUASE("mxu2");
	unsigned short CPUPart = GetCPUPart();
	num_cores = GetCoreCount();
#endif
}

std::vector<std::string> CPUInfo::Features() {
	std::vector<std::string> features;

	struct Flag {
		bool &flag;
		const char *str;
	};
	const Flag list[] = {
		{ bXBurst1, "XBurst1" },
		{ bXBurst2, "XBurst2" },
		{ CPU64bit, "64-bit" },
	};

	for (auto &item : list) {
		if (item.flag) {
			features.push_back(item.str);
		}
	}

	return features;
}

// Turn the cpu info into a string we can show
std::string CPUInfo::Summarize()
{
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

#endif // PPSSPP_ARCH(MIPS) || PPSSPP_ARCH(MIPS64)
