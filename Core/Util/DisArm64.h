// Copyright (c) 2015- PPSSPP Project.

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

// Basic ARM64 disassembler.
// No promises of accuracy, mostly just made to debug JIT code.

#include <stdint.h>

typedef bool (*SymbolCallback)(char *buffer, int bufsize, uint8_t *address);

void Arm64Dis(uint64_t addr, uint32_t w, char *output, int bufsize, bool includeWord, SymbolCallback symbolCallback = nullptr);

// Information about a load/store instruction.
struct Arm64LSInstructionInfo {
	int instructionSize;

	bool isLoadOrStore;

	bool isIntegerLoadStore;
	bool isFPLoadStore;
	bool isPairLoadStore;

	int size;  // 0 = 8-bit, 1 = 16-bit, 2 = 32-bit, 3 = 64-bit
	bool isMemoryWrite;

	int Rt;
	int Rn;
	int Rm;

	// TODO: more.
};

void Arm64AnalyzeLoadStore(uint64_t addr, uint32_t op, Arm64LSInstructionInfo *info);
