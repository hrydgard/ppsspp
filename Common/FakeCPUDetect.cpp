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
#if PPSSPP_ARCH(ARM) || PPSSPP_ARCH(ARM64)
#define REAL_CPUDETECT_AVAIL 1
#elif (PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)) && !defined(__EMSCRIPTEN__)
#define REAL_CPUDETECT_AVAIL 1
#elif PPSSPP_ARCH(MIPS) || PPSSPP_ARCH(MIPS64)
#define REAL_CPUDETECT_AVAIL 1
#elif PPSSPP_ARCH(RISCV64)
#define REAL_CPUDETECT_AVAIL 1
#elif PPSSPP_ARCH(LOONGARCH64)
#define REAL_CPUDETECT_AVAIL 1
#endif

#ifndef REAL_CPUDETECT_AVAIL
#include <cstdint>
#include <cstring>
#include <memory>

#include "Common/CommonTypes.h"
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
	num_cores = 1;
	strcpy(cpu_string, "Unknown");
	strcpy(brand_string, "Unknown");

	HTT = false;
	logical_cpu_count = 2;
}

std::vector<std::string> CPUInfo::Features() {
	std::vector<std::string> features;
	return features;
}

// Turn the cpu info into a string we can show
std::string CPUInfo::Summarize() {
	std::string sum;
	sum = StringFromFormat("%s, %i core", cpu_string, num_cores);

	auto features = Features();
	for (std::string &feature : features) {
		sum += ", " + feature;
	}
	return sum;
}
#endif
