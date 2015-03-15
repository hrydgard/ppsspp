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
// Contains just enough to sort of understand what's going on without having to resort to an
// external disassembler all the time...

#include <stdlib.h>

#include "Common/Arm64Emitter.h"
#include "Common/StringUtils.h"

struct Instruction {
	char text[128];
	bool undefined;
	bool badbits;
	bool oddbits;
};

int SignExtend26(int x) {
	return (x & 0x02000000) ? (0xFC000000 | x) : (x & 0x3FFFFFF);
}

int SignExtend19(int x) {
	return (x & 0x00040000) ? (0xFFF80000 | x) : (x & 0x7FFFF);
}

int SignExtend9(int x) {
	return (x & 0x00000100) ? (0xFFFFFE00 | x) : (x & 0x1FF);
}

int SignExtend12(int x) {
	return (x & 0x00000800) ? (0xFFFFF000 | x) : (x & 0xFFF);
}

static const char *conds[16] = {
	"eq", // Equal
	"ne", // Not equal
	"cs", // Carry Set    "HS"
	"cc", // Carry Clear	"LO"
	"mi", // Minus (Negative)
	"pl", // Plus
	"vs", // Overflow
	"vc", // No Overflow
	"hi", // Unsigned higher
	"ls", // Unsigned lower or same
	"ge", // Signed greater than or equal
	"lt", // Signed less than
	"gt", // Signed greater than
	"le", // Signed less than or equal
	"al", // Always (unconditional) 14
};

static void DataProcessingImmediate(uint32_t w, uint64_t addr, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	char r = ((w >> 31) & 1) ? 'x' : 'w';
	if (((w >> 23) & 0x3f) == 0x25) {
		// Constant initialization.
		int imm16 = (w >> 5) & 0xFFFF;
		int opc = (w >> 29) & 3;
		int shift = ((w >> 21) & 0x3) * 16;
		const char *opnames[4] = { "movn", "(undef)", "movz", "movk" };
		snprintf(instr->text, sizeof(instr->text), "%s %c%d, 0x%04x << %d", opnames[opc], r, Rd, imm16, shift);
	} else if (((w >> 24) & 0x1F) == 0x10) {
		// Address generation relative to PC
		int op = w >> 31;
		int imm = SignExtend19(w >> 5);
		if (op & 1) imm <<= 12;
		u64 daddr = addr + imm;
		snprintf(instr->text, sizeof(instr->text), "%s x%d, 0x%04x%08x", op ? "adrp" : "adr", Rd, daddr >> 32, daddr & 0xFFFFFFFF);
	} else if (((w >> 24) & 0x1F) == 0x11) {
		// Add/subtract immediate value
		int op = (w >> 30) & 1;
		int imm = ((w >> 10) & 0xFFF);
		int shift = ((w >> 22) & 0x3) * 16;
		imm <<= shift;
		snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, %d", op == 0 ? "add" : "sub", r, Rd, r, Rn, imm);
	} else {
		snprintf(instr->text, sizeof(instr->text), "(DPI %08x)", w);
	}
}

static void BranchExceptionAndSystem(uint32_t w, uint64_t addr, Instruction *instr) {
	int Rt = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	if (((w >> 26) & 0x1F) == 5) {
		// Unconditional branch / branch+link
		int offset = SignExtend26(w) << 2;
		uint64_t target = addr + offset;
		snprintf(instr->text, sizeof(instr->text), "b%s %04x%08x", (w >> 31) ? "l" : "", (target >> 32), (target & 0xFFFFFFFF));
	} else if (((w >> 25) & 0x3F) == 0x1A) {
		// Compare and branch
		int op = (w >> 24) & 1;
		const char *opname[2] = { "cbz", "cbnz" };
		char r = ((w >> 31) & 1) ? 'x' : 'w';
		int offset = SignExtend19(w >> 5);
		snprintf(instr->text, sizeof(instr->text), "%s %c%d", op, r, Rt);
	} else if (((w >> 25) & 0x3F) == 0x1B) {
		// Test and branch
		snprintf(instr->text, sizeof(instr->text), "(test & branch %08x)", w);
	} else if (((w >> 25) & 0x7F) == 0x2A) {
		// Conditional branch
		int offset = SignExtend19(w >> 5) << 2;
		uint64_t target = addr + offset;
		int cond = w & 0xF;
		snprintf(instr->text, sizeof(instr->text), "b.%s %04x08x", conds[cond], (target >> 32), (target & 0xFFFFFFFF));
	} else if ((w >> 24) == 0xD4) {
		snprintf(instr->text, sizeof(instr->text), "(exception-gen %08x)", w);
	} else if (((w >> 20) & 0xFFC) == 0xD50) {
		snprintf(instr->text, sizeof(instr->text), "(system-reg %08x)", w);
	} else if (((w >> 25) & 0x7F) == 0x6B) {
		int op = (w >> 21) & 3;
		const char *opname[4] = { "b", "bl", "ret", "(unk)" };
		snprintf(instr->text, sizeof(instr->text), "%s x%d", opname[op], Rn);
	} else {
		snprintf(instr->text, sizeof(instr->text), "(BRX ?? %08x)", w);
	}
}

static void LoadStore(uint32_t w, uint64_t addr, Instruction *instr) {
	int size = w >> 30;
	int imm9 = SignExtend9((w >> 12) & 0x1FF);
	int Rt = (w & 0x1F);
	int Rn = ((w >> 5) & 0x1F);
	int Rm = ((w >> 16) & 0x1F);
	int option = (w >> 13) & 0x7;
	int opc = (w >> 22) & 0x3;
	char r = size == 3 ? 'x' : 'w';
	const char *opname[4] = { "str", "ldr", "(unk)", "(unk)" };
	const char *sizeSuffix[4] = { "b", "w", "", "" };

	if (((w >> 21) & 1) == 1) {
		// register offset
		snprintf(instr->text, sizeof(instr->text), "%s%s %c%d, [x%d + w%d]", opname[opc], sizeSuffix[size], r, Rt, Rn, Rm);
		return;
	} else if (((w >> 27) & 7) == 7) {
		int V = (w >> 26) & 1;
		if (V == 0) {
			bool index_unsigned = ((w >> 24) & 3) == 1;
			int imm12 = SignExtend12((w >> 10) & 0xFFF) << size;
			if (index_unsigned) {
				snprintf(instr->text, sizeof(instr->text), "%s%s %c%d, [x%d + %d]", opname[opc], sizeSuffix[size], r, Rt, Rn, imm12);
				return;
			}
		}
	}
	snprintf(instr->text, sizeof(instr->text), "(LS %08x)", w);
}

static void DataProcessingRegister(uint32_t w, uint64_t addr, Instruction *instr) {
	int Rd = w & 0x1F;
	int Rn = (w >> 5) & 0x1F;
	int Rm = (w >> 16) & 0x1F;
	char r = ((w >> 31) & 1) ? 'x' : 'w';

	if (((w >> 21) & 0xF) == 9) {
		bool S = (w >> 29) & 1;
		bool sub = (w >> 30) & 1;
		if (Rd == 31 && S) {
			// It's a CMP
			snprintf(instr->text, sizeof(instr->text), "%s%s %c%d, %c%d", "cmp", S ? "s" : "", r, Rn, r, Rm);
		} else {
			snprintf(instr->text, sizeof(instr->text), "%s%s %c%d, %c%d, %c%d", sub ? "sub" : "add", S ? "s" : "", r, Rd, r, Rn, r, Rm);
		}
	} else if (((w >> 21) & 0x2FF) == 0x2D6) {
		// Data processing
		int opcode2 = (w >> 16) & 0x1F;
		if (opcode2 == 0) {
			int opcode = (w >> 10) & 0x3F;
			// Data-processing (1 source)
			const char *opname[8] = { "rbit", "rev16", "rev", "(unk)", "clz", "cls" };
			const char *op = opcode2 >= 8 ? "unk" : opname[opcode];
			snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d", op, r, Rn, r, Rm);
		} else {
			// Data processing (2 source)
			snprintf(instr->text, sizeof(instr->text), "(data-proc-2-source %08x)", w);
		}
	} else if (((w >> 24) & 0x1f) == 0xA) {
		// Logical (shifted register)
		int shift = (w >> 22) & 0x3;
		int imm6 = (w >> 10) & 0x3f;
		int N = (w >> 20) & 1;
		int opc = (((w >> 29) & 3) << 1) | N;
		const char *opnames[8] = { "and", "bic", "orr", "orn", "eor", "eon", "ands", "bics" };
		if (imm6 == 0) {
			snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, %c%d", opnames[opc], r, Rd, r, Rn, r, Rm);
		} else {
			snprintf(instr->text, sizeof(instr->text), "(logical-shifted-register %08x", w);
		}
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
