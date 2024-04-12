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

#include "ext/cpu_features/include/cpuinfo_riscv.h"

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
const char firmwarefile[] = "/sys/firmware/devicetree/base/compatible";

class RiscVCPUInfoParser {
public:
	RiscVCPUInfoParser();

	int ProcessorCount();
	int TotalLogicalCount();

	std::string ISAString();
	bool FirmwareMatchesCompatible(const std::string &str);

private:
	std::vector<std::vector<std::string>> cores_;
	std::vector<std::string> firmware_;
	bool firmwareLoaded_ = false;
};

RiscVCPUInfoParser::RiscVCPUInfoParser() {
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

int RiscVCPUInfoParser::ProcessorCount() {
	// Not using present as that counts the logical CPUs (aka harts.)
	static const char * const marker = "processor\t: ";
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
	bool presentSuccess = File::ReadSysTextFileToString(Path(syscpupresentfile), &presentData);
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

	static const char * const marker = "hart\t\t: ";
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
	static const char * const marker = "isa\t\t: ";
	for (auto core : cores_) {
		for (auto line : core) {
			if (line.find(marker) != line.npos)
				return line.substr(strlen(marker));
		}
	}

	return "Unknown";
}

bool RiscVCPUInfoParser::FirmwareMatchesCompatible(const std::string &str) {
	if (!firmwareLoaded_) {
		firmwareLoaded_ = true;

		std::string data;
		if (!File::ReadSysTextFileToString(Path(firmwarefile), &data))
			return false;

		SplitString(data, '\0', firmware_);
	}

	for (auto compatible : firmware_) {
		if (compatible == str)
			return true;
	}

	return false;
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

	// A number of CPUs support a limited set of bitmanip.  It's not all U74, so we use SOC for now...
	if (parser.FirmwareMatchesCompatible("starfive,jh7110")) {
		RiscV_Zba = true;
		RiscV_Zbb = true;
	}
#endif

	unsigned long hwcap = getauxval(AT_HWCAP);
	RiscV_M = ExtensionSupported(hwcap, 'M');
	RiscV_A = ExtensionSupported(hwcap, 'A');
	RiscV_F = ExtensionSupported(hwcap, 'F');
	RiscV_D = ExtensionSupported(hwcap, 'D');
	RiscV_C = ExtensionSupported(hwcap, 'C');
	RiscV_V = ExtensionSupported(hwcap, 'V');
	// We assume as in RVA20U64 that F means Zicsr is available.
	RiscV_Zicsr = RiscV_F;

#ifdef USE_CPU_FEATURES
	cpu_features::RiscvInfo info = cpu_features::GetRiscvInfo();
	CPU64bit = info.features.RV64I;
	RiscV_M = info.features.M;
	RiscV_A = info.features.A;
	RiscV_F = info.features.F;
	RiscV_D = info.features.D;
	RiscV_C = info.features.C;
	RiscV_V = info.features.V;
	// Seems to be wrong sometimes, assume we have it if we have F.
	RiscV_Zicsr = info.features.Zicsr || info.features.F;

	truncate_cpy(brand_string, info.uarch);
#endif
}

std::vector<std::string> CPUInfo::Features() {
	std::vector<std::string> features;

	struct Flag {
		bool &flag;
		const char *str;
	};
	const Flag list[] = {
		{ RiscV_M, "Muldiv" },
		{ RiscV_A, "Atomic" },
		{ RiscV_F, "Float" },
		{ RiscV_D, "Double" },
		{ RiscV_C, "Compressed" },
		{ RiscV_V, "Vector" },
		{ RiscV_Zvbb, "Vector Basic Bitmanip" },
		{ RiscV_Zvkb, "Vector Crypto Bitmanip" },
		{ RiscV_Zba, "Bitmanip Zba" },
		{ RiscV_Zbb, "Bitmanip Zbb" },
		{ RiscV_Zbc, "Bitmanip Zbc" },
		{ RiscV_Zbs, "Bitmanip Zbs" },
		{ RiscV_Zcb, "Compress Zcb" },
		{ RiscV_Zfa, "Float Additional" },
		{ RiscV_Zfh, "Float Half" },
		{ RiscV_Zfhmin, "Float Half Minimal" },
		{ RiscV_Zicond, "Integer Conditional" },
		{ RiscV_Zicsr, "Zicsr" },
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

#endif // PPSSPP_ARCH(RISCV64)
