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

#include <sstream>

#if PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(MAC)
#include <sys/sysctl.h>
#endif


#if PPSSPP_ARCH(ARM) || PPSSPP_ARCH(ARM64)

#include <ctype.h>

#include "Common/CommonTypes.h"
#include "Common/CPUDetect.h"
#include "Common/StringUtils.h"
#include "Common/File/FileUtil.h"
#include "Common/Data/Encoding/Utf8.h"

#if PPSSPP_PLATFORM(WINDOWS) 
#if PPSSPP_PLATFORM(UWP)
// TODO: Maybe we can move the implementation here? 
std::string GetCPUBrandString();
#else
// No CPUID on ARM, so we'll have to read the registry
#include <windows.h>
std::string GetCPUBrandString() {
	std::string cpu_string;
	
	HKEY key;
	LSTATUS result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key);
	if (result == ERROR_SUCCESS) {
		DWORD size = 0;
		DWORD type = REG_SZ;
		RegQueryValueEx(key, L"ProcessorNameString", NULL, &type, NULL, &size);
		LPBYTE buff = (LPBYTE)malloc(size);
		if (buff != NULL) {
			RegQueryValueEx(key, L"ProcessorNameString", NULL, &type, buff, &size);
			cpu_string = ConvertWStringToUTF8((wchar_t*)buff);
			free(buff);
		}
		RegCloseKey(key);
	}

	if (cpu_string.empty())
		return "Unknown";
	else
		return cpu_string;
}
#endif
#endif

// Only Linux platforms have /proc/cpuinfo
#if PPSSPP_PLATFORM(LINUX)
const char procfile[] = "/proc/cpuinfo";
// https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-devices-system-cpu
const char syscpupresentfile[] = "/sys/devices/system/cpu/present";

std::string GetCPUString() {
	std::string procdata;
	bool readSuccess = File::ReadFileToString(true, Path(procfile), procdata);
	std::istringstream file(procdata);
	std::string cpu_string;

	if (readSuccess) {
		std::string line, marker = "Hardware\t: ";
		while (std::getline(file, line)) {
			if (line.find(marker) != std::string::npos) {
				cpu_string = line.substr(marker.length());
			}
		}
	}

	if (cpu_string.empty())
		cpu_string = "Unknown";
	else if (cpu_string.back() == '\n')
		cpu_string.pop_back(); // Drop the new-line character

	return cpu_string;
}

std::string GetCPUBrandString() {
	std::string procdata;
	bool readSuccess = File::ReadFileToString(true, Path(procfile), procdata);
	std::istringstream file(procdata);
	std::string brand_string;

	if (readSuccess) {
		std::string line, marker = "Processor\t: ";
		while (std::getline(file, line)) {
			if (line.find(marker) != std::string::npos) {
				brand_string = line.substr(marker.length());
				if (brand_string.length() != 0 && !isdigit(brand_string[0])) {
					break;
				}
			}
		}
	}

	if (brand_string.empty())
		brand_string = "Unknown";
	else if (brand_string.back() == '\n')
		brand_string.pop_back(); // Drop the new-line character

	return brand_string;
}

unsigned char GetCPUImplementer()
{
	std::string line, marker = "CPU implementer\t: ";
	unsigned char implementer = 0;

	std::string procdata;
	if (!File::ReadFileToString(true, Path(procfile), procdata))
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
	if (!File::ReadFileToString(true, Path(procfile), procdata))
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

bool CheckCPUFeature(const std::string& feature)
{
	std::string line, marker = "Features\t: ";

	std::string procdata;
	if (!File::ReadFileToString(true, Path(procfile), procdata))
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
				if (token == feature)
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
#if PPSSPP_ARCH(ARM64)
	OS64bit = true;
	CPU64bit = true;
	Mode64bit = true;
#else
	OS64bit = false;
	CPU64bit = false;
	Mode64bit = false;
#endif
	vendor = VENDOR_ARM;
	logical_cpu_count = 1;

	// Get the information about the CPU 
#if !PPSSPP_PLATFORM(LINUX)
	bool isVFP3 = false;
	bool isVFP4 = false;
#if PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(MAC)
#if PPSSPP_PLATFORM(IOS)
	isVFP3 = true;
	// Check for swift arch (VFP4)
#ifdef __ARM_ARCH_7S__
	isVFP4 = true;
#endif
#endif // PPSSPP_PLATFORM(IOS)
	size_t sz = 0x41; // char brand_string[0x41]
	if (sysctlbyname("machdep.cpu.brand_string", brand_string, &sz, nullptr, 0) != 0) {
		strcpy(brand_string, "Unknown");
	}
	int num = 0;
	sz = sizeof(num);
	if (sysctlbyname("hw.physicalcpu_max", &num, &sz, nullptr, 0) == 0) {
		num_cores = num;
		sz = sizeof(num);
		if (sysctlbyname("hw.logicalcpu_max", &num, &sz, nullptr, 0) == 0) {
			logical_cpu_count = num / num_cores;
		}
	}
#elif PPSSPP_PLATFORM(WINDOWS)
	truncate_cpy(brand_string, GetCPUBrandString().c_str());
	isVFP3 = true;
	isVFP4 = false;
	SYSTEM_INFO sysInfo;
	GetSystemInfo(&sysInfo);
	num_cores = sysInfo.dwNumberOfProcessors;
#else // !PPSSPP_PLATFORM(IOS) && !PPSSPP_PLATFORM(MAC) && !PPSSPP_PLATFORM(WINDOWS)
	strcpy(brand_string, "Unknown");
	num_cores = 1;
#endif
	truncate_cpy(cpu_string, brand_string);
	// Hardcode this for now
	bSwp = true;
	bHalf = true;
	bThumb = false;
	bFastMult = true;
	bVFP = true;
	bEDSP = true;
	bThumbEE = isVFP3;
	bNEON = isVFP3;
	bVFPv3 = isVFP3;
	bTLS = true;
	bVFPv4 = isVFP4;
	bIDIVa = isVFP4;
	bIDIVt = isVFP4;
	bFP = false;
	bASIMD = false;
#else // PPSSPP_PLATFORM(LINUX)
	truncate_cpy(cpu_string, GetCPUString().c_str());
	truncate_cpy(brand_string, GetCPUBrandString().c_str());

	bSwp = CheckCPUFeature("swp");
	bHalf = CheckCPUFeature("half");
	bThumb = CheckCPUFeature("thumb");
	bFastMult = CheckCPUFeature("fastmult");
	bVFP = CheckCPUFeature("vfp");
	bEDSP = CheckCPUFeature("edsp");
	bThumbEE = CheckCPUFeature("thumbee");
	bNEON = CheckCPUFeature("neon");
	bVFPv3 = CheckCPUFeature("vfpv3");
	bTLS = CheckCPUFeature("tls");
	bVFPv4 = CheckCPUFeature("vfpv4");
	bIDIVa = CheckCPUFeature("idiva");
	bIDIVt = CheckCPUFeature("idivt");
	// Qualcomm Krait supports IDIVA but it doesn't report it. Check for krait (0x4D = Plus, 0x6F = Pro).
	unsigned short CPUPart = GetCPUPart();
	if (GetCPUImplementer() == 0x51 && (CPUPart == 0x4D || CPUPart == 0x6F))
		bIDIVa = bIDIVt = true;
	// Vero4k supports NEON but doesn't report it. Check for Arm Cortex-A53.
	if (GetCPUImplementer() == 0x41 && CPUPart == 0xd03)
		bNEON = true;
	// These two require ARMv8 or higher
	bFP = CheckCPUFeature("fp");
	bASIMD = CheckCPUFeature("asimd");
	num_cores = GetCoreCount();
#endif
#if PPSSPP_ARCH(ARM64)
	// Whether the above detection failed or not, on ARM64 we do have ASIMD/NEON.
	bNEON = true;
	bASIMD = true;
#endif
}

// Turn the cpu info into a string we can show
std::string CPUInfo::Summarize()
{
	std::string sum;
	if (num_cores == 1)
		sum = StringFromFormat("%s, %d core", cpu_string, num_cores);
	else
		sum = StringFromFormat("%s, %d cores", cpu_string, num_cores);
	if (bSwp) sum += ", SWP";
	if (bHalf) sum += ", Half";
	if (bThumb) sum += ", Thumb";
	if (bFastMult) sum += ", FastMult";
	if (bEDSP) sum += ", EDSP";
	if (bThumbEE) sum += ", ThumbEE";
	if (bTLS) sum += ", TLS";
	if (bVFP) sum += ", VFP";
	if (bVFPv3) sum += ", VFPv3";
	if (bVFPv4) sum += ", VFPv4";
	if (bNEON) sum += ", NEON";
	if (bIDIVa) sum += ", IDIVa";
	if (bIDIVt) sum += ", IDIVt";
	if (CPU64bit) sum += ", 64-bit";

	return sum;
}

#endif // PPSSPP_ARCH(ARM) || PPSSPP_ARCH(ARM64)
