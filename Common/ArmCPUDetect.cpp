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
    // Samsung Exynos , Qualcomm Snapdragon , Mediatek , NVIDIA Tegra
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
	// These require ARMv8 or higher , see the documentation link on below
    // https://en.wikichip.org/wiki/arm/armv8
    // https://en.wikipedia.org/wiki/AArch64
    // https://sourceware.org/binutils/docs/as/AArch64-Extensions.html
    // https://marcin.juszkiewicz.com.pl/ and https://marcin.juszkiewicz.com.pl/download/tables/arm-socs.html
    // ARMv8.0
	bFP = CheckCPUFeature("fp");
	bASIMD = CheckCPUFeature("asimd");
    bEVTSTRM = CheckCPUFeature("evtstrm");
    bCPUID = CheckCPUFeature("cpuid");
    bAESARM = CheckCPUFeature("aes");
    // CRC32 is optional on ARMv8.0 , mandatory on ARMv8.1 later
    // See https://en.wikichip.org/wiki/arm/armv8.1
    bCRC32 = CheckCPUFeature("crc32");
    bPMULL = CheckCPUFeature("pmull");
    bSHA1 = CheckCPUFeature("sha1");
    bSHA2 = CheckCPUFeature("sha2");
    bSSBS = CheckCPUFeature("ssbs");
    bSB = CheckCPUFeature("sb");
    bDGH = CheckCPUFeature("dgh");
    // These extension require of later version of ARMv8.0 ( Cortex A55 , A510, A710 , A715, A72 , A73 , A75 , A76 , A77 , A78 , X1 , X2 , X3)
    // ARMv8.1
    bASIMDRDM = CheckCPUFeature("asimdrdm");
    bATOMICS = CheckCPUFeature("atomics");
    // ARMv8.2
    bFPHP = CheckCPUFeature("fphp");
    bDCPOP = CheckCPUFeature("dcpop");
    bSHA3 = CheckCPUFeature("sha3");
    bSM3 = CheckCPUFeature("sm3");
    bSM4 = CheckCPUFeature("sm4");
    bASIMDDP = CheckCPUFeature("asimddp");
    bSHA512 = CheckCPUFeature("sha512");
    bASIMDHP = CheckCPUFeature("asimdhp");
    bASIMDFHM = CheckCPUFeature("asimdfhm");
    bUSCAT = CheckCPUFeature("uscat");
    bFLAGM = CheckCPUFeature("flagm");
    bDCPODP = CheckCPUFeature("dcpodp");
    bI8MM = CheckCPUFeature("i8mm");
    bBF16 = CheckCPUFeature("bf16");
    bSVE = CheckCPUFeature("sve");
    bSVEBF16 = CheckCPUFeature("svebf16");
    bSVEF32MM = CheckCPUFeature("svef32mm");
    bSVEF64MM = CheckCPUFeature("svef64mm");
    bSVEI8MM = CheckCPUFeature("svei8mm");
    // ARMv8.3
    bJSCVT = CheckCPUFeature("jscvt");
    bLRCPC = CheckCPUFeature("lrcpc");
    bFCMA = CheckCPUFeature("fcma");
    // ARMv8.4
    bILRCPC = CheckCPUFeature("ilrcpc");
    bDIT = CheckCPUFeature("dit");
    bPACA = CheckCPUFeature("paca");
    bPACG = CheckCPUFeature("pacg");
    // ARMv8.5
    bFLAGM2 = CheckCPUFeature("flagm2");
    bFRINT = CheckCPUFeature("frint");
    bBTI = CheckCPUFeature("bti");
    bMTE = CheckCPUFeature("mte");
    bMTE3 = CheckCPUFeature("mte3");
    bRNG = CheckCPUFeature("rng");
    // ARMv8.6
    bECV = CheckCPUFeature("ecv");
    // ARMv8.7
    bAFP = CheckCPUFeature("afp");
    bRPRES = CheckCPUFeature("rpres");
    bWFXT = CheckCPUFeature("wfxt");
    // ARMv9.0
    bSVE2 = CheckCPUFeature("sve2");
    bSVEAES = CheckCPUFeature("sveaes");
    bSVEBITPERM = CheckCPUFeature("svebitperm");
    bSVEPMULL = CheckCPUFeature("svepmull");
    bSVESHA3 = CheckCPUFeature("svesha3");
    bSVESM4 = CheckCPUFeature("svesm4");
	num_cores = GetCoreCount();
#endif
#if PPSSPP_ARCH(ARM64)
	// Whether the above detection failed or not, on ARMv8.0 and later we do have basic crypto extension ASIMD/NEON,FP.
	bNEON = true;
	bASIMD = true;
    bFP = true;
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
    // ARMv7 Extension
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
    // ARM64 Extension
    // ARMv8.0
    if (bFP) sum += ", FP";
    if (bASIMD) sum += ", ASIMD";
    if (bEVTSTRM) sum += ", EVTSTRM";
    if (bCPUID) sum += ", CPUID";
    if (bAESARM) sum += ", AES";
    if (bCRC32) sum += ", CRC32";
    if (bPMULL) sum += ", PMULL";
    if (bSHA1) sum += ", SHA1";
    if (bSHA2) sum += ", SHA2";
    if (bSSBS) sum += ", SSBS";
    if (bSB) sum += ", SB";
    if (bDGH) sum += ", DGH";
    // ARMv8.1
    if (bATOMICS) sum += ", ATOMICS";
    if (bASIMDRDM) sum += ", ASIMDRDM";
    // ARMv8.2
    if (bFPHP) sum += ", FPHP";
    if (bDCPOP) sum += ", DCPOP";
    if (bSHA3) sum += ", SHA3";
    if (bSM3) sum += ", SM3";
    if (bSM4) sum += ", SM4";
    if (bASIMDDP) sum += ", ASIMDDP";
    if (bSHA512) sum += ", SHA512";
    if (bASIMDHP) sum += ", ASIMDHP";
    if (bASIMDFHM) sum += ", ASIMDFHM";
    if (bUSCAT) sum += ", USCAT";
    if (bFLAGM) sum += ", FLAGM";
    if (bDCPODP) sum += ", DCPODP";
    if (bI8MM) sum += ", I8MM";
    if (bBF16) sum += ", BF16";
    if (bSVE) sum += ", SVE";
    if (bSVEBF16) sum += ", SVEBF16";
    if (bSVEF32MM) sum += ", SVEF32MM";
    if (bSVEF64MM) sum += ", SVEF64MM";
    if (bSVEI8MM) sum += ", SVEI8MM";
    // ARMv8.3
    if (bJSCVT) sum += ", JSCVT";
    if (bLRCPC) sum += ", LRCPC";
    if (bFCMA) sum += ", FCMA";
    // ARMv8.4
    if (bILRCPC) sum += ", ILRCPC";
    if (bDIT) sum += ", DIT";
    if (bPACA) sum += ", PACA";
    if (bPACG) sum += ", PACG";
    // ARMv8.5
    if (bFLAGM2) sum += ", FLAGM2";
    if (bFRINT) sum += ", FRINT";
    if (bBTI) sum += ", BTI";
    if (bMTE) sum += ", MTE";
    if (bMTE3) sum += ", MTE3";
    if (bRNG) sum += ", RNG";
    // ARMv8.6
    if (bECV) sum += ", ECV";
    // ARMv8.7
    if (bAFP) sum += ", AFP";
    if (bRPRES) sum += ", RPRES";
    if (bWFXT) sum  += ", WFXT";
    // ARMv9.0
    if (bSVE2) sum += ", SVE2";
    if (bSVEAES) sum += ", SVEAES";
    if (bSVEBITPERM) sum += ", SVEBITPERM";
    if (bSVEPMULL) sum += ", SVEPMULL";
    if (bSVESHA3) sum += ", SVESHA3";
    if (bSVESM4) sum += ", SVESM4";
	if (CPU64bit) sum += ", 64-bit";

	return sum;
}

#endif // PPSSPP_ARCH(ARM) || PPSSPP_ARCH(ARM64)
