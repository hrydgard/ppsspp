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

#include <stdlib.h>

#include "Common/StringUtils.h"

struct Instruction {
	char text[128];
	bool undefined;
	bool badbits;
	bool oddbits;
};

int SignExtend26(int x) {
	return (x & 0x02000000) ? (0xFC000000 | x) : x;
}

static void DataProcessingImmediate(uint32_t w, uint64_t addr, Instruction *instr) {
	snprintf(instr->text, sizeof(instr->text), "(DPI %08x)", w);
}

static void BranchExceptionAndSystem(uint32_t w, uint64_t addr, Instruction *instr) {
	if (((w >> 26) & 0x1F) == 5) {
		// Unconditional branch / branch+link
		int offset = SignExtend26(w & 0x03FFFFFF) << 2;
		uint64_t target = addr + offset;
		snprintf(instr->text, sizeof(instr->text), "b%s %08x%08x", (w >> 31) ? "l" : "", (target >> 32), (target & 0xFFFFFFFF));
	} else {
		snprintf(instr->text, sizeof(instr->text), "(BRX %08x)", w);
	}
}

static void LoadStore(uint32_t w, uint64_t addr, Instruction *instr) {
	snprintf(instr->text, sizeof(instr->text), "(LS %08x)", w);
}

static void DataProcessingRegister(uint32_t w, uint64_t addr, Instruction *instr) {
	int rd = w & 0x1F;
	int rn = (w >> 5) & 0x1F;
	int rm = (w >> 16) & 0x1F;

	if (((w >> 21) & 0xF) == 9) {
		bool S = (w >> 29) & 1;
		char r = ((w >> 31) & 1) ? 'x' : 'w';
		bool sub = (w >> 30) & 1;
		snprintf(instr->text, sizeof(instr->text), "%s%s %c%d, %c%d, %c%d", sub ? "sub" : "add", S ? "s" : "", r, rd, r, rn, r, rm);
	} else {
		snprintf(instr->text, sizeof(instr->text), "(DPR %08x)", w);
	}
}

static void FPandASIMD1(uint32_t w, uint64_t addr, Instruction *instr) {
	snprintf(instr->text, sizeof(instr->text), "(FP1 %08x)", w);
}

static void FPandASIMD2(uint32_t w, uint64_t addr, Instruction *instr) {
	snprintf(instr->text, sizeof(instr->text), "(FP2 %08x)", w);
}

static void DisassembleInstruction(uint32_t w, uint64_t addr, Instruction *instr) {
	memset(instr, 0, sizeof(*instr));
	
	// Identify the main encoding groups. See C3.1 A64 instruction index by encoding
	int id = (w >> 25) & 0xF;
	switch (id) {
	case 0: case 1: case 2: case 3: // 00xx
		instr->undefined = true;
		break;
	case 8: case 9:
		DataProcessingImmediate(w, addr, instr);
		break;
	case 0xA: case 0xB:
		BranchExceptionAndSystem(w, addr, instr);
		break;
	case 4: case 6: case 0xC: case 0xE:
		LoadStore(w, addr, instr);
		break;
	case 5: case 0xD:
		DataProcessingRegister(w, addr, instr);
		break;
	case 7:
		FPandASIMD1(w, addr, instr);
		break;
	case 0xF:
		FPandASIMD2(w, addr, instr);
		break;
	}
}

void Arm64Dis(uint64_t addr, uint32_t w, char *output, int bufsize, bool includeWord) {
	Instruction instr;
	DisassembleInstruction(w, addr, &instr);
	char temp[256];
	if (includeWord) {
		snprintf(output, bufsize, "%08x\t%s", w, instr.text);
	} else {
		snprintf(output, bufsize, "%s", instr.text);
	}
	if (instr.undefined || instr.badbits || instr.oddbits) {
		if (instr.undefined) snprintf(output, bufsize, "%08x\t[undefined instr]", w);
		if (instr.badbits) snprintf(output, bufsize, "%08x\t[illegal bits]", w);

		// strcat(output, " ? (extra bits)");  
		if (instr.oddbits) {
			snprintf(temp, sizeof(temp), " [unexpected bits %08x]", w);
			strcat(output, temp);
		}
	}
	// zap tabs
	while (*output) {
		if (*output == '\t')
			*output = ' ';
		output++;
	}
}
