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

class RiscVCPUInfoParser {
public:
	RiscVCPUInfoParser();

	int ProcessorCount();
	int TotalLogicalCount();

	std::string ISAString();

private:
	std::vector<std::vector<std::string>> cores_;
};

RiscVCPUInfoParser::RiscVCPUInfoParser() {
	std::string procdata, line;
	if (!File::ReadFileToString(true, Path(procfile), procdata))
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

int RiscVCPUInfoParser::ProcessorCount() {
	// Not using present as that counts the logical CPUs (aka harts.)
	static const char *marker = "processor\t: ";
	std::set<std::string> processors;
	for (auto core : cores_) {
		for (auto line : core) {
			if (line.find(marker) != line.npos)
				processors.insert(line);
		}
	}

	return (int)processors.size();
}

int RiscVCPUInfoParser::TotalLogicalCount() {
	std::string presentData, line;
	bool presentSuccess = File::ReadFileToString(true, Path(syscpupresentfile), presentData);
	if (presentSuccess) {
		std::istringstream presentFile(presentData);

		int low, high, found;
		std::getline(presentFile, line);
		found = sscanf(line.c_str(), "%d-%d", &low, &high);
		if (found == 1)
			return 1;
		if (found == 2)
			return high - low + 1;
	}

	static const char *marker = "hart\t\t: ";
	std::set<std::string> harts;
	for (auto core : cores_) {
		for (auto line : core) {
			if (line.find(marker) != line.npos)
				harts.insert(line);
		}
	}

	return (int)harts.size();
}

std::string RiscVCPUInfoParser::ISAString() {
	static const char *marker = "isa\t\t: ";
	for (auto core : cores_) {
		for (auto line : core) {
			if (line.find(marker) != line.npos)
				return line.substr(strlen(marker));
		}
	}

	return "Unknown";
}
#endif

static bool ExtensionSupported(unsigned long v, char c) {
	unsigned long bit = (v >> (c - 'A')) & 1;
	return bit == 1;
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

	// Not sure how to get anything great here.
	truncate_cpy(brand_string, "Unknown");
	
#if !defined(__linux__)
	num_cores = 1;
	logical_cpu_count = 1;
	truncate_cpy(cpu_string, "Unknown");
#else // __linux__
	RiscVCPUInfoParser parser;
	num_cores = parser.ProcessorCount();
	logical_cpu_count = parser.TotalLogicalCount() / num_cores;
	if (logical_cpu_count <= 0)
		logical_cpu_count = 1;

	truncate_cpy(cpu_string, parser.ISAString().c_str());
#endif

	unsigned long hwcap = getauxval(AT_HWCAP);
	RiscV_M = ExtensionSupported(hwcap, 'M');
	RiscV_A = ExtensionSupported(hwcap, 'A');
	RiscV_F = ExtensionSupported(hwcap, 'F');
	RiscV_D = ExtensionSupported(hwcap, 'D');
	RiscV_C = ExtensionSupported(hwcap, 'C');
	RiscV_V = ExtensionSupported(hwcap, 'V');
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
