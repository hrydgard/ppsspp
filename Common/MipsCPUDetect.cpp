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

#include "Common.h"
#include "CPUDetect.h"
#include "StringUtils.h"
#include "FileUtil.h"

// Only Linux platforms have /proc/cpuinfo
#if defined(__linux__)
const char procfile[] = "/proc/cpuinfo";
// https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-devices-system-cpu
const char syscpupresentfile[] = "/sys/devices/system/cpu/present";

std::string GetCPUString()
{
	std::string line, marker = "Hardware\t: ";
	std::string cpu_string = "Unknown";
	std::fstream file;
	if (!File::OpenCPPFile(file, procfile, std::ios::in))
		return cpu_string;
	
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
	std::fstream file;

	if (!File::OpenCPPFile(file, procfile, std::ios::in))
		return 0;

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
	std::fstream file;

	if (!File::OpenCPPFile(file, procfile, std::ios::in))
		return 0;

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
	std::fstream file;

	if (!File::OpenCPPFile(file, procfile, std::ios::in))
		return 0;
	
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
	std::fstream file;

	if (File::OpenCPPFile(file, syscpupresentfile, std::ios::in))
	{
		int low, high, found;
		std::getline(file, line);
		found = sscanf(line.c_str(), "%d-%d", &low, &high);
		if (found == 1)
			return 1;
		if (found == 2)
			return high - low + 1;
	}

	if (!File::OpenCPPFile(file, procfile, std::ios::in))
		return 1;
	
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
#ifdef _ARCH_64
	OS64bit = true;
	CPU64bit = true;
	Mode64bit = true;
#else
	OS64bit = false;
	CPU64bit = false;
	Mode64bit = false;
#endif
	vendor = VENDOR_OTHER;
	
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

// Turn the cpu info into a string we can show
std::string CPUInfo::Summarize()
{
	std::string sum;
	if (num_cores == 1)
		sum = StringFromFormat("%s, %i core", cpu_string, num_cores);
	else
		sum = StringFromFormat("%s, %i cores", cpu_string, num_cores);
	if (bXBurst1) sum += ", XBurst1";
	if (bXBurst2) sum += ", XBurst2";
	if (CPU64bit) sum += ", 64-bit";

	return sum;
}
