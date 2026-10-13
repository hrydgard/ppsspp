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

// ARMv7-A disassembler for ARM state: the integer instructions, VFPv4 and NEON.
// It replaces Gareth McCaughan's disarm, which this file used to hold.
// Syntax is UAL with upper case mnemonics: "VADDNE.f32 s0, s1, s2", "LDRB r0, [r1, #-4]!".

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "Common/CommonTypes.h"
#include "ext/disarm.h"

namespace {

const char *const condNames[16] = {
	"EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC", "HI", "LS", "GE", "LT", "GT", "LE", "", "",
};

const char *const regNames[16] = {
	"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "sp", "lr", "pc",
};

const char *const shiftNames[4] = { "lsl", "lsr", "asr", "ror" };
const char *const shiftMnemonics[4] = { "LSL", "LSR", "ASR", "ROR" };

const char *const dataProcNames[16] = {
	"AND", "EOR", "SUB", "RSB", "ADD", "ADC", "SBC", "RSC", "TST", "TEQ", "CMP", "CMN", "ORR", "MOV", "BIC", "MVN",
};

struct Out {
	Out(char *b, size_t s) : buf(b), size(s) { buf[0] = 0; }

	void Add(const char *fmt, ...) {
		if (len + 1 >= size)
			return;
		va_list args;
		va_start(args, fmt);
		int n = vsnprintf(buf + len, size - len, fmt, args);
		va_end(args);
		if (n > 0)
			len = std::min(size - 1, len + (size_t)n);
	}

	char *buf;
	size_t size;
	size_t len = 0;
};

inline u32 Bits(u32 w, int hi, int lo) {
	return (w >> lo) & ((1U << (hi - lo + 1)) - 1);
}

inline bool Bit(u32 w, int b) {
	return (w >> b) & 1;
}

inline const char *R(int r) {
	return regNames[r & 15];
}

inline const char *Cond(u32 w) {
	return condNames[w >> 28];
}

inline u32 RotR(u32 v, int amount) {
	amount &= 31;
	return amount ? (v >> amount) | (v << (32 - amount)) : v;
}

inline int SignExtend(u32 v, int bits) {
	return (int)(v << (32 - bits)) >> (32 - bits);
}

void AddImm(Out &o, u32 v) {
	if (v < 256)
		o.Add("#%u", v);
	else
		o.Add("#0x%x", v);
}

// VFPExpandImm. The same 8-bit format is used by NEON's VMOV.f32.
float ExpandFloatImm(int imm8) {
	float f = (float)((imm8 & 0xF) + 16) / 16.0f;
	int exp = (((imm8 >> 4) & 7) ^ 4) - 3;
	while (exp > 0) {
		f *= 2.0f;
		exp--;
	}
	while (exp < 0) {
		f /= 2.0f;
		exp++;
	}
	return (imm8 & 0x80) ? -f : f;
}

void AddFloat(Out &o, float f) {
	char temp[32];
	snprintf(temp, sizeof(temp), "%g", f);
	o.Add("#%s%s", temp, strpbrk(temp, ".e") ? "" : ".0");
}

void AddShiftImm(Out &o, int type, int imm5) {
	if (type == 0 && imm5 == 0)
		return;
	if (type == 3 && imm5 == 0)
		o.Add(", rrx");
	else
		o.Add(", %s #%d", shiftNames[type], imm5 == 0 ? 32 : imm5);
}

void AddRegList(Out &o, u32 list) {
	o.Add("{");
	bool first = true;
	for (int i = 0; i < 16; i++) {
		if (list & (1 << i)) {
			o.Add("%s%s", first ? "" : ", ", R(i));
			first = false;
		}
	}
	o.Add("}");
}

// "[rn, offset]{!}" or "[rn], offset". An empty offset prints as "[rn]".
void AddAddress(Out &o, int rn, bool pre, bool writeback, const char *offset) {
	if (!pre)
		o.Add("[%s], %s", R(rn), offset);
	else if (offset[0])
		o.Add("[%s, %s]%s", R(rn), offset, writeback ? "!" : "");
	else
		o.Add("[%s]%s", R(rn), writeback ? "!" : "");
}

bool DisDataProcessing(u32 w, Out &o) {
	int opc = Bits(w, 24, 21);
	bool s = Bit(w, 20);
	int rn = Bits(w, 19, 16);
	int rd = Bits(w, 15, 12);
	bool compare = opc >= 8 && opc <= 11;
	if (compare && !s)
		return false;
	const char *sfx = s && !compare ? "S" : "";
	const char *cond = Cond(w);

	// MOV with a shifted register is printed as the shift.
	if (opc == 13 && !Bit(w, 25)) {
		int type = Bits(w, 6, 5);
		int rm = Bits(w, 3, 0);
		if (Bit(w, 4)) {
			if (Bit(w, 7))
				return false;
			o.Add("%s%s%s %s, %s, %s", shiftMnemonics[type], sfx, cond, R(rd), R(rm), R(Bits(w, 11, 8)));
			return true;
		}
		int imm5 = Bits(w, 11, 7);
		if (type == 3 && imm5 == 0) {
			o.Add("RRX%s%s %s, %s", sfx, cond, R(rd), R(rm));
			return true;
		} else if (type != 0 || imm5 != 0) {
			o.Add("%s%s%s %s, %s, #%d", shiftMnemonics[type], sfx, cond, R(rd), R(rm), imm5 ? imm5 : 32);
			return true;
		}
	}

	o.Add("%s%s%s ", dataProcNames[opc], sfx, cond);
	if (compare)
		o.Add("%s, ", R(rn));
	else if (opc == 13 || opc == 15)
		o.Add("%s, ", R(rd));
	else
		o.Add("%s, %s, ", R(rd), R(rn));

	if (Bit(w, 25)) {
		AddImm(o, RotR(w & 0xFF, 2 * Bits(w, 11, 8)));
	} else {
		int type = Bits(w, 6, 5);
		o.Add("%s", R(Bits(w, 3, 0)));
		if (Bit(w, 4)) {
			if (Bit(w, 7))
				return false;
			o.Add(", %s %s", shiftNames[type], R(Bits(w, 11, 8)));
		} else {
			AddShiftImm(o, type, Bits(w, 11, 7));
		}
	}
	return true;
}

bool DisMultiply(u32 w, Out &o) {
	bool s = Bit(w, 20);
	int hi = Bits(w, 19, 16);
	int lo = Bits(w, 15, 12);
	int rm = Bits(w, 11, 8);
	int rn = Bits(w, 3, 0);
	const char *sfx = s ? "S" : "";
	const char *cond = Cond(w);
	switch (Bits(w, 23, 21)) {
	case 0:
		o.Add("MUL%s%s %s, %s, %s", sfx, cond, R(hi), R(rn), R(rm));
		return true;
	case 1:
		o.Add("MLA%s%s %s, %s, %s, %s", sfx, cond, R(hi), R(rn), R(rm), R(lo));
		return true;
	case 2:
		if (s)
			return false;
		o.Add("UMAAL%s %s, %s, %s, %s", cond, R(lo), R(hi), R(rn), R(rm));
		return true;
	case 3:
		if (s)
			return false;
		o.Add("MLS%s %s, %s, %s, %s", cond, R(hi), R(rn), R(rm), R(lo));
		return true;
	default:
		{
			static const char *const names[4] = { "UMULL", "UMLAL", "SMULL", "SMLAL" };
			o.Add("%s%s%s %s, %s, %s, %s", names[Bits(w, 22, 21)], sfx, cond, R(lo), R(hi), R(rn), R(rm));
			return true;
		}
	}
}

bool DisSynchronization(u32 w, Out &o) {
	int rn = Bits(w, 19, 16);
	int rd = Bits(w, 15, 12);
	int rt = Bits(w, 3, 0);
	const char *cond = Cond(w);
	int op = Bits(w, 23, 20);
	static const char *const sizes[4] = { "", "D", "B", "H" };
	if (op == 0 || op == 4) {
		o.Add("SWP%s%s %s, %s, [%s]", op == 4 ? "B" : "", cond, R(rd), R(rt), R(rn));
	} else if ((op & 8) == 0) {
		return false;
	} else if (op & 1) {
		o.Add("LDREX%s%s %s, ", sizes[(op >> 1) & 3], cond, R(rd));
		if (op == 0xB)
			o.Add("%s, ", R(rd + 1));
		o.Add("[%s]", R(rn));
	} else {
		o.Add("STREX%s%s %s, %s, ", sizes[(op >> 1) & 3], cond, R(rd), R(rt));
		if (op == 0xA)
			o.Add("%s, ", R(rt + 1));
		o.Add("[%s]", R(rn));
	}
	return true;
}

bool DisExtraLoadStore(u32 w, Out &o) {
	bool pre = Bit(w, 24), up = Bit(w, 23), writeback = Bit(w, 21), load = Bit(w, 20);
	int rn = Bits(w, 19, 16);
	int rt = Bits(w, 15, 12);
	const char *name = nullptr;
	bool dual = false;
	switch (Bits(w, 6, 5)) {
	case 1: name = load ? "LDRH" : "STRH"; break;
	case 2: name = load ? "LDRSB" : "LDRD"; dual = !load; break;
	case 3: name = load ? "LDRSH" : "STRD"; dual = !load; break;
	}
	bool unprivileged = !pre && writeback;
	if (unprivileged && dual)
		return false;

	char offset[32];
	if (Bit(w, 22)) {
		int imm = (Bits(w, 11, 8) << 4) | Bits(w, 3, 0);
		if (pre && up && imm == 0)
			offset[0] = 0;
		else
			snprintf(offset, sizeof(offset), "#%s%d", up ? "" : "-", imm);
	} else {
		snprintf(offset, sizeof(offset), "%s%s", up ? "" : "-", R(Bits(w, 3, 0)));
	}
	o.Add("%s%s%s %s, ", name, unprivileged ? "T" : "", Cond(w), R(rt));
	if (dual)
		o.Add("%s, ", R(rt + 1));
	AddAddress(o, rn, pre, writeback, offset);
	return true;
}

bool DisLoadStore(u32 w, Out &o) {
	bool pre = Bit(w, 24), up = Bit(w, 23), byte = Bit(w, 22), writeback = Bit(w, 21), load = Bit(w, 20);
	int rn = Bits(w, 19, 16);
	int rt = Bits(w, 15, 12);
	bool unprivileged = !pre && writeback;

	char offset[48];
	if (!Bit(w, 25)) {
		int imm = w & 0xFFF;
		if (pre && up && imm == 0)
			offset[0] = 0;
		else
			snprintf(offset, sizeof(offset), "#%s%d", up ? "" : "-", imm);
	} else {
		int type = Bits(w, 6, 5);
		int imm5 = Bits(w, 11, 7);
		int len = snprintf(offset, sizeof(offset), "%s%s", up ? "" : "-", R(Bits(w, 3, 0)));
		if (type == 3 && imm5 == 0)
			snprintf(offset + len, sizeof(offset) - len, ", rrx");
		else if (type != 0 || imm5 != 0)
			snprintf(offset + len, sizeof(offset) - len, ", %s #%d", shiftNames[type], imm5 ? imm5 : 32);
	}
	o.Add("%s%s%s%s %s, ", load ? "LDR" : "STR", byte ? "B" : "", unprivileged ? "T" : "", Cond(w), R(rt));
	AddAddress(o, rn, pre, writeback, offset);
	return true;
}

// MRS, MSR (register), BX, BLX, CLZ, BKPT, QADD and friends.
bool DisMisc(u32 w, Out &o) {
	int op = Bits(w, 22, 21);
	const char *cond = Cond(w);
	switch (Bits(w, 6, 4)) {
	case 0:
		if (Bit(w, 9))
			return false;
		if (!(op & 1)) {
			o.Add("MRS%s %s, %s", cond, R(Bits(w, 15, 12)), Bit(w, 22) ? "spsr" : "apsr");
		} else {
			int mask = Bits(w, 19, 16);
			if (op == 1 && (mask & 3) == 0) {
				if (mask == 0)
					return false;
				o.Add("MSR%s APSR_%s%s, %s", cond, (mask & 8) ? "nzcvq" : "", (mask & 4) ? "g" : "", R(Bits(w, 3, 0)));
			} else {
				o.Add("MSR%s %s_%s%s%s%s, %s", cond, Bit(w, 22) ? "SPSR" : "CPSR",
					(mask & 8) ? "f" : "", (mask & 4) ? "s" : "", (mask & 2) ? "x" : "", (mask & 1) ? "c" : "", R(Bits(w, 3, 0)));
			}
		}
		return true;
	case 1:
		if (op == 1)
			o.Add("BX%s %s", cond, R(Bits(w, 3, 0)));
		else if (op == 3)
			o.Add("CLZ%s %s, %s", cond, R(Bits(w, 15, 12)), R(Bits(w, 3, 0)));
		else
			return false;
		return true;
	case 2:
		if (op != 1)
			return false;
		o.Add("BXJ%s %s", cond, R(Bits(w, 3, 0)));
		return true;
	case 3:
		if (op != 1)
			return false;
		o.Add("BLX%s %s", cond, R(Bits(w, 3, 0)));
		return true;
	case 5:
		{
			static const char *const names[4] = { "QADD", "QSUB", "QDADD", "QDSUB" };
			o.Add("%s%s %s, %s, %s", names[op], cond, R(Bits(w, 15, 12)), R(Bits(w, 3, 0)), R(Bits(w, 19, 16)));
			return true;
		}
	case 7:
		if (op == 1)
			o.Add("BKPT #%d", (Bits(w, 19, 8) << 4) | Bits(w, 3, 0));
		else if (op == 3)
			o.Add("SMC%s #%d", cond, Bits(w, 3, 0));
		else
			return false;
		return true;
	default:
		return false;
	}
}

// SMLA<x><y> and the other 16-bit multiplies.
bool DisHalfwordMultiply(u32 w, Out &o) {
	int rd = Bits(w, 19, 16);
	int ra = Bits(w, 15, 12);
	int rm = Bits(w, 11, 8);
	int rn = Bits(w, 3, 0);
	char x = Bit(w, 5) ? 'T' : 'B';
	char y = Bit(w, 6) ? 'T' : 'B';
	const char *cond = Cond(w);
	switch (Bits(w, 22, 21)) {
	case 0:
		o.Add("SMLA%c%c%s %s, %s, %s, %s", x, y, cond, R(rd), R(rn), R(rm), R(ra));
		break;
	case 1:
		if (Bit(w, 5))
			o.Add("SMULW%c%s %s, %s, %s", y, cond, R(rd), R(rn), R(rm));
		else
			o.Add("SMLAW%c%s %s, %s, %s, %s", y, cond, R(rd), R(rn), R(rm), R(ra));
		break;
	case 2:
		o.Add("SMLAL%c%c%s %s, %s, %s, %s", x, y, cond, R(ra), R(rd), R(rn), R(rm));
		break;
	case 3:
		o.Add("SMUL%c%c%s %s, %s, %s", x, y, cond, R(rd), R(rn), R(rm));
		break;
	}
	return true;
}

// MOVW, MOVT, MSR (immediate) and the hints.
bool DisMovImmAndHints(u32 w, Out &o) {
	const char *cond = Cond(w);
	int rd = Bits(w, 15, 12);
	switch (Bits(w, 22, 21)) {
	case 0:
		o.Add("MOVW%s %s, #0x%x", cond, R(rd), (Bits(w, 19, 16) << 12) | Bits(w, 11, 0));
		return true;
	case 2:
		o.Add("MOVT%s %s, #0x%x", cond, R(rd), (Bits(w, 19, 16) << 12) | Bits(w, 11, 0));
		return true;
	default:
		break;
	}

	int mask = Bits(w, 19, 16);
	if (!Bit(w, 22) && mask == 0) {
		static const char *const hints[5] = { "NOP", "YIELD", "WFE", "WFI", "SEV" };
		int hint = Bits(w, 7, 0);
		if (hint < 5)
			o.Add("%s%s", hints[hint], cond);
		else if (hint >= 0xF0)
			o.Add("DBG%s #%d", cond, hint & 15);
		else
			o.Add("HINT%s #%d", cond, hint);
		return true;
	}

	u32 imm = RotR(w & 0xFF, 2 * Bits(w, 11, 8));
	if (!Bit(w, 22) && (mask & 3) == 0) {
		o.Add("MSR%s APSR_%s%s, ", cond, (mask & 8) ? "nzcvq" : "", (mask & 4) ? "g" : "");
	} else {
		o.Add("MSR%s %s_%s%s%s%s, ", cond, Bit(w, 22) ? "SPSR" : "CPSR",
			(mask & 8) ? "f" : "", (mask & 4) ? "s" : "", (mask & 2) ? "x" : "", (mask & 1) ? "c" : "");
	}
	AddImm(o, imm);
	return true;
}

bool DisMedia(u32 w, Out &o) {
	int op1 = Bits(w, 24, 20);
	int op2 = Bits(w, 7, 5);
	int rn = Bits(w, 19, 16);
	int rd = Bits(w, 15, 12);
	int rs = Bits(w, 11, 8);
	int rm = Bits(w, 3, 0);
	const char *cond = Cond(w);

	if ((op1 & 0x18) == 0) {
		// Parallel add and subtract.
		static const char *const prefixes[2][4] = { { nullptr, "S", "Q", "SH" }, { nullptr, "U", "UQ", "UH" } };
		static const char *const ops[8] = { "ADD16", "ASX", "SAX", "SUB16", "ADD8", nullptr, nullptr, "SUB8" };
		const char *prefix = prefixes[Bit(w, 22)][op1 & 3];
		if (!prefix || !ops[op2] || Bit(w, 23))
			return false;
		o.Add("%s%s%s %s, %s, %s", prefix, ops[op2], cond, R(rd), R(rn), R(rm));
		return true;
	}

	if ((op1 & 0x18) == 0x08) {
		// Packing, unpacking, saturation and reversal.
		int sub = op1 & 7;
		int imm5 = Bits(w, 11, 7);
		if (sub == 0 && (op2 & 1) == 0) {
			bool tb = Bit(w, 6);
			o.Add("PKH%s%s %s, %s, %s", tb ? "TB" : "BT", cond, R(rd), R(rn), R(rm));
			if (tb || imm5)
				o.Add(", %s #%d", tb ? "asr" : "lsl", (tb && imm5 == 0) ? 32 : imm5);
			return true;
		}
		if ((sub & 2) && (op2 & 1) == 0) {
			bool isUnsigned = (sub & 4) != 0;
			int sat = Bits(w, 20, 16) + (isUnsigned ? 0 : 1);
			o.Add("%s%s %s, #%d, %s", isUnsigned ? "USAT" : "SSAT", cond, R(rd), sat, R(rm));
			if (Bit(w, 6))
				o.Add(", asr #%d", imm5 ? imm5 : 32);
			else if (imm5)
				o.Add(", lsl #%d", imm5);
			return true;
		}
		if (op2 == 3) {
			static const char *const names[8] = { "SXTAB16", nullptr, "SXTAB", "SXTAH", "UXTAB16", nullptr, "UXTAB", "UXTAH" };
			const char *name = names[sub];
			if (!name)
				return false;
			if (rn == 15)
				o.Add("%.3s%s%s %s, %s", name, name + 4, cond, R(rd), R(rm));
			else
				o.Add("%s%s %s, %s, %s", name, cond, R(rd), R(rn), R(rm));
			int rot = Bits(w, 11, 10);
			if (rot)
				o.Add(", ror #%d", rot * 8);
			return true;
		}
		if (sub == 0 && op2 == 5) {
			o.Add("SEL%s %s, %s, %s", cond, R(rd), R(rn), R(rm));
			return true;
		}
		if ((sub == 2 || sub == 6) && op2 == 1) {
			bool isUnsigned = sub == 6;
			o.Add("%s%s %s, #%d, %s", isUnsigned ? "USAT16" : "SSAT16", cond, R(rd), Bits(w, 19, 16) + (isUnsigned ? 0 : 1), R(rm));
			return true;
		}
		const char *name = nullptr;
		if (sub == 3 && op2 == 1)
			name = "REV";
		else if (sub == 3 && op2 == 5)
			name = "REV16";
		else if (sub == 7 && op2 == 1)
			name = "RBIT";
		else if (sub == 7 && op2 == 5)
			name = "REVSH";
		if (!name)
			return false;
		o.Add("%s%s %s, %s", name, cond, R(rd), R(rm));
		return true;
	}

	if ((op1 & 0x18) == 0x10) {
		// Signed multiplies and divides. Rd is in 19:16 here, Ra in 15:12.
		int sub = op1 & 7;
		const char *x = (op2 & 1) ? "X" : "";
		if (sub == 0 && op2 < 4) {
			const char *name = op2 < 2 ? (rd == 15 ? "SMUAD" : "SMLAD") : (rd == 15 ? "SMUSD" : "SMLSD");
			o.Add("%s%s%s %s, %s, %s", name, x, cond, R(rn), R(rm), R(rs));
			if (rd != 15)
				o.Add(", %s", R(rd));
			return true;
		}
		if ((sub == 1 || sub == 3) && op2 == 0) {
			o.Add("%s%s %s, %s, %s", sub == 1 ? "SDIV" : "UDIV", cond, R(rn), R(rm), R(rs));
			return true;
		}
		if (sub == 4 && op2 < 4) {
			o.Add("%s%s%s %s, %s, %s, %s", op2 < 2 ? "SMLALD" : "SMLSLD", x, cond, R(rd), R(rn), R(rm), R(rs));
			return true;
		}
		if (sub == 5 && (op2 < 2 || op2 >= 6)) {
			const char *name = op2 >= 6 ? "SMMLS" : (rd == 15 ? "SMMUL" : "SMMLA");
			o.Add("%s%s%s %s, %s, %s", name, x, cond, R(rn), R(rm), R(rs));
			if (rd != 15 || op2 >= 6)
				o.Add(", %s", R(rd));
			return true;
		}
		return false;
	}

	if (op1 == 0x18 && op2 == 0) {
		if (rd == 15)
			o.Add("USAD8%s %s, %s, %s", cond, R(rn), R(rm), R(rs));
		else
			o.Add("USADA8%s %s, %s, %s, %s", cond, R(rn), R(rm), R(rs), R(rd));
		return true;
	}

	int lsb = Bits(w, 11, 7);
	int msb = Bits(w, 20, 16);
	if ((op1 & 0x1E) == 0x1A && (op2 & 3) == 2) {
		o.Add("SBFX%s %s, %s, #%d, #%d", cond, R(rd), R(rm), lsb, msb + 1);
		return true;
	}
	if ((op1 & 0x1E) == 0x1E && (op2 & 3) == 2) {
		o.Add("UBFX%s %s, %s, #%d, #%d", cond, R(rd), R(rm), lsb, msb + 1);
		return true;
	}
	if ((op1 & 0x1E) == 0x1C && (op2 & 3) == 0) {
		if (msb < lsb)
			return false;
		if (rm == 15)
			o.Add("BFC%s %s, #%d, #%d", cond, R(rd), lsb, msb - lsb + 1);
		else
			o.Add("BFI%s %s, %s, #%d, #%d", cond, R(rd), R(rm), lsb, msb - lsb + 1);
		return true;
	}
	if (op1 == 0x1F && op2 == 7) {
		o.Add("UDF #%d", (Bits(w, 19, 8) << 4) | Bits(w, 3, 0));
		return true;
	}
	return false;
}

bool DisBlockTransfer(u32 w, Out &o) {
	bool pre = Bit(w, 24), up = Bit(w, 23), user = Bit(w, 22), writeback = Bit(w, 21), load = Bit(w, 20);
	int rn = Bits(w, 19, 16);
	u32 list = w & 0xFFFF;
	int count = 0;
	for (int i = 0; i < 16; i++)
		count += (list >> i) & 1;

	bool stack = load ? (!pre && up) : (pre && !up);
	if (rn == 13 && writeback && !user && stack && count >= 2) {
		o.Add("%s%s ", load ? "POP" : "PUSH", Cond(w));
	} else {
		static const char *const modes[2][2] = { { "DA", "IA" }, { "DB", "IB" } };
		o.Add("%s%s%s %s%s, ", load ? "LDM" : "STM", modes[pre][up], Cond(w), R(rn), writeback ? "!" : "");
	}
	AddRegList(o, list);
	if (user)
		o.Add("^");
	return true;
}

// VFP

int SReg(int hi4, int lo1) {
	return (hi4 << 1) | lo1;
}

int DReg(int hi1, int lo4) {
	return (hi1 << 4) | lo4;
}

// A VFP register from a 4-bit field and the extra bit, as S or D depending on dbl.
int VfpReg(bool dbl, int field, int extra) {
	return dbl ? DReg(extra, field) : SReg(field, extra);
}

bool DisVfpLoadStore(u32 w, Out &o) {
	bool pre = Bit(w, 24), up = Bit(w, 23), writeback = Bit(w, 21), load = Bit(w, 20);
	bool dbl = Bit(w, 8);
	int rn = Bits(w, 19, 16);
	int imm8 = Bits(w, 7, 0);
	char t = dbl ? 'd' : 's';
	int vd = VfpReg(dbl, Bits(w, 15, 12), Bit(w, 22));
	const char *cond = Cond(w);

	if (pre && !writeback) {
		o.Add("%s%s %c%d, ", load ? "VLDR" : "VSTR", cond, t, vd);
		int offset = imm8 * 4;
		if (up && offset == 0)
			o.Add("[%s]", R(rn));
		else
			o.Add("[%s, #%s%d]", R(rn), up ? "" : "-", offset);
		return true;
	}
	if (pre == up || (dbl && (imm8 & 1)) || imm8 == 0)
		return false;

	int count = dbl ? imm8 / 2 : imm8;
	bool stack = load ? (!pre && up) : (pre && !up);
	if (rn == 13 && writeback && stack) {
		o.Add("%s%s ", load ? "VPOP" : "VPUSH", cond);
	} else {
		o.Add("%s%s%s %s%s, ", load ? "VLDM" : "VSTM", pre ? "DB" : "IA", cond, R(rn), writeback ? "!" : "");
	}
	if (count == 1)
		o.Add("{%c%d}", t, vd);
	else
		o.Add("{%c%d-%c%d}", t, vd, t, vd + count - 1);
	return true;
}

bool DisVfp64Transfer(u32 w, Out &o) {
	if (Bits(w, 7, 6) != 0 || !Bit(w, 4))
		return false;
	bool toCore = Bit(w, 20);
	int rt = Bits(w, 15, 12);
	int rt2 = Bits(w, 19, 16);
	const char *cond = Cond(w);
	if (Bit(w, 8)) {
		int dm = DReg(Bit(w, 5), Bits(w, 3, 0));
		if (toCore)
			o.Add("VMOV%s %s, %s, d%d", cond, R(rt), R(rt2), dm);
		else
			o.Add("VMOV%s d%d, %s, %s", cond, dm, R(rt), R(rt2));
	} else {
		int sm = SReg(Bits(w, 3, 0), Bit(w, 5));
		if (toCore)
			o.Add("VMOV%s %s, %s, s%d, s%d", cond, R(rt), R(rt2), sm, sm + 1);
		else
			o.Add("VMOV%s s%d, s%d, %s, %s", cond, sm, sm + 1, R(rt), R(rt2));
	}
	return true;
}

// Size and index of a scalar from VMOV's opc1:opc2 field.
bool DecodeScalarIndex(int opc1, int opc2, int *size, int *index) {
	if (opc1 & 2) {
		*size = 8;
		*index = ((opc1 & 1) << 2) | opc2;
	} else if (opc2 & 1) {
		*size = 16;
		*index = ((opc1 & 1) << 1) | (opc2 >> 1);
	} else if (opc2 == 0) {
		*size = 32;
		*index = opc1 & 1;
	} else {
		return false;
	}
	return true;
}

const char *VfpSysRegName(int reg) {
	switch (reg) {
	case 0: return "fpsid";
	case 1: return "fpscr";
	case 6: return "mvfr1";
	case 7: return "mvfr0";
	case 8: return "fpexc";
	default: return nullptr;
	}
}

// Moves between a core register and an S register, a scalar or a system register, and VDUP.
bool DisVfpTransfer(u32 w, Out &o) {
	bool load = Bit(w, 20);
	int a = Bits(w, 23, 21);
	int rt = Bits(w, 15, 12);
	const char *cond = Cond(w);
	if (Bits(w, 3, 0) != 0)
		return false;

	if (!Bit(w, 8)) {
		if (a == 0) {
			int sn = SReg(Bits(w, 19, 16), Bit(w, 7));
			if (load)
				o.Add("VMOV%s %s, s%d", cond, R(rt), sn);
			else
				o.Add("VMOV%s s%d, %s", cond, sn, R(rt));
			return true;
		} else if (a == 7) {
			const char *reg = VfpSysRegName(Bits(w, 19, 16));
			if (!reg)
				return false;
			if (!load)
				o.Add("VMSR%s %s, %s", cond, reg, R(rt));
			else if (rt == 15)
				o.Add("VMRS%s APSR_nzcv, %s", cond, reg);
			else
				o.Add("VMRS%s %s, %s", cond, R(rt), reg);
			return true;
		}
		return false;
	}

	int dn = DReg(Bit(w, 7), Bits(w, 19, 16));
	if (!load && (a & 4)) {
		if (Bit(w, 6))
			return false;
		int be = (Bit(w, 22) << 1) | Bit(w, 5);
		if (be == 3)
			return false;
		static const int sizes[3] = { 32, 16, 8 };
		bool q = Bit(w, 21);
		if (q)
			o.Add("VDUP%s.%d q%d, %s", cond, sizes[be], dn >> 1, R(rt));
		else
			o.Add("VDUP%s.%d d%d, %s", cond, sizes[be], dn, R(rt));
		return true;
	}

	int size, index;
	if (!DecodeScalarIndex(Bits(w, 22, 21), Bits(w, 6, 5), &size, &index))
		return false;
	if (!load) {
		o.Add("VMOV%s.%d d%d[%d], %s", cond, size, dn, index, R(rt));
	} else {
		bool isUnsigned = Bit(w, 23);
		if (size == 32 && isUnsigned)
			return false;
		if (size == 32)
			o.Add("VMOV%s.32 %s, d%d[%d]", cond, R(rt), dn, index);
		else
			o.Add("VMOV%s.%c%d %s, d%d[%d]", cond, isUnsigned ? 'u' : 's', size, R(rt), dn, index);
	}
	return true;
}

bool DisVfpData(u32 w, Out &o) {
	bool dbl = Bit(w, 8);
	char t = dbl ? 'd' : 's';
	const char *dt = dbl ? ".f64" : ".f32";
	const char *cond = Cond(w);
	int vd = VfpReg(dbl, Bits(w, 15, 12), Bit(w, 22));
	int vn = VfpReg(dbl, Bits(w, 19, 16), Bit(w, 7));
	int vm = VfpReg(dbl, Bits(w, 3, 0), Bit(w, 5));
	bool op6 = Bit(w, 6);

	int opc1 = (Bit(w, 23) << 2) | Bits(w, 21, 20);
	if (opc1 != 7) {
		static const char *const names[7][2] = {
			{ "VMLA", "VMLS" }, { "VNMLS", "VNMLA" }, { "VMUL", "VNMUL" }, { "VADD", "VSUB" },
			{ "VDIV", nullptr }, { "VFNMS", "VFNMA" }, { "VFMA", "VFMS" },
		};
		const char *name = names[opc1][op6];
		if (!name)
			return false;
		o.Add("%s%s%s %c%d, %c%d, %c%d", name, cond, dt, t, vd, t, vn, t, vm);
		return true;
	}

	if (!op6) {
		if (Bits(w, 7, 4) != 0)
			return false;
		o.Add("VMOV%s%s %c%d, ", cond, dt, t, vd);
		AddFloat(o, ExpandFloatImm((Bits(w, 19, 16) << 4) | Bits(w, 3, 0)));
		return true;
	}

	bool op7 = Bit(w, 7);
	int sd = SReg(Bits(w, 15, 12), Bit(w, 22));
	int sm = SReg(Bits(w, 3, 0), Bit(w, 5));
	int dd = DReg(Bit(w, 22), Bits(w, 15, 12));
	int dm = DReg(Bit(w, 5), Bits(w, 3, 0));
	int opc2 = Bits(w, 19, 16);
	switch (opc2) {
	case 0:
		o.Add("%s%s%s %c%d, %c%d", op7 ? "VABS" : "VMOV", cond, dt, t, vd, t, vm);
		return true;
	case 1:
		o.Add("%s%s%s %c%d, %c%d", op7 ? "VSQRT" : "VNEG", cond, dt, t, vd, t, vm);
		return true;
	case 2:
	case 3:
		if (dbl)
			return false;
		o.Add("VCVT%c%s%s s%d, s%d", op7 ? 'T' : 'B', cond, (opc2 & 1) ? ".f16.f32" : ".f32.f16", sd, sm);
		return true;
	case 4:
		o.Add("%s%s%s %c%d, %c%d", op7 ? "VCMPE" : "VCMP", cond, dt, t, vd, t, vm);
		return true;
	case 5:
		if (Bits(w, 5, 0) != 0)
			return false;
		o.Add("%s%s%s %c%d, #0.0", op7 ? "VCMPE" : "VCMP", cond, dt, t, vd);
		return true;
	case 7:
		if (!op7)
			return false;
		if (dbl)
			o.Add("VCVT%s.f32.f64 s%d, d%d", cond, sd, dm);
		else
			o.Add("VCVT%s.f64.f32 d%d, s%d", cond, dd, sm);
		return true;
	case 8:
		o.Add("VCVT%s%s.%c32 %c%d, s%d", cond, dt, op7 ? 's' : 'u', t, vd, sm);
		return true;
	case 12:
	case 13:
		o.Add("VCVT%s%s.%c32%s s%d, %c%d", op7 ? "" : "R", cond, (opc2 & 1) ? 's' : 'u', dt, sd, t, vm);
		return true;
	case 10:
	case 11:
	case 14:
	case 15:
		{
			// Fixed point, in place.
			int size = op7 ? 32 : 16;
			int imm = SReg(Bits(w, 3, 0), Bit(w, 5));
			char fx = Bit(w, 16) ? 'u' : 's';
			if (Bit(w, 18))
				o.Add("VCVT%s.%c%d%s %c%d, %c%d, #%d", cond, fx, size, dt, t, vd, t, vd, size - imm);
			else
				o.Add("VCVT%s%s.%c%d %c%d, %c%d, #%d", cond, dt, fx, size, t, vd, t, vd, size - imm);
			return true;
		}
	default:
		return false;
	}
}

bool DisCoprocessor(u32 w, Out &o) {
	if (Bits(w, 27, 24) == 0xF) {
		o.Add("SVC%s #0x%x", Cond(w), Bits(w, 23, 0));
		return true;
	}
	if ((Bits(w, 11, 8) & 0xE) != 0xA)
		return false;
	if (Bits(w, 27, 25) == 6) {
		if (Bits(w, 24, 21) == 2)
			return DisVfp64Transfer(w, o);
		if (Bits(w, 24, 21) == 0)
			return false;
		return DisVfpLoadStore(w, o);
	}
	return Bit(w, 4) ? DisVfpTransfer(w, o) : DisVfpData(w, o);
}

// NEON

int NeonD(u32 w) {
	return DReg(Bit(w, 22), Bits(w, 15, 12));
}

int NeonN(u32 w) {
	return DReg(Bit(w, 7), Bits(w, 19, 16));
}

int NeonM(u32 w) {
	return DReg(Bit(w, 5), Bits(w, 3, 0));
}

// Register text for a D register number, printed as Q if q is set.
struct V {
	V(bool q, int d) {
		snprintf(s, sizeof(s), q ? "q%d" : "d%d", q ? d >> 1 : d);
	}
	char s[8];
};

enum class Dt {
	NONE,
	SU,  // .s or .u depending on U
	I,
	S,
	U,
	F,
	P,
	PLAIN,  // just the size: .8
};

void AddDt(Out &o, Dt dt, bool u, int bits) {
	switch (dt) {
	case Dt::NONE: break;
	case Dt::SU: o.Add(".%c%d", u ? 'u' : 's', bits); break;
	case Dt::I: o.Add(".i%d", bits); break;
	case Dt::S: o.Add(".s%d", bits); break;
	case Dt::U: o.Add(".u%d", bits); break;
	case Dt::F: o.Add(".f32"); break;
	case Dt::P: o.Add(".p%d", bits); break;
	case Dt::PLAIN: o.Add(".%d", bits); break;
	}
}

bool DisNeon3Same(u32 w, Out &o) {
	int a = Bits(w, 11, 8);
	bool b = Bit(w, 4);
	bool u = Bit(w, 24);
	int c = Bits(w, 21, 20);
	bool q = Bit(w, 6);
	const char *name = nullptr;
	Dt dt = Dt::SU;
	bool shiftOrder = false;
	bool pairwise = false;
	bool fsz = (c & 1) == 0;  // the float ops only exist with sz == 0
	bool cHi = (c & 2) != 0;

	switch (a) {
	case 0: name = b ? "VQADD" : "VHADD"; break;
	case 1:
		if (!b) {
			name = "VRHADD";
		} else {
			static const char *const logic[2][4] = { { "VAND", "VBIC", "VORR", "VORN" }, { "VEOR", "VBSL", "VBIT", "VBIF" } };
			name = logic[u][c];
			dt = Dt::NONE;
			if (!u && c == 2 && NeonN(w) == NeonM(w)) {
				o.Add("VMOV %s, %s", V(q, NeonD(w)).s, V(q, NeonM(w)).s);
				return true;
			}
		}
		break;
	case 2: name = b ? "VQSUB" : "VHSUB"; break;
	case 3: name = b ? "VCGE" : "VCGT"; break;
	case 4: name = b ? "VQSHL" : "VSHL"; shiftOrder = true; break;
	case 5: name = b ? "VQRSHL" : "VRSHL"; shiftOrder = true; break;
	case 6: name = b ? "VMIN" : "VMAX"; break;
	case 7: name = b ? "VABA" : "VABD"; break;
	case 8:
		name = b ? (u ? "VCEQ" : "VTST") : (u ? "VSUB" : "VADD");
		dt = (b && !u) ? Dt::PLAIN : Dt::I;
		break;
	case 9:
		if (!b) {
			name = u ? "VMLS" : "VMLA";
			dt = Dt::I;
		} else {
			name = "VMUL";
			dt = u ? Dt::P : Dt::I;
		}
		break;
	case 10: name = b ? "VPMIN" : "VPMAX"; pairwise = true; break;
	case 11:
		if (!b) {
			name = u ? "VQRDMULH" : "VQDMULH";
			dt = Dt::S;
		} else if (!u) {
			name = "VPADD";
			dt = Dt::I;
			pairwise = true;
		}
		break;
	case 12:
		if (b && !u && fsz)
			name = cHi ? "VFMS" : "VFMA";
		dt = Dt::F;
		break;
	case 13:
		if (!b) {
			name = u ? (cHi ? "VABD" : "VPADD") : (cHi ? "VSUB" : "VADD");
			pairwise = u && !cHi;
		} else if (!u) {
			name = cHi ? "VMLS" : "VMLA";
		} else if (!cHi) {
			name = "VMUL";
		}
		if (!fsz)
			name = nullptr;
		dt = Dt::F;
		break;
	case 14:
		if (!b) {
			name = u ? (cHi ? "VCGT" : "VCGE") : (cHi ? nullptr : "VCEQ");
		} else if (u) {
			name = cHi ? "VACGT" : "VACGE";
		}
		if (!fsz)
			name = nullptr;
		dt = Dt::F;
		break;
	case 15:
		if (!b) {
			name = cHi ? (u ? "VPMIN" : "VMIN") : (u ? "VPMAX" : "VMAX");
			pairwise = u;
		} else if (!u) {
			name = cHi ? "VRSQRTS" : "VRECPS";
		}
		if (!fsz)
			name = nullptr;
		dt = Dt::F;
		break;
	}
	if (!name || (pairwise && q))
		return false;
	if (c == 3 && dt != Dt::NONE && dt != Dt::F && a != 8 && a != 0 && a != 2 && a != 4 && a != 5)
		return false;

	o.Add("%s", name);
	AddDt(o, dt, u, 8 << c);
	if (shiftOrder)
		o.Add(" %s, %s, %s", V(q, NeonD(w)).s, V(q, NeonM(w)).s, V(q, NeonN(w)).s);
	else
		o.Add(" %s, %s, %s", V(q, NeonD(w)).s, V(q, NeonN(w)).s, V(q, NeonM(w)).s);
	return true;
}

bool DisNeon3Diff(u32 w, Out &o) {
	int a = Bits(w, 11, 8);
	bool u = Bit(w, 24);
	int size = Bits(w, 21, 20);
	enum Shape { LONG, WIDE, NARROW } shape = LONG;
	const char *name = nullptr;
	Dt dt = Dt::SU;
	int bits = 8 << size;
	switch (a) {
	case 0: name = "VADDL"; break;
	case 1: name = "VADDW"; shape = WIDE; break;
	case 2: name = "VSUBL"; break;
	case 3: name = "VSUBW"; shape = WIDE; break;
	case 4: name = u ? "VRADDHN" : "VADDHN"; shape = NARROW; dt = Dt::I; bits *= 2; break;
	case 5: name = "VABAL"; break;
	case 6: name = u ? "VRSUBHN" : "VSUBHN"; shape = NARROW; dt = Dt::I; bits *= 2; break;
	case 7: name = "VABDL"; break;
	case 8: name = "VMLAL"; break;
	case 9: name = u ? nullptr : "VQDMLAL"; dt = Dt::S; break;
	case 10: name = "VMLSL"; break;
	case 11: name = u ? nullptr : "VQDMLSL"; dt = Dt::S; break;
	case 12: name = "VMULL"; break;
	case 13: name = u ? nullptr : "VQDMULL"; dt = Dt::S; break;
	case 14: name = u ? nullptr : "VMULL"; dt = Dt::P; break;
	}
	if (!name)
		return false;
	o.Add("%s", name);
	AddDt(o, dt, u, bits);
	int d = NeonD(w), n = NeonN(w), m = NeonM(w);
	switch (shape) {
	case LONG: o.Add(" q%d, d%d, d%d", d >> 1, n, m); break;
	case WIDE: o.Add(" q%d, q%d, d%d", d >> 1, n >> 1, m); break;
	case NARROW: o.Add(" d%d, q%d, q%d", d, n >> 1, m >> 1); break;
	}
	return true;
}

bool DisNeonScalar(u32 w, Out &o) {
	int a = Bits(w, 11, 8);
	bool u = Bit(w, 24);
	int size = Bits(w, 21, 20);
	if (size == 0)
		return false;
	int bits = 8 << size;
	const char *name = nullptr;
	Dt dt = Dt::I;
	bool longOp = false;
	switch (a) {
	case 0: name = "VMLA"; break;
	case 1: name = "VMLA"; dt = Dt::F; break;
	case 2: name = "VMLAL"; dt = Dt::SU; longOp = true; break;
	case 3: name = u ? nullptr : "VQDMLAL"; dt = Dt::S; longOp = true; break;
	case 4: name = "VMLS"; break;
	case 5: name = "VMLS"; dt = Dt::F; break;
	case 6: name = "VMLSL"; dt = Dt::SU; longOp = true; break;
	case 7: name = u ? nullptr : "VQDMLSL"; dt = Dt::S; longOp = true; break;
	case 8: name = "VMUL"; break;
	case 9: name = "VMUL"; dt = Dt::F; break;
	case 10: name = "VMULL"; dt = Dt::SU; longOp = true; break;
	case 11: name = u ? nullptr : "VQDMULL"; dt = Dt::S; longOp = true; break;
	case 12: name = "VQDMULH"; dt = Dt::S; break;
	case 13: name = "VQRDMULH"; dt = Dt::S; break;
	}
	if (!name || (dt == Dt::F && size != 2))
		return false;

	int dm, index;
	if (size == 1) {
		dm = Bits(w, 2, 0);
		index = (Bit(w, 5) << 1) | Bit(w, 3);
	} else {
		dm = Bits(w, 3, 0);
		index = Bit(w, 5);
	}
	o.Add("%s", name);
	AddDt(o, dt, u, bits);
	int d = NeonD(w), n = NeonN(w);
	if (longOp)
		o.Add(" q%d, d%d, d%d[%d]", d >> 1, n, dm, index);
	else
		o.Add(" %s, %s, d%d[%d]", V(u, d).s, V(u, n).s, dm, index);
	return true;
}

bool DisNeonShift(u32 w, Out &o) {
	int a = Bits(w, 11, 8);
	bool u = Bit(w, 24);
	bool l = Bit(w, 7);
	bool q = Bit(w, 6);
	int imm6 = Bits(w, 21, 16);
	int esize;
	if (l)
		esize = 64;
	else if (imm6 & 0x20)
		esize = 32;
	else if (imm6 & 0x10)
		esize = 16;
	else
		esize = 8;
	int rightShift = (l ? 64 : 2 * esize) - imm6;
	int leftShift = l ? imm6 : imm6 - esize;
	int d = NeonD(w), m = NeonM(w);

	const char *name = nullptr;
	Dt dt = Dt::SU;
	int shift = rightShift;
	switch (a) {
	case 0: name = "VSHR"; break;
	case 1: name = "VSRA"; break;
	case 2: name = "VRSHR"; break;
	case 3: name = "VRSRA"; break;
	case 4: name = u ? "VSRI" : nullptr; dt = Dt::PLAIN; break;
	case 5: name = u ? "VSLI" : "VSHL"; dt = u ? Dt::PLAIN : Dt::I; shift = leftShift; break;
	case 6: name = u ? "VQSHLU" : nullptr; dt = Dt::S; shift = leftShift; break;
	case 7: name = "VQSHL"; shift = leftShift; break;
	case 8:
	case 9:
		{
			if (l)
				return false;
			bool round = q;
			if (a == 8 && !u) {
				name = round ? "VRSHRN" : "VSHRN";
				dt = Dt::I;
			} else if (a == 8) {
				name = round ? "VQRSHRUN" : "VQSHRUN";
				dt = Dt::S;
			} else {
				name = round ? "VQRSHRN" : "VQSHRN";
			}
			o.Add("%s", name);
			AddDt(o, dt, u, esize * 2);
			o.Add(" d%d, q%d, #%d", d, m >> 1, rightShift);
			return true;
		}
	case 10:
		if (l || q)
			return false;
		if (leftShift == 0) {
			o.Add("VMOVL.%c%d q%d, d%d", u ? 'u' : 's', esize, d >> 1, m);
		} else {
			o.Add("VSHLL.%c%d q%d, d%d, #%d", u ? 'u' : 's', esize, d >> 1, m, leftShift);
		}
		return true;
	case 14:
	case 15:
		{
			if (l || !(imm6 & 0x20))
				return false;
			char sign = u ? 'u' : 's';
			if (Bit(w, 8))
				o.Add("VCVT.%c32.f32", sign);
			else
				o.Add("VCVT.f32.%c32", sign);
			o.Add(" %s, %s, #%d", V(q, d).s, V(q, m).s, 64 - imm6);
			return true;
		}
	}
	if (!name)
		return false;
	o.Add("%s", name);
	AddDt(o, dt, u, esize);
	o.Add(" %s, %s, #%d", V(q, d).s, V(q, m).s, shift);
	return true;
}

bool DisNeonModImm(u32 w, Out &o) {
	bool op = Bit(w, 5);
	bool q = Bit(w, 6);
	int cmode = Bits(w, 11, 8);
	u32 imm8 = (Bit(w, 24) << 7) | (Bits(w, 18, 16) << 4) | Bits(w, 3, 0);
	const char *name;
	char dt[8];
	unsigned long long value = imm8;
	bool isFloat = false;

	if (cmode < 8) {
		// 32-bit, shifted by 0/8/16/24.
		bool orr = cmode & 1;
		name = op ? (orr ? "VBIC" : "VMVN") : (orr ? "VORR" : "VMOV");
		strcpy(dt, ".i32");
		value <<= 8 * (cmode >> 1);
	} else if (cmode < 12) {
		bool orr = cmode & 1;
		name = op ? (orr ? "VBIC" : "VMVN") : (orr ? "VORR" : "VMOV");
		strcpy(dt, ".i16");
		value <<= 8 * ((cmode >> 1) & 1);
	} else if (cmode < 14) {
		name = op ? "VMVN" : "VMOV";
		strcpy(dt, ".i32");
		value = (cmode == 12) ? ((value << 8) | 0xFF) : ((value << 16) | 0xFFFF);
	} else if (cmode == 14) {
		name = "VMOV";
		if (op) {
			strcpy(dt, ".i64");
			value = 0;
			for (int i = 0; i < 8; i++) {
				if (imm8 & (1 << i))
					value |= 0xFFULL << (i * 8);
			}
		} else {
			strcpy(dt, ".i8");
		}
	} else {
		if (op)
			return false;
		name = "VMOV";
		strcpy(dt, ".f32");
		isFloat = true;
	}

	o.Add("%s%s %s, ", name, dt, V(q, NeonD(w)).s);
	if (isFloat)
		AddFloat(o, ExpandFloatImm(imm8));
	else
		o.Add("#0x%llx", value);
	return true;
}

bool DisNeon2Misc(u32 w, Out &o) {
	int size = Bits(w, 19, 18);
	int a = Bits(w, 17, 16);
	int b = Bits(w, 10, 6);
	bool q = Bit(w, 6);
	int bits = 8 << size;
	int d = NeonD(w), m = NeonM(w);
	const char *name = nullptr;
	Dt dt = Dt::NONE;

	switch (a) {
	case 0:
		switch (b >> 1) {
		case 0: name = "VREV64"; dt = Dt::PLAIN; break;
		case 1: name = "VREV32"; dt = Dt::PLAIN; break;
		case 2: name = "VREV16"; dt = Dt::PLAIN; break;
		case 4: case 5: name = "VPADDL"; dt = Bit(w, 7) ? Dt::U : Dt::S; break;
		case 8: name = "VCLS"; dt = Dt::S; break;
		case 9: name = "VCLZ"; dt = Dt::I; break;
		case 10: name = "VCNT"; dt = Dt::PLAIN; break;
		case 11: name = "VMVN"; break;
		case 12: case 13: name = "VPADAL"; dt = Bit(w, 7) ? Dt::U : Dt::S; break;
		case 14: name = "VQABS"; dt = Dt::S; break;
		case 15: name = "VQNEG"; dt = Dt::S; break;
		}
		break;
	case 1:
		{
			static const char *const names[8] = { "VCGT", "VCGE", "VCEQ", "VCLE", "VCLT", nullptr, "VABS", "VNEG" };
			int op = Bits(w, 9, 7);
			bool f = Bit(w, 10);
			name = names[op];
			if (!name)
				return false;
			o.Add("%s", name);
			if (f) {
				if (size != 2)
					return false;
				o.Add(".f32");
			} else {
				AddDt(o, op == 2 ? Dt::I : Dt::S, false, bits);
			}
			o.Add(" %s, %s", V(q, d).s, V(q, m).s);
			if (op < 5)
				o.Add(", #0");
			return true;
		}
	case 2:
		switch (b) {
		case 0x08:
			o.Add("VMOVN.i%d d%d, q%d", bits * 2, d, m >> 1);
			return true;
		case 0x09:
			o.Add("VQMOVUN.s%d d%d, q%d", bits * 2, d, m >> 1);
			return true;
		case 0x0A:
		case 0x0B:
			o.Add("VQMOVN.%c%d d%d, q%d", Bit(w, 6) ? 'u' : 's', bits * 2, d, m >> 1);
			return true;
		case 0x0C:
			o.Add("VSHLL.i%d q%d, d%d, #%d", bits, d >> 1, m, bits);
			return true;
		case 0x18:
			if (size != 1)
				return false;
			o.Add("VCVT.f16.f32 d%d, q%d", d, m >> 1);
			return true;
		case 0x1C:
			if (size != 1)
				return false;
			o.Add("VCVT.f32.f16 q%d, d%d", d >> 1, m);
			return true;
		}
		switch (b >> 1) {
		case 0: name = "VSWP"; break;
		case 1: name = "VTRN"; dt = Dt::PLAIN; break;
		case 2: name = "VUZP"; dt = Dt::PLAIN; break;
		case 3: name = "VZIP"; dt = Dt::PLAIN; break;
		}
		if ((b >> 1) >= 2 && size == 2 && !q)
			return false;
		break;
	case 3:
		if ((b & 0x1A) == 0x10 || (b & 0x1A) == 0x12) {
			name = (b & 2) ? "VRSQRTE" : "VRECPE";
			dt = Bit(w, 8) ? Dt::F : Dt::U;
		} else if ((b & 0x18) == 0x18) {
			static const char *const conv[4] = { ".f32.s32", ".f32.u32", ".s32.f32", ".u32.f32" };
			o.Add("VCVT%s %s, %s", conv[Bits(w, 8, 7)], V(q, d).s, V(q, m).s);
			return true;
		}
		break;
	}
	if (!name)
		return false;
	o.Add("%s", name);
	AddDt(o, dt, false, bits);
	o.Add(" %s, %s", V(q, d).s, V(q, m).s);
	return true;
}

bool DisNeonData(u32 w, Out &o) {
	if (!Bit(w, 23))
		return DisNeon3Same(w, o);
	if (Bit(w, 4)) {
		if (Bits(w, 21, 19) == 0 && !Bit(w, 7))
			return DisNeonModImm(w, o);
		return DisNeonShift(w, o);
	}
	if (Bits(w, 21, 20) != 3)
		return Bit(w, 6) ? DisNeonScalar(w, o) : DisNeon3Diff(w, o);

	bool q = Bit(w, 6);
	int d = NeonD(w), n = NeonN(w), m = NeonM(w);
	if (!Bit(w, 24)) {
		o.Add("VEXT.8 %s, %s, %s, #%d", V(q, d).s, V(q, n).s, V(q, m).s, Bits(w, 11, 8));
		return true;
	}
	if (!Bit(w, 11))
		return DisNeon2Misc(w, o);
	if (Bits(w, 11, 10) == 2) {
		int len = Bits(w, 9, 8) + 1;
		o.Add("%s.8 d%d, {d%d", Bit(w, 6) ? "VTBX" : "VTBL", d, n);
		if (len > 1)
			o.Add("-d%d", n + len - 1);
		o.Add("}, d%d", m);
		return true;
	}
	if (Bits(w, 11, 8) == 0xC) {
		int imm4 = Bits(w, 19, 16);
		int size, index;
		if (imm4 & 1) {
			size = 8;
			index = imm4 >> 1;
		} else if (imm4 & 2) {
			size = 16;
			index = imm4 >> 2;
		} else if (imm4 & 4) {
			size = 32;
			index = imm4 >> 3;
		} else {
			return false;
		}
		o.Add("VDUP.%d %s, d%d[%d]", size, V(q, d).s, m, index);
		return true;
	}
	return false;
}

bool DisNeonLoadStore(u32 w, Out &o) {
	bool load = Bit(w, 21);
	int rn = Bits(w, 19, 16);
	int rm = Bits(w, 3, 0);
	int d = NeonD(w);
	const char *name = load ? "VLD" : "VST";

	int n, regs, inc, size, align = 0;
	bool lanes = false, allLanes = false;
	int index = 0;
	if (!Bit(w, 23)) {
		size = Bits(w, 7, 6);
		switch (Bits(w, 11, 8)) {
		case 7: n = 1; regs = 1; inc = 1; break;
		case 10: n = 1; regs = 2; inc = 1; break;
		case 6: n = 1; regs = 3; inc = 1; break;
		case 2: n = 1; regs = 4; inc = 1; break;
		case 8: n = 2; regs = 2; inc = 1; break;
		case 9: n = 2; regs = 2; inc = 2; break;
		case 3: n = 2; regs = 4; inc = 1; break;
		case 4: n = 3; regs = 3; inc = 1; break;
		case 5: n = 3; regs = 3; inc = 2; break;
		case 0: n = 4; regs = 4; inc = 1; break;
		case 1: n = 4; regs = 4; inc = 2; break;
		default: return false;
		}
		int a = Bits(w, 5, 4);
		if (a)
			align = 32 << a;
	} else if (Bits(w, 11, 10) == 3) {
		if (!load)
			return false;
		allLanes = true;
		n = Bits(w, 9, 8) + 1;
		size = Bits(w, 7, 6);
		bool t = Bit(w, 5);
		if (n == 1) {
			regs = t ? 2 : 1;
			inc = 1;
		} else {
			regs = n;
			inc = t ? 2 : 1;
		}
		if (Bit(w, 4)) {
			if (n == 1 || n == 2)
				align = (8 << size) * n;
			else if (n == 4)
				align = size == 2 ? 64 : (size == 3 ? 128 : 32 << size);
			else
				return false;
		}
	} else {
		lanes = true;
		size = Bits(w, 11, 10);
		n = Bits(w, 9, 8) + 1;
		regs = n;
		int ia = Bits(w, 7, 4);
		index = ia >> (size + 1);
		inc = 1;
		if (n > 1 && ((size == 1 && (ia & 2)) || (size == 2 && (ia & 4))))
			inc = 2;
		switch (n) {
		case 1:
			if (size == 1 && (ia & 1))
				align = 16;
			else if (size == 2 && (ia & 3) == 3)
				align = 32;
			break;
		case 2:
			if (ia & 1)
				align = 16 << size;
			break;
		case 4:
			if (size == 2 && (ia & 3))
				align = 32 << (ia & 3);
			else if (size < 2 && (ia & 1))
				align = 32 << size;
			break;
		}
	}

	o.Add("%s%d.%d {", name, n, 8 << size);
	if (n == 1 && !lanes && !allLanes && regs > 1) {
		o.Add("d%d-d%d", d, d + regs - 1);
	} else {
		for (int i = 0; i < regs; i++) {
			o.Add("%sd%d", i ? ", " : "", d + i * inc);
			if (lanes)
				o.Add("[%d]", index);
			else if (allLanes)
				o.Add("[]");
		}
	}
	o.Add("}, [%s", R(rn));
	if (align)
		o.Add(":%d", align);
	o.Add("]");
	if (rm == 13)
		o.Add("!");
	else if (rm != 15)
		o.Add(", %s", R(rm));
	return true;
}

bool DisUnconditional(u32 addr, u32 w, Out &o) {
	if (Bits(w, 27, 25) == 1)
		return DisNeonData(w, o);
	if (Bits(w, 27, 24) == 4 && !Bit(w, 20))
		return DisNeonLoadStore(w, o);
	if ((w & 0xFFFFFDFF) == 0xF1010000) {
		o.Add("SETEND %s", Bit(w, 9) ? "be" : "le");
		return true;
	}
	if ((w & 0xFFFFFF00) == 0xF57FF000) {
		static const char *const options[16] = {
			nullptr, nullptr, "oshst", "osh", nullptr, nullptr, "nshst", "nsh",
			nullptr, nullptr, "ishst", "ish", nullptr, nullptr, "st", "sy",
		};
		int option = Bits(w, 3, 0);
		switch (Bits(w, 7, 4)) {
		case 1:
			o.Add("CLREX");
			return true;
		case 4:
		case 5:
			o.Add("%s ", Bits(w, 7, 4) == 4 ? "DSB" : "DMB");
			if (options[option])
				o.Add("%s", options[option]);
			else
				o.Add("#%d", option);
			return true;
		case 6:
			o.Add("ISB %s", option == 15 ? "sy" : "");
			return true;
		default:
			return false;
		}
	}
	if ((Bits(w, 27, 24) == 5 || (Bits(w, 27, 24) == 7 && !Bit(w, 4))) && Bits(w, 21, 20) == 1 && Bits(w, 15, 12) == 15) {
		bool up = Bit(w, 23);
		o.Add("%s [%s", Bit(w, 22) ? "PLD" : "PLDW", R(Bits(w, 19, 16)));
		if (Bits(w, 27, 24) == 5) {
			int imm = Bits(w, 11, 0);
			if (up && imm == 0)
				o.Add("]");
			else
				o.Add(", #%s%d]", up ? "" : "-", imm);
		} else {
			o.Add(", %s%s", up ? "" : "-", R(Bits(w, 3, 0)));
			AddShiftImm(o, Bits(w, 6, 5), Bits(w, 11, 7));
			o.Add("]");
		}
		return true;
	}
	if (Bits(w, 27, 25) == 5) {
		u32 target = addr + 8 + (SignExtend(Bits(w, 23, 0), 24) << 2) + (Bit(w, 24) << 1);
		o.Add("BLX 0x%08x", target);
		return true;
	}
	return false;
}

bool Disassemble(u32 addr, u32 w, Out &o) {
	if ((w >> 28) == 0xF)
		return DisUnconditional(addr, w, o);

	switch (Bits(w, 27, 25)) {
	case 0:
		if (Bit(w, 4) && Bit(w, 7)) {
			if (Bits(w, 6, 5) == 0)
				return Bit(w, 24) ? DisSynchronization(w, o) : DisMultiply(w, o);
			return DisExtraLoadStore(w, o);
		}
		if (Bits(w, 24, 23) == 2 && !Bit(w, 20))
			return Bit(w, 7) ? DisHalfwordMultiply(w, o) : DisMisc(w, o);
		return DisDataProcessing(w, o);
	case 1:
		if (Bits(w, 24, 23) == 2 && !Bit(w, 20))
			return DisMovImmAndHints(w, o);
		return DisDataProcessing(w, o);
	case 2:
		return DisLoadStore(w, o);
	case 3:
		return Bit(w, 4) ? DisMedia(w, o) : DisLoadStore(w, o);
	case 4:
		return DisBlockTransfer(w, o);
	case 5:
		{
			u32 target = addr + 8 + (SignExtend(Bits(w, 23, 0), 24) << 2);
			o.Add("%s%s 0x%08x", Bit(w, 24) ? "BL" : "B", Cond(w), target);
			return true;
		}
	default:
		return DisCoprocessor(w, o);
	}
}

}  // namespace

const char *ArmRegName(int r) {
	return R(r);
}

bool ArmAnalyzeLoadStore(uint32_t addr, uint32_t op, ArmLSInstructionInfo *info) {
	*info = {};
	info->instructionSize = 4;

	// TODO

	return false;
}

void ArmDis(unsigned int addr, unsigned int w, char *output, int bufsize, bool includeWord) {
	char text[256];
	Out o(text, sizeof(text));
	if (!Disassemble(addr, w, o))
		snprintf(text, sizeof(text), "[undefined instr]");
	if (includeWord)
		snprintf(output, bufsize, "%08x %s", w, text);
	else
		snprintf(output, bufsize, "%s", text);
}
