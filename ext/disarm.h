// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include <cstdint>

// This stuff used to only disassemble very old ARM but has now
// been extended to support most (but not all) modern instructions, including NEON.

// Disarm itself has the license you can see in the cpp file.
// I'm not entirely sure it's 100% gpl compatible but it's nearly
// public domain so meh.

const char *ArmRegName(int r);
void ArmDis(unsigned int addr, unsigned int w, char *output, int bufsize, bool includeWord);

// Information about a load/store instruction.
struct ArmLSInstructionInfo {
	int instructionSize;

	bool isIntegerLoadStore;
	bool isFPLoadStore;
	bool isMultiLoadStore;

	int size;  // 0 = 8-bit, 1 = 16-bit, 2 = 32-bit, 3 = 64-bit
	bool isMemoryWrite;

	int Rt;
	int Rn;
	int Rm;

	// TODO: more.
};

void ArmAnalyzeLoadStore(uint32_t addr, uint32_t op, ArmLSInstructionInfo *info);
