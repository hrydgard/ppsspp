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
// Does enough to understand what's going on without having to resort to an
// external disassembler all the time...

#include <stdlib.h>

#include "Common/Arm64Emitter.h"
#include "Common/StringUtils.h"
#include "Core/Util/DisArm64.h"

struct Instruction {
	char text[128];
	bool undefined;
	bool badbits;
	bool oddbits;
};

static const char * const shiftnames[4] = { "lsl", "lsr", "asr", "ror" };
static const char * const extendnames[8] = { "uxtb", "uxth", "uxtw", "uxtx", "sxtb", "sxth", "sxtw", "sxtx" };
static const char * const condnames[16] = {
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

int SignExtend26(int x) {
	return (x & 0x02000000) ? (0xFC000000 | x) : (x & 0x3FFFFFF);
}

int SignExtend19(int x) {
	return (x & 0x00040000) ? (0xFFF80000 | x) : (x & 0x7FFFF);
}

int SignExtend9(int x) {
	return (x & 0x00000100) ? (0xFFFFFE00 | x) : (x & 0x1FF);
}

int SignExtend7(int x) {
	return (x & 0x00000040) ? (0xFFFFFF80 | x) : (x & 0x7F);
}

int SignExtend12(int x) {
	return (x & 0x00000800) ? (0xFFFFF000 | x) : (x & 0xFFF);
}

int HighestSetBit(int value) {
	int highest = 0;
	for (int i = 0; i < 32; i++) {
		if (value & (1 << i))
			highest = i;
	}
	return highest;
}

int LowestSetBit(int value, int maximum = 32) {
	int lowest = 0;
	for (int i = 0; i < maximum; i++) {
		if (value & (1 << i))
			return i;
	}
	return maximum;
}

static uint64_t Ones(int len) {
	if (len == 0x40) {
		return 0xFFFFFFFFFFFFFFFF;
	}
	return (1ULL << len) - 1;
}

static uint64_t Replicate(uint64_t value, int esize) {
	uint64_t out = 0;
	value &= Ones(esize);
	for (int i = 0; i < 64; i += esize) {
		out |= value << i;
	}
	return out;
}

static uint64_t ROR(uint64_t value, int amount, int esize) {
	uint64_t rotated = (value >> amount) | (value << (esize - amount));
	return rotated & Ones(esize);
}

void DecodeBitMasks(int immN, int imms, int immr, uint64_t *tmask, uint64_t *wmask) {
	// Compute log2 of element size
	// 2^len must be in range [2, M]
	int len = HighestSetBit((immN << 6) | ((~imms) & 0x3f));
	// if len < 1 then ReservedValue();
	// assert M >= (1 << len);
	// Determine S, R and S - R parameters
	int levels = Ones(len);
	uint32_t S = imms & levels;
	uint32_t R = immr & levels;
	int diff = S - R; // 6-bit subtract with borrow
	int esize = 1 << len;
	int d = diff & Ones(len - 1);
	uint32_t welem = Ones(S + 1);
	uint32_t telem = Ones(d + 1);
	if (wmask) {
		uint64_t rotated = ROR(welem, R, esize);
		*wmask = Replicate(rotated, esize);
	}
	if (tmask) {
		*tmask = Replicate(telem, esize);
	}
}

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
		snprintf(instr->text, sizeof(instr->text), "%s %c%d, #0x%04x << %d", opnames[opc], r, Rd, imm16, shift);
	} else if (((w >> 24) & 0x1F) == 0x10) {
		// Address generation relative to PC
		int op = w >> 31;
		int imm = (SignExtend19(w >> 5) << 2) | ((w >> 29) & 3);
		if (op & 1) imm <<= 12;
		u64 daddr = addr + imm;
		snprintf(instr->text, sizeof(instr->text), "%s x%d, #0x%04x%08x", op ? "adrp" : "adr", Rd, (u32)(daddr >> 32), (u32)(daddr & 0xFFFFFFFF));
	} else if (((w >> 24) & 0x1F) == 0x11) {
		// Add/subtract immediate value
		int op = (w >> 30) & 1;
		int imm = ((w >> 10) & 0xFFF);
		int shift = ((w >> 22) & 0x1) * 12;
		int s = ((w >> 29) & 1);
		imm <<= shift;
		if (s && Rd == 31) {
			snprintf(instr->text, sizeof(instr->text), "cmp %c%d, #%d", r, Rn, imm);
		} else if (!shift && Rn == 31 && imm == 0) {
			snprintf(instr->text, sizeof(instr->text), "mov %c%d, sp", r, Rd);
		} else if (!shift && Rd == 31 && imm == 0) {
			snprintf(instr->text, sizeof(instr->text), "mov sp, %c%d", r, Rn);
		} else {
			snprintf(instr->text, sizeof(instr->text), "%s%s %c%d, %c%d, #%d", op == 0 ? "add" : "sub", s ? "s" : "", r, Rd, r, Rn, imm);
		}
	} else if (((w >> 23) & 0x3f) == 0x24) {
		int immr = (w >> 16) & 0x3f;
		int imms = (w >> 10) & 0x3f;
		int N = (w >> 22) & 1;
		int opc = (w >> 29) & 3;
		const char *opname[4] = { "and", "orr", "eor", "ands" };
		uint64_t wmask;
		DecodeBitMasks(N, imms, immr, NULL, &wmask);
		if (((w >> 31) & 1) && wmask & 0xFFFFFFFF00000000ULL)
			snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, #0x%x%08x", opname[opc], r, Rd, r, Rn, (uint32_t)(wmask >> 32), (uint32_t)(wmask & 0xFFFFFFFF));
		else
			snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, #0x%x", opname[opc], r, Rd, r, Rn, (uint32_t)wmask);
	} else if (((w >> 23) & 0x3f) == 0x26) {
		int N = (w >> 22) & 1;
		int opc = (w >> 29) & 3;
		int immr = (w >> 16) & 0x3f;
		int imms = (w >> 10) & 0x3f;
		const char *opname[4] = { "sbfm", "bfm", "ubfm" };
		snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, #%d, #%d", opname[opc], r, Rd, r, Rn, immr, imms);
	} else {
		snprintf(instr->text, sizeof(instr->text), "(DPI %08x)", w);
	}
}

static const char *GetSystemRegName(int o0, int op1, int CRn, int CRm, int op2) {
	if (o0 == 3 && op1 == 3 && CRn == 4 && CRm == 2 && op2 == 0) {
		return "nzcv";
	} else if (o0 == 3 && op1 == 3 && CRn == 4 && CRm == 4 && op2 == 0) {
		return "fpsr";
	} else {
		return "(unknown)";
	}
}

static void BranchExceptionAndSystem(uint32_t w, uint64_t addr, Instruction *instr, SymbolCallback symbolCallback) {
	char buffer[128];
	int Rt = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	if (((w >> 26) & 0x1F) == 5) {
		// Unconditional branch / branch+link
		int offset = SignExtend26(w) << 2;
		uint64_t target = addr + offset;
		if (symbolCallback && symbolCallback(buffer, sizeof(buffer), (uint8_t *)(uintptr_t)target)) {
			snprintf(instr->text, sizeof(instr->text), "b%s %s", (w >> 31) ? "l" : "", buffer);
		} else {
			snprintf(instr->text, sizeof(instr->text), "b%s %04x%08x", (w >> 31) ? "l" : "", (uint32_t)(target >> 32), (uint32_t)(target & 0xFFFFFFFF));
		}
	} else if (((w >> 25) & 0x3F) == 0x1A) {
		// Compare and branch
		int op = (w >> 24) & 1;
		const char *opname[2] = { "cbz", "cbnz" };
		char r = ((w >> 31) & 1) ? 'x' : 'w';
		int offset = SignExtend19(w >> 5);
		snprintf(instr->text, sizeof(instr->text), "%s %c%d", opname[op], r, Rt);
	} else if (((w >> 25) & 0x3F) == 0x1B) {
		// Test and branch
		snprintf(instr->text, sizeof(instr->text), "(test & branch %08x)", w);
	} else if (((w >> 25) & 0x7F) == 0x2A) {
		// Conditional branch
		int offset = SignExtend19(w >> 5) << 2;
		uint64_t target = addr + offset;
		int cond = w & 0xF;
		snprintf(instr->text, sizeof(instr->text), "b.%s %04x%08x", condnames[cond], (uint32_t)(target >> 32), (uint32_t)(target & 0xFFFFFFFF));
	} else if ((w >> 24) == 0xD4) {
		if (((w >> 21) & 0x7) == 1 && Rt == 0) {
			int imm = (w >> 5) & 0xFFFF;
			snprintf(instr->text, sizeof(instr->text), "brk #%d", imm);
		} else {
			snprintf(instr->text, sizeof(instr->text), "(exception-gen %08x)", w);
		}
	} else if (((w >> 20) & 0xFFC) == 0xD50) {
		bool L = (w >> 21) & 1;  // read
		// Could check them all at once, but feels better to do it like the manual says.
		int o0 = (w >> 19) & 3;
		int op1 = (w >> 16) & 7;
		int CRn = (w >> 12) & 0xF;
		int CRm = (w >> 8) & 0xf;
		int op2 = (w >> 5) & 0x7;
		const char *sysreg = GetSystemRegName(o0, op1, CRn, CRm, op2);
		if (L) {
			snprintf(instr->text, sizeof(instr->text), "mrs x%d, %s", Rt, sysreg);
		} else {
			snprintf(instr->text, sizeof(instr->text), "msr %s, x%d", sysreg, Rt);
		}

	} else if (((w >> 25) & 0x7F) == 0x6B) {
		int op = (w >> 21) & 3;
		const char *opname[4] = { "b", "bl", "ret", "(unk)" };
		snprintf(instr->text, sizeof(instr->text), "%s x%d", opname[op], Rn);
	} else {
		snprintf(instr->text, sizeof(instr->text), "(BRX ?? %08x)", w);
	}
}

void Arm64AnalyzeLoadStore(uint64_t addr, uint32_t w, Arm64LSInstructionInfo *info) {
	*info = {};
	info->instructionSize = 4;
	int id = (w >> 25) & 0xF;
	switch (id) {
	case 4: case 6: case 0xC: case 0xE:
		info->isLoadOrStore = true;
		break;
	default:
		ERROR_LOG(CPU, "Tried to disassemble %08x at %p as a load/store instruction", w, (void *)addr);
		return;  // not the expected instruction
	}

	info->size = w >> 30;
	info->Rt = (w & 0x1F);
	info->Rn = ((w >> 5) & 0x1F);
	info->Rm = ((w >> 16) & 0x1F);
	int opc = (w >> 22) & 0x3;
	if (opc == 0 || opc == 2) {
		info->isMemoryWrite = true;
	}

	if (((w >> 27) & 7) == 7) {
		int V = (w >> 26) & 1;
		if (V == 0) {
			info->isIntegerLoadStore = true;
		} else {
			info->isFPLoadStore = true;
		}
	} else {
		info->isPairLoadStore = true;
		// TODO
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
	const char *opname[4] = { "str", "ldr", "str", "ldr" };
	const char *sizeSuffix[4] = { "b", "h", "", "" };

	if (((w >> 27) & 7) == 7) {
		int V = (w >> 26) & 1;
		bool index_unsigned = ((w >> 24) & 3) == 1;
		bool index_post = !index_unsigned && ((w >> 10) & 3) == 1;
		bool index_pre = !index_unsigned && ((w >> 10) & 3) == 3;
		if (V == 0) {
			const char *signExt = ((opc & 0x2) && size < 3) ? "s" : "";
			int imm12 = SignExtend12((w >> 10) & 0xFFF) << size;
			// Integer type
			if (index_unsigned) {
				snprintf(instr->text, sizeof(instr->text), "%s%s%s %c%d, [x%d, #%d]", opname[opc], signExt, sizeSuffix[size], r, Rt, Rn, imm12);
				return;
			} else if (index_post) {
				snprintf(instr->text, sizeof(instr->text), "%s%s%s %c%d, [x%d], #%d", opname[opc], signExt, sizeSuffix[size], r, Rt, Rn, SignExtend9(imm9));
				return;
			} else if (index_pre) {
				snprintf(instr->text, sizeof(instr->text), "%s%s%s %c%d, [x%d, #%d]!", opname[opc], signExt, sizeSuffix[size], r, Rt, Rn, SignExtend9(imm9));
				return;
			} else {
				// register offset
				int S = (w >> 12) & 1;
				char index_w = (option & 3) == 2 ? 'w' : 'x';
				// TODO: Needs index support
				snprintf(instr->text, sizeof(instr->text), "%s%s %c%d, [x%d + %c%d]", opname[opc], sizeSuffix[size], r, Rt, Rn, index_w, Rm);
				return;
			}
		} else {
			// FP/Vector type
			if ((opc & 2) && size == 0) {
				size = 4;
			}
			char vr = "bhsdq"[size];
			int imm12 = SignExtend12((w >> 10) & 0xFFF) << size;
			if (index_unsigned) {
				snprintf(instr->text, sizeof(instr->text), "%s %c%d, [x%d, #%d]", opname[opc], vr, Rt, Rn, imm12);
				return;
			} else if (index_post) {
				snprintf(instr->text, sizeof(instr->text), "%s %c%d, [x%d], #%d", opname[opc], vr, Rt, Rn, SignExtend9(imm9));
				return;
			} else if (index_pre) {
				snprintf(instr->text, sizeof(instr->text), "%s %c%d, [x%d, #%d]!", opname[opc], vr, Rt, Rn, SignExtend9(imm9));
				return;
			} else {
				snprintf(instr->text, sizeof(instr->text), "(loadstore-fp-vector %08x)", w);
				return;
			}
		}
	} else if (((w >> 25) & 0x1D) == 0x14) {
		// load/store pair
		int Rt2 = (w >> 10) & 0x1f;
		bool load = (w >> 22) & 1;
		int index_type = ((w >> 23) & 3);
		bool sf = (w >> 31) != 0;
		bool V = (w >> 26) & 1;
		int op = (w >> 30);

		int offset = SignExtend7((w >> 15) & 0x7f);
		if (V) {
			offset <<= 2;
			switch (op) {
			case 0:
				r = 's';
				break;
			case 1:
				r = 'd';
				if (index_type == 2)
					offset <<= 1;
				break;
			case 2:
				r = 'q';
				if (index_type == 2)
					offset <<= 2;
			}
		} else {
			r = sf ? 'x' : 'w';
			offset <<= (sf ? 3 : 2);
		}
		if (index_type == 2) {
			snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, [x%d, #%d]", load ? "ldp" : "stp", r, Rt, r, Rt2, Rn, offset);
			return;
		} else if (index_type == 1) {
			snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, [x%d], #%d", load ? "ldp" : "stp", r, Rt, r, Rt2, Rn, offset);
			return;
		} else if (index_type == 3) {
			snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, [x%d, #%d]!", load ? "ldp" : "stp", r, Rt, r, Rt2, Rn, offset);
			return;
		} else if (index_type == 0) {
			// LDNP/STNP (ldp/stp with non-temporal hint). Automatically signed offset.
			snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, [x%d, #%d]", load ? "ldnp" : "stnp", r, Rt, r, Rt2, Rn, offset);
			return;
		}
	}
	snprintf(instr->text, sizeof(instr->text), "(LS %08x)", w);
}

static void DataProcessingRegister(uint32_t w, uint64_t addr, Instruction *instr) {
	int Rd = w & 0x1F;
	int Rn = (w >> 5) & 0x1F;
	int Rm = (w >> 16) & 0x1F;
	char r = ((w >> 31) & 1) ? 'x' : 'w';

	if (((w >> 21) & 0x2FF) == 0x2D6) {
		// Data processing
		int opcode2 = (w >> 16) & 0x1F;
		int opcode = (w >> 10) & 0x3F;
		// Data-processing (1 source)
		const char *opname[8] = { "rbit", "rev16", "rev32", "(unk)", "clz", "cls" };
		const char *op = opcode2 >= 8 ? "unk" : opname[opcode];
		snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d", op, r, Rd, r, Rn);
	} else if (((w >> 21) & 0x2FF) == 0x0D6) {
		const char *opname[32] = {
			0, 0, "udiv", "sdiv", 0, 0, 0, 0,
			"lslv", "lsrv", "asrv", "rorv", 0, 0, 0, 0,
			"crc32b", "crc32h", "crc32w", 0, "crc32cb", "crc32ch", "crc32cw", 0,
		};
		int opcode = (w >> 10) & 0x3F;
		// Data processing (2 source)
		snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, %c%d", opname[opcode], r, Rd, r, Rn, r, Rm);
	} else if (((w >> 24) & 0x1f) == 0xA) {
		// Logical (shifted register)
		int shift = (w >> 22) & 0x3;
		int imm6 = (w >> 10) & 0x3f;
		int N = (w >> 21) & 1;
		int opc = (((w >> 29) & 3) << 1) | N;
		const char *opnames[8] = { "and", "bic", "orr", "orn", "eor", "eon", "ands", "bics" };
		if (opc == 2 && Rn == 31) {
			// Special case for MOV (which is constructed from an ORR)
			if (imm6 != 0) {
				snprintf(instr->text, sizeof(instr->text), "mov %c%d, %c%d, %s #%d", r, Rd, r, Rm, shiftnames[shift], imm6);
			} else {
				snprintf(instr->text, sizeof(instr->text), "mov %c%d, %c%d", r, Rd, r, Rm);
			}
		} else if (imm6 == 0 && shift == 0) {
			snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, %c%d", opnames[opc], r, Rd, r, Rn, r, Rm);
		} else {
			snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, %c%d, %s #%d", opnames[opc], r, Rd, r, Rn, r, Rm, shiftnames[shift], imm6);
		}
	} else if (((w >> 21) & 0xf9) == 0x58) {
		// Add/sub/cmp (shifted register)
		bool S = (w >> 29) & 1;
		int shift = (w >> 22) & 0x3;
		int imm6 = (w >> 10) & 0x3f;
		int opc = ((w >> 29) & 3);
		const char *opnames[8] = { "add", "adds", "sub", "subs"};
		if (imm6 == 0 && shift == 0) {
			if (Rd == 31 && opc == 3) {
				// It's a CMP
				snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d", "cmp", r, Rn, r, Rm);
			} else if (Rn == 31 && opc == 2) {
				// It's a NEG
				snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d", "neg", r, Rd, r, Rm);
			} else {
				snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, %c%d", opnames[opc], r, Rd, r, Rn, r, Rm);
			}
		} else {
			snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, %c%d, %s #%d", opnames[opc], r, Rd, r, Rn, r, Rm, shiftnames[shift], imm6);
		}
	} else if (((w >> 21) & 0xFF) == 0x59) {
		// Add/sub (extended register)
		bool S = (w >> 29) & 1;
		bool sub = (w >> 30) & 1;
		int option = (w >> 13) & 0x7;
		int imm3 = (w >> 10) & 0x7;
		if (Rd == 31 && sub && S) {
			// It's a CMP
			snprintf(instr->text, sizeof(instr->text), "%s%s %c%d, %c%d, %s", "cmp", S ? "s" : "", r, Rn, r, Rm, extendnames[option]);
		} else {
			snprintf(instr->text, sizeof(instr->text), "%s%s %c%d, %c%d, %c%d, %s", sub ? "sub" : "add", S ? "s" : "", r, Rd, r, Rn, r, Rm, extendnames[option]);
		}
	} else if (((w >> 21) & 0xFF) == 0xD6 && ((w >> 12) & 0xF) == 2) {
		// Variable shifts
		int opc = (w >> 10) & 3;
		snprintf(instr->text, sizeof(instr->text), "%sv %c%d, %c%d, %c%d", shiftnames[opc], r, Rd, r, Rn, r, Rm);
	} else if (((w >> 21) & 0xFF) == 0xD4) {
		// Conditional select
		int op = (w >> 30) & 1;
		int op2 = (w >> 10) & 3;
		int cond = (w >> 12) & 0xf;
		const char *opnames[4] = { "csel", "csinc", "csinv", "csneg" };
		snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, %c%d, %s", opnames[(op << 1) | op2], r, Rd, r, Rn, r, Rm, condnames[cond]);
	} else if (((w >> 24) & 0x1f) == 0x1b) {
		// Data processing - 3 source
		int op31 = (w >> 21) & 0x7;
		int o0 = (w >> 15) & 1;
		int Ra = (w >> 10) & 0x1f;
		const char *opnames[8] = { 0, 0, "maddl", "msubl", "smulh", 0, 0, 0 };

		if (op31 == 0) {
			// madd/msub supports both 32-bit and 64-bit modes
			snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, %c%d, %c%d", o0 ? "msub" : "madd", r, Rd, r, Rn, r, Rm, r, Ra);
		} else {
			// The rest are 64-bit accumulator, 32-bit operands
			char sign = (op31 >> 2) ? 'u' : 's';
			int opn = (op31 & 0x3) << 1 | o0;
			if (opn < 4 && Ra == 31) {
				snprintf(instr->text, sizeof(instr->text), "%cmull x%d, w%d, w%d", sign, Rd, Rn, Rm);
			} else {
				snprintf(instr->text, sizeof(instr->text), "%c%s x%d, w%d, w%d, x%d", sign, opnames[opn], Rd, Rn, Rm, Ra);
			}
		}
	} else {
		// Logical (extended register)
		snprintf(instr->text, sizeof(instr->text), "(DPR %08x)", w);
	}
}

inline bool GetQ(uint32_t w) { return (w >> 30) & 1; }
inline bool GetU(uint32_t w) { return (w >> 29) & 1; }
const char *GetArrangement(bool Q, bool sz) {
	if (Q == 0 && sz == 0) return "2s";
	else if (Q == 1 && sz == 0) return "4s";
	else if (Q == 1 && sz == 1) return "2d";
	else return "ERROR";
}

// (w >> 25) & 0xF  == 7
static void FPandASIMD1(uint32_t w, uint64_t addr, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	int Rm = (w >> 16) & 0x1f;
	if (((w >> 21) & 0x4F9) == 0x71) {
		switch ((w >> 10) & 3) {
		case 1: case 3: {
			int opcode = (w >> 11) & 0x1f;
			int sz = (w >> 22) & 3;
			int Q = GetQ(w);
			int U = GetU(w);
			const char *opnames000[32] = {
				"shadd", "sqadd", "srhadd", 0,
				"shsub", "sqsub", "cmgt", "cmge",
				"sshl", "sqshl", "srshl", "sqrshl",
				"smax", "smin", "sabd", "saba",
				"add", "cmtst", "mla", "mul",
				"smaxp", "sminp", "sqdmulh", "addp",
				"fmaxnm", "fmla", "fadd", "fmulx",
				"fcmeq", 0, "fmax", "frecps",
			};
			const char *opnames100[32] = {
				"uhadd", "uqadd", "urhadd", 0,
				"uhsub", "uqsub", "cmhi", "cmhs",
				"ushl", "uqshl", "urshl", "uqrshl",
				"umax", "umin", "uabd", "uaba",
				"sub", "cmeq", "mls", "pmul",
				"umaxp", "uminp", "sqrdmulh", "addp",
				"fmaxnmp", 0, "faddp", "fmul",
				"fcmge", "facge", "fmaxp", "fdiv",
			};
			const char *opnames010[8] = { 
				"fminm", "fmls", "fsub", 0,
				0, 0, "fmin", "frsqrts",
			};
			const char *opnames110[8] = {
				"fminnmp", 0, "fabd", 0,
				"fcmgt", "facgt", "fminp", 0,
			};
			char r = Q ? 'q' : 'd';
			const char *opname = nullptr;
			bool fp = false;
			bool nosize = false;
			if (U == 0) {
				if (opcode < 0x18) {
					opname = opnames000[opcode];
				} else if ((sz & 0x2) == 0) {
					opname = opnames000[opcode];
					fp = true;
				} else if ((sz & 0x2) == 2) {
					opname = opnames010[opcode - 0x18];
					fp = true;
				}
				if (!opname && opcode == 3 && (sz & 2) == 0) {
					opname = !(sz & 1) ? "and" : "bic";
					nosize = true;
				} else if (!opname && opcode == 3 && (sz & 2) == 2) {
					opname = !(sz & 1) ? "orr" : "orn";
					nosize = true;
				}
			} else if (U == 1) {
				if (opcode < 0x18) {
					opname = opnames100[opcode];
				} else if ((sz & 0x2) == 0) {
					opname = opnames100[opcode];
					fp = true;
				} else if ((sz & 0x2) == 2) {
					opname = opnames110[opcode - 0x18];
					fp = true;
				}
				if (!opname && opcode == 3 && (sz & 2) == 0) {
					opname = !(sz & 1) ? "eor" : "bsl";
					if (!strcmp(opname, "eor"))
						nosize = true;
				} else if (!opname && opcode == 3 && (sz & 2) == 2) {
					opname = !(sz & 1) ? "bit" : "bif";
				}
			}
			int size = (fp ? ((sz & 1) ? 64 : 32) : (8 << sz));

			if (opname != nullptr) {
				if (!nosize) {
					snprintf(instr->text, sizeof(instr->text), "%s.%d %c%d, %c%d, %c%d", opname, size, r, Rd, r, Rn, r, Rm);
				} else {
					if (!strcmp(opname, "orr") && Rn == Rm) {
						snprintf(instr->text, sizeof(instr->text), "mov %c%d, %c%d", r, Rd, r, Rn);
					} else {
						snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, %c%d", opname, r, Rd, r, Rn, r, Rm);
					}
				}
			} else {
				snprintf(instr->text, sizeof(instr->text), "(asimd three-same %08x)", w);
			}
			break;
		}
		case 0:
			snprintf(instr->text, sizeof(instr->text), "(asimd three-different %08x)", w);
			break;
		case 2:
			if (((w >> 17) & 0xf) == 0) {
				int opcode = (w >> 12) & 0x1F;
				int sz = (w >> 22) & 3;
				int Q = GetQ(w);
				int U = GetU(w);
				const char *opname = nullptr;
				bool narrow = false;
				if (!U) {
					switch (opcode) {
					case 0: opname = "rev64"; break;
					case 1: opname = "rev16"; break;
					case 2: opname = "saddlp"; break;
					case 3: opname = "suqadd"; break;
					case 4: opname = "cls"; break;
					case 5: opname = "cnt"; break;
					case 6: opname = "sadalp"; break;
					case 7: opname = "sqabs"; break;
					case 8: opname = "cmgt"; break;
					case 9: opname = "cmeq"; break;
					case 0xA: opname = "cmlt"; break;
					case 0xB: opname = "abs"; break;
					case 0x12: opname = "xtn"; narrow = true; break;
					case 0x14: opname = "sqxtn"; narrow = true; break;
					default:
						if (!(sz & 0x2)) {
							switch (opcode) {
							case 0x16: opname = "fcvtn"; break;
							case 0x17: opname = "fcvtl"; break;
							case 0x18: opname = "frintn"; break;
							case 0x19: opname = "frintm"; break;
							case 0x1a: opname = "fcvtns"; break;
							case 0x1b: opname = "fcvtms"; break;
							case 0x1c: opname = "fcvtas"; break;
							case 0x1d: opname = "scvtf"; break;
							}
						} else {
							switch (opcode) {
							case 0xc: opname = "fcmgt"; break;
							case 0xd: opname = "fcmeq"; break;
							case 0xe: opname = "fcmlt"; break;
							case 0xf: opname = "fabs"; break;
							case 0x18: opname = "frintp"; break;
							case 0x19: opname = "frintz"; break;
							case 0x1a: opname = "fcvtps"; break;
							case 0x1b: opname = "fcvtzs"; break;
							case 0x1c: opname = "urepce"; break;
							case 0x1d: opname = "frepce"; break;
							}
						}
					}
				} else {
					switch (opcode) {
					case 0: opname = "rev32"; break;
					case 2: opname = "uaddlp"; break;
					case 3: opname = "usqadd"; break;
					case 4: opname = "clz"; break;
					case 6: opname = "uadalp"; break;
					case 7: opname = "sqneg"; break;
					case 8: opname = "cmge"; break; // with zero
					case 9: opname = "cmle"; break; // with zero
					case 0xB: opname = "neg"; break;
					case 0x12: opname = "sqxtun"; narrow = true; break;
					case 0x13: opname = "shll"; break;
					case 0x14: opname = "uqxtn"; narrow = true; break;
					case 5: if (sz == 0) opname = "not"; else opname = "rbit"; break;
					default:
						if (!(sz & 0x2)) {
							switch (opcode) {
							case 0x16: opname = "fcvtxn"; break;
							case 0x18: opname = "frinta"; break;
							case 0x19: opname = "frintx"; break;
							case 0x1a: opname = "fcvtnu"; break;
							case 0x1b: opname = "fcvtmu"; break;
							case 0x1c: opname = "fcvtau"; break;
							case 0x1d: opname = "ucvtf"; break;
							}
						} else {
							switch (opcode) {
							case 0xC: opname = "fcmge"; break;  // with zero
							case 0xD: opname = "fcmge"; break;  // with zero
							case 0xF: opname = "fneg"; break;
							case 0x19: opname = "frinti"; break;
							case 0x1a: opname = "fcvtpu"; break;
							case 0x1b: opname = "fcvtzu"; break;
							case 0x1c: opname = "ursqrte"; break;
							case 0x1d: opname = "frsqrte"; break;
							case 0x1f: opname = "fsqrt"; break;
							}
						}
					}
				}

				if (opname) {
					if (narrow) {
						int esize = 8 << sz;
						const char *two = "";  // todo
						snprintf(instr->text, sizeof(instr->text), "%s%s.%d.%d d%d, q%d", opname, two, esize, esize * 2, Rd, Rn);
					} else {
						snprintf(instr->text, sizeof(instr->text), "%s", opname);
					}
				} else {
					// Very similar to scalar two-reg misc. can we share code?
					snprintf(instr->text, sizeof(instr->text), "(asimd vector two-reg misc %08x)", w);
				}
			} else if (((w >> 17) & 0xf) == 1) {
				snprintf(instr->text, sizeof(instr->text), "(asimd across lanes %08x)", w);
			} else {
				goto bail;
			}
		}
	} else if (((w >> 21) & 0x4F9) == 0x70) {
		if (((w >> 10) & 0x21) == 1) {
			if (((w >> 11) & 3) == 3) {
				// From GPR
				snprintf(instr->text, sizeof(instr->text), "(asimd copy gpr %08x)", w);
			} else {
				int imm5 = (w >> 16) & 0x1F;
				int size = LowestSetBit(imm5, 5);
				int imm4 = (w >> 11) & 0xF;
				int dst_index = imm5 >> (size + 1);
				int src_index = imm4 >> size;
				int op = (w >> 29) & 1;
				char s = "bhsd"[size];
				if (op == 0 && imm4 == 0) {
					// DUP (element)
					int idxdsize = (imm5 & 8) ? 128 : 64;
					char r = "dq"[idxdsize == 128];
					snprintf(instr->text, sizeof(instr->text), "dup %c%d, %c%d.%c[%d]", r, Rd, r, Rn, s, dst_index);
				} else {
					int idxdsize = (imm4 & 8) ? 128 : 64;
					char r = "dq"[idxdsize == 128];
					snprintf(instr->text, sizeof(instr->text), "ins %c%d.%c[%d], %c%d.%c[%d]", r, Rd, s, dst_index, r, Rn, s, src_index);
				}
			}
		}
	} else if (((w >> 21) & 0x4F8) == 0x78) {
		if ((w >> 10) & 1) {
			if (((w >> 19) & 0xf) == 0) {
				snprintf(instr->text, sizeof(instr->text), "(asimd modified immediate %08x)", w);
			} else {
				bool Q = GetQ(w);
				bool U = GetU(w);
				int immh = (w >> 19) & 0xf;
				int immb = (w >> 16) & 7;
				int opcode = (w >> 11) & 0x1f;
				const char *opnamesU0[32] = {
					"sshr", 0, "ssra", 0,
					"srshr", 0, "srsra", 0,
					0, 0, "shl", 0,
					0, 0, "sqshl", 0,
					"shrn", "rshrn", "sqshrn", "sqrshrn",
					"sshll", 0, 0, 0,
					0, 0, 0, 0,
					"scvtf", 0, 0, "fcvtzs",
				};
				const char *opnamesU1[32] = {
					"ushr", 0, "usra", 0,
					"urshr", 0, "ursra", 0,
					"sri", 0, "sli", 0,
					"sqslu", 0, "uqshl", 0,
					"sqshrun", "sqrshrun", "uqshrn", "uqrshrn",
					"ushll", 0, 0, 0,
					0, 0, 0, 0,
					"ucvtf", 0, 0, "fcvtzu",
				};
				const char *opname = U ? opnamesU1[opcode] : opnamesU0[opcode];
				const char *two = Q ? "2" : "";  // TODO: This doesn't apply to all the ops
				if (opname && (!strcmp(opname, "scvtf") || !strcmp(opname, "ucvtf"))) {
					int esize = (8 << HighestSetBit(immh));
					int shift = 2 * esize - ((immh << 3) | immb);
					int r = Q ? 'q' : 'd';
					snprintf(instr->text, sizeof(instr->text), "%ccvtf %c%d.s, %c%d.s, #%d", U ? 'u' : 's', r, Rd, r, Rn, shift);
				} else if (opname && (!strcmp(opname, "ushr") || !strcmp(opname, "sshr"))) {
					int esize = (8 << HighestSetBit(immh));
					int shift = esize * 2 - ((immh << 3) | immb);
					int r = Q ? 'q' : 'd';
					snprintf(instr->text, sizeof(instr->text), "%s.%d %c%d, %c%d, #%d", opname, esize, r, Rd, r, Rn, shift);
				} else if (opname && (!strcmp(opname, "rshrn") || !strcmp(opname, "shrn"))) {
					int esize = (8 << HighestSetBit(immh));
					int shift = esize * 2 - ((immh << 3) | immb);
					snprintf(instr->text, sizeof(instr->text), "%s%s.%d.%d d%d, q%d, #%d", opname, two, esize, esize * 2, Rd, Rn, shift);
				} else if (opname && (!strcmp(opname, "shl"))) {
					int esize = (8 << HighestSetBit(immh));
					int r = Q ? 'q' : 'd';
					int shift = ((immh << 3) | immb) - esize;
					snprintf(instr->text, sizeof(instr->text), "%s.%d %c%d, %c%d, #%d", opname, esize, r, Rd, r, Rn, shift);
				} else if (opname) {
					int esize = (8 << HighestSetBit(immh));
					int shift = ((immh << 3) | immb) - esize;
					if (shift == 0 && opcode == 0x14) {
						snprintf(instr->text, sizeof(instr->text), "%cxtl%s.%d.%d q%d, d%d", U ? 'u' : 's', two, esize * 2, esize, Rd, Rn);
					} else {
						snprintf(instr->text, sizeof(instr->text), "%s%s.%d.%d q%d, d%d, #%d", opname, two, esize * 2, esize, Rd, Rn, shift);
					}
				} else {
					snprintf(instr->text, sizeof(instr->text), "(asimd shift-by-immediate %08x)", w);
				}
			}
		} else {
			bool Q = GetQ(w);
			bool U = GetU(w);
			int size = (w >> 22) & 3;
			bool L = (w >> 21) & 1;
			bool M = (w >> 20) & 1;
			bool H = (w >> 11) & 1;
			int opcode = (w >> 12) & 0xf;
			if (size & 0x2) {
				const char *opname = 0;
				switch (opcode) {
				case 1: opname = "fmla"; break;
				case 5: opname = "fmls"; break;
				case 9: opname = "fmul"; break;
				}
				int index;
				if ((size & 1) == 0) {
					index = (H << 1) | (int)L;
				} else {
					index = H;
				}
				char r = Q ? 'q' : 'd';
				const char *arrangement = GetArrangement(Q, size & 1);
				snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, %c%d.%s[%d]", opname, r, Rd, r, Rn, r, Rm, arrangement, index);
			} else {
				snprintf(instr->text, sizeof(instr->text), "(asimd vector x indexed elem %08x)", w);
			}
		}
	} else {
		bail:
		snprintf(instr->text, sizeof(instr->text), "(FP1 %08x)", w);
	}
}

// (w >> 25) & 0xF  == f
static void FPandASIMD2(uint32_t w, uint64_t addr, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	int Rm = (w >> 16) & 0x1f;
	int type = (w >> 22) & 0x3;
	int sf = (w >> 31);
	if (((w >> 21) & 0x2F9) == 0xF0) {
		int rmode = (w >> 19) & 3;
		int opcode = (w >> 16) & 7;
		int scale = 64 - ((w >> 10) & 0x3f);
		char fr = type == 1 ? 'd' : 's';
		char ir = sf == 1 ? 'x' : 'w';
		char sign = (opcode & 1) ? 'u' : 's';
		if (rmode == 0) {
			snprintf(instr->text, sizeof(instr->text), "%ccvtf %c%d, %c%d, #%d", sign, fr, Rd, ir, Rn, scale);
		} else if (rmode == 3) {
			snprintf(instr->text, sizeof(instr->text), "fcvtz%c %c%d, %c%d, #%d", sign, ir, Rd, fr, Rn, scale);
		} else {
			snprintf(instr->text, sizeof(instr->text), "(float<->fixed %08x)", w);
		}
	} else if (((w >> 21) & 0x2F9) == 0xF1) {
		int opcode = (w >> 16) & 7;
		if (((w >> 10) & 3) == 0) {
			if (((w >> 10) & 7) == 4) {
				uint8_t uimm8 = (w >> 13) & 0xff;
				float fl_imm = Arm64Gen::FPImm8ToFloat(uimm8);
				char fr = ((w >> 22) & 1) ? 'd' : 's';
				snprintf(instr->text, sizeof(instr->text), "fmov %c%d, #%f", fr, Rd, fl_imm);
			} else if (((w >> 10) & 0xf) == 8) {
				int opcode2 = w & 0x1f;
				int e = opcode2 >> 4;
				int z = (opcode2 >> 3) & 1;
				char r = type == 1 ? 'd' : 's';
				if (z) {
					snprintf(instr->text, sizeof(instr->text), "fcmp %c%d, #0.0", r, Rn);
				} else {
					snprintf(instr->text, sizeof(instr->text), "fcmp %c%d, %c%d", r, Rn, r, Rm);
				}
			} else if (((w >> 10) & 0x1f) == 0x10) {
				// snprintf(instr->text, sizeof(instr->text), "(data 1-source %08x)", w);
				const char *opnames[4] = { "fmov", "fabs", "fneg", "fsqrt" };
				int opc = (w >> 15) & 0x3;
				snprintf(instr->text, sizeof(instr->text), "%s s%d, s%d", opnames[opc], Rd, Rn);  // TODO: Support doubles too
			} else if (((w >> 10) & 0x1bf) == 0x180) {
				// Generalized FMOV
				char ir = sf ? 'x' : 'w';
				bool tosimd = (opcode & 0x1);
				char fr = ((w >> 22) & 1) ? 'd' : 's';
				if (tosimd) {
					snprintf(instr->text, sizeof(instr->text), "fmov %c%d, %c%d", fr, Rd, ir, Rn);
				} else {
					snprintf(instr->text, sizeof(instr->text), "fmov %c%d, %c%d", ir, Rd, fr, Rn);
				}
			} else if (((w >> 10) & 0x3f) == 0x0 && opcode == 0) {
				char ir = sf ? 'x' : 'w';
				char roundmode = "npmz"[(w >> 19) & 3];
				if (opcode & 0x4)
					roundmode = 'a';
				char fr = ((w >> 22) & 1) ? 'd' : 's';
				snprintf(instr->text, sizeof(instr->text), "fcvt%cs %c%d, %c%d", roundmode, ir, Rd, fr, Rn);
			} else if ((opcode & 6) == 2) {
				char ir = sf ? 'x' : 'w';
				char fr = ((w >> 22) & 1) ? 'd' : 's';
				char sign = (opcode & 1) ? 'u' : 's';
				snprintf(instr->text, sizeof(instr->text), "%ccvtf %c%d, %c%d", sign, fr, Rd, ir, Rn);
			}
		} else if (((w >> 10) & 3) == 1) {
			snprintf(instr->text, sizeof(instr->text), "(float cond compare %08x)", w);
		} else if (((w >> 10) & 3) == 2) {
			int opc = (w >> 12) & 0xf;
			const char *opnames[9] = { "fmul", "fdiv", "fadd", "fsub", "fmax", "fmin", "fmaxnm", "fminnm", "fnmul" };
			char r = ((w >> 22) & 1) ? 'd' : 's';
			snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, %c%d", opnames[opc], r, Rd, r, Rn, r, Rm);
		} else if (((w >> 10) & 3) == 3) {
			char fr = ((w >> 22) & 1) ? 'd' : 's';
			int cond = (w >> 12) & 0xf;
			snprintf(instr->text, sizeof(instr->text), "fcsel %c%d, %c%d, %c%d, %s", fr, Rd, fr, Rn, fr, Rm, condnames[cond]);
		}
	} else if (((w >> 21) & 0x2F8) == 0xF8) {
		int opcode = ((w >> 15) & 1) | ((w >> 20) & 2);
		const char *opnames[9] = { "fmadd", "fmsub", "fnmadd", "fnmsub" };
		int size = (w >> 22) & 1;
		char r = size ? 'd' : 's';
		int Ra = (w >> 10) & 0x1f;
		snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d, %c%d, %c%d", opnames[opcode], r, Rd, r, Rn, r, Rm, r, Ra);
	} else if (((w >> 21) & 0x2F9) == 0x2F1) {
		if (((w >> 10) & 3) == 0) {
			snprintf(instr->text, sizeof(instr->text), "(asimd scalar three different %08x)", w);
		} else if (((w >> 10) & 1) == 1) {
			snprintf(instr->text, sizeof(instr->text), "(asimd scalar three same %08x)", w);
		} else if (((w >> 10) & 3) == 2) {
			// asimd scalar two-reg misc: This is a particularly sad and messy encoding :/
			if (((w >> 17) & 0xf) == 0) {
				int sz = (w >> 22) & 3;
				char r = (sz & 1) ? 'd' : 's';
				int opcode = (w >> 12) & 0x1f;
				bool U = ((w >> 29) & 1);
				const char *opname = NULL;
				bool zero = false;
				bool sign_suffix = false;
				bool sign_prefix = false;
				if ((sz & 2) == 0) {
					switch (opcode) {
					case 0x1a: opname = "fcvtn"; sign_suffix = true; break;
					case 0x1b: opname = "fcvtm"; sign_suffix = true; break;
					case 0x1c: opname = "fcvta"; sign_suffix = true; break;
					case 0x1d: opname = "cvtf"; sign_prefix = true;  break;
					}
				} else {
					if (U == 0) {
						switch (opcode) {
						case 0xC: opname = "fcmgt"; zero = true; break;
						case 0xD: opname = "fcmeq"; zero = true; break;
						case 0xE: opname = "fcmlt"; zero = true; break;
						case 0x1A: opname = "fcvtp"; sign_suffix = true;  break;
						case 0x1B: opname = "fcvtz"; sign_suffix = true;  break;
						}
					} else {
						switch (opcode) {
						case 0x1A: opname = "fcvtp"; sign_suffix = true;  break;
						case 0x1B: opname = "fcvtz"; sign_suffix = true;  break;
						}
					}
				}
				if (!opname) {  // These ignore size.
					if (U == 0) {
						switch (opcode) {
						case 3: opname = "suqadd"; break;
						case 7: opname = "sqabs"; break;
						case 8: opname = "cmgt"; zero = true;  break;
						case 9: opname = "cmeq"; zero = true;  break;
						case 0xa: opname = "cmlt"; zero = true;  break;
						case 0xb: opname = "abs"; break;
						case 0xc: opname = "sqxtn?"; break;
						}
					} else {
						switch (opcode) {
						case 3: opname = "usqadd"; break;
						case 7: opname = "sqneg"; break;
						case 8: opname = "cmge"; zero = true;  break;
						case 9: opname = "cmle"; zero = true;  break;
						case 0xB: opname = "neg"; break;
						case 0x12: opname = "sqxtun?"; break;
						case 0x14: opname = "uqxtn?"; break;
						}
					}
				}

				if (opname) {
					if (sign_suffix) {
						char sign = U ? 'u' : 's';
						snprintf(instr->text, sizeof(instr->text), "%s%c %c%d, %c%d", opname, sign, r, Rd, r, Rn);
					} else if (sign_prefix) {
						char sign = U ? 'u' : 's';
						snprintf(instr->text, sizeof(instr->text), "%c%s %c%d, %c%d", sign, opname, r, Rd, r, Rn);
					} else if (!zero) {
						snprintf(instr->text, sizeof(instr->text), "%s %c%d, %c%d", opname, r, Rd, r, Rn);
					} else if (zero) {
						snprintf(instr->text, sizeof(instr->text), "%s %c%d, #0.0", opname, r, Rd);
					}
				} else {
					snprintf(instr->text, sizeof(instr->text), "(asimd scalar two-reg misc %08x)", w);
				}
			} else if (((w >> 17) & 0xf) == 8) {
				snprintf(instr->text, sizeof(instr->text), "(asimd scalar pair-wise %08x)", w);
			} else {
				snprintf(instr->text, sizeof(instr->text), "(asimd scalar stuff %08x)", w);
			}
		} else {
			snprintf(instr->text, sizeof(instr->text), "(asimd stuff %08x)", w);
		}
	} else if (((w >> 21) & 0x2F1) == 0x2F0) {
		// many lines
		snprintf(instr->text, sizeof(instr->text), "(asimd stuff %08x)", w);
	} else {
		snprintf(instr->text, sizeof(instr->text), "(FP2 %08x)", w);
	}
}

static void DisassembleInstruction(uint32_t w, uint64_t addr, Instruction *instr, SymbolCallback symbolCallback) {
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
		BranchExceptionAndSystem(w, addr, instr, symbolCallback);
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

void Arm64Dis(uint64_t addr, uint32_t w, char *output, int bufsize, bool includeWord, SymbolCallback symbolCallback) {
	Instruction instr;
	DisassembleInstruction(w, addr, &instr, symbolCallback);
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
