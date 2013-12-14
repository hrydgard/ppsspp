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
#ifdef BLACKBERRY
#include <bps/deviceinfo.h>
#endif

// Only Linux platforms have /proc/cpuinfo
#if defined(__linux__)
const char procfile[] = "/proc/cpuinfo";
// https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-devices-system-cpu
const char syscpupresentfile[] = "/sys/devices/system/cpu/present";

char *GetCPUString()
{
	const char marker[] = "Hardware\t: ";
	char *cpu_string = 0;
	// Count the number of processor lines in /proc/cpuinfo
	char buf[1024];
	FILE *fp;

	fp = fopen(procfile, "r");
	if (!fp)
		return 0;
	
	while (fgets(buf, sizeof(buf), fp))
	{
		if (strncmp(buf, marker, sizeof(marker) - 1))
			continue;
		cpu_string = buf + sizeof(marker) - 1;
		cpu_string = strndup(cpu_string, strlen(cpu_string) - 1); // Strip the newline
		// INFO_LOG(BOOT, "CPU: %s", cpu_string);
		break;
	}
	
	fclose(fp);
	return cpu_string;
}

unsigned char GetCPUImplementer()
{
	const char marker[] = "CPU implementer\t: ";
	char *implementer_string = 0;
	unsigned char implementer = 0;
	char buf[1024];

	File::IOFile file(procfile, "r");
	auto const fp = file.GetHandle();
	if (!fp)
		return 0;

	while (fgets(buf, sizeof(buf), fp))
	{
		if (strncmp(buf, marker, sizeof(marker) - 1))
			continue;
		implementer_string = buf + sizeof(marker) - 1;
		implementer_string = strndup(implementer_string, strlen(implementer_string) - 1); // Strip the newline
		sscanf(implementer_string, "0x%02hhx", &implementer);
		break;
	}

	free(implementer_string);

	return implementer;
}

unsigned short GetCPUPart()
{
	const char marker[] = "CPU part\t: ";
	char *part_string = 0;
	unsigned short part = 0;
	char buf[1024];

	File::IOFile file(procfile, "r");
	auto const fp = file.GetHandle();
	if (!fp)
		return 0;

	while (fgets(buf, sizeof(buf), fp))
	{
		if (strncmp(buf, marker, sizeof(marker) - 1))
			continue;
		part_string = buf + sizeof(marker) - 1;
		part_string = strndup(part_string, strlen(part_string) - 1); // Strip the newline
		sscanf(part_string, "0x%03hx", &part);
		break;
	}

	free(part_string);

	return part;
}

bool CheckCPUFeature(const char *feature)
{
	const char marker[] = "Features\t: ";
	char buf[1024];
	FILE *fp;

	fp = fopen(procfile, "r");
	if (!fp)
		return 0;
	
	while (fgets(buf, sizeof(buf), fp))
	{
		if (strncmp(buf, marker, sizeof(marker) - 1))
			continue;
		char *featurestring = buf + sizeof(marker) - 1;
		char *token = strtok(featurestring, " ");
		while (token != NULL)
		{
			if (strstr(token, feature))
			{
				fclose(fp);
				return true; 
			}
			token = strtok(NULL, " ");
		}
	}
	
	fclose(fp);
	return false;
}

int GetCoreCount()
{
	const char marker[] = "processor\t: ";
	int cores = 0;
	char buf[1024];
	FILE *fp;

	fp = fopen(syscpupresentfile, "r");
	if (fp)
	{
		fgets(buf, sizeof(buf), fp);
		fclose(fp);

		int low, high;
		// Technically, this could be "1-2,4,8-23" but for ARM devices that seems unlikely.
		int found = sscanf(buf, "%d-%d", &low, &high);

		// Only a single number, so just one slot/core (actually threads.)
		if (found == 1)
			return 1;
		if (found == 2)
			return high - low + 1;

		// Okay, let's fall back.
	}

	fp = fopen(procfile, "r");
	if (!fp)
		return 0;
	
	while (fgets(buf, sizeof(buf), fp))
	{
		if (strncmp(buf, marker, sizeof(marker) - 1))
			continue;
		++cores;
	}
	
	fclose(fp);
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
	// When ARMv8 cpus come out, these need to be updated.
	HTT = false;
	OS64bit = false;
	CPU64bit = false;
	Mode64bit = false;				 
	vendor = VENDOR_ARM;
	
	// Get the information about the CPU 
#if !defined(__linux__)
	bool isVFP3 = false;
	bool isVFP4 = false;
#ifdef IOS
	isVFP3 = true;
	// Check for swift arch (VFP4)
#ifdef __ARM_ARCH_7S__
	isVFP4 = true;
#endif
	strcpy(brand_string, "Apple A");
	num_cores = 2;
#elif defined(BLACKBERRY)
	isVFP3 = true;
	deviceinfo_details_t* details;
	deviceinfo_get_details(&details);
	num_cores = deviceinfo_details_get_processor_core_count(details);
	strcpy(brand_string, deviceinfo_details_get_processor_name(details));
	if (!strncmp(brand_string, "MSM", 3))
		isVFP4 = true;
	deviceinfo_free_details(&details);
#elif defined(__SYMBIAN32__)
	strcpy(brand_string, "Samsung ARMv6");
	num_cores = 1;
#else
	strcpy(brand_string, "Unknown");
	num_cores = 1;
#endif
	strncpy(cpu_string, brand_string, sizeof(cpu_string));
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
#else // __linux__
	strncpy(cpu_string, GetCPUString(), sizeof(cpu_string));
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
	// These two require ARMv8 or higher
	bFP = CheckCPUFeature("fp");
	bASIMD = CheckCPUFeature("asimd");
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

	return sum;
}
