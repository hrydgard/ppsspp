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


#include <memory.h>
#include "base/logging.h"
#include "base/basictypes.h"
#include "Common.h"
#include "CPUDetect.h"
#include "StringUtils.h"

CPUInfo cpu_info;

CPUInfo::CPUInfo() {
	Detect();
}

// Detects the various cpu features
void CPUInfo::Detect()
{
	memset(this, 0, sizeof(*this));
	num_cores = 3;
	strcpy(cpu_string, "Xenon");
	strcpy(brand_string, "Microsoft");

	memset(cpu_string, 0, sizeof(cpu_string));

	HTT = true;
	logical_cpu_count = 2;
}

// Turn the cpu info into a string we can show
std::string CPUInfo::Summarize()
{
	std::string sum;
	if (num_cores == 1)
		sum = StringFromFormat("%s, %i core", cpu_string, num_cores);
	else
	{
		sum = StringFromFormat("%s, %i cores", cpu_string, num_cores);
		if (HTT) sum += StringFromFormat(" (%i logical threads per physical core)", logical_cpu_count);
	}
	if (bSSE) sum += ", SSE";
	if (bSSE2) sum += ", SSE2";
	if (bSSE3) sum += ", SSE3";
	if (bSSSE3) sum += ", SSSE3";
	if (bSSE4_1) sum += ", SSE4.1";
	if (bSSE4_2) sum += ", SSE4.2";
	if (HTT) sum += ", HTT";
	if (bAVX) sum += ", AVX";
	if (bAES) sum += ", AES";
	if (bLongMode) sum += ", 64-bit support";
	return sum;
}