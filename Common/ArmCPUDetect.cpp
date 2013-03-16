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
#include "StringUtil.h"

// Only Linux platforms have /proc/cpuinfo
#if !defined(BLACKBERRY) && !defined(IOS) && !defined(__SYMBIAN32__)
const char procfile[] = "/proc/cpuinfo";

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
#endif
int GetCoreCount()
{
#ifdef __SYMBIAN32__
	return 1;
#elif defined(BLACKBERRY) || defined(IOS)
	return 2;
#else
	const char marker[] = "processor\t: ";
	int cores = 0;
	char buf[1024];
	FILE *fp;

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
#endif
}

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
	num_cores = GetCoreCount();
#if !defined(BLACKBERRY) && !defined(IOS) && !defined(__SYMBIAN32__)
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
	// On some buggy kernels(Qualcomm) they show that they support VFPv4 but not IDIVa
	// All VFPv4 CPUs will support IDIVa
	bIDIVa = bVFPv4 || CheckCPUFeature("idiva");
	bIDIVt = bVFPv4 || CheckCPUFeature("idivt");
	// These two require ARMv8 or higher
	bFP = CheckCPUFeature("fp");
	bASIMD = CheckCPUFeature("asimd");
#else
	// Hardcode this for now
	bSwp = true;
	bHalf = true;
	bThumb = false;
	bFastMult = true;
	bVFP = true;
	bEDSP = true;
#ifdef __SYMBIAN32__
	bThumbEE = false;
	bNEON = false;
	bVFPv3 = false;
#else
	bThumbEE = true;
	bNEON = true;
	bVFPv3 = true;
#endif
	bTLS = true;
	bVFPv4 = false;
	bIDIVa = false;
	bIDIVt = false;
	bFP = false;
	bASIMD = false;
#endif
// On android, we build a separate library for ARMv7 so this is fine.
// TODO: Check for ARMv7 on other platforms.
#if defined(__ARM_ARCH_7A__)
	bArmV7 = true;
#else
	bArmV7 = false;
#endif
}

// Turn the cpu info into a string we can show
std::string CPUInfo::Summarize()
{
	std::string sum;
#if defined(BLACKBERRY) || defined(IOS) || defined(__SYMBIAN32__)
	sum = StringFromFormat("%i cores", num_cores);
#else
	if (num_cores == 1)
		sum = StringFromFormat("%s, %i core", cpu_string, num_cores);
	else
		sum = StringFromFormat("%s, %i cores", cpu_string, num_cores);
#endif
	if (bSwp) sum += ", SWP";
	if (bHalf) sum += ", Half";
	if (bThumb) sum += ", Thumb";
	if (bFastMult) sum += ", FastMult";
	if (bVFP) sum += ", VFP";
	if (bEDSP) sum += ", EDSP";
	if (bThumbEE) sum += ", ThumbEE";
	if (bNEON) sum += ", NEON";
	if (bVFPv3) sum += ", VFPv3";
	if (bTLS) sum += ", TLS";
	if (bVFPv4) sum += ", VFPv4";
	if (bIDIVa) sum += ", IDIVa";
	if (bIDIVt) sum += ", IDIVt";

	return sum;
}
