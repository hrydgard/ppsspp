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

// Every architecture has its own define. This needs to be added to.
#if defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7S__)
#define HAVE_ARMV7 1
#endif

#include <string>

enum CPUVendor {
	VENDOR_INTEL = 0,
	VENDOR_AMD = 1,
	VENDOR_ARM = 2,
	VENDOR_OTHER = 3,
};

struct CPUInfo {
	CPUVendor vendor;

	char cpu_string[0x21];
	char brand_string[0x41];
	bool OS64bit = false;
	bool CPU64bit = false;
	bool Mode64bit = false;

	bool HTT = false;
	int num_cores = 1;
	int logical_cpu_count = 1;

	bool bSSE = false;
	bool bSSE2 = false;
	bool bSSE3 = false;
	bool bSSSE3 = false;
	bool bPOPCNT = false;
	bool bSSE4_1 = false;
	bool bSSE4_2 = false;
	bool bLZCNT = false;
	bool bSSE4A = false;
	bool bAVX = false;
	bool bAVX2 = false;
	bool bFMA = false;
	bool bAES = false;
	bool bLAHFSAHF64 = false;
	bool bLongMode = false;
	bool bAtom = false;
	bool bBMI1 = false;
	bool bBMI2 = false;
	bool bMOVBE = false;
	bool bFXSR = false;

	// ARM specific CPUInfo
	bool bSwp = false;
	bool bHalf = false;
	bool bThumb = false;
	bool bFastMult = false;
	bool bVFP = false;
	bool bEDSP = false;
	bool bThumbEE = false;
	bool bNEON = false;
	bool bVFPv3 = false;
	bool bTLS = false;
	bool bVFPv4 = false;
	bool bIDIVa = false;
	bool bIDIVt = false;

	// ARMv8 specific
	bool bFP = false;
	bool bASIMD = false;

	// MIPS specific
	bool bXBurst1 = false;
	bool bXBurst2 = false;

	// Call Detect()
	explicit CPUInfo();

	// Turn the cpu info into a string we can show
	std::string Summarize();

private:
	// Detects the various cpu features
	void Detect();
};

extern CPUInfo cpu_info;
