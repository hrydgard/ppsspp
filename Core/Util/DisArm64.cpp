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

// Made to debug JIT code: it covers everything Common/Arm64Emitter can emit (unittest/TestArm64Disasm.cpp
// checks that), and not much beyond.
//
// Vector instructions use a compact syntax: the element size goes on the mnemonic and the register
// is q or d by width, so "fadd.32 q1, q2, q3" is "fadd v1.4s, v2.4s, v3.4s". Widening and narrowing
// ops list the destination element size first ("xtn.32.64 d0, q3").

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Common/Arm64Emitter.h"
#include "Common/StringUtils.h"
#include "Core/Util/DisArm64.h"

struct Instruction {
	char text[128];
	bool undefined;
};

static void Out(Instruction *instr, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vsnprintf(instr->text, sizeof(instr->text), fmt, args);
	va_end(args);
}

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
	"nv", // Also always
};

static int SignExtend(uint32_t value, int bits) {
	return (int)(value << (32 - bits)) >> (32 - bits);
}

static int HighestSetBit(int value) {
	int highest = 0;
	for (int i = 0; i < 32; i++) {
		if (value & (1 << i))
			highest = i;
	}
	return highest;
}

static uint64_t Ones(int len) {
	if (len >= 64) {
		return 0xFFFFFFFFFFFFFFFFULL;
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
	if (amount == 0)
		return value & Ones(esize);
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
	int levels = (int)Ones(len);
	uint32_t S = imms & levels;
	uint32_t R = immr & levels;
	int diff = S - R; // 6-bit subtract with borrow
	int esize = 1 << len;
	int d = diff & Ones(len - 1);
	uint64_t welem = Ones(S + 1);
	uint64_t telem = Ones(d + 1);
	if (wmask) {
		uint64_t rotated = ROR(welem, R, esize);
		*wmask = Replicate(rotated, esize);
	}
	if (tmask) {
		*tmask = Replicate(telem, esize);
	}
}

// General purpose register names. Register 31 is the zero register, or the stack pointer where the
// instruction says so.
static const char *GPR(bool sf, int r, bool sp = false) {
	static const struct Names {
		char n[4][32][5];
		Names() {
			for (int i = 0; i < 31; i++) {
				for (int j = 0; j < 4; j++)
					snprintf(n[j][i], sizeof(n[j][i]), "%c%d", (j & 1) ? 'x' : 'w', i);
			}
			strcpy(n[0][31], "wzr");
			strcpy(n[1][31], "xzr");
			strcpy(n[2][31], "wsp");
			strcpy(n[3][31], "sp");
		}
	} names;
	return names.n[(sp ? 2 : 0) | (sf ? 1 : 0)][r & 31];
}

static const char *X(int r) { return GPR(true, r); }
static const char *XSP(int r) { return GPR(true, r, true); }
static const char *W(int r) { return GPR(false, r); }

static void FormatTarget(char *buf, size_t size, uint64_t target, SymbolCallback symbolCallback) {
	if (!symbolCallback || !symbolCallback(buf, (int)size, (uint8_t *)(uintptr_t)target)) {
		snprintf(buf, size, "%04x%08x", (uint32_t)(target >> 32), (uint32_t)(target & 0xFFFFFFFF));
	}
}

static void DataProcessingImmediate(uint32_t w, uint64_t addr, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	bool sf = (w >> 31) & 1;
	int op = (w >> 23) & 7;
	int size = sf ? 64 : 32;
	switch (op) {
	case 0: case 1: {
		// Address generation relative to PC
		int imm = SignExtend((((w >> 5) & 0x7FFFF) << 2) | ((w >> 29) & 3), 21);
		uint64_t target;
		if (sf) {
			target = (addr & ~0xFFFULL) + ((int64_t)imm << 12);
		} else {
			target = addr + imm;
		}
		Out(instr, "%s %s, #0x%04x%08x", sf ? "adrp" : "adr", X(Rd), (uint32_t)(target >> 32), (uint32_t)(target & 0xFFFFFFFF));
		break;
	}
	case 2: {
		// Add/subtract immediate value
		bool sub = (w >> 30) & 1;
		bool S = (w >> 29) & 1;
		int imm = (w >> 10) & 0xFFF;
		bool shift = (w >> 22) & 1;
		char immstr[32];
		snprintf(immstr, sizeof(immstr), shift ? "#%d, lsl #12" : "#%d", imm);
		if (S && Rd == 31) {
			Out(instr, "%s %s, %s", sub ? "cmp" : "cmn", GPR(sf, Rn, true), immstr);
		} else if (!sub && !S && !shift && imm == 0 && (Rd == 31 || Rn == 31)) {
			Out(instr, "mov %s, %s", GPR(sf, Rd, true), GPR(sf, Rn, true));
		} else {
			Out(instr, "%s%s %s, %s, %s", sub ? "sub" : "add", S ? "s" : "", GPR(sf, Rd, !S), GPR(sf, Rn, true), immstr);
		}
		break;
	}
	case 4: {
		// Logical immediate
		int opc = (w >> 29) & 3;
		int N = (w >> 22) & 1;
		int immr = (w >> 16) & 0x3f;
		int imms = (w >> 10) & 0x3f;
		if (!sf && N) {
			Out(instr, "(DPI %08x)", w);
			break;
		}
		static const char * const opnames[4] = { "and", "orr", "eor", "ands" };
		uint64_t wmask;
		DecodeBitMasks(N, imms, immr, nullptr, &wmask);
		if (!sf)
			wmask &= 0xFFFFFFFF;
		char immstr[32];
		if (wmask >> 32)
			snprintf(immstr, sizeof(immstr), "#0x%x%08x", (uint32_t)(wmask >> 32), (uint32_t)wmask);
		else
			snprintf(immstr, sizeof(immstr), "#0x%x", (uint32_t)wmask);
		if (opc == 3 && Rd == 31) {
			Out(instr, "tst %s, %s", GPR(sf, Rn), immstr);
		} else {
			Out(instr, "%s %s, %s, %s", opnames[opc], GPR(sf, Rd, opc != 3), GPR(sf, Rn), immstr);
		}
		break;
	}
	case 5: {
		// Move wide
		int imm16 = (w >> 5) & 0xFFFF;
		int opc = (w >> 29) & 3;
		int hw = (w >> 21) & 3;
		static const char * const opnames[4] = { "movn", nullptr, "movz", "movk" };
		if (!opnames[opc] || (!sf && hw >= 2)) {
			Out(instr, "(DPI %08x)", w);
			break;
		}
		Out(instr, "%s %s, #0x%04x << %d", opnames[opc], GPR(sf, Rd), imm16, hw * 16);
		break;
	}
	case 6: {
		// Bitfield
		int opc = (w >> 29) & 3;
		int immr = (w >> 16) & 0x3f;
		int imms = (w >> 10) & 0x3f;
		const char *d = GPR(sf, Rd);
		const char *n = GPR(sf, Rn);
		if (opc == 0) {
			if (imms == size - 1) {
				Out(instr, "asr %s, %s, #%d", d, n, immr);
			} else if (immr == 0 && (imms == 7 || imms == 15 || (sf && imms == 31))) {
				Out(instr, "sxt%c %s, %s", imms == 7 ? 'b' : (imms == 15 ? 'h' : 'w'), d, W(Rn));
			} else if (imms < immr) {
				Out(instr, "sbfiz %s, %s, #%d, #%d", d, n, size - immr, imms + 1);
			} else {
				Out(instr, "sbfx %s, %s, #%d, #%d", d, n, immr, imms - immr + 1);
			}
		} else if (opc == 1) {
			if (imms < immr) {
				Out(instr, "bfi %s, %s, #%d, #%d", d, n, size - immr, imms + 1);
			} else {
				Out(instr, "bfxil %s, %s, #%d, #%d", d, n, immr, imms - immr + 1);
			}
		} else if (opc == 2) {
			if (imms != size - 1 && imms + 1 == immr) {
				Out(instr, "lsl %s, %s, #%d", d, n, size - 1 - imms);
			} else if (imms == size - 1) {
				Out(instr, "lsr %s, %s, #%d", d, n, immr);
			} else if (!sf && immr == 0 && (imms == 7 || imms == 15)) {
				Out(instr, "uxt%c %s, %s", imms == 7 ? 'b' : 'h', d, n);
			} else if (imms < immr) {
				Out(instr, "ubfiz %s, %s, #%d, #%d", d, n, size - immr, imms + 1);
			} else {
				Out(instr, "ubfx %s, %s, #%d, #%d", d, n, immr, imms - immr + 1);
			}
		} else {
			Out(instr, "(DPI %08x)", w);
		}
		break;
	}
	case 7: {
		// Extract
		int Rm = (w >> 16) & 0x1f;
		int imms = (w >> 10) & 0x3f;
		if (Rn == Rm) {
			Out(instr, "ror %s, %s, #%d", GPR(sf, Rd), GPR(sf, Rn), imms);
		} else {
			Out(instr, "extr %s, %s, %s, #%d", GPR(sf, Rd), GPR(sf, Rn), GPR(sf, Rm), imms);
		}
		break;
	}
	default:
		Out(instr, "(DPI %08x)", w);
		break;
	}
}

static const char *GetSystemRegName(int op0, int op1, int CRn, int CRm, int op2) {
	if (op0 == 3 && op1 == 3 && CRn == 4 && CRm == 2 && op2 == 0) {
		return "nzcv";
	} else if (op0 == 3 && op1 == 3 && CRn == 4 && CRm == 4 && op2 == 0) {
		return "fpcr";
	} else if (op0 == 3 && op1 == 3 && CRn == 4 && CRm == 4 && op2 == 1) {
		return "fpsr";
	} else {
		return nullptr;
	}
}

static void System(uint32_t w, Instruction *instr) {
	int Rt = w & 0x1f;
	bool L = (w >> 21) & 1;
	int op0 = (w >> 19) & 3;
	int op1 = (w >> 16) & 7;
	int CRn = (w >> 12) & 0xF;
	int CRm = (w >> 8) & 0xF;
	int op2 = (w >> 5) & 0x7;
	if (op0 >= 2) {
		const char *sysreg = GetSystemRegName(op0, op1, CRn, CRm, op2);
		char name[32];
		if (sysreg) {
			truncate_cpy(name, sysreg);
		} else {
			snprintf(name, sizeof(name), "s%d_%d_c%d_c%d_%d", op0, op1, CRn, CRm, op2);
		}
		if (L) {
			Out(instr, "mrs %s, %s", X(Rt), name);
		} else {
			Out(instr, "msr %s, %s", name, X(Rt));
		}
		return;
	}
	if (L || op0 != 0) {
		Out(instr, "(sys %08x)", w);
		return;
	}
	if (CRn == 2 && Rt == 31) {
		static const char * const hints[6] = { "nop", "yield", "wfe", "wfi", "sev", "sevl" };
		int imm = (CRm << 3) | op2;
		if (imm < 6) {
			Out(instr, "%s", hints[imm]);
		} else {
			Out(instr, "hint #%d", imm);
		}
	} else if (CRn == 3 && op1 == 3 && Rt == 31) {
		static const char * const barriers[16] = {
			nullptr, "oshld", "oshst", "osh", nullptr, "nshld", "nshst", "nsh",
			nullptr, "ishld", "ishst", "ish", nullptr, "ld", "st", "sy",
		};
		if (op2 == 2) {
			if (CRm == 15)
				Out(instr, "clrex");
			else
				Out(instr, "clrex #%d", CRm);
		} else if ((op2 == 4 || op2 == 5) && barriers[CRm]) {
			Out(instr, "%s %s", op2 == 4 ? "dsb" : "dmb", barriers[CRm]);
		} else if (op2 == 6) {
			if (CRm == 15)
				Out(instr, "isb");
			else
				Out(instr, "isb #%d", CRm);
		} else {
			Out(instr, "(barrier %08x)", w);
		}
	} else if (CRn == 4 && Rt == 31) {
		const char *field = nullptr;
		if (op1 == 0 && op2 == 5)
			field = "spsel";
		else if (op1 == 3 && op2 == 6)
			field = "daifset";
		else if (op1 == 3 && op2 == 7)
			field = "daifclr";
		if (field)
			Out(instr, "msr %s, #%d", field, CRm);
		else
			Out(instr, "(msr pstate %08x)", w);
	} else {
		Out(instr, "(sys %08x)", w);
	}
}

static void BranchExceptionAndSystem(uint32_t w, uint64_t addr, Instruction *instr, SymbolCallback symbolCallback) {
	char target[128];
	int Rt = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	if (((w >> 26) & 0x1F) == 5) {
		// Unconditional branch / branch+link
		FormatTarget(target, sizeof(target), addr + (int64_t)(SignExtend(w & 0x3FFFFFF, 26) << 2), symbolCallback);
		Out(instr, "b%s %s", (w >> 31) ? "l" : "", target);
	} else if (((w >> 25) & 0x3F) == 0x1A) {
		// Compare and branch
		FormatTarget(target, sizeof(target), addr + (int64_t)(SignExtend((w >> 5) & 0x7FFFF, 19) << 2), symbolCallback);
		Out(instr, "%s %s, %s", ((w >> 24) & 1) ? "cbnz" : "cbz", GPR(w >> 31, Rt), target);
	} else if (((w >> 25) & 0x3F) == 0x1B) {
		// Test and branch
		int bit = ((w >> 26) & 0x20) | ((w >> 19) & 0x1F);
		FormatTarget(target, sizeof(target), addr + (int64_t)(SignExtend((w >> 5) & 0x3FFF, 14) << 2), symbolCallback);
		Out(instr, "%s %s, #%d, %s", ((w >> 24) & 1) ? "tbnz" : "tbz", GPR(bit >= 32, Rt), bit, target);
	} else if (((w >> 24) & 0xFF) == 0x54 && ((w >> 4) & 1) == 0) {
		// Conditional branch
		FormatTarget(target, sizeof(target), addr + (int64_t)(SignExtend((w >> 5) & 0x7FFFF, 19) << 2), symbolCallback);
		Out(instr, "b.%s %s", condnames[w & 0xF], target);
	} else if ((w >> 24) == 0xD4) {
		// Exception generation
		int opc = (w >> 21) & 7;
		int imm = (w >> 5) & 0xFFFF;
		int ll = w & 3;
		const char *name = nullptr;
		if (opc == 0 && ll != 0) {
			static const char * const names[4] = { nullptr, "svc", "hvc", "smc" };
			name = names[ll];
		} else if (opc == 1 && ll == 0) {
			name = "brk";
		} else if (opc == 2 && ll == 0) {
			name = "hlt";
		} else if (opc == 5 && ll != 0) {
			static const char * const names[4] = { nullptr, "dcps1", "dcps2", "dcps3" };
			name = names[ll];
			if (imm == 0) {
				Out(instr, "%s", name);
				return;
			}
		}
		if (name && ((w >> 2) & 7) == 0)
			Out(instr, "%s #%d", name, imm);
		else
			Out(instr, "(exception-gen %08x)", w);
	} else if (((w >> 22) & 0x3FF) == 0x354) {
		System(w, instr);
	} else if (((w >> 25) & 0x7F) == 0x6B) {
		// Unconditional branch (register)
		int opc = (w >> 21) & 0xF;
		if (((w >> 10) & 0x7FF) != 0x7C0 || Rt != 0) {
			Out(instr, "(BRX ?? %08x)", w);
		} else if (opc == 0) {
			Out(instr, "br %s", X(Rn));
		} else if (opc == 1) {
			Out(instr, "blr %s", X(Rn));
		} else if (opc == 2) {
			if (Rn == 30)
				Out(instr, "ret");
			else
				Out(instr, "ret %s", X(Rn));
		} else if (opc == 4 && Rn == 31) {
			Out(instr, "eret");
		} else if (opc == 5 && Rn == 31) {
			Out(instr, "drps");
		} else {
			Out(instr, "(BRX ?? %08x)", w);
		}
	} else {
		Out(instr, "(BRX ?? %08x)", w);
	}
}

bool Arm64AnalyzeLoadStore(uint64_t addr, uint32_t w, Arm64LSInstructionInfo *info) {
	*info = {};
	info->instructionSize = 4;
	int id = (w >> 25) & 0xF;
	switch (id) {
	case 4: case 6: case 0xC: case 0xE:
		break;
	default:
		return false;  // not the expected instruction
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
	return true;
}

static void PrefetchName(char *buf, size_t size, int Rt) {
	static const char * const types[4] = { "pld", "pli", "pst", nullptr };
	if (types[Rt >> 3] && ((Rt >> 1) & 3) != 3) {
		snprintf(buf, size, "%sl%d%s", types[Rt >> 3], ((Rt >> 1) & 3) + 1, (Rt & 1) ? "strm" : "keep");
	} else {
		snprintf(buf, size, "#%d", Rt);
	}
}

// Loads and stores of a single register, both integer and FP/SIMD.
static void LoadStoreRegister(uint32_t w, uint64_t addr, Instruction *instr) {
	int size = w >> 30;
	bool V = (w >> 26) & 1;
	int opc = (w >> 22) & 3;
	int Rt = w & 0x1F;
	int Rn = (w >> 5) & 0x1F;
	int Rm = (w >> 16) & 0x1F;
	bool unsignedOffset = (w >> 24) & 1;
	int type = (w >> 10) & 3;
	bool regOffset = !unsignedOffset && ((w >> 21) & 1);

	if (regOffset && type != 2) {
		Out(instr, "(LS atomic %08x)", w);
		return;
	}
	if (!unsignedOffset && !regOffset && type == 2) {
		Out(instr, "(LS unprivileged %08x)", w);
		return;
	}

	// Build the mnemonic and the register.
	char name[16];
	char reg[16];
	int scale = size;
	bool unscaled = !unsignedOffset && !regOffset && type == 0;
	if (V) {
		if (opc & 2) {
			if (size != 0) {
				Out(instr, "(LS %08x)", w);
				return;
			}
			scale = 4;
		}
		snprintf(name, sizeof(name), "%s%s", (opc & 1) ? "ld" : "st", unscaled ? "ur" : "r");
		snprintf(reg, sizeof(reg), "%c%d", "bhsdq"[scale], Rt);
	} else {
		static const char * const sizeSuffix[4] = { "b", "h", "", "" };
		const char *r = unscaled ? "ur" : "r";
		if (opc == 2 && size == 3) {
			snprintf(name, sizeof(name), "prf%s", unscaled ? "um" : "m");
			PrefetchName(reg, sizeof(reg), Rt);
		} else if (opc >= 2) {
			if (size == 3 || (size == 2 && opc == 3)) {
				Out(instr, "(LS %08x)", w);
				return;
			}
			snprintf(name, sizeof(name), "ld%ss%s", r, size == 2 ? "w" : sizeSuffix[size]);
			truncate_cpy(reg, GPR(opc == 2, Rt));
		} else {
			snprintf(name, sizeof(name), "%s%s%s", opc ? "ld" : "st", r, sizeSuffix[size]);
			truncate_cpy(reg, GPR(size == 3, Rt));
		}
	}

	if (unsignedOffset) {
		int imm = ((w >> 10) & 0xFFF) << scale;
		if (imm)
			Out(instr, "%s %s, [%s, #%d]", name, reg, XSP(Rn), imm);
		else
			Out(instr, "%s %s, [%s]", name, reg, XSP(Rn));
	} else if (regOffset) {
		int option = (w >> 13) & 7;
		bool S = (w >> 12) & 1;
		if (!(option & 2)) {
			Out(instr, "(LS %08x)", w);
			return;
		}
		const char *index = GPR(option & 1, Rm);
		if (option == 3) {
			if (S)
				Out(instr, "%s %s, [%s, %s, lsl #%d]", name, reg, XSP(Rn), index, scale);
			else
				Out(instr, "%s %s, [%s, %s]", name, reg, XSP(Rn), index);
		} else {
			if (S)
				Out(instr, "%s %s, [%s, %s, %s #%d]", name, reg, XSP(Rn), index, extendnames[option], scale);
			else
				Out(instr, "%s %s, [%s, %s, %s]", name, reg, XSP(Rn), index, extendnames[option]);
		}
	} else {
		int imm9 = SignExtend((w >> 12) & 0x1FF, 9);
		if (type == 1) {
			Out(instr, "%s %s, [%s], #%d", name, reg, XSP(Rn), imm9);
		} else if (type == 3) {
			Out(instr, "%s %s, [%s, #%d]!", name, reg, XSP(Rn), imm9);
		} else if (imm9) {
			Out(instr, "%s %s, [%s, #%d]", name, reg, XSP(Rn), imm9);
		} else {
			Out(instr, "%s %s, [%s]", name, reg, XSP(Rn));
		}
	}
}

static void LoadStorePair(uint32_t w, Instruction *instr) {
	int opc = w >> 30;
	bool V = (w >> 26) & 1;
	int indexType = (w >> 23) & 3;
	bool L = (w >> 22) & 1;
	int Rt = w & 0x1F;
	int Rn = (w >> 5) & 0x1F;
	int Rt2 = (w >> 10) & 0x1F;
	int imm7 = SignExtend((w >> 15) & 0x7F, 7);

	const char *name = L ? "ldp" : "stp";
	char r;
	int scale;
	if (V) {
		if (opc == 3) {
			Out(instr, "(LS pair %08x)", w);
			return;
		}
		r = "sdq"[opc];
		scale = 2 + opc;
	} else if (opc == 0) {
		r = 'w';
		scale = 2;
	} else if (opc == 1 && L && indexType != 0) {
		name = "ldpsw";
		r = 'x';
		scale = 2;
	} else if (opc == 2) {
		r = 'x';
		scale = 3;
	} else {
		Out(instr, "(LS pair %08x)", w);
		return;
	}
	if (indexType == 0)
		name = L ? "ldnp" : "stnp";

	char t1[8], t2[8];
	if (r == 'w' || r == 'x') {
		truncate_cpy(t1, GPR(r == 'x', Rt));
		truncate_cpy(t2, GPR(r == 'x', Rt2));
	} else {
		snprintf(t1, sizeof(t1), "%c%d", r, Rt);
		snprintf(t2, sizeof(t2), "%c%d", r, Rt2);
	}
	int offset = imm7 << scale;
	if (indexType == 1) {
		Out(instr, "%s %s, %s, [%s], #%d", name, t1, t2, XSP(Rn), offset);
	} else if (indexType == 3) {
		Out(instr, "%s %s, %s, [%s, #%d]!", name, t1, t2, XSP(Rn), offset);
	} else if (offset) {
		Out(instr, "%s %s, %s, [%s, #%d]", name, t1, t2, XSP(Rn), offset);
	} else {
		Out(instr, "%s %s, %s, [%s]", name, t1, t2, XSP(Rn));
	}
}

static void LoadStoreExclusive(uint32_t w, Instruction *instr) {
	int size = w >> 30;
	bool o2 = (w >> 23) & 1;
	bool L = (w >> 22) & 1;
	bool o1 = (w >> 21) & 1;
	int Rs = (w >> 16) & 0x1F;
	bool o0 = (w >> 15) & 1;
	int Rt2 = (w >> 10) & 0x1F;
	int Rn = (w >> 5) & 0x1F;
	int Rt = w & 0x1F;

	static const char * const suffix[4] = { "b", "h", "", "" };
	bool sf = size == 3;
	if (!o2 && !o1) {
		if (L)
			Out(instr, "ld%sxr%s %s, [%s]", o0 ? "a" : "", suffix[size], GPR(sf, Rt), XSP(Rn));
		else
			Out(instr, "st%sxr%s %s, %s, [%s]", o0 ? "l" : "", suffix[size], W(Rs), GPR(sf, Rt), XSP(Rn));
	} else if (!o2 && o1 && size >= 2) {
		if (L)
			Out(instr, "ld%sxp %s, %s, [%s]", o0 ? "a" : "", GPR(sf, Rt), GPR(sf, Rt2), XSP(Rn));
		else
			Out(instr, "st%sxp %s, %s, %s, [%s]", o0 ? "l" : "", W(Rs), GPR(sf, Rt), GPR(sf, Rt2), XSP(Rn));
	} else if (o2 && !o1) {
		if (L)
			Out(instr, "%sr%s %s, [%s]", o0 ? "lda" : "ldla", suffix[size], GPR(sf, Rt), XSP(Rn));
		else
			Out(instr, "%sr%s %s, [%s]", o0 ? "stl" : "stll", suffix[size], GPR(sf, Rt), XSP(Rn));
	} else {
		Out(instr, "(LS exclusive %08x)", w);
	}
}

static void LoadLiteral(uint32_t w, uint64_t addr, Instruction *instr) {
	int opc = w >> 30;
	bool V = (w >> 26) & 1;
	int Rt = w & 0x1F;
	char target[32];
	FormatTarget(target, sizeof(target), addr + (int64_t)(SignExtend((w >> 5) & 0x7FFFF, 19) << 2), nullptr);
	if (V) {
		if (opc == 3)
			Out(instr, "(LS literal %08x)", w);
		else
			Out(instr, "ldr %c%d, %s", "sdq"[opc], Rt, target);
	} else if (opc == 3) {
		char prfop[16];
		PrefetchName(prfop, sizeof(prfop), Rt);
		Out(instr, "prfm %s, %s", prfop, target);
	} else {
		Out(instr, "%s %s, %s", opc == 2 ? "ldrsw" : "ldr", GPR(opc != 0, Rt), target);
	}
}

// Appends a list of count consecutive vector registers, like "{q1, q2}".
static void RegList(char *buf, size_t size, char r, int Rt, int count) {
	int pos = snprintf(buf, size, "{");
	for (int i = 0; i < count; i++) {
		pos += snprintf(buf + pos, size - pos, "%s%c%d", i ? ", " : "", r, (Rt + i) & 31);
	}
	snprintf(buf + pos, size - pos, "}");
}

static void SIMDLoadStoreStructure(uint32_t w, Instruction *instr) {
	bool Q = (w >> 30) & 1;
	bool single = (w >> 24) & 1;
	bool post = (w >> 23) & 1;
	bool L = (w >> 22) & 1;
	int Rm = (w >> 16) & 0x1F;
	int opcode = (w >> 12) & 0xF;
	int size = (w >> 10) & 3;
	int Rn = (w >> 5) & 0x1F;
	int Rt = w & 0x1F;
	char r = Q ? 'q' : 'd';
	char regs[48];
	char addrstr[32];

	if (!post && Rm != 0) {
		Out(instr, "(LS simd %08x)", w);
		return;
	}

	int bytes;
	if (!single) {
		static const int regCount[16] = { 4, 0, 4, 0, 3, 0, 3, 1, 2, 0, 2, 0, 0, 0, 0, 0 };
		static const int selem[16] = { 4, 0, 1, 0, 3, 0, 1, 1, 2, 0, 1, 0, 0, 0, 0, 0 };
		int count = regCount[opcode];
		if (count == 0 || ((w >> 21) & 1)) {
			Out(instr, "(LS simd %08x)", w);
			return;
		}
		RegList(regs, sizeof(regs), r, Rt, count);
		bytes = count * (Q ? 16 : 8);
		if (post) {
			if (Rm == 31)
				snprintf(addrstr, sizeof(addrstr), "[%s], #%d", XSP(Rn), bytes);
			else
				snprintf(addrstr, sizeof(addrstr), "[%s], %s", XSP(Rn), X(Rm));
		} else {
			snprintf(addrstr, sizeof(addrstr), "[%s]", XSP(Rn));
		}
		Out(instr, "%s%d.%d %s, %s", L ? "ld" : "st", selem[opcode], 8 << size, regs, addrstr);
		return;
	}

	bool R = (w >> 21) & 1;
	bool S = (w >> 12) & 1;
	int op = (w >> 13) & 7;
	int count = (((op & 1) << 1) | R) + 1;
	if (op >= 6) {
		// Load and replicate.
		if (!L || S) {
			Out(instr, "(LS simd %08x)", w);
			return;
		}
		RegList(regs, sizeof(regs), r, Rt, count);
		bytes = count << size;
		if (post) {
			if (Rm == 31)
				snprintf(addrstr, sizeof(addrstr), "[%s], #%d", XSP(Rn), bytes);
			else
				snprintf(addrstr, sizeof(addrstr), "[%s], %s", XSP(Rn), X(Rm));
		} else {
			snprintf(addrstr, sizeof(addrstr), "[%s]", XSP(Rn));
		}
		Out(instr, "ld%dr.%d %s, %s", count, 8 << size, regs, addrstr);
		return;
	}

	// Single lane.
	int index;
	int esize;
	switch (op >> 1) {
	case 0:
		esize = 0;
		index = (Q << 3) | (S << 2) | size;
		break;
	case 1:
		if (size & 1) {
			Out(instr, "(LS simd %08x)", w);
			return;
		}
		esize = 1;
		index = (Q << 2) | (S << 1) | (size >> 1);
		break;
	default:
		if (size == 0) {
			esize = 2;
			index = (Q << 1) | S;
		} else if (size == 1 && !S) {
			esize = 3;
			index = Q;
		} else {
			Out(instr, "(LS simd %08x)", w);
			return;
		}
		break;
	}
	bytes = count << esize;
	if (count == 1) {
		snprintf(regs, sizeof(regs), "q%d.%c[%d]", Rt, "bhsd"[esize], index);
	} else {
		char list[40];
		RegList(list, sizeof(list), 'q', Rt, count);
		snprintf(regs, sizeof(regs), "%s.%c[%d]", list, "bhsd"[esize], index);
	}
	if (post) {
		if (Rm == 31)
			snprintf(addrstr, sizeof(addrstr), "[%s], #%d", XSP(Rn), bytes);
		else
			snprintf(addrstr, sizeof(addrstr), "[%s], %s", XSP(Rn), X(Rm));
	} else {
		snprintf(addrstr, sizeof(addrstr), "[%s]", XSP(Rn));
	}
	Out(instr, "%s%d %s, %s", L ? "ld" : "st", count, regs, addrstr);
}

static void LoadStore(uint32_t w, uint64_t addr, Instruction *instr) {
	if ((w & 0xBE000000) == 0x0C000000) {
		// AdvSIMD load/store multiple / single structure
		SIMDLoadStoreStructure(w, instr);
	} else if (((w >> 24) & 0x3F) == 0x08) {
		LoadStoreExclusive(w, instr);
	} else if (((w >> 27) & 7) == 3 && ((w >> 24) & 3) == 0) {
		LoadLiteral(w, addr, instr);
	} else if (((w >> 27) & 7) == 5) {
		LoadStorePair(w, instr);
	} else if (((w >> 27) & 7) == 7) {
		LoadStoreRegister(w, addr, instr);
	} else {
		Out(instr, "(LS %08x)", w);
	}
}

static void DataProcessingRegister(uint32_t w, uint64_t addr, Instruction *instr) {
	int Rd = w & 0x1F;
	int Rn = (w >> 5) & 0x1F;
	int Rm = (w >> 16) & 0x1F;
	bool sf = (w >> 31) & 1;
	const char *d = GPR(sf, Rd);
	const char *n = GPR(sf, Rn);
	const char *m = GPR(sf, Rm);

	if (((w >> 28) & 1) == 0) {
		int shift = (w >> 22) & 0x3;
		int imm6 = (w >> 10) & 0x3f;
		char shiftstr[16] = "";
		if (shift || imm6)
			snprintf(shiftstr, sizeof(shiftstr), ", %s #%d", shiftnames[shift], imm6);

		if (((w >> 24) & 1) == 0) {
			// Logical (shifted register)
			int opc = (((w >> 29) & 3) << 1) | ((w >> 21) & 1);
			static const char * const opnames[8] = { "and", "bic", "orr", "orn", "eor", "eon", "ands", "bics" };
			if (opc == 2 && Rn == 31) {
				Out(instr, "mov %s, %s%s", d, m, shiftstr);
			} else if (opc == 3 && Rn == 31) {
				Out(instr, "mvn %s, %s%s", d, m, shiftstr);
			} else if (opc == 6 && Rd == 31) {
				Out(instr, "tst %s, %s%s", n, m, shiftstr);
			} else {
				Out(instr, "%s %s, %s, %s%s", opnames[opc], d, n, m, shiftstr);
			}
		} else if (((w >> 21) & 1) == 0) {
			// Add/subtract (shifted register)
			bool sub = (w >> 30) & 1;
			bool S = (w >> 29) & 1;
			if (shift == 3) {
				Out(instr, "(DPR %08x)", w);
			} else if (S && Rd == 31) {
				Out(instr, "%s %s, %s%s", sub ? "cmp" : "cmn", n, m, shiftstr);
			} else if (sub && Rn == 31) {
				Out(instr, "neg%s %s, %s%s", S ? "s" : "", d, m, shiftstr);
			} else {
				Out(instr, "%s%s %s, %s, %s%s", sub ? "sub" : "add", S ? "s" : "", d, n, m, shiftstr);
			}
		} else {
			// Add/subtract (extended register)
			bool sub = (w >> 30) & 1;
			bool S = (w >> 29) & 1;
			int option = (w >> 13) & 0x7;
			int imm3 = (w >> 10) & 0x7;
			const char *rm = GPR((option & 3) == 3, Rm);
			char ext[16];
			if ((Rd == 31 || Rn == 31) && option == (sf ? 3 : 2)) {
				if (imm3)
					snprintf(ext, sizeof(ext), ", lsl #%d", imm3);
				else
					ext[0] = '\0';
			} else if (imm3) {
				snprintf(ext, sizeof(ext), ", %s #%d", extendnames[option], imm3);
			} else {
				snprintf(ext, sizeof(ext), ", %s", extendnames[option]);
			}
			if (S && Rd == 31) {
				Out(instr, "%s %s, %s%s", sub ? "cmp" : "cmn", GPR(sf, Rn, true), rm, ext);
			} else {
				Out(instr, "%s%s %s, %s, %s%s", sub ? "sub" : "add", S ? "s" : "", GPR(sf, Rd, !S), GPR(sf, Rn, true), rm, ext);
			}
		}
		return;
	}

	switch ((w >> 21) & 0xF) {
	case 0x0: {
		// Add/subtract with carry
		bool sub = (w >> 30) & 1;
		bool S = (w >> 29) & 1;
		if (((w >> 10) & 0x3F) != 0) {
			Out(instr, "(DPR %08x)", w);
		} else if (sub && Rn == 31) {
			Out(instr, "ngc%s %s, %s", S ? "s" : "", d, m);
		} else {
			Out(instr, "%s%s %s, %s, %s", sub ? "sbc" : "adc", S ? "s" : "", d, n, m);
		}
		break;
	}
	case 0x2: {
		// Conditional compare
		const char *name = ((w >> 30) & 1) ? "ccmp" : "ccmn";
		int cond = (w >> 12) & 0xF;
		int nzcv = w & 0xF;
		if (((w >> 29) & 1) == 0 || ((w >> 10) & 1) || ((w >> 4) & 1)) {
			Out(instr, "(DPR %08x)", w);
		} else if ((w >> 11) & 1) {
			Out(instr, "%s %s, #%d, #%d, %s", name, n, Rm, nzcv, condnames[cond]);
		} else {
			Out(instr, "%s %s, %s, #%d, %s", name, n, m, nzcv, condnames[cond]);
		}
		break;
	}
	case 0x4: {
		// Conditional select
		int op = (((w >> 30) & 1) << 1) | ((w >> 10) & 1);
		int cond = (w >> 12) & 0xF;
		static const char * const opnames[4] = { "csel", "csinc", "csinv", "csneg" };
		if (((w >> 29) & 1) || ((w >> 11) & 1)) {
			Out(instr, "(DPR %08x)", w);
		} else if ((op == 1 || op == 2) && Rn == 31 && Rm == 31 && cond < 14) {
			Out(instr, "%s %s, %s", op == 1 ? "cset" : "csetm", d, condnames[cond ^ 1]);
		} else if (op != 0 && Rn == Rm && Rn != 31 && cond < 14) {
			static const char * const aliases[4] = { nullptr, "cinc", "cinv", "cneg" };
			Out(instr, "%s %s, %s, %s", aliases[op], d, n, condnames[cond ^ 1]);
		} else {
			Out(instr, "%s %s, %s, %s, %s", opnames[op], d, n, m, condnames[cond]);
		}
		break;
	}
	case 0x6: {
		int opcode = (w >> 10) & 0x3F;
		if ((w >> 30) & 1) {
			// Data-processing (1 source)
			static const char * const opnames[6] = { "rbit", "rev16", "rev32", "rev", "clz", "cls" };
			const char *name = opcode < 6 ? opnames[opcode] : nullptr;
			if (opcode == 2 && !sf)
				name = "rev";
			else if (opcode == 3 && !sf)
				name = nullptr;
			if (name && Rm == 0 && ((w >> 29) & 1) == 0)
				Out(instr, "%s %s, %s", name, d, n);
			else
				Out(instr, "(DPR %08x)", w);
		} else {
			// Data-processing (2 source)
			static const char * const opnames[16] = {
				nullptr, nullptr, "udiv", "sdiv", nullptr, nullptr, nullptr, nullptr,
				"lslv", "lsrv", "asrv", "rorv", nullptr, nullptr, nullptr, nullptr,
			};
			if ((opcode & 0x38) == 0x10) {
				// CRC32
				int sz = opcode & 3;
				bool C = (opcode >> 2) & 1;
				if (sf != (sz == 3)) {
					Out(instr, "(DPR %08x)", w);
				} else {
					Out(instr, "crc32%s%c %s, %s, %s", C ? "c" : "", "bhwx"[sz], W(Rd), W(Rn), GPR(sz == 3, Rm));
				}
			} else if (opcode < 16 && opnames[opcode]) {
				Out(instr, "%s %s, %s, %s", opnames[opcode], d, n, m);
			} else {
				Out(instr, "(DPR %08x)", w);
			}
		}
		break;
	}
	default:
		if ((w >> 24) & 1) {
			// Data processing - 3 source
			int op31 = (w >> 21) & 0x7;
			bool o0 = (w >> 15) & 1;
			int Ra = (w >> 10) & 0x1f;
			if (((w >> 29) & 3) != 0) {
				Out(instr, "(DPR %08x)", w);
			} else if (op31 == 0) {
				if (Ra == 31)
					Out(instr, "%s %s, %s, %s", o0 ? "mneg" : "mul", d, n, m);
				else
					Out(instr, "%s %s, %s, %s, %s", o0 ? "msub" : "madd", d, n, m, GPR(sf, Ra));
			} else if (sf && (op31 == 1 || op31 == 5)) {
				char sign = op31 == 5 ? 'u' : 's';
				if (Ra == 31)
					Out(instr, "%c%s %s, %s, %s", sign, o0 ? "mnegl" : "mull", d, W(Rn), W(Rm));
				else
					Out(instr, "%c%s %s, %s, %s, %s", sign, o0 ? "msubl" : "maddl", d, W(Rn), W(Rm), X(Ra));
			} else if (sf && (op31 == 2 || op31 == 6) && !o0) {
				Out(instr, "%cmulh %s, %s, %s", op31 == 6 ? 'u' : 's', d, n, m);
			} else {
				Out(instr, "(DPR %08x)", w);
			}
		} else {
			Out(instr, "(DPR %08x)", w);
		}
		break;
	}
}

inline bool GetQ(uint32_t w) { return (w >> 30) & 1; }
inline bool GetU(uint32_t w) { return (w >> 29) & 1; }
static const char *GetArrangement(bool Q, bool sz) {
	if (Q == 0 && sz == 0) return "2s";
	else if (Q == 1 && sz == 0) return "4s";
	else if (Q == 1 && sz == 1) return "2d";
	else return "ERROR";
}

static void SIMDThreeSame(uint32_t w, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	int Rm = (w >> 16) & 0x1f;
	int opcode = (w >> 11) & 0x1f;
	int size = (w >> 22) & 3;
	bool U = GetU(w);
	char r = GetQ(w) ? 'q' : 'd';

	if (opcode == 3) {
		static const char * const logical[2][4] = {
			{ "and", "bic", "orr", "orn" },
			{ "eor", "bsl", "bit", "bif" },
		};
		if (!U && size == 2 && Rn == Rm) {
			Out(instr, "mov %c%d, %c%d", r, Rd, r, Rn);
		} else {
			Out(instr, "%s %c%d, %c%d, %c%d", logical[U][size], r, Rd, r, Rn, r, Rm);
		}
		return;
	}

	const char *opname = nullptr;
	int esize;
	if (opcode < 0x18) {
		static const char * const opnames[2][24] = {
			{
				"shadd", "sqadd", "srhadd", nullptr, "shsub", "sqsub", "cmgt", "cmge",
				"sshl", "sqshl", "srshl", "sqrshl", "smax", "smin", "sabd", "saba",
				"add", "cmtst", "mla", "mul", "smaxp", "sminp", "sqdmulh", "addp",
			},
			{
				"uhadd", "uqadd", "urhadd", nullptr, "uhsub", "uqsub", "cmhi", "cmhs",
				"ushl", "uqshl", "urshl", "uqrshl", "umax", "umin", "uabd", "uaba",
				"sub", "cmeq", "mls", "pmul", "umaxp", "uminp", "sqrdmulh", nullptr,
			},
		};
		opname = opnames[U][opcode];
		esize = 8 << size;
	} else {
		// [U][size >> 1][opcode - 0x18]
		static const char * const fpnames[2][2][8] = {
			{
				{ "fmaxnm", "fmla", "fadd", "fmulx", "fcmeq", nullptr, "fmax", "frecps" },
				{ "fminnm", "fmls", "fsub", nullptr, nullptr, nullptr, "fmin", "frsqrts" },
			},
			{
				{ "fmaxnmp", nullptr, "faddp", "fmul", "fcmge", "facge", "fmaxp", "fdiv" },
				{ "fminnmp", nullptr, "fabd", nullptr, "fcmgt", "facgt", "fminp", nullptr },
			},
		};
		opname = fpnames[U][size >> 1][opcode - 0x18];
		esize = (size & 1) ? 64 : 32;
	}
	if (opname) {
		Out(instr, "%s.%d %c%d, %c%d, %c%d", opname, esize, r, Rd, r, Rn, r, Rm);
	} else {
		Out(instr, "(asimd three-same %08x)", w);
	}
}

static void SIMDTwoRegMisc(uint32_t w, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	int opcode = (w >> 12) & 0x1F;
	int size = (w >> 22) & 3;
	bool Q = GetQ(w);
	bool U = GetU(w);
	char r = Q ? 'q' : 'd';
	const char *two = Q ? "2" : "";
	int esize = 8 << size;
	int fsize = (size & 1) ? 64 : 32;

	// Narrowing and widening ops.
	switch (opcode) {
	case 0x12: case 0x14: {
		static const char * const names[2][2] = { { "xtn", "sqxtn" }, { "sqxtun", "uqxtn" } };
		Out(instr, "%s%s.%d.%d %c%d, q%d", names[U][opcode == 0x14], two, esize, esize * 2, r, Rd, Rn);
		return;
	}
	case 0x13:
		if (U) {
			Out(instr, "shll%s.%d.%d q%d, %c%d, #%d", two, esize * 2, esize, Rd, r, Rn, esize);
			return;
		}
		break;
	case 0x16:
		Out(instr, "%s%s.%d.%d %c%d, q%d", U ? "fcvtxn" : "fcvtn", two, fsize / 2, fsize, r, Rd, Rn);
		return;
	case 0x17:
		if (!U) {
			Out(instr, "fcvtl%s.%d.%d q%d, %c%d", two, fsize, fsize / 2, Rd, r, Rn);
			return;
		}
		break;
	}

	const char *opname = nullptr;
	const char *zero = nullptr;
	bool fp = false;
	bool nosize = false;
	if (opcode < 0xC) {
		static const char * const names[2][12] = {
			{ "rev64", "rev16", "saddlp", "suqadd", "cls", "cnt", "sadalp", "sqabs", "cmgt", "cmeq", "cmlt", "abs" },
			{ "rev32", nullptr, "uaddlp", "usqadd", "clz", nullptr, "uadalp", "sqneg", "cmge", "cmle", nullptr, "neg" },
		};
		opname = names[U][opcode];
		if (opcode >= 8 && opcode <= 10)
			zero = "#0";
		if (U && opcode == 5) {
			opname = size == 0 ? "not" : "rbit";
			nosize = size == 0;
		}
	} else if (size & 2) {
		// [U][opcode - 0xC]
		static const char * const names[2][20] = {
			{
				"fcmgt", "fcmeq", "fcmlt", "fabs", nullptr, nullptr, nullptr, nullptr,
				nullptr, nullptr, nullptr, nullptr, "frintp", "frintz", "fcvtps", "fcvtzs",
				"urecpe", "frecpe", nullptr, nullptr,
			},
			{
				"fcmge", "fcmle", nullptr, "fneg", nullptr, nullptr, nullptr, nullptr,
				nullptr, nullptr, nullptr, nullptr, nullptr, "frinti", "fcvtpu", "fcvtzu",
				"ursqrte", "frsqrte", nullptr, "fsqrt",
			},
		};
		opname = names[U][opcode - 0xC];
		fp = opcode != 0x1C;
		if (opcode >= 0xC && opcode <= 0xE)
			zero = "#0.0";
	} else if (opcode >= 0x18) {
		static const char * const names[2][8] = {
			{ "frintn", "frintm", "fcvtns", "fcvtms", "fcvtas", "scvtf", nullptr, nullptr },
			{ "frinta", "frintx", "fcvtnu", "fcvtmu", "fcvtau", "ucvtf", nullptr, nullptr },
		};
		opname = names[U][opcode - 0x18];
		fp = true;
	}

	if (!opname) {
		Out(instr, "(asimd vector two-reg misc %08x)", w);
	} else if (nosize) {
		Out(instr, "%s %c%d, %c%d", opname, r, Rd, r, Rn);
	} else if (zero) {
		Out(instr, "%s.%d %c%d, %c%d, %s", opname, fp ? fsize : esize, r, Rd, r, Rn, zero);
	} else {
		Out(instr, "%s.%d %c%d, %c%d", opname, fp ? fsize : esize, r, Rd, r, Rn);
	}
}

static void SIMDCopy(uint32_t w, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	bool Q = GetQ(w);
	bool op = GetU(w);
	int imm5 = (w >> 16) & 0x1F;
	int imm4 = (w >> 11) & 0xF;
	int size = 0;
	while (size < 4 && !(imm5 & (1 << size)))
		size++;
	if (size == 4) {
		Out(instr, "(asimd copy %08x)", w);
		return;
	}
	char s = "bhsd"[size];
	int index = imm5 >> (size + 1);
	char r = Q ? 'q' : 'd';

	if (op) {
		if (!Q) {
			Out(instr, "(asimd copy %08x)", w);
			return;
		}
		Out(instr, "ins q%d.%c[%d], q%d.%c[%d]", Rd, s, index, Rn, s, imm4 >> size);
		return;
	}
	switch (imm4) {
	case 0:
		Out(instr, "dup %c%d, q%d.%c[%d]", r, Rd, Rn, s, index);
		break;
	case 1:
		Out(instr, "dup.%d %c%d, %s", 8 << size, r, Rd, GPR(size == 3, Rn));
		break;
	case 3:
		Out(instr, "ins q%d.%c[%d], %s", Rd, s, index, GPR(size == 3, Rn));
		break;
	case 5:
		Out(instr, "smov %s, q%d.%c[%d]", GPR(Q, Rd), Rn, s, index);
		break;
	case 7:
		Out(instr, "umov %s, q%d.%c[%d]", GPR(Q, Rd), Rn, s, index);
		break;
	default:
		Out(instr, "(asimd copy %08x)", w);
		break;
	}
}

static void SIMDModifiedImmediate(uint32_t w, Instruction *instr) {
	int Rd = w & 0x1f;
	bool Q = GetQ(w);
	bool op = GetU(w);
	int cmode = (w >> 12) & 0xF;
	int imm8 = (((w >> 16) & 7) << 5) | ((w >> 5) & 0x1F);
	char r = Q ? 'q' : 'd';

	if (((w >> 11) & 1) != 0) {
		Out(instr, "(asimd modified immediate %08x)", w);
	} else if (cmode < 8) {
		// 32-bit shifted immediate.
		const char *name = (cmode & 1) ? (op ? "bic" : "orr") : (op ? "mvni" : "movi");
		int shift = (cmode >> 1) * 8;
		if (shift)
			Out(instr, "%s.32 %c%d, #0x%02x, lsl #%d", name, r, Rd, imm8, shift);
		else
			Out(instr, "%s.32 %c%d, #0x%02x", name, r, Rd, imm8);
	} else if (cmode < 12) {
		// 16-bit shifted immediate.
		const char *name = (cmode & 1) ? (op ? "bic" : "orr") : (op ? "mvni" : "movi");
		int shift = ((cmode >> 1) & 1) * 8;
		if (shift)
			Out(instr, "%s.16 %c%d, #0x%02x, lsl #%d", name, r, Rd, imm8, shift);
		else
			Out(instr, "%s.16 %c%d, #0x%02x", name, r, Rd, imm8);
	} else if (cmode < 14) {
		Out(instr, "%s.32 %c%d, #0x%02x, msl #%d", op ? "mvni" : "movi", r, Rd, imm8, (cmode & 1) ? 16 : 8);
	} else if (cmode == 14) {
		if (!op) {
			Out(instr, "movi.8 %c%d, #0x%02x", r, Rd, imm8);
		} else {
			uint64_t imm = 0;
			for (int i = 0; i < 8; i++) {
				if (imm8 & (1 << i))
					imm |= 0xFFULL << (i * 8);
			}
			// With Q clear this is the scalar form, which writes the whole d register.
			Out(instr, Q ? "movi.64 q%d, #0x%08x%08x" : "movi d%d, #0x%08x%08x", Rd, (uint32_t)(imm >> 32), (uint32_t)imm);
		}
	} else {
		if (op && !Q) {
			Out(instr, "(asimd modified immediate %08x)", w);
		} else {
			Out(instr, "fmov.%d %c%d, #%f", op ? 64 : 32, r, Rd, Arm64Gen::FPImm8ToFloat(imm8));
		}
	}
}

static void SIMDShiftByImmediate(uint32_t w, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	bool Q = GetQ(w);
	bool U = GetU(w);
	int immh = (w >> 19) & 0xf;
	int immhb = (w >> 16) & 0x7f;
	int opcode = (w >> 11) & 0x1f;
	int esize = 8 << HighestSetBit(immh);
	int rshift = esize * 2 - immhb;
	int lshift = immhb - esize;
	char r = Q ? 'q' : 'd';
	const char *two = Q ? "2" : "";

	static const char * const opnamesU0[32] = {
		"sshr", nullptr, "ssra", nullptr, "srshr", nullptr, "srsra", nullptr,
		nullptr, nullptr, "shl", nullptr, nullptr, nullptr, "sqshl", nullptr,
		"shrn", "rshrn", "sqshrn", "sqrshrn", "sshll", nullptr, nullptr, nullptr,
		nullptr, nullptr, nullptr, nullptr, "scvtf", nullptr, nullptr, "fcvtzs",
	};
	static const char * const opnamesU1[32] = {
		"ushr", nullptr, "usra", nullptr, "urshr", nullptr, "ursra", nullptr,
		"sri", nullptr, "sli", nullptr, "sqshlu", nullptr, "uqshl", nullptr,
		"sqshrun", "sqrshrun", "uqshrn", "uqrshrn", "ushll", nullptr, nullptr, nullptr,
		nullptr, nullptr, nullptr, nullptr, "ucvtf", nullptr, nullptr, "fcvtzu",
	};
	const char *opname = U ? opnamesU1[opcode] : opnamesU0[opcode];
	if (!opname || (immh & 8 && !Q && opcode != 0x10 && opcode != 0x11 && opcode != 0x12 && opcode != 0x13 && opcode != 0x14)) {
		Out(instr, "(asimd shift-by-immediate %08x)", w);
	} else if (opcode == 0x1C || opcode == 0x1F) {
		// Fixed point conversions.
		Out(instr, "%s.%d %c%d, %c%d, #%d", opname, esize, r, Rd, r, Rn, rshift);
	} else if (opcode >= 0x10 && opcode <= 0x13) {
		// Narrowing right shifts.
		if (immh & 8)
			Out(instr, "(asimd shift-by-immediate %08x)", w);
		else
			Out(instr, "%s%s.%d.%d %c%d, q%d, #%d", opname, two, esize, esize * 2, r, Rd, Rn, rshift);
	} else if (opcode == 0x14) {
		// Widening left shifts.
		if (immh & 8) {
			Out(instr, "(asimd shift-by-immediate %08x)", w);
		} else if (lshift == 0) {
			Out(instr, "%cxtl%s.%d.%d q%d, %c%d", U ? 'u' : 's', two, esize * 2, esize, Rd, r, Rn);
		} else {
			Out(instr, "%s%s.%d.%d q%d, %c%d, #%d", opname, two, esize * 2, esize, Rd, r, Rn, lshift);
		}
	} else if (opcode >= 0xA) {
		Out(instr, "%s.%d %c%d, %c%d, #%d", opname, esize, r, Rd, r, Rn, lshift);
	} else {
		Out(instr, "%s.%d %c%d, %c%d, #%d", opname, esize, r, Rd, r, Rn, rshift);
	}
}

static void SIMDVectorIndexedElement(uint32_t w, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	int Rm = (w >> 16) & 0x1f;
	bool Q = GetQ(w);
	bool U = GetU(w);
	int size = (w >> 22) & 3;
	bool L = (w >> 21) & 1;
	bool H = (w >> 11) & 1;
	int opcode = (w >> 12) & 0xf;
	const char *opname = nullptr;
	// The widening integer multiplies (long, so Rd is always q).
	if ((opcode == 0x2 || opcode == 0x6 || opcode == 0xA) && (size == 1 || size == 2)) {
		static const char * const names[2][3] = { { "smlal", "smlsl", "smull" }, { "umlal", "umlsl", "umull" } };
		const char *name = names[U][opcode == 0x2 ? 0 : (opcode == 0x6 ? 1 : 2)];
		const int narrow = 8 << size;
		const int index = size == 2 ? ((H << 1) | (int)L) : ((H << 2) | ((int)L << 1) | ((Rm >> 4) & 1));
		const int m = size == 2 ? Rm : (Rm & 0xF);
		Out(instr, "%s%s.%d.%d q%d, %c%d, q%d.%c[%d]", name, Q ? "2" : "", narrow * 2, narrow, Rd, Q ? 'q' : 'd', Rn, m, size == 2 ? 's' : 'h', index);
		return;
	}
	if (size & 2) {
		if (!U) {
			switch (opcode) {
			case 1: opname = "fmla"; break;
			case 5: opname = "fmls"; break;
			case 9: opname = "fmul"; break;
			}
		} else if (opcode == 9) {
			opname = "fmulx";
		}
	}
	if (!opname || ((size & 1) && (L || !Q))) {
		Out(instr, "(asimd vector x indexed elem %08x)", w);
		return;
	}
	int index = (size & 1) ? H : ((H << 1) | (int)L);
	char r = Q ? 'q' : 'd';
	Out(instr, "%s %c%d, %c%d, %c%d.%s[%d]", opname, r, Rd, r, Rn, r, Rm, GetArrangement(Q, size & 1), index);
}

// Reductions across the lanes of a vector into a scalar.
static void SIMDAcrossLanes(uint32_t w, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	int opcode = (w >> 12) & 0x1F;
	int size = (w >> 22) & 3;
	bool Q = GetQ(w);
	bool U = GetU(w);
	char r = Q ? 'q' : 'd';
	if (U && (opcode == 0x0C || opcode == 0x0F)) {
		// fmaxnmv, fmaxv, fminnmv, fminv: single precision only.
		const char *name = opcode == 0x0C ? ((size & 2) ? "fminnmv" : "fmaxnmv") : ((size & 2) ? "fminv" : "fmaxv");
		Out(instr, "%s s%d, %c%d.4s", name, Rd, r, Rn);
		return;
	}
	const char *name = nullptr;
	bool longResult = false;
	switch (opcode) {
	case 0x03: name = U ? "uaddlv" : "saddlv"; longResult = true; break;
	case 0x0A: name = U ? "umaxv" : "smaxv"; break;
	case 0x1A: name = U ? "uminv" : "sminv"; break;
	case 0x1B: name = U ? nullptr : "addv"; break;
	}
	if (!name || size == 3) {
		Out(instr, "(asimd across lanes %08x)", w);
		return;
	}
	static const char scalarRegs[] = "bhsd";
	Out(instr, "%s.%d %c%d, %c%d", name, 8 << size, scalarRegs[size + (longResult ? 1 : 0)], Rd, r, Rn);
}

// Long (L), wide (W) and narrowing (HN) ops on lanes of different sizes.
static void SIMDThreeDifferent(uint32_t w, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	int Rm = (w >> 16) & 0x1f;
	int opcode = (w >> 12) & 0xF;
	int size = (w >> 22) & 3;
	bool Q = GetQ(w);
	bool U = GetU(w);
	static const char * const names[2][16] = {
		{ "saddl", "saddw", "ssubl", "ssubw", "addhn", "sabal", "subhn", "sabdl", "smlal", "sqdmlal", "smlsl", "sqdmlsl", "smull", "sqdmull", "pmull", nullptr },
		{ "uaddl", "uaddw", "usubl", "usubw", "raddhn", "uabal", "rsubhn", "uabdl", "umlal", nullptr, "umlsl", nullptr, "umull", nullptr, nullptr, nullptr },
	};
	const char *name = names[U][opcode];
	if (!name || size == 3) {
		Out(instr, "(asimd three-different %08x)", w);
		return;
	}
	const char *two = Q ? "2" : "";
	const char half = Q ? 'q' : 'd';
	const int narrow = 8 << size, wide = 16 << size;
	if (opcode == 1 || opcode == 3) {
		// Wide: Rd and Rn are the wide lanes.
		Out(instr, "%s%s.%d.%d q%d, q%d, %c%d", name, two, wide, narrow, Rd, Rn, half, Rm);
	} else if (opcode == 4 || opcode == 6) {
		// Narrowing: Rd gets the high halves.
		Out(instr, "%s%s.%d.%d %c%d, q%d, q%d", name, two, narrow, wide, half, Rd, Rn, Rm);
	} else {
		Out(instr, "%s%s.%d.%d q%d, %c%d, %c%d", name, two, wide, narrow, Rd, half, Rn, half, Rm);
	}
}

// (w >> 25) & 0xF  == 7: AdvSIMD vector instructions.
static void FPandASIMD1(uint32_t w, uint64_t addr, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	int Rm = (w >> 16) & 0x1f;
	char r = GetQ(w) ? 'q' : 'd';
	if ((w >> 31) & 1) {
		Out(instr, "(FP1 %08x)", w);
	} else if (((w >> 24) & 1) == 0) {
		if ((w >> 21) & 1) {
			if ((w >> 10) & 1) {
				SIMDThreeSame(w, instr);
			} else if (((w >> 10) & 3) == 2 && ((w >> 17) & 0xF) == 0) {
				SIMDTwoRegMisc(w, instr);
			} else if (((w >> 10) & 3) == 2 && ((w >> 17) & 0xF) == 8) {
				SIMDAcrossLanes(w, instr);
			} else {
				SIMDThreeDifferent(w, instr);
			}
		} else if ((w & 0x9FE08400) == 0x0E000400) {
			SIMDCopy(w, instr);
		} else if ((w & 0xBF208C00) == 0x0E000800 && ((w >> 12) & 3) != 0) {
			static const char * const opnames[8] = { nullptr, "uzp1", "trn1", "zip1", nullptr, "uzp2", "trn2", "zip2" };
			int size = (w >> 22) & 3;
			Out(instr, "%s.%d %c%d, %c%d, %c%d", opnames[(w >> 12) & 7], 8 << size, r, Rd, r, Rn, r, Rm);
		} else if ((w & 0xBFE08400) == 0x2E000000) {
			Out(instr, "ext %c%d, %c%d, %c%d, #%d", r, Rd, r, Rn, r, Rm, (w >> 11) & 0xF);
		} else {
			Out(instr, "(FP1 %08x)", w);
		}
	} else if (((w >> 10) & 1) == 1) {
		if (((w >> 19) & 0xf) == 0) {
			SIMDModifiedImmediate(w, instr);
		} else {
			SIMDShiftByImmediate(w, instr);
		}
	} else {
		SIMDVectorIndexedElement(w, instr);
	}
}

static char FPRegChar(int type) {
	switch (type) {
	case 0: return 's';
	case 1: return 'd';
	case 3: return 'h';
	default: return '?';
	}
}

static void FPDataProcessing1(uint32_t w, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	int type = (w >> 22) & 3;
	int opcode = (w >> 15) & 0x3F;
	char fr = FPRegChar(type);
	if (opcode < 4) {
		static const char * const opnames[4] = { "fmov", "fabs", "fneg", "fsqrt" };
		Out(instr, "%s %c%d, %c%d", opnames[opcode], fr, Rd, fr, Rn);
	} else if (opcode < 8 && (opcode & 3) != 2 && (opcode & 3) != type) {
		Out(instr, "fcvt %c%d, %c%d", FPRegChar(opcode & 3), Rd, fr, Rn);
	} else if (opcode >= 8 && opcode < 16 && opcode != 13) {
		static const char * const opnames[8] = { "frintn", "frintp", "frintm", "frintz", "frinta", nullptr, "frintx", "frinti" };
		Out(instr, "%s %c%d, %c%d", opnames[opcode - 8], fr, Rd, fr, Rn);
	} else {
		Out(instr, "(fp 1-source %08x)", w);
	}
}

static void FPIntConversion(uint32_t w, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	bool sf = (w >> 31) & 1;
	int type = (w >> 22) & 3;
	int rmode = (w >> 19) & 3;
	int opcode = (w >> 16) & 7;
	char fr = FPRegChar(type);
	if (opcode < 2 || opcode == 4 || opcode == 5) {
		// FP to int
		char round = (opcode >= 4) ? 'a' : "npmz"[rmode];
		if (opcode >= 4 && rmode != 0) {
			Out(instr, "(fp<->int %08x)", w);
			return;
		}
		Out(instr, "fcvt%c%c %s, %c%d", round, (opcode & 1) ? 'u' : 's', GPR(sf, Rd), fr, Rn);
	} else if ((opcode == 2 || opcode == 3) && rmode == 0) {
		Out(instr, "%ccvtf %c%d, %s", (opcode & 1) ? 'u' : 's', fr, Rd, GPR(sf, Rn));
	} else if (opcode == 6 && rmode == 0) {
		Out(instr, "fmov %s, %c%d", GPR(sf, Rd), fr, Rn);
	} else if (opcode == 7 && rmode == 0) {
		Out(instr, "fmov %c%d, %s", fr, Rd, GPR(sf, Rn));
	} else if (opcode == 6 && rmode == 1 && sf && type == 2) {
		Out(instr, "fmov %s, q%d.d[1]", X(Rd), Rn);
	} else if (opcode == 7 && rmode == 1 && sf && type == 2) {
		Out(instr, "fmov q%d.d[1], %s", Rd, X(Rn));
	} else {
		Out(instr, "(fp<->int %08x)", w);
	}
}

static void SIMDScalar(uint32_t w, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	bool U = GetU(w);
	int size = (w >> 22) & 3;
	int opcode = (w >> 12) & 0x1f;

	if ((w >> 24) & 1) {
		if (((w >> 10) & 1) == 0) {
			Out(instr, "(asimd scalar x indexed elem %08x)", w);
			return;
		}
		// Scalar shift by immediate.
		int immh = (w >> 19) & 0xf;
		int immhb = (w >> 16) & 0x7f;
		int sopcode = (w >> 11) & 0x1f;
		if (immh == 0) {
			Out(instr, "(asimd scalar shift-by-immediate %08x)", w);
			return;
		}
		int esize = 8 << HighestSetBit(immh);
		char r = "bhsd"[HighestSetBit(immh)];
		if (sopcode == 0x1C || sopcode == 0x1F) {
			const char *name = sopcode == 0x1C ? (U ? "ucvtf" : "scvtf") : (U ? "fcvtzu" : "fcvtzs");
			if (esize < 16)
				Out(instr, "(asimd scalar shift-by-immediate %08x)", w);
			else
				Out(instr, "%s %c%d, %c%d, #%d", name, r, Rd, r, Rn, esize * 2 - immhb);
		} else if (esize == 64 && (sopcode == 0 || sopcode == 2 || sopcode == 4 || sopcode == 6 || (U && sopcode == 8))) {
			static const char * const names[2][5] = { { "sshr", "ssra", "srshr", "srsra", nullptr }, { "ushr", "usra", "urshr", "ursra", "sri" } };
			Out(instr, "%s d%d, d%d, #%d", names[U][sopcode >> 1], Rd, Rn, 128 - immhb);
		} else if (esize == 64 && sopcode == 0xA) {
			Out(instr, "%s d%d, d%d, #%d", U ? "sli" : "shl", Rd, Rn, immhb - 64);
		} else {
			Out(instr, "(asimd scalar shift-by-immediate %08x)", w);
		}
		return;
	}

	if (((w >> 21) & 1) == 0) {
		Out(instr, "(asimd scalar copy %08x)", w);
		return;
	}
	if ((w >> 10) & 1) {
		Out(instr, "(asimd scalar three same %08x)", w);
		return;
	}
	if (((w >> 10) & 3) != 2) {
		Out(instr, "(asimd scalar three different %08x)", w);
		return;
	}

	if (((w >> 17) & 0xf) == 8) {
		// Scalar pairwise.
		const char *opname = nullptr;
		if (U) {
			switch (opcode) {
			case 0xC: opname = (size & 2) ? "fminnmp" : "fmaxnmp"; break;
			case 0xD: opname = (size & 2) ? nullptr : "faddp"; break;
			case 0xF: opname = (size & 2) ? "fminp" : "fmaxp"; break;
			}
		}
		if (opname) {
			bool sz = size & 1;
			Out(instr, "%s %c%d, %c%d", opname, sz ? 'd' : 's', Rd, sz ? 'q' : 'd', Rn);
		} else if (!U && opcode == 0x1B && size == 3) {
			Out(instr, "addp d%d, q%d", Rd, Rn);
		} else {
			Out(instr, "(asimd scalar pair-wise %08x)", w);
		}
		return;
	}
	if (((w >> 17) & 0xf) != 0) {
		Out(instr, "(asimd scalar stuff %08x)", w);
		return;
	}

	// Scalar two-reg misc.
	const char *opname = nullptr;
	const char *zero = nullptr;
	bool fp = false;
	if (opcode >= 0x1A && opcode <= 0x1D) {
		static const char * const names[2][2][4] = {
			{ { "fcvtns", "fcvtms", "fcvtas", "scvtf" }, { "fcvtps", "fcvtzs", nullptr, "frecpe" } },
			{ { "fcvtnu", "fcvtmu", "fcvtau", "ucvtf" }, { "fcvtpu", "fcvtzu", nullptr, "frsqrte" } },
		};
		opname = names[U][size >> 1][opcode - 0x1A];
		fp = true;
	} else if ((size & 2) && opcode >= 0xC && opcode <= 0xE) {
		static const char * const names[2][3] = { { "fcmgt", "fcmeq", "fcmlt" }, { "fcmge", "fcmle", nullptr } };
		opname = names[U][opcode - 0xC];
		zero = "#0.0";
		fp = true;
	} else if (size == 3 && opcode >= 8 && opcode <= 0xB) {
		static const char * const names[2][4] = { { "cmgt", "cmeq", "cmlt", "abs" }, { "cmge", "cmle", nullptr, "neg" } };
		opname = names[U][opcode - 8];
		if (opcode != 0xB)
			zero = "#0";
	}
	if (!opname) {
		Out(instr, "(asimd scalar two-reg misc %08x)", w);
		return;
	}
	char r = fp ? ((size & 1) ? 'd' : 's') : 'd';
	if (zero)
		Out(instr, "%s %c%d, %c%d, %s", opname, r, Rd, r, Rn, zero);
	else
		Out(instr, "%s %c%d, %c%d", opname, r, Rd, r, Rn);
}

// (w >> 25) & 0xF  == f: scalar floating point, and AdvSIMD scalar.
static void FPandASIMD2(uint32_t w, uint64_t addr, Instruction *instr) {
	int Rd = w & 0x1f;
	int Rn = (w >> 5) & 0x1f;
	int Rm = (w >> 16) & 0x1f;
	int type = (w >> 22) & 0x3;
	bool sf = (w >> 31) & 1;
	char fr = FPRegChar(type);

	if (GetQ(w)) {
		if (sf)
			Out(instr, "(FP2 %08x)", w);
		else
			SIMDScalar(w, instr);
		return;
	}
	if (GetU(w) || type == 2) {
		// The S bit, and the type used by fmov to the top half of a vector.
		if (((w >> 21) & 1) && ((w >> 10) & 0x3F) == 0 && type == 2 && !GetU(w))
			FPIntConversion(w, instr);
		else
			Out(instr, "(FP2 %08x)", w);
		return;
	}

	if ((w >> 24) & 1) {
		// Data-processing (3 source)
		int opcode = (((w >> 21) & 1) << 1) | ((w >> 15) & 1);
		static const char * const opnames[4] = { "fmadd", "fmsub", "fnmadd", "fnmsub" };
		int Ra = (w >> 10) & 0x1f;
		Out(instr, "%s %c%d, %c%d, %c%d, %c%d", opnames[opcode], fr, Rd, fr, Rn, fr, Rm, fr, Ra);
	} else if (((w >> 21) & 1) == 0) {
		// Conversion between floating point and fixed point.
		int rmode = (w >> 19) & 3;
		int opcode = (w >> 16) & 7;
		int scale = (w >> 10) & 0x3f;
		int fbits = 64 - scale;
		char sign = (opcode & 1) ? 'u' : 's';
		if (!sf && scale < 32) {
			Out(instr, "(float<->fixed %08x)", w);
		} else if (rmode == 0 && (opcode & 6) == 2) {
			Out(instr, "%ccvtf %c%d, %s, #%d", sign, fr, Rd, GPR(sf, Rn), fbits);
		} else if (rmode == 3 && (opcode & 6) == 0) {
			Out(instr, "fcvtz%c %s, %c%d, #%d", sign, GPR(sf, Rd), fr, Rn, fbits);
		} else {
			Out(instr, "(float<->fixed %08x)", w);
		}
	} else {
		switch ((w >> 10) & 3) {
		case 0:
			if (((w >> 10) & 7) == 4) {
				if (((w >> 5) & 0x1F) != 0) {
					Out(instr, "(fp imm %08x)", w);
				} else {
					uint8_t uimm8 = (w >> 13) & 0xff;
					Out(instr, "fmov %c%d, #%f", fr, Rd, Arm64Gen::FPImm8ToFloat(uimm8));
				}
			} else if (((w >> 10) & 0xf) == 8) {
				int opcode2 = w & 0x1f;
				bool e = (opcode2 >> 4) & 1;
				bool z = (opcode2 >> 3) & 1;
				if ((opcode2 & 7) != 0 || ((w >> 14) & 3) != 0) {
					Out(instr, "(fp compare %08x)", w);
				} else if (z) {
					Out(instr, "fcmp%s %c%d, #0.0", e ? "e" : "", fr, Rn);
				} else {
					Out(instr, "fcmp%s %c%d, %c%d", e ? "e" : "", fr, Rn, fr, Rm);
				}
			} else if (((w >> 10) & 0x1f) == 0x10) {
				FPDataProcessing1(w, instr);
			} else if (((w >> 10) & 0x3f) == 0) {
				FPIntConversion(w, instr);
			} else {
				Out(instr, "(FP2 %08x)", w);
			}
			break;
		case 1: {
			int cond = (w >> 12) & 0xf;
			bool e = (w >> 4) & 1;
			Out(instr, "fccmp%s %c%d, %c%d, #%d, %s", e ? "e" : "", fr, Rn, fr, Rm, w & 0xF, condnames[cond]);
			break;
		}
		case 2: {
			int opc = (w >> 12) & 0xf;
			static const char * const opnames[16] = { "fmul", "fdiv", "fadd", "fsub", "fmax", "fmin", "fmaxnm", "fminnm", "fnmul" };
			if (opnames[opc])
				Out(instr, "%s %c%d, %c%d, %c%d", opnames[opc], fr, Rd, fr, Rn, fr, Rm);
			else
				Out(instr, "(fp 2-source %08x)", w);
			break;
		}
		case 3: {
			int cond = (w >> 12) & 0xf;
			Out(instr, "fcsel %c%d, %c%d, %c%d, %s", fr, Rd, fr, Rn, fr, Rm, condnames[cond]);
			break;
		}
		}
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
	if (instr.undefined) {
		snprintf(output, bufsize, "%08x\t[undefined instr]", w);
	} else if (includeWord) {
		snprintf(output, bufsize, "%08x\t%s", w, instr.text);
	} else {
		snprintf(output, bufsize, "%s", instr.text);
	}
	// zap tabs
	while (*output) {
		if (*output == '\t')
			*output = ' ';
		output++;
	}
}
