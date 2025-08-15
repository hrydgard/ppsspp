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

// Detect the cpu, so we'll know which optimizations to use
#pragma once

#include "ppsspp_config.h"
#include <string>
#include <vector>

enum CPUVendor {
	VENDOR_INTEL = 0,
	VENDOR_AMD = 1,
	VENDOR_ARM = 2,
	VENDOR_OTHER = 3,
};

struct CPUInfo {
	CPUVendor vendor;

	// Misc
	char cpu_string[0x21];
	char brand_string[0x41];
	bool OS64bit;
	bool CPU64bit;
	bool Mode64bit;

	bool HTT;

	// Number of real CPU cores.
	int num_cores;
	// Number of logical CPUs per core.
	int logical_cpu_count;

	bool bAtom;
	bool bPOPCNT;
	bool bLAHFSAHF64;
	bool bLongMode;
	bool bMOVBE;
	bool bFXSR;
	bool bLZCNT;
	bool bBMI1;
	bool bBMI2;
	bool bBMI2_fast;
	bool bXOP;
	bool bRTM;

	// x86 : SIMD 128 bit
	bool bSSE;
	bool bSSE2;
	bool bSSE3;
	bool bSSSE3;
	bool bSSE4_1;
	bool bSSE4_2;
	bool bSSE4A;
	bool bAES;
	bool bSHA;
	bool bF16C;
	// x86 : SIMD 256 bit
	bool bAVX;
	bool bAVX2;
	bool bFMA3;
	bool bFMA4;

	// ARM specific CPUInfo
	bool bSwp;
	bool bHalf;
	bool bThumb;
	bool bFastMult;
	bool bVFP;
	bool bEDSP;
	bool bThumbEE;
	bool bNEON;
	bool bVFPv3;
	bool bTLS;
	bool bVFPv4;
	bool bIDIVa;
	bool bIDIVt;

	// ARMv8 specific
	bool bFP;
	bool bASIMD;
	bool bSVE;
	bool bSVE2;
	bool bFRINT;

	// MIPS specific
	bool bXBurst1;
	bool bXBurst2;

	// RiscV specific extension flags.
	bool RiscV_M;
	bool RiscV_A;
	bool RiscV_F;
	bool RiscV_D;
	bool RiscV_C;
	bool RiscV_V;
	bool RiscV_Zicsr;
	bool RiscV_Zba;
	bool RiscV_Zbb;
	bool RiscV_Zbc;
	bool RiscV_Zbs;
	bool RiscV_Zcb;
	bool RiscV_Zfa;
	bool RiscV_Zfh;
	bool RiscV_Zfhmin;
	bool RiscV_Zicond;
	bool RiscV_Zvbb;
	bool RiscV_Zvkb;

	// LoongArch specific extension flags.
	bool LOONGARCH_CPUCFG;
	bool LOONGARCH_LAM;
	bool LOONGARCH_UAL;
	bool LOONGARCH_FPU;
	bool LOONGARCH_LSX;
	bool LOONGARCH_LASX;
	bool LOONGARCH_CRC32;
	bool LOONGARCH_COMPLEX;
	bool LOONGARCH_CRYPTO;
	bool LOONGARCH_LVZ;
	bool LOONGARCH_LBT_X86;
	bool LOONGARCH_LBT_ARM;
	bool LOONGARCH_LBT_MIPS;
	bool LOONGARCH_PTW;

	// Quirks
	struct {
		// Samsung Galaxy S7 devices (Exynos 8890) have a big.LITTLE configuration where the cacheline size differs between big and LITTLE.
		// GCC's cache clearing function would detect the cacheline size on one and keep it for later. When clearing
		// with the wrong cacheline size on the other, that's an issue. In case we want to do something different in this
		// situation in the future, let's keep this as a quirk, but our current code won't detect it reliably
		// if it happens on new archs. We now use better clearing code on ARM64 that doesn't have this issue.
		bool bExynos8890DifferingCachelineSizes;
	} sQuirks;

	// Call Detect()
	explicit CPUInfo();

	// Turn the cpu info into a string we can show
	std::vector<std::string> Features();
	std::string Summarize();

private:
	// Detects the various cpu features
	void Detect();
};

extern CPUInfo cpu_info;

const char *GetCompilerABI();
