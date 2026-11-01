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

// Runs every instruction the ARM64 emitter can produce through our disassembler (Core/Util/DisArm64.cpp),
// two examples each. The expected strings were checked against llvm-mc, which
// Tools/arm64_disasm_check.py automates: it sets ARM64_DISASM_DUMP to make this test print every line.

#include <cstdlib>
#include <cstring>
#include <string>

#include "Common/Arm64Emitter.h"
#include "Core/Util/DisArm64.h"

#include "UnitTest.h"

#undef ADD
#undef RET

using namespace Arm64Gen;

namespace {

struct DisasmChecker {
	ARM64XEmitter &emit;
	const u8 *base;
	const u8 *start = nullptr;
	int failures = 0;
	int checks = 0;
	bool dumpAll = getenv("ARM64_DISASM_DUMP") != nullptr;

	void Begin() {
		start = emit.GetCodePointer();
	}

	// Multiple instructions are joined with "; ".
	void Check(const char *expected, const char *code, int line) {
		std::string actual;
		std::string words;
		for (const u8 *p = start; p < emit.GetCodePointer(); p += 4) {
			u32 w;
			memcpy(&w, p, 4);
			char buf[256];
			// Addresses are relative to the start of the buffer, which keeps branch targets stable.
			Arm64Dis((uint64_t)(p - base), w, buf, sizeof(buf), false);
			if (!actual.empty()) {
				actual += "; ";
				words += " ";
			}
			actual += buf;
			snprintf(buf, sizeof(buf), "%08x", w);
			words += buf;
		}
		checks++;
		bool fail = actual != expected;
		if (fail || dumpAll) {
			// Machine readable, for Tools/arm64_disasm_check.py.
			printf("DIS|%d|%x|%s|%s|%s|%s\n", line, (int)(start - base), words.c_str(), actual.c_str(), expected, code);
		}
		if (fail) {
			failures++;
		}
	}
};

}  // namespace

#define DIS(expected, ...) do { c.Begin(); __VA_ARGS__; c.Check(expected, #__VA_ARGS__, __LINE__); } while (false)

static void TestIntegerInstructions(DisasmChecker &c, ARM64XEmitter &e, const u8 *base) {
	// Branches. The buffer is at address 0 as far as the disassembler is concerned.
	e.SetCodePointer(base + 0x100, (u8 *)base + 0x100);
	DIS("b 000000000180", e.B(base + 0x180));
	DIS("b 000000000040", e.B(base + 0x40));
	DIS("bl 000000002000", e.BL(base + 0x2000));
	DIS("bl 000000000000", e.BL(base));
	DIS("b.eq 000000000200", e.B(CC_EQ, base + 0x200));
	DIS("b.le 000000000080", e.B(CC_LE, base + 0x80));
	DIS("cbz w3, 000000000120", e.CBZ(W3, base + 0x120));
	DIS("cbz x30, 000000000040", e.CBZ(X30, base + 0x40));
	DIS("cbnz w0, 000000000200", e.CBNZ(W0, base + 0x200));
	DIS("cbnz x9, 000000000100", e.CBNZ(X9, base + 0x100));
	DIS("tbz w3, #5, 000000000200", e.TBZ(W3, 5, base + 0x200));
	DIS("tbz x3, #40, 000000000100", e.TBZ(X3, 40, base + 0x100));
	DIS("tbnz w30, #31, 000000000180", e.TBNZ(W30, 31, base + 0x180));
	DIS("tbnz x12, #63, 0000000000c0", e.TBNZ(X12, 63, base + 0xc0));
	DIS("b 00000000013c", FixupBranch b = e.B(); e.SetJumpTarget(b));
	DIS("bl 000000000140", FixupBranch b = e.BL(); e.SetJumpTarget(b));
	DIS("b.hi 000000000144", FixupBranch b = e.B(CC_HI); e.SetJumpTarget(b));
	DIS("cbz x4, 000000000148", FixupBranch b = e.CBZ(X4); e.SetJumpTarget(b));
	DIS("cbnz w5, 00000000014c", FixupBranch b = e.CBNZ(W5); e.SetJumpTarget(b));
	DIS("tbz w6, #7, 000000000150", FixupBranch b = e.TBZ(W6, 7); e.SetJumpTarget(b));
	DIS("tbnz x7, #33, 000000000154", FixupBranch b = e.TBNZ(X7, 33); e.SetJumpTarget(b));
	DIS("bl 000000001000", e.QuickCallFunction(X16, base + 0x1000));
	DIS("br x16", e.BR(X16));
	DIS("br x3", e.BR(X3));
	DIS("blr x30", e.BLR(X30));
	DIS("blr x1", e.BLR(X1));
	DIS("ret", e.RET());
	DIS("ret x5", e.RET(X5));
	DIS("eret", e.ERET());
	DIS("drps", e.DRPS());

	// Exception generation
	DIS("svc #0", e.SVC(0));
	DIS("svc #4660", e.SVC(0x1234));
	DIS("hvc #1", e.HVC(1));
	DIS("hvc #65535", e.HVC(0xffff));
	DIS("smc #2", e.SMC(2));
	DIS("smc #100", e.SMC(100));
	DIS("brk #0", e.BRK(0));
	DIS("brk #1000", e.BRK(0x3e8));
	DIS("hlt #3", e.HLT(3));
	DIS("hlt #61440", e.HLT(0xf000));
	DIS("dcps1", e.DCPS1(0));
	DIS("dcps1 #5", e.DCPS1(5));
	DIS("dcps2", e.DCPS2(0));
	DIS("dcps2 #6", e.DCPS2(6));
	DIS("dcps3", e.DCPS3(0));
	DIS("dcps3 #7", e.DCPS3(7));

	// System
	DIS("msr spsel, #1", e._MSR(FIELD_SPSel, 1));
	DIS("msr daifset, #2", e._MSR(FIELD_DAIFSet, 2));
	DIS("msr daifclr, #15", e._MSR(FIELD_DAIFClr, 15));
	DIS("msr nzcv, x25", e._MSR(FIELD_NZCV, X25));
	DIS("msr fpcr, x0", e._MSR(FIELD_FPCR, X0));
	DIS("msr fpsr, x1", e._MSR(FIELD_FPSR, X1));
	DIS("mrs x25, nzcv", e.MRS(X25, FIELD_NZCV));
	DIS("mrs x2, fpcr", e.MRS(X2, FIELD_FPCR));
	DIS("mrs x3, fpsr", e.MRS(X3, FIELD_FPSR));
	DIS("nop", e.HINT(HINT_NOP));
	DIS("yield", e.HINT(HINT_YIELD));
	DIS("wfe", e.HINT(HINT_WFE));
	DIS("wfi", e.HINT(HINT_WFI));
	DIS("sev", e.HINT(HINT_SEV));
	DIS("sevl", e.HINT(HINT_SEVL));
	DIS("clrex", e.CLREX());
	DIS("dsb ish", e.DSB(ISH));
	DIS("dsb sy", e.DSB(SY));
	DIS("dmb ishld", e.DMB(ISHLD));
	DIS("dmb oshst", e.DMB(OSHST));
	DIS("isb", e.ISB(SY));
	DIS("isb #3", e.ISB(OSH));

	// Add/subtract (shifted and extended register)
	DIS("add x1, x2, x30", e.ADD(X1, X2, X30));
	DIS("add w1, w2, w3, lsr #7", e.ADD(W1, W2, W3, ArithOption(W1, ST_LSR, 7)));
	DIS("add x1, x2, w3, sxtw", e.ADD(X1, X2, W3, ArithOption(W3, false, true)));
	DIS("add x1, sp, x3, lsl #4", e.ADD(X1, SP, X3, ArithOption(X3, true)));
	DIS("adds w4, w5, w6", e.ADDS(W4, W5, W6));
	DIS("adds x4, x5, x6, asr #63", e.ADDS(X4, X5, X6, ArithOption(X4, ST_ASR, 63)));
	DIS("sub w1, w2, w30", e.SUB(W1, W2, W30));
	DIS("sub x7, x8, x9, lsl #12", e.SUB(X7, X8, X9, ArithOption(X7, ST_LSL, 12)));
	DIS("subs w1, w2, w30", e.SUBS(W1, W2, W30));
	DIS("subs x1, x2, w3, uxtw", e.SUBS(X1, X2, W3, ArithOption(W3, false, false)));
	DIS("cmn w1, w2", e.CMN(W1, W2));
	DIS("cmn x1, x2, lsl #2", e.CMN(X1, X2, ArithOption(X1, ST_LSL, 2)));
	DIS("cmp w2, w30", e.CMP(W2, W30));
	DIS("cmp x2, w3, sxtw", e.CMP(X2, W3, ArithOption(W3, false, true)));
	DIS("neg x1, x2", e.NEG(X1, X2));
	DIS("neg w5, w6", e.NEG(W5, W6));

	// Add/subtract with carry
	DIS("adc w1, w2, w3", e.ADC(W1, W2, W3));
	DIS("adc x1, x2, x3", e.ADC(X1, X2, X3));
	DIS("adcs w4, w5, w6", e.ADCS(W4, W5, W6));
	DIS("adcs x4, x5, x6", e.ADCS(X4, X5, X6));
	DIS("sbc w7, w8, w9", e.SBC(W7, W8, W9));
	DIS("sbc x7, x8, x9", e.SBC(X7, X8, X9));
	DIS("sbcs w10, w11, w12", e.SBCS(W10, W11, W12));
	DIS("sbcs x10, x11, x12", e.SBCS(X10, X11, X12));

	// Conditional compare
	DIS("ccmn w1, #5, #2, eq", e.CCMN(W1, 5, 0x2, CC_EQ));
	DIS("ccmn x1, #31, #15, lt", e.CCMN(X1, 31, 0xF, CC_LT));
	DIS("ccmp w2, #0, #0, ne", e.CCMP(W2, 0, 0x0, CC_NEQ));
	DIS("ccmp x2, #17, #8, ge", e.CCMP(X2, 17, 0x8, CC_GE));
	DIS("ccmn w3, w4, #4, hi", e.CCMN(W3, W4, 0x4, CC_HI));
	DIS("ccmn x3, x4, #1, mi", e.CCMN(X3, X4, 0x1, CC_MI));
	DIS("ccmp w5, w6, #3, ls", e.CCMP(W5, W6, 0x3, CC_LS));
	DIS("ccmp x5, x6, #6, vs", e.CCMP(X5, X6, 0x6, CC_VS));

	// Conditional select
	DIS("csel w3, w5, w7, eq", e.CSEL(W3, W5, W7, CC_EQ));
	DIS("csel x3, x5, x7, gt", e.CSEL(X3, X5, X7, CC_GT));
	DIS("csinc w1, w2, w3, cs", e.CSINC(W1, W2, W3, CC_CS));
	DIS("csinc x1, x2, x3, cc", e.CSINC(X1, X2, X3, CC_CC));
	DIS("csinv w1, w2, w3, pl", e.CSINV(W1, W2, W3, CC_PL));
	DIS("csinv x1, x2, x3, vc", e.CSINV(X1, X2, X3, CC_VC));
	DIS("csneg w1, w2, w3, ge", e.CSNEG(W1, W2, W3, CC_GE));
	DIS("csneg x1, x2, x3, lt", e.CSNEG(X1, X2, X3, CC_LT));
	DIS("cset w1, eq", e.CSET(W1, CC_EQ));
	DIS("cset x9, cc", e.CSET(X9, CC_LO));

	// Data-processing (1 source)
	DIS("rbit w3, w2", e.RBIT(W3, W2));
	DIS("rbit x3, x2", e.RBIT(X3, X2));
	DIS("rev16 w1, w2", e.REV16(W1, W2));
	DIS("rev16 x1, x2", e.REV16(X1, X2));
	DIS("rev w1, w2", e.REV32(W1, W2));
	DIS("rev32 x1, x2", e.REV32(X1, X2));
	DIS("rev x1, x2", e.REV64(X1, X2));
	DIS("rev x30, x0", e.REV64(X30, X0));
	DIS("clz w1, w2", e.CLZ(W1, W2));
	DIS("clz x1, x2", e.CLZ(X1, X2));
	DIS("cls w1, w2", e.CLS(W1, W2));
	DIS("cls x1, x2", e.CLS(X1, X2));

	// Data-processing (2 source)
	DIS("udiv w1, w2, w3", e.UDIV(W1, W2, W3));
	DIS("udiv x1, x2, x3", e.UDIV(X1, X2, X3));
	DIS("sdiv w1, w2, w3", e.SDIV(W1, W2, W3));
	DIS("sdiv x1, x2, x3", e.SDIV(X1, X2, X3));
	DIS("lslv w1, w2, w3", e.LSLV(W1, W2, W3));
	DIS("lslv x1, x2, x30", e.LSLV(X1, X2, X30));
	DIS("lsrv w1, w2, w3", e.LSRV(W1, W2, W3));
	DIS("lsrv x1, x2, x3", e.LSRV(X1, X2, X3));
	DIS("asrv w1, w2, w3", e.ASRV(W1, W2, W3));
	DIS("asrv x1, x2, x3", e.ASRV(X1, X2, X3));
	DIS("rorv w1, w2, w3", e.RORV(W1, W2, W3));
	DIS("rorv x1, x2, x3", e.RORV(X1, X2, X3));
	DIS("crc32b w1, w2, w3", e.CRC32B(W1, W2, W3));
	DIS("crc32b w4, w5, w6", e.CRC32B(W4, W5, W6));
	DIS("crc32h w1, w2, w3", e.CRC32H(W1, W2, W3));
	DIS("crc32h w4, w5, w6", e.CRC32H(W4, W5, W6));
	DIS("crc32w w1, w2, w3", e.CRC32W(W1, W2, W3));
	DIS("crc32w w4, w5, w6", e.CRC32W(W4, W5, W6));
	DIS("crc32x w1, w2, x3", e.CRC32X(W1, W2, X3));
	DIS("crc32x w4, w5, x6", e.CRC32X(W4, W5, X6));
	DIS("crc32cb w1, w2, w3", e.CRC32CB(W1, W2, W3));
	DIS("crc32cb w4, w5, w6", e.CRC32CB(W4, W5, W6));
	DIS("crc32ch w1, w2, w3", e.CRC32CH(W1, W2, W3));
	DIS("crc32ch w4, w5, w6", e.CRC32CH(W4, W5, W6));
	DIS("crc32cw w1, w2, w3", e.CRC32CW(W1, W2, W3));
	DIS("crc32cw w4, w5, w6", e.CRC32CW(W4, W5, W6));
	DIS("crc32cx w1, w2, x3", e.CRC32CX(W1, W2, X3));
	DIS("crc32cx w4, w5, x6", e.CRC32CX(W4, W5, X6));

	// Data-processing (3 source)
	DIS("madd w3, w7, w1, w8", e.MADD(W3, W7, W1, W8));
	DIS("madd x3, x7, x1, x8", e.MADD(X3, X7, X1, X8));
	DIS("msub w3, w7, w1, w8", e.MSUB(W3, W7, W1, W8));
	DIS("msub x3, x7, x1, x8", e.MSUB(X3, X7, X1, X8));
	DIS("smaddl x3, w6, w1, x8", e.SMADDL(X3, W6, W1, X8));
	DIS("smaddl x0, w1, w2, x3", e.SMADDL(X0, W1, W2, X3));
	DIS("smsubl x3, w6, w1, x8", e.SMSUBL(X3, W6, W1, X8));
	DIS("smsubl x0, w1, w2, x3", e.SMSUBL(X0, W1, W2, X3));
	DIS("smull x3, w6, w1", e.SMULL(X3, W6, W1));
	DIS("smull x0, w1, w2", e.SMULL(X0, W1, W2));
	DIS("smulh x3, x6, x1", e.SMULH(X3, X6, X1));
	DIS("smulh x0, x1, x2", e.SMULH(X0, X1, X2));
	DIS("umaddl x3, w6, w1, x8", e.UMADDL(X3, W6, W1, X8));
	DIS("umaddl x0, w1, w2, x3", e.UMADDL(X0, W1, W2, X3));
	DIS("umsubl x3, w6, w1, x8", e.UMSUBL(X3, W6, W1, X8));
	DIS("umsubl x0, w1, w2, x3", e.UMSUBL(X0, W1, W2, X3));
	DIS("umull x3, w6, w1", e.UMULL(X3, W6, W1));
	DIS("umull x0, w1, w2", e.UMULL(X0, W1, W2));
	DIS("umulh x3, x6, x1", e.UMULH(X3, X6, X1));
	DIS("umulh x0, x1, x2", e.UMULH(X0, X1, X2));
	DIS("mul w1, w2, w3", e.MUL(W1, W2, W3));
	DIS("mul x1, x2, x3", e.MUL(X1, X2, X3));
	DIS("mneg w1, w2, w3", e.MNEG(W1, W2, W3));
	DIS("mneg x1, x2, x3", e.MNEG(X1, X2, X3));

	// Logical (shifted register)
	DIS("and w1, w2, w30", e.AND(W1, W2, W30));
	DIS("and x1, x2, x3, lsr #5", e.AND(X1, X2, X3, ArithOption(X1, ST_LSR, 5)));
	DIS("bic w1, w2, w3", e.BIC(W1, W2, W3));
	DIS("bic x1, x2, x3, asr #1", e.BIC(X1, X2, X3, ArithOption(X1, ST_ASR, 1)));
	DIS("orr x1, x2, x30", e.ORR(X1, X2, X30));
	DIS("orr w1, w2, w3, ror #31", e.ORR(W1, W2, W3, ArithOption(W1, ST_ROR, 31)));
	DIS("orn w1, w2, w3", e.ORN(W1, W2, W3));
	DIS("orn x1, x2, x3, lsl #4", e.ORN(X1, X2, X3, ArithOption(X1, ST_LSL, 4)));
	DIS("eor x1, x2, x30", e.EOR(X1, X2, X30));
	DIS("eor w1, w2, w3, lsl #16", e.EOR(W1, W2, W3, ArithOption(W1, ST_LSL, 16)));
	DIS("eon w1, w2, w3", e.EON(W1, W2, W3));
	DIS("eon x1, x2, x3, lsr #32", e.EON(X1, X2, X3, ArithOption(X1, ST_LSR, 32)));
	DIS("ands w1, w2, w30", e.ANDS(W1, W2, W30));
	DIS("ands x1, x2, x3, lsl #1", e.ANDS(X1, X2, X3, ArithOption(X1, ST_LSL, 1)));
	DIS("bics w1, w2, w3", e.BICS(W1, W2, W3));
	DIS("bics x1, x2, x3, lsl #2", e.BICS(X1, X2, X3, ArithOption(X1, ST_LSL, 2)));
	DIS("tst w1, w2", e.TST(W1, W2));
	DIS("tst x1, x2, lsl #3", e.TST(X1, X2, ArithOption(X1, ST_LSL, 3)));
	DIS("mov x23, x30", e.MOV(X23, X30));
	DIS("mov w1, w2", e.MOV(W1, W2));
	DIS("mov x1, x2, lsl #14", e.MOV(X1, X2, ArithOption(X1, ST_LSL, 14)));
	DIS("mov w1, w2, lsr #3", e.MOV(W1, W2, ArithOption(W1, ST_LSR, 3)));
	DIS("mvn w1, w2", e.MVN(W1, W2));
	DIS("mvn x1, x2", e.MVN(X1, X2));
	DIS("mov x3, sp", e.MOVfromSP(X3));
	DIS("mov sp, x11", e.MOVtoSP(X11));
	DIS("mov w1, w2, lsl #3", e.LSL(W1, W2, 3));
	DIS("mov x1, x2, lsl #40", e.LSL(X1, X2, 40));
	DIS("mov w1, w2, lsr #3", e.LSR(W1, W2, 3));
	DIS("mov x1, x2, lsr #40", e.LSR(X1, X2, 40));
	DIS("mov w1, w2, asr #3", e.ASR(W1, W2, 3));
	DIS("mov x1, x2, asr #40", e.ASR(X1, X2, 40));
	DIS("mov w1, w2, ror #3", e.ROR(W1, W2, 3));
	DIS("mov x1, x2, ror #40", e.ROR(X1, X2, 40));

	// Logical (immediate)
	DIS("and w1, w2, #0xff", e.AND(W1, W2, 0, 7));
	DIS("and x1, x2, #0xff000000000000ff", e.AND(X1, X2, 8, 15, true));
	DIS("ands w1, w2, #0x10000000", e.ANDS(W1, W2, 4, 0));
	DIS("ands x1, x2, #0xffffffff", e.ANDS(X1, X2, 0, 31, true));
	DIS("eor w1, w2, #0x80000000", e.EOR(W1, W2, 1, 0));
	DIS("eor x1, x2, #0x5555555555555555", e.EOR(X1, X2, 0, 0x3c));
	DIS("orr w1, w2, #0xffff0000", e.ORR(W1, W2, 16, 15));
	DIS("orr x1, x3, #0x3f", e.ORR(X1, X3, 0, 5, true));
	DIS("tst w1, #0x1", e.TST(W1, 0, 0));
	DIS("tst x1, #0xbfffffffffffffff", e.TST(X1, 1, 62, true));
	DIS("and w1, w2, #0xff", e.ANDI2R(W1, W2, 0xFF));
	DIS("and x1, x2, #0xffff0000ffff0000", e.ANDI2R(X1, X2, 0xFFFF0000FFFF0000ULL));
	DIS("ands w1, w2, #0xf0f0f0f", e.ANDSI2R(W1, W2, 0x0F0F0F0F));
	DIS("movz x3, #0x2345 << 0; movk x3, #0x0001 << 16; ands x1, x2, x3", e.ANDSI2R(X1, X2, 0x12345, X3));
	DIS("tst w1, #0x80000000", e.TSTI2R(W1, 0x80000000));
	DIS("tst x1, #0x1", e.TSTI2R(X1, 1));
	DIS("orr x1, x3, #0x3f", e.ORRI2R(X1, X3, 0x3F));
	DIS("movz w4, #0x2345 << 0; movk w4, #0x0001 << 16; orr w1, w3, w4", e.ORRI2R(W1, W3, 0x12345, W4));
	DIS("eor x1, x3, #0x3f0000003f0", e.EORI2R(X1, X3, 0x3F0000003F0ULL));
	DIS("eor w1, w3, #0x80000001", e.EORI2R(W1, W3, 0x80000001));

	// Add/subtract (immediate)
	DIS("add x23, x30, #123", e.ADD(X23, X30, 123));
	DIS("add w1, w2, #4095, lsl #12", e.ADD(W1, W2, 0xFFF, true));
	DIS("adds w1, w2, #1", e.ADDS(W1, W2, 1));
	DIS("adds x1, sp, #4, lsl #12", e.ADDS(X1, SP, 4, true));
	DIS("sub w1, w2, #7", e.SUB(W1, W2, 7));
	DIS("sub sp, sp, #16", e.SUB(SP, SP, 16));
	DIS("subs w1, w2, #2048", e.SUBS(W1, W2, 0x800));
	DIS("subs x1, x2, #1, lsl #12", e.SUBS(X1, X2, 1, true));
	DIS("cmp w3, #3", e.CMP(W3, 3));
	DIS("cmp x3, #16, lsl #12", e.CMP(X3, 0x10, true));
	DIS("cmn w3, #3", e.CMN(W3, 3));
	DIS("cmn x3, #16, lsl #12", e.CMN(X3, 0x10, true));
	DIS("add w1, w2, #1, lsl #12", e.ADDI2R(W1, W2, 0x1000));
	DIS("sub x1, x2, #8", e.ADDI2R(X1, X2, (u64)-8));
	DIS("sub w1, w2, #5", e.SUBI2R(W1, W2, 5));
	DIS("movz x3, #0x2345 << 0; movk x3, #0x0001 << 16; sub x1, x2, x3", e.SUBI2R(X1, X2, 0x12345, X3));
	DIS("subs w1, w2, #5", e.SUBSI2R(W1, W2, 5));
	DIS("movz x3, #0x2345 << 0; movk x3, #0x0001 << 16; subs x1, x2, x3", e.SUBSI2R(X1, X2, 0x12345, X3));
	DIS("cmp w1, #5", e.CMPI2R(W1, 5));
	DIS("cmn x1, #5", e.CMPI2R(X1, (u64)-5));

	// Move wide
	DIS("movz w1, #0x1234 << 0", e.MOVZ(W1, 0x1234));
	DIS("movz x1, #0xffff << 48", e.MOVZ(X1, 0xFFFF, SHIFT_48));
	DIS("movn w1, #0x0000 << 0", e.MOVN(W1, 0));
	DIS("movn x1, #0x1234 << 16", e.MOVN(X1, 0x1234, SHIFT_16));
	DIS("movk w1, #0x8000 << 16", e.MOVK(W1, 0x8000, SHIFT_16));
	DIS("movk x1, #0xabcd << 32", e.MOVK(X1, 0xABCD, SHIFT_32));
	DIS("movz w1, #0x0000 << 0", e.MOVI2R(W1, 0));
	DIS("movz w1, #0x5678 << 0; movk w1, #0x1234 << 16", e.MOVI2R(W1, 0x12345678));
	DIS("movz x1, #0x9abc << 0; movk x1, #0x5678 << 16; movk x1, #0x1234 << 32", e.MOVI2R(X1, 0x123456789ABCULL));
	DIS("mvn w1, wzr", e.MOVI2R(W1, 0xFFFFFFFF));
	DIS("movn w1, #0x0004 << 0", e.MOVI2R(W1, (u32)-5));
	DIS("adrp x2, #0x000000003000", e.MOVP2R(X2, base + 0x3000));

	// Bitfield
	DIS("bfxil w1, w2, #3, #2", e.BFM(W1, W2, 3, 4));
	DIS("bfi x1, x2, #4, #11", e.BFM(X1, X2, 60, 10));
	DIS("asr w1, w2, #4", e.SBFM(W1, W2, 4, 31));
	DIS("sbfx x1, x2, #8, #16", e.SBFM(X1, X2, 8, 23));
	DIS("uxtb w1, w3", e.UBFM(W1, W3, 0, 7));
	DIS("lsl x1, x3, #1", e.UBFM(X1, X3, 63, 62));
	DIS("bfi w1, w2, #8, #4", e.BFI(W1, W2, 8, 4));
	DIS("bfxil x1, x2, #0, #32", e.BFI(X1, X2, 0, 32));
	DIS("ubfiz w1, w2, #3, #5", e.UBFIZ(W1, W2, 3, 5));
	DIS("ubfiz x1, x2, #32, #16", e.UBFIZ(X1, X2, 32, 16));
	DIS("ubfx w1, w2, #3, #5", e.UBFX(W1, W2, 3, 5));
	DIS("ubfx x1, x2, #32, #16", e.UBFX(X1, X2, 32, 16));
	DIS("extr w1, w2, w3, #7", e.EXTR(W1, W2, W3, 7));
	DIS("extr x1, x2, x3, #40", e.EXTR(X1, X2, X3, 40));
	DIS("sxtb w1, w2", e.SXTB(W1, W2));
	DIS("sxtb x1, w2", e.SXTB(X1, W2));
	DIS("sxth w1, w2", e.SXTH(W1, W2));
	DIS("sxth x1, w2", e.SXTH(X1, W2));
	DIS("sxtw x1, w2", e.SXTW(X1, W2));
	DIS("sxtw x30, w0", e.SXTW(X30, W0));
	DIS("uxtb w1, w2", e.UXTB(W1, W2));
	DIS("uxtb w30, w0", e.UXTB(W30, W0));
	DIS("uxth w1, w2", e.UXTH(W1, W2));
	DIS("uxth w30, w0", e.UXTH(W30, W0));

	// Load literal
	DIS("ldr w1, 0000000005ac", e.LDR(W1, 4));
	DIS("ldr x1, 0000000009a0", e.LDR(X1, 0x100));
	DIS("ldrsw x1, 0000000005c4", e.LDRSW(X1, 8));
	DIS("ldrsw x2, 0000000005ac", e.LDRSW(X2, 1));
	DIS("prfm pldl1keep, 0000000005bc", e.PRFM(PLDL1KEEP, 4));
	DIS("prfm pstl2strm, 0000000005b8", e.PRFM(PLTL2STRM, 2));

	// Load/store exclusive
	DIS("stxrb w1, w3, [x2]", e.STXRB(W1, W2, X3));
	DIS("stxrb w4, wzr, [x5]", e.STXRB(W4, W5, SP));
	DIS("stlxrb w1, w3, [x2]", e.STLXRB(W1, W2, X3));
	DIS("stlxrb w4, w6, [x5]", e.STLXRB(W4, W5, X6));
	DIS("ldxrb w2, [x1]", e.LDXRB(W1, X2));
	DIS("ldxrb wzr, [x3]", e.LDXRB(W3, SP));
	DIS("ldaxrb w2, [x1]", e.LDAXRB(W1, X2));
	DIS("ldaxrb w4, [x3]", e.LDAXRB(W3, X4));
	DIS("stlrb w2, [x1]", e.STLRB(W1, X2));
	DIS("stlrb w4, [x3]", e.STLRB(W3, X4));
	DIS("ldarb w2, [x1]", e.LDARB(W1, X2));
	DIS("ldarb w4, [x3]", e.LDARB(W3, X4));
	DIS("stxrh w1, w3, [x2]", e.STXRH(W1, W2, X3));
	DIS("stxrh w4, w6, [x5]", e.STXRH(W4, W5, X6));
	DIS("stlxrh w1, w3, [x2]", e.STLXRH(W1, W2, X3));
	DIS("stlxrh w4, w6, [x5]", e.STLXRH(W4, W5, X6));
	DIS("ldxrh w2, [x1]", e.LDXRH(W1, X2));
	DIS("ldxrh w4, [x3]", e.LDXRH(W3, X4));
	DIS("ldaxrh w2, [x1]", e.LDAXRH(W1, X2));
	DIS("ldaxrh w4, [x3]", e.LDAXRH(W3, X4));
	DIS("stlrh w2, [x1]", e.STLRH(W1, X2));
	DIS("stlrh w4, [x3]", e.STLRH(W3, X4));
	DIS("ldarh w2, [x1]", e.LDARH(W1, X2));
	DIS("ldarh w4, [x3]", e.LDARH(W3, X4));
	DIS("stxr w1, w3, [x2]", e.STXR(W1, W2, X3));
	DIS("stxr w4, x6, [x5]", e.STXR(W4, X5, X6));
	DIS("stlxr w1, w3, [x2]", e.STLXR(W1, W2, X3));
	DIS("stlxr w4, x6, [x5]", e.STLXR(W4, X5, X6));
	DIS("stxp w1, w4, w3, [x2]", e.STXP(W1, W2, W3, X4));
	DIS("stxp w5, x8, x7, [x6]", e.STXP(W5, X6, X7, X8));
	DIS("stlxp w1, w4, w3, [x2]", e.STLXP(W1, W2, W3, X4));
	DIS("stlxp w5, x8, x7, [x6]", e.STLXP(W5, X6, X7, X8));
	DIS("ldxr w2, [x1]", e.LDXR(W1, X2));
	DIS("ldxr x4, [x3]", e.LDXR(X3, X4));
	DIS("ldaxr w2, [x1]", e.LDAXR(W1, X2));
	DIS("ldaxr x4, [x3]", e.LDAXR(X3, X4));
	DIS("ldxp w3, w2, [x1]", e.LDXP(W1, W2, X3));
	DIS("ldxp x6, x5, [x4]", e.LDXP(X4, X5, X6));
	DIS("ldaxp w3, w2, [x1]", e.LDAXP(W1, W2, X3));
	DIS("ldaxp x6, x5, [x4]", e.LDAXP(X4, X5, X6));
	DIS("stlr w2, [x1]", e.STLR(W1, X2));
	DIS("stlr x4, [x3]", e.STLR(X3, X4));
	DIS("ldar w2, [x1]", e.LDAR(W1, X2));
	DIS("ldar x4, [x3]", e.LDAR(X3, X4));

	// Load/store pair
	DIS("stnp w21, w1, [x27, #48]", e.STNP(W21, W1, X27, 48));
	DIS("stnp x1, x2, [sp, #16]", e.STNP(X1, X2, SP, 16));
	DIS("ldnp w21, w1, [x27, #48]", e.LDNP(W21, W1, X27, 48));
	DIS("ldnp x1, x2, [x3, #120]", e.LDNP(X1, X2, X3, 120));
	DIS("ldp x23, x5, [x3, #32]", e.LDP(INDEX_SIGNED, X23, X5, X3, 32));
	DIS("ldp w23, w5, [x3], #36", e.LDP(INDEX_POST, W23, W5, X3, 36));
	DIS("ldp x1, x2, [sp, #-512]!", e.LDP(INDEX_PRE, X1, X2, SP, -512));
	DIS("ldpsw x1, x2, [x3, #8]", e.LDPSW(INDEX_SIGNED, X1, X2, X3, 8));
	DIS("ldpsw x1, x2, [x3], #-4", e.LDPSW(INDEX_POST, X1, X2, X3, -4));
	DIS("stp w21, w1, [x27, #48]", e.STP(INDEX_SIGNED, W21, W1, X27, 48));
	DIS("stp w23, w5, [sp, #-16]!", e.STP(INDEX_PRE, W23, W5, SP, -16));
	DIS("stp x23, x5, [x3], #504", e.STP(INDEX_POST, X23, X5, X3, 504));
	DIS("str x1, [sp, #-16]!", e.PUSH(X1));
	DIS("ldr x1, [sp], #16", e.POP(X1));
	DIS("stp x29, x30, [sp, #-16]!", e.PUSH2(X29, X30));
	DIS("ldp x29, x30, [sp], #16", e.POP2(X29, X30));

	// Load/store (immediate)
	DIS("strb w1, [x2, #4095]", e.STRB(INDEX_UNSIGNED, W1, X2, 4095));
	DIS("strb w1, [x2], #-1", e.STRB(INDEX_POST, W1, X2, -1));
	DIS("ldrb w1, [x2]", e.LDRB(INDEX_UNSIGNED, W1, X2, 0));
	DIS("ldrb w1, [x2, #255]!", e.LDRB(INDEX_PRE, W1, X2, 255));
	DIS("ldrsb w1, [x2, #1]", e.LDRSB(INDEX_UNSIGNED, W1, X2, 1));
	DIS("ldrsb x1, [x2], #2", e.LDRSB(INDEX_POST, X1, X2, 2));
	DIS("strh w1, [x2, #8190]", e.STRH(INDEX_UNSIGNED, W1, X2, 8190));
	DIS("strh w1, [sp, #-2]!", e.STRH(INDEX_PRE, W1, SP, -2));
	DIS("ldrh w3, [x7, #18]", e.LDRH(INDEX_UNSIGNED, W3, X7, 18));
	DIS("ldrh w3, [x7], #18", e.LDRH(INDEX_POST, W3, X7, 18));
	DIS("ldrsh w3, [x7, #18]", e.LDRSH(INDEX_UNSIGNED, W3, X7, 18));
	DIS("ldrsh x3, [x7, #-18]!", e.LDRSH(INDEX_PRE, X3, X7, -18));
	DIS("str w23, [x3, #36]", e.STR(INDEX_UNSIGNED, W23, X3, 36));
	DIS("str x23, [sp, #-16]!", e.STR(INDEX_PRE, X23, SP, -16));
	DIS("ldr x1, [x2, #32760]", e.LDR(INDEX_UNSIGNED, X1, X2, 32760));
	DIS("ldr w1, [x2], #4", e.LDR(INDEX_POST, W1, X2, 4));
	DIS("ldrsw x1, [x2, #16]", e.LDRSW(INDEX_UNSIGNED, X1, X2, 16));
	DIS("ldrsw x1, [x2, #-4]!", e.LDRSW(INDEX_PRE, X1, X2, -4));

	// Load/store (register offset)
	DIS("strb w1, [x2, x3]", e.STRB(W1, X2, ArithOption(X3)));
	DIS("strb w1, [x2, w3, sxtw]", e.STRB(W1, X2, ArithOption(W3, false, true)));
	DIS("ldrb w1, [x2, x3]", e.LDRB(W1, X2, ArithOption(X3)));
	DIS("ldrb w1, [x2, w3, uxtw]", e.LDRB(W1, X2, ArithOption(W3)));
	DIS("ldrsb w1, [x2, x3]", e.LDRSB(W1, X2, ArithOption(X3)));
	DIS("ldrsb x1, [x2, x3]", e.LDRSB(X1, X2, ArithOption(X3)));
	DIS("strh w1, [x2, x3, lsl #1]", e.STRH(W1, X2, ArithOption(X3, true)));
	DIS("strh w1, [x2, x3]", e.STRH(W1, X2, ArithOption(X3)));
	DIS("ldrh w1, [x2, x3, lsl #1]", e.LDRH(W1, X2, ArithOption(X3, true)));
	DIS("ldrh w1, [x2, w3, sxtw #1]", e.LDRH(W1, X2, ArithOption(W3, true, true)));
	DIS("ldrsh w1, [x2, x3]", e.LDRSH(W1, X2, ArithOption(X3)));
	DIS("ldrsh x1, [x2, x3, lsl #1]", e.LDRSH(X1, X2, ArithOption(X3, true)));
	DIS("str w1, [x2, x3]", e.STR(W1, X2, ArithOption(X3)));
	DIS("str x1, [x2, x3, lsl #3]", e.STR(X1, X2, ArithOption(X3, true)));
	DIS("ldr w23, [x3, x12]", e.LDR(W23, X3, ArithOption(X12)));
	DIS("ldr x1, [x2, w3, uxtw #3]", e.LDR(X1, X2, ArithOption(W3, true, false)));
	DIS("ldrsw x1, [x2, x3]", e.LDRSW(X1, X2, ArithOption(X3)));
	DIS("ldrsw x1, [x2, x3, lsl #2]", e.LDRSW(X1, X2, ArithOption(X3, true)));
	DIS("prfm pldl1keep, [x2, x3]", e.PRFM(PLDL1KEEP, X2, ArithOption(X3)));
	DIS("prfm plil3strm, [x2, x3, lsl #3]", e.PRFM(PLIL3STRM, X2, ArithOption(X3, true)));

	// Load/store (unscaled)
	DIS("sturb w1, [x2, #-1]", e.STURB(W1, X2, -1));
	DIS("sturb w1, [x2, #255]", e.STURB(W1, X2, 255));
	DIS("ldurb w1, [x2, #-256]", e.LDURB(W1, X2, -256));
	DIS("ldurb w1, [x2, #3]", e.LDURB(W1, X2, 3));
	DIS("ldursb w1, [x2, #-1]", e.LDURSB(W1, X2, -1));
	DIS("ldursb x1, [x2, #1]", e.LDURSB(X1, X2, 1));
	DIS("sturh w1, [x2, #-2]", e.STURH(W1, X2, -2));
	DIS("sturh w1, [x2, #3]", e.STURH(W1, X2, 3));
	DIS("ldurh w1, [x2, #-2]", e.LDURH(W1, X2, -2));
	DIS("ldurh w1, [x2, #3]", e.LDURH(W1, X2, 3));
	DIS("ldursh w1, [x2, #-2]", e.LDURSH(W1, X2, -2));
	DIS("ldursh x1, [x2, #3]", e.LDURSH(X1, X2, 3));
	DIS("stur w1, [x2, #-4]", e.STUR(W1, X2, -4));
	DIS("stur x1, [sp, #1]", e.STUR(X1, SP, 1));
	DIS("ldur w1, [x2, #-4]", e.LDUR(W1, X2, -4));
	DIS("ldur x1, [x2, #1]", e.LDUR(X1, X2, 1));
	DIS("ldursw x1, [x2, #-4]", e.LDURSW(X1, X2, -4));
	DIS("ldursw x1, [x2, #5]", e.LDURSW(X1, X2, 5));

	// PC-relative addressing
	e.SetCodePointer(base + 0x1100, (u8 *)base + 0x1100);
	DIS("adr x1, #0x000000001140", e.ADR(X1, 0x40));
	DIS("adr x1, #0x0000000010c3", e.ADR(X1, -0x41));
	DIS("adrp x1, #0x000000004000", e.ADRP(X1, 0x3000));
	DIS("adrp x2, #0x000000000000", e.ADRP(X2, -0x1000));
}

static void TestFloatInstructions(DisasmChecker &c, ARM64XEmitter &e, ARM64FloatEmitter &fp) {
	// Load/store (immediate)
	DIS("ldr b1, [x1, #96]!", fp.LDR(8, INDEX_PRE, Q1, X1, 96));
	DIS("ldr b1, [x1], #96", fp.LDR(8, INDEX_POST, Q1, X1, 96));
	DIS("ldr b1, [x1, #96]", fp.LDR(8, INDEX_UNSIGNED, Q1, X1, 96));
	DIS("ldr h1, [x1, #96]", fp.LDR(16, INDEX_UNSIGNED, Q1, X1, 96));
	DIS("ldr s1, [x1, #96]", fp.LDR(32, INDEX_UNSIGNED, S1, X1, 96));
	DIS("ldr d1, [x1, #96]", fp.LDR(64, INDEX_UNSIGNED, D1, X1, 96));
	DIS("ldr q1, [x1, #96]", fp.LDR(128, INDEX_UNSIGNED, Q1, X1, 96));
	DIS("ldr q1, [sp], #-16", fp.LDR(128, INDEX_POST, Q1, SP, -16));
	DIS("str b1, [x1, #96]", fp.STR(8, INDEX_UNSIGNED, Q1, X1, 96));
	DIS("str h1, [x1, #-2]!", fp.STR(16, INDEX_PRE, Q1, X1, -2));
	DIS("str s1, [x1, #96]", fp.STR(32, INDEX_UNSIGNED, S1, X1, 96));
	DIS("str d1, [x1], #8", fp.STR(64, INDEX_POST, D1, X1, 8));
	DIS("str q1, [x1, #96]", fp.STR(128, INDEX_UNSIGNED, Q1, X1, 96));
	DIS("str q1, [sp, #-32]!", fp.STR(128, INDEX_PRE, Q1, SP, -32));

	// Load/store (unscaled)
	DIS("ldur b1, [x2, #-1]", fp.LDUR(8, Q1, X2, -1));
	DIS("ldur h1, [x2, #3]", fp.LDUR(16, Q1, X2, 3));
	DIS("ldur s1, [x2, #-4]", fp.LDUR(32, S1, X2, -4));
	DIS("ldur d1, [x2, #5]", fp.LDUR(64, D1, X2, 5));
	DIS("ldur q1, [x2, #-16]", fp.LDUR(128, Q1, X2, -16));
	DIS("stur b1, [x2, #-1]", fp.STUR(8, Q1, X2, -1));
	DIS("stur h1, [x2, #3]", fp.STUR(16, Q1, X2, 3));
	DIS("stur s1, [x2, #-4]", fp.STUR(32, S1, X2, -4));
	DIS("stur d1, [x2, #5]", fp.STUR(64, D1, X2, 5));
	DIS("stur q1, [sp, #255]", fp.STUR(128, Q1, SP, 255));

	// Load/store (register offset)
	DIS("ldr b1, [x2, x3]", fp.LDR(8, Q1, X2, ArithOption(X3)));
	DIS("ldr h1, [x2, x3, lsl #1]", fp.LDR(16, Q1, X2, ArithOption(X3, true)));
	DIS("ldr s1, [x2, x3, lsl #2]", fp.LDR(32, S1, X2, ArithOption(X3, true)));
	DIS("ldr d1, [x2, w3, sxtw]", fp.LDR(64, D1, X2, ArithOption(W3, false, true)));
	DIS("ldr q1, [x2, x3, lsl #4]", fp.LDR(128, Q1, X2, ArithOption(X3, true)));
	DIS("str b1, [x2, x3]", fp.STR(8, Q1, X2, ArithOption(X3)));
	DIS("str h1, [x2, x3]", fp.STR(16, Q1, X2, ArithOption(X3)));
	DIS("str s1, [x2, w3, uxtw]", fp.STR(32, S1, X2, ArithOption(W3)));
	DIS("str d1, [x2, x3, lsl #3]", fp.STR(64, D1, X2, ArithOption(X3, true)));
	DIS("str q1, [x2, x3]", fp.STR(128, Q1, X2, ArithOption(X3)));

	// Load/store pair
	DIS("ldp s1, s2, [x3, #8]", fp.LDP(32, INDEX_SIGNED, S1, S2, X3, 8));
	DIS("ldp d1, d2, [sp], #16", fp.LDP(64, INDEX_POST, D1, D2, SP, 16));
	DIS("ldp q3, q7, [x3, #32]", fp.LDP(128, INDEX_SIGNED, Q3, Q7, X3, 32));
	DIS("ldp q3, q7, [x3, #-1024]!", fp.LDP(128, INDEX_PRE, Q3, Q7, X3, -1024));
	DIS("stp s1, s2, [x3, #-8]!", fp.STP(32, INDEX_PRE, S1, S2, X3, -8));
	DIS("stp d8, d9, [sp, #504]", fp.STP(64, INDEX_SIGNED, D8, D9, SP, 504));
	DIS("stp q3, q7, [x3, #32]", fp.STP(128, INDEX_SIGNED, Q3, Q7, X3, 32));
	DIS("stp q3, q7, [x3], #1008", fp.STP(128, INDEX_POST, Q3, Q7, X3, 1008));

	// Load/store single structure
	DIS("ld1 q1.b[15], [x2]", fp.LD1(8, Q1, 15, X2));
	DIS("ld1 q1.h[2], [x2]", fp.LD1(16, Q1, 2, X2));
	DIS("ld1 q1.s[3], [x2]", fp.LD1(32, Q1, 3, X2));
	DIS("ld1 q1.d[1], [x2]", fp.LD1(64, Q1, 1, X2));
	DIS("ld1 q1.b[0], [x2], x3", fp.LD1(8, Q1, 0, X2, X3));
	DIS("ld1 q1.s[1], [x2], x3", fp.LD1(32, Q1, 1, X2, X3));
	DIS("st1 q1.b[9], [x2]", fp.ST1(8, Q1, 9, X2));
	DIS("st1 q1.h[7], [x2]", fp.ST1(16, Q1, 7, X2));
	DIS("st1 q1.s[2], [x2]", fp.ST1(32, Q1, 2, X2));
	DIS("st1 q1.d[0], [sp]", fp.ST1(64, Q1, 0, SP));
	DIS("st1 q1.h[1], [x2], x3", fp.ST1(16, Q1, 1, X2, X3));
	DIS("st1 q1.d[1], [x2], x3", fp.ST1(64, Q1, 1, X2, X3));
	DIS("ld1r.8 {q1}, [x2]", fp.LD1R(8, Q1, X2));
	DIS("ld1r.32 {d1}, [x2]", fp.LD1R(32, D1, X2));
	DIS("ld1r.16 {q1}, [x2], x3", fp.LD1R(16, Q1, X2, X3));
	DIS("ld1r.64 {q1}, [x2], x3", fp.LD1R(64, Q1, X2, X3));
	DIS("ld2r.8 {d1, d2}, [x2]", fp.LD2R(8, D1, X2));
	DIS("ld2r.32 {q1, q2}, [x2]", fp.LD2R(32, Q1, X2));
	DIS("ld2r.16 {d1, d2}, [x2], x3", fp.LD2R(16, D1, X2, X3));
	DIS("ld2r.64 {q1, q2}, [x2], x3", fp.LD2R(64, Q1, X2, X3));

	// Load/store multiple structures
	DIS("ld1.8 {q1}, [x2]", fp.LD1(8, 1, Q1, X2));
	DIS("ld1.32 {d1, d2}, [x2]", fp.LD1(32, 2, D1, X2));
	DIS("ld1.16 {q30, q31, q0}, [x2]", fp.LD1(16, 3, Q30, X2));
	DIS("ld1.64 {q1, q2, q3, q4}, [sp]", fp.LD1(64, 4, Q1, SP));
	DIS("ld4.32 {q0, q1, q2, q3}, [x2]", fp.LD4(32, Q0, X2));
	DIS("ld4.8 {d30, d31, d0, d1}, [x2]", fp.LD4(8, D30, X2));
	DIS("ld1.32 {q1}, [x2], x3", fp.LD1(32, 1, INDEX_POST, Q1, X2, X3));
	DIS("ld1.8 {d1, d2, d3, d4}, [x2], x3", fp.LD1(8, 4, INDEX_POST, D1, X2, X3));
	DIS("st1.8 {q1}, [x2]", fp.ST1(8, 1, Q1, X2));
	DIS("st1.32 {d1, d2}, [x2]", fp.ST1(32, 2, D1, X2));
	DIS("st1.16 {q30, q31, q0}, [x2]", fp.ST1(16, 3, Q30, X2));
	DIS("st1.64 {q1, q2, q3, q4}, [sp]", fp.ST1(64, 4, Q1, SP));
	DIS("st1.32 {q1}, [x2], x3", fp.ST1(32, 1, INDEX_POST, Q1, X2, X3));
	DIS("st1.8 {d1, d2}, [x2], x3", fp.ST1(8, 2, INDEX_POST, D1, X2, X3));

	// Scalar 1 source
	DIS("fabs s20, s25", fp.FABS(S20, S25));
	DIS("fabs d1, d2", fp.FABS(D1, D2));
	DIS("fneg s20, s25", fp.FNEG(S20, S25));
	DIS("fneg d1, d2", fp.FNEG(D1, D2));
	DIS("fsqrt s20, s25", fp.FSQRT(S20, S25));
	DIS("fsqrt d1, d2", fp.FSQRT(D1, D2));
	DIS("fmov s20, s25", fp.FMOV(S20, S25));
	DIS("fmov d1, d2", fp.FMOV(D1, D2));
	DIS("fmov w1, s3", fp.FMOV(W1, S3));
	DIS("fmov s1, w3", fp.FMOV(S1, W3));
	DIS("fcvt d1, s2", fp.FCVT(64, 32, D1, S2));
	DIS("fcvt s1, d2", fp.FCVT(32, 64, S1, D2));
	DIS("fcvt h1, s2", fp.FCVT(16, 32, S1, S2));
	DIS("fcvt s1, h2", fp.FCVT(32, 16, S1, S2));

	// Scalar pairwise
	DIS("faddp s1, d2", fp.FADDP(S1, D2));
	DIS("faddp d1, q2", fp.FADDP(D1, Q2));
	DIS("fmaxp s1, d2", fp.FMAXP(S1, D2));
	DIS("fmaxp d1, q2", fp.FMAXP(D1, Q2));
	DIS("fminp s1, d2", fp.FMINP(S1, D2));
	DIS("fminp d1, q2", fp.FMINP(D1, Q2));
	DIS("fmaxnmp s1, d2", fp.FMAXNMP(S1, D2));
	DIS("fmaxnmp d1, q2", fp.FMAXNMP(D1, Q2));
	DIS("fminnmp s1, d2", fp.FMINNMP(S1, D2));
	DIS("fminnmp d1, q2", fp.FMINNMP(D1, Q2));

	// Scalar 2 source
	DIS("fadd s1, s2, s3", fp.FADD(S1, S2, S3));
	DIS("fadd d1, d2, d3", fp.FADD(D1, D2, D3));
	DIS("fmul s0, s12, s22", fp.FMUL(S0, S12, S22));
	DIS("fmul d1, d2, d3", fp.FMUL(D1, D2, D3));
	DIS("fsub s1, s2, s3", fp.FSUB(S1, S2, S3));
	DIS("fsub d1, d2, d3", fp.FSUB(D1, D2, D3));
	DIS("fdiv s1, s2, s3", fp.FDIV(S1, S2, S3));
	DIS("fdiv d1, d2, d3", fp.FDIV(D1, D2, D3));
	DIS("fmax s1, s2, s3", fp.FMAX(S1, S2, S3));
	DIS("fmax d1, d2, d3", fp.FMAX(D1, D2, D3));
	DIS("fmin s1, s2, s3", fp.FMIN(S1, S2, S3));
	DIS("fmin d1, d2, d3", fp.FMIN(D1, D2, D3));
	DIS("fmaxnm s1, s2, s3", fp.FMAXNM(S1, S2, S3));
	DIS("fmaxnm d1, d2, d3", fp.FMAXNM(D1, D2, D3));
	DIS("fminnm s1, s2, s3", fp.FMINNM(S1, S2, S3));
	DIS("fminnm d1, d2, d3", fp.FMINNM(D1, D2, D3));
	DIS("fnmul s1, s2, s3", fp.FNMUL(S1, S2, S3));
	DIS("fnmul d1, d2, d3", fp.FNMUL(D1, D2, D3));

	// Scalar 3 source
	DIS("fmadd s1, s2, s3, s4", fp.FMADD(S1, S2, S3, S4));
	DIS("fmadd d1, d2, d3, d4", fp.FMADD(D1, D2, D3, D4));
	DIS("fmsub s1, s2, s3, s4", fp.FMSUB(S1, S2, S3, S4));
	DIS("fmsub d1, d2, d3, d4", fp.FMSUB(D1, D2, D3, D4));
	DIS("fnmadd s1, s2, s3, s4", fp.FNMADD(S1, S2, S3, S4));
	DIS("fnmadd d1, d2, d3, d4", fp.FNMADD(D1, D2, D3, D4));
	DIS("fnmsub s1, s2, s3, s4", fp.FNMSUB(S1, S2, S3, S4));
	DIS("fnmsub d1, d2, d3, d4", fp.FNMSUB(D1, D2, D3, D4));

	// Scalar immediate
	uint8_t imm8 = 0;
	FPImm8FromFloat(1.0f, &imm8);
	DIS("fmov s7, #1.000000", fp.FMOV(S7, imm8));
	FPImm8FromFloat(-0.5f, &imm8);
	DIS("fmov d7, #-0.500000", fp.FMOV(D7, imm8));
	DIS("fmov s1, wzr", fp.MOVI2F(S1, 0.0f));
	DIS("fmov s1, #-2.000000", fp.MOVI2F(S1, -2.0f));
	DIS("fmov s1, #-1.000000", fp.MOVI2F(S1, 1.0f, INVALID_REG, true));
	DIS("movz w2, #0xcccd << 0; movk w2, #0x3f8c << 16; fmov s1, w2", fp.MOVI2F(S1, 1.1f, W2));

	// Vector logical
	DIS("and q1, q2, q3", fp.AND(Q1, Q2, Q3));
	DIS("and d1, d2, d3", fp.AND(D1, D2, D3));
	DIS("bic q1, q2, q3", fp.BIC(Q1, Q2, Q3));
	DIS("bic d1, d2, d3", fp.BIC(D1, D2, D3));
	DIS("eor q0, q1, q2", fp.EOR(Q0, Q1, Q2));
	DIS("eor d0, d1, d2", fp.EOR(D0, D1, D2));
	DIS("bsl q1, q2, q3", fp.BSL(Q1, Q2, Q3));
	DIS("bsl d1, d2, d3", fp.BSL(D1, D2, D3));
	DIS("bit q1, q2, q3", fp.BIT(Q1, Q2, Q3));
	DIS("bit d1, d2, d3", fp.BIT(D1, D2, D3));
	DIS("bif q1, q2, q3", fp.BIF(Q1, Q2, Q3));
	DIS("bif d1, d2, d3", fp.BIF(D1, D2, D3));
	DIS("orr q1, q2, q3", fp.ORR(Q1, Q2, Q3));
	DIS("orr d1, d2, d3", fp.ORR(D1, D2, D3));
	DIS("mov q20, q25", fp.MOV(Q20, Q25));
	DIS("mov d1, d2", fp.MOV(D1, D2));
	DIS("not q1, q2", fp.NOT(Q1, Q2));
	DIS("not d1, d2", fp.NOT(D1, D2));

	// Vector float arithmetic
	DIS("fabs.32 q1, q2", fp.FABS(32, Q1, Q2));
	DIS("fabs.64 q1, q2", fp.FABS(64, Q1, Q2));
	DIS("fadd.32 q1, q13, q21", fp.FADD(32, Q1, Q13, Q21));
	DIS("fadd.64 q1, q2, q3", fp.FADD(64, Q1, Q2, Q3));
	DIS("faddp.32 d1, d2, d3", fp.FADDP(32, D1, D2, D3));
	DIS("faddp.64 q1, q2, q3", fp.FADDP(64, Q1, Q2, Q3));
	DIS("fmax.32 q1, q2, q3", fp.FMAX(32, Q1, Q2, Q3));
	DIS("fmax.64 q1, q2, q3", fp.FMAX(64, Q1, Q2, Q3));
	DIS("fmaxnm.32 d1, d2, d3", fp.FMAXNM(32, D1, D2, D3));
	DIS("fmaxnm.64 q1, q2, q3", fp.FMAXNM(64, Q1, Q2, Q3));
	DIS("fmin.32 q1, q2, q3", fp.FMIN(32, Q1, Q2, Q3));
	DIS("fmin.64 q1, q2, q3", fp.FMIN(64, Q1, Q2, Q3));
	DIS("fminnm.32 d1, d2, d3", fp.FMINNM(32, D1, D2, D3));
	DIS("fminnm.64 q1, q2, q3", fp.FMINNM(64, Q1, Q2, Q3));
	DIS("fmla.32 d1, d2, d3", fp.FMLA(32, D1, D2, D3));
	DIS("fmla.64 q1, q2, q3", fp.FMLA(64, Q1, Q2, Q3));
	DIS("fmls.32 d1, d2, d3", fp.FMLS(32, D1, D2, D3));
	DIS("fmls.64 q1, q2, q3", fp.FMLS(64, Q1, Q2, Q3));
	DIS("fdiv.32 q1, q2, q3", fp.FDIV(32, Q1, Q2, Q3));
	DIS("fdiv.64 q1, q2, q3", fp.FDIV(64, Q1, Q2, Q3));
	DIS("fmul.32 d1, d13, d21", fp.FMUL(32, D1, D13, D21));
	DIS("fmul.64 q1, q2, q3", fp.FMUL(64, Q1, Q2, Q3));
	DIS("fsub.32 q1, q2, q3", fp.FSUB(32, Q1, Q2, Q3));
	DIS("fsub.64 q1, q2, q3", fp.FSUB(64, Q1, Q2, Q3));
	DIS("fneg.32 q1, q2", fp.FNEG(32, Q1, Q2));
	DIS("fneg.64 q1, q2", fp.FNEG(64, Q1, Q2));
	DIS("frsqrte.32 q1, q2", fp.FRSQRTE(32, Q1, Q2));
	DIS("frsqrte.64 q1, q2", fp.FRSQRTE(64, Q1, Q2));

	// Vector float conversion
	DIS("fcvtl.32.16 q1, d2", fp.FCVTL(32, Q1, D2));
	DIS("fcvtl.64.32 q1, d2", fp.FCVTL(64, Q1, D2));
	DIS("fcvtl2.32.16 q1, q2", fp.FCVTL2(32, Q1, Q2));
	DIS("fcvtl2.64.32 q1, q2", fp.FCVTL2(64, Q1, Q2));
	DIS("fcvtn.32.64 d1, q2", fp.FCVTN(32, D1, Q2));
	DIS("fcvtn2.16.32 q1, q2", fp.FCVTN(16, Q1, Q2));
	DIS("fcvtzs.32 q1, q2", fp.FCVTZS(32, Q1, Q2));
	DIS("fcvtzs.64 q1, q2", fp.FCVTZS(64, Q1, Q2));
	DIS("fcvtzu.32 d1, d2", fp.FCVTZU(32, D1, D2));
	DIS("fcvtzu.64 q1, q2", fp.FCVTZU(64, Q1, Q2));
	DIS("fcvtzs.32 q1, q2, #8", fp.FCVTZS(32, Q1, Q2, 8));
	DIS("fcvtzs.64 q1, q2, #1", fp.FCVTZS(64, Q1, Q2, 1));
	DIS("fcvtzu.32 d1, d2, #31", fp.FCVTZU(32, D1, D2, 31));
	DIS("fcvtzu.64 q1, q2, #16", fp.FCVTZU(64, Q1, Q2, 16));
	DIS("scvtf.32 q1, q2", fp.SCVTF(32, Q1, Q2));
	DIS("scvtf.64 q1, q2", fp.SCVTF(64, Q1, Q2));
	DIS("ucvtf.32 d1, d2", fp.UCVTF(32, D1, D2));
	DIS("ucvtf.64 q1, q2", fp.UCVTF(64, Q1, Q2));
	DIS("scvtf.32 q3, q7, #7", fp.SCVTF(32, Q3, Q7, 7));
	DIS("scvtf.64 q1, q2, #33", fp.SCVTF(64, Q1, Q2, 33));
	DIS("ucvtf.32 d3, d7, #15", fp.UCVTF(32, D3, D7, 15));
	DIS("ucvtf.64 q1, q2, #1", fp.UCVTF(64, Q1, Q2, 1));

	// Vector integer
	DIS("umin.32 d0, d3, d4", fp.UMIN(32, D0, D3, D4));
	DIS("umin.8 q0, q3, q4", fp.UMIN(8, Q0, Q3, Q4));
	DIS("umax.16 q0, q3, q4", fp.UMAX(16, Q0, Q3, Q4));
	DIS("umax.32 d0, d3, d4", fp.UMAX(32, D0, D3, D4));
	DIS("add.32 q0, q3, q4", fp.ADD(32, Q0, Q3, Q4));
	DIS("add.16 d0, d3, d4", fp.ADD(16, D0, D3, D4));
	DIS("sub.32 q0, q3, q4", fp.SUB(32, Q0, Q3, Q4));
	DIS("sub.8 d0, d3, d4", fp.SUB(8, D0, D3, D4));
	DIS("umaxp.32 q0, q3, q4", fp.UMAXP(32, Q0, Q3, Q4));
	DIS("umaxp.8 d0, d3, d4", fp.UMAXP(8, D0, D3, D4));
	DIS("ushl.32 q1, q2, q3", fp.USHL(32, Q1, Q2, Q3));
	DIS("ushl.16 d1, d2, d3", fp.USHL(16, D1, D2, D3));
	DIS("clz.32 q1, q2", fp.CLZ(32, Q1, Q2));
	DIS("abs.32 q1, q2", fp.ABS(32, Q1, Q2));
	DIS("abs.8 d3, d4", fp.ABS(8, D3, D4));
	DIS("sli.32 q5, q6, #23", fp.SLI(32, Q5, Q6, 23));
	DIS("umull.64.32 q6, d0, q17.s[2]", fp.UMULL(32, Q6, D0, Q17, 2));
	DIS("umull2.64.32 q0, q0, q31.s[3]", fp.UMULL2(32, Q0, Q0, Q31, 3));
	DIS("umull.64.32 q1, d2, q3.s[1]", fp.UMULL(32, Q1, D2, Q3, 1));
	DIS("clz.8 d1, d2", fp.CLZ(8, D1, D2));
	DIS("umull.64.32 q1, d2, d3", fp.UMULL(32, Q1, D2, D3));
	DIS("umull.16.8 q4, d5, d6", fp.UMULL(8, Q4, D5, D6));
	DIS("umull2.64.32 q1, q2, q3", fp.UMULL2(32, Q1, Q2, Q3));
	DIS("umull2.32.16 q7, q8, q9", fp.UMULL2(16, Q7, Q8, Q9));
	DIS("addv.32 s1, q2", fp.ADDV(32, S1, Q2));
	DIS("addv.16 h3, d4", fp.ADDV(16, S3, D4));
	DIS("smin.8 d0, d3, d4", fp.SMIN(8, D0, D3, D4));
	DIS("smin.16 q0, q3, q4", fp.SMIN(16, Q0, Q3, Q4));
	DIS("smax.16 d0, d3, d4", fp.SMAX(16, D0, D3, D4));
	DIS("smax.32 q0, q3, q4", fp.SMAX(32, Q0, Q3, Q4));
	DIS("rev16.8 q1, q2", fp.REV16(8, Q1, Q2));
	DIS("rev16.8 d1, d2", fp.REV16(8, D1, D2));
	DIS("rev32.8 q1, q2", fp.REV32(8, Q1, Q2));
	DIS("rev32.16 d1, d2", fp.REV32(16, D1, D2));
	DIS("rev64.8 q1, q2", fp.REV64(8, Q1, Q2));
	DIS("rev64.32 d1, d2", fp.REV64(32, D1, D2));
	DIS("sqxtn.8.16 d1, q2", fp.SQXTN(8, D1, Q2));
	DIS("sqxtn.32.64 d1, q2", fp.SQXTN(32, D1, Q2));
	DIS("sqxtn2.16.32 q1, q2", fp.SQXTN2(16, Q1, Q2));
	DIS("sqxtn2.8.16 q1, q2", fp.SQXTN2(8, Q1, Q2));
	DIS("uqxtn.8.16 d1, q2", fp.UQXTN(8, D1, Q2));
	DIS("uqxtn.16.32 d1, q2", fp.UQXTN(16, D1, Q2));
	DIS("uqxtn2.32.64 q1, q2", fp.UQXTN2(32, Q1, Q2));
	DIS("uqxtn2.8.16 q1, q2", fp.UQXTN2(8, Q1, Q2));
	DIS("xtn.32.64 d0, q3", fp.XTN(32, D0, Q3));
	DIS("xtn.8.16 d4, q1", fp.XTN(8, D4, Q1));
	DIS("xtn2.16.32 q1, q2", fp.XTN2(16, Q1, Q2));
	DIS("xtn2.32.64 q1, q2", fp.XTN2(32, Q1, Q2));

	// Vector compares
	DIS("cmeq.8 q1, q2, q3", fp.CMEQ(8, Q1, Q2, Q3));
	DIS("cmeq.32 d1, d2, d3", fp.CMEQ(32, D1, D2, D3));
	DIS("cmge.16 q1, q2, q3", fp.CMGE(16, Q1, Q2, Q3));
	DIS("cmge.64 q1, q2, q3", fp.CMGE(64, Q1, Q2, Q3));
	DIS("cmgt.8 d1, d2, d3", fp.CMGT(8, D1, D2, D3));
	DIS("cmgt.32 q1, q2, q3", fp.CMGT(32, Q1, Q2, Q3));
	DIS("cmhi.16 q1, q2, q3", fp.CMHI(16, Q1, Q2, Q3));
	DIS("cmhi.8 d1, d2, d3", fp.CMHI(8, D1, D2, D3));
	DIS("cmhs.32 q1, q2, q3", fp.CMHS(32, Q1, Q2, Q3));
	DIS("cmhs.16 d1, d2, d3", fp.CMHS(16, D1, D2, D3));
	DIS("cmtst.8 q1, q2, q3", fp.CMTST(8, Q1, Q2, Q3));
	DIS("cmtst.32 d1, d2, d3", fp.CMTST(32, D1, D2, D3));
	DIS("cmeq.8 q1, q2, #0", fp.CMEQ(8, Q1, Q2));
	DIS("cmeq.64 q1, q2, #0", fp.CMEQ(64, Q1, Q2));
	DIS("cmge.16 q1, q2, #0", fp.CMGE(16, Q1, Q2));
	DIS("cmge.32 d1, d2, #0", fp.CMGE(32, D1, D2));
	DIS("cmgt.8 d1, d2, #0", fp.CMGT(8, D1, D2));
	DIS("cmgt.32 q1, q2, #0", fp.CMGT(32, Q1, Q2));
	DIS("cmle.16 q1, q2, #0", fp.CMLE(16, Q1, Q2));
	DIS("cmle.8 d1, d2, #0", fp.CMLE(8, D1, D2));
	DIS("cmlt.32 q1, q2, #0", fp.CMLT(32, Q1, Q2));
	DIS("cmlt.64 q1, q2, #0", fp.CMLT(64, Q1, Q2));
	DIS("fcmeq.32 q1, q2, q3", fp.FCMEQ(32, Q1, Q2, Q3));
	DIS("fcmeq.64 q1, q2, q3", fp.FCMEQ(64, Q1, Q2, Q3));
	DIS("fcmeq.32 d1, d2, #0.0", fp.FCMEQ(32, D1, D2));
	DIS("fcmeq.64 q1, q2, #0.0", fp.FCMEQ(64, Q1, Q2));
	DIS("fcmge.32 q1, q2, q3", fp.FCMGE(32, Q1, Q2, Q3));
	DIS("fcmge.64 q1, q2, q3", fp.FCMGE(64, Q1, Q2, Q3));
	DIS("fcmge.32 q1, q2, #0.0", fp.FCMGE(32, Q1, Q2));
	DIS("fcmge.64 q1, q2, #0.0", fp.FCMGE(64, Q1, Q2));
	DIS("fcmgt.32 d1, d2, d3", fp.FCMGT(32, D1, D2, D3));
	DIS("fcmgt.64 q1, q2, q3", fp.FCMGT(64, Q1, Q2, Q3));
	DIS("fcmgt.32 q1, q2, #0.0", fp.FCMGT(32, Q1, Q2));
	DIS("fcmgt.64 q1, q2, #0.0", fp.FCMGT(64, Q1, Q2));
	DIS("fcmle.32 q1, q2, #0.0", fp.FCMLE(32, Q1, Q2));
	DIS("fcmle.64 q1, q2, #0.0", fp.FCMLE(64, Q1, Q2));
	DIS("fcmlt.32 d1, d2, #0.0", fp.FCMLT(32, D1, D2));
	DIS("fcmlt.64 q1, q2, #0.0", fp.FCMLT(64, Q1, Q2));

	// Moves between lanes and registers
	DIS("dup q1, q30.s[3]", fp.DUP(32, Q1, Q30, 3));
	DIS("dup d1, q2.b[15]", fp.DUP(8, D1, Q2, 15));
	DIS("dup.16 q1, w2", fp.DUP(16, Q1, W2));
	DIS("dup.64 q1, x2", fp.DUP(64, Q1, X2));
	DIS("ins q3.s[1], q12.s[3]", fp.INS(32, Q3, 1, Q12, 3));
	DIS("ins q4.b[5], q11.b[2]", fp.INS(8, D4, 5, D11, 2));
	DIS("ins q1.h[7], w2", fp.INS(16, Q1, 7, W2));
	DIS("ins q1.d[1], x2", fp.INS(64, Q1, 1, X2));
	DIS("umov w1, q2.b[15]", fp.UMOV(8, W1, Q2, 15));
	DIS("umov w1, q2.h[3]", fp.UMOV(16, W1, Q2, 3));
	DIS("umov w1, q2.s[1]", fp.UMOV(32, W1, Q2, 1));
	DIS("umov x1, q2.d[1]", fp.UMOV(64, X1, Q2, 1));
	DIS("smov w1, q2.b[15]", fp.SMOV(8, W1, Q2, 15));
	DIS("smov x1, q2.h[3]", fp.SMOV(16, X1, Q2, 3));
	DIS("smov x1, q2.s[1]", fp.SMOV(32, X1, Q2, 1));

	// Vector immediates
	FPImm8FromFloat(1.0f, &imm8);
	DIS("fmov.32 q1, #1.000000", fp.FMOV(32, Q1, imm8));
	FPImm8FromFloat(-2.0f, &imm8);
	DIS("fmov.32 d1, #-2.000000", fp.FMOV(32, D1, imm8));
	DIS("fmov.64 q1, #-2.000000", fp.FMOV(64, Q1, imm8));
	DIS("movi.8 q1, #0xff", fp.MOVI(8, Q1, 0xFF));
	DIS("movi.8 d1, #0x12", fp.MOVI(8, D1, 0x12));
	DIS("movi.16 q1, #0x34", fp.MOVI(16, Q1, 0x34));
	DIS("movi.16 d1, #0x34, lsl #8", fp.MOVI(16, D1, 0x34, 8));
	DIS("movi.32 q1, #0x56, lsl #24", fp.MOVI(32, Q1, 0x56, 24));
	DIS("movi.32 d1, #0x56", fp.MOVI(32, D1, 0x56, 0));
	DIS("movi.32 q1, #0x78, msl #8", fp.MOVI(32, Q1, 0x78, 8, true));
	DIS("movi.32 q1, #0x78, msl #16", fp.MOVI(32, Q1, 0x78, 16, true));
	DIS("movi.64 q1, #0xff00ff0000ff00ff", fp.MOVI(64, Q1, 0xA5));
	DIS("movi d1, #0xffffffffffffffff", fp.MOVI(64, D1, 0xFF));
	DIS("mvni.16 q1, #0x34", fp.MVNI(16, Q1, 0x34));
	DIS("mvni.16 d1, #0x34, lsl #8", fp.MVNI(16, D1, 0x34, 8));
	DIS("mvni.32 q1, #0x56, lsl #16", fp.MVNI(32, Q1, 0x56, 16));
	DIS("mvni.32 q1, #0x78, msl #8", fp.MVNI(32, Q1, 0x78, 8, true));
	DIS("orr.16 q1, #0x12, lsl #8", fp.ORR(16, Q1, 0x12, 8));
	DIS("orr.32 d1, #0x12, lsl #24", fp.ORR(32, D1, 0x12, 24));
	DIS("bic.16 q1, #0x12", fp.BIC(16, Q1, 0x12, 0));
	DIS("bic.32 d1, #0x12, lsl #16", fp.BIC(32, D1, 0x12, 16));
	DIS("movi.32 q0, #0x00", fp.MOVI2FDUP(Q0, 0.0f));
	DIS("fmov.32 q0, #-1.000000", fp.MOVI2FDUP(Q0, 1.0f, INVALID_REG, true));
	DIS("movz w1, #0xcccd << 0; movk w1, #0x3f8c << 16; dup.32 q0, w1", fp.MOVI2FDUP(Q0, 1.1f, W1));

	// Scalar float <-> int conversion
	DIS("fcvtns w13, s12", fp.FCVTS(W13, S12, ROUND_N));
	DIS("fcvtms x3, d2", fp.FCVTS(X3, D2, ROUND_M));
	DIS("fcvtas w1, d2", fp.FCVTS(W1, D2, ROUND_A));
	DIS("fcvtps x1, s2", fp.FCVTS(X1, S2, ROUND_P));
	DIS("fcvtzs w12, s3", fp.FCVTS(W12, S3, ROUND_Z));
	DIS("fcvtnu w1, s2", fp.FCVTU(W1, S2, ROUND_N));
	DIS("fcvtzu x1, d2", fp.FCVTU(X1, D2, ROUND_Z));
	DIS("fcvtau w1, s2", fp.FCVTU(W1, S2, ROUND_A));
	DIS("fcvtns s13, s12", fp.FCVTS(S13, S12, ROUND_N));
	DIS("fcvtps d13, d12", fp.FCVTS(D13, D12, ROUND_P));
	DIS("fcvtzs s22, s12", fp.FCVTS(S22, S12, ROUND_Z));
	DIS("fcvtms d1, d2", fp.FCVTS(D1, D2, ROUND_M));
	DIS("fcvtas s1, s2", fp.FCVTS(S1, S2, ROUND_A));
	DIS("fcvtnu s1, s2", fp.FCVTU(S1, S2, ROUND_N));
	DIS("fcvtzu d1, d2", fp.FCVTU(D1, D2, ROUND_Z));
	DIS("fcvtzs w1, s2, #8", fp.FCVTZS(W1, S2, 8));
	DIS("fcvtzs x1, d2, #32", fp.FCVTZS(X1, D2, 32));
	DIS("fcvtzu w1, s2, #1", fp.FCVTZU(W1, S2, 1));
	DIS("fcvtzu x1, d2, #64", fp.FCVTZU(X1, D2, 64));
	DIS("fcvtzs s1, s2, #8", fp.FCVTZS(S1, S2, 8));
	DIS("fcvtzs d1, d2, #40", fp.FCVTZS(D1, D2, 40));
	DIS("fcvtzu s1, s2, #16", fp.FCVTZU(S1, S2, 16));
	DIS("fcvtzu d1, d2, #1", fp.FCVTZU(D1, D2, 1));
	DIS("scvtf s13, w7", fp.SCVTF(S13, W7));
	DIS("scvtf d1, x2", fp.SCVTF(D1, X2));
	DIS("ucvtf s1, x2", fp.UCVTF(S1, X2));
	DIS("ucvtf d1, w2", fp.UCVTF(D1, W2));
	DIS("scvtf s13, s12", fp.SCVTF(S13, S12));
	DIS("scvtf d1, d2", fp.SCVTF(D1, D2));
	DIS("ucvtf s1, s2", fp.UCVTF(S1, S2));
	DIS("ucvtf d1, d2", fp.UCVTF(D1, D2));
	DIS("scvtf s12, w13, #12", fp.SCVTF(S12, W13, 12));
	DIS("scvtf d1, x2, #64", fp.SCVTF(D1, X2, 64));
	DIS("ucvtf s12, x3, #8", fp.UCVTF(S12, X3, 8));
	DIS("ucvtf d1, w2, #1", fp.UCVTF(D1, W2, 1));
	DIS("scvtf s1, s2, #8", fp.SCVTF(S1, S2, 8));
	DIS("scvtf d1, d2, #40", fp.SCVTF(D1, D2, 40));
	DIS("ucvtf s1, s2, #16", fp.UCVTF(S1, S2, 16));
	DIS("ucvtf d1, d2, #1", fp.UCVTF(D1, D2, 1));

	// Compare and select
	DIS("fcmp s7, s3", fp.FCMP(S7, S3));
	DIS("fcmp d7, d3", fp.FCMP(D7, D3));
	DIS("fcmp s7, #0.0", fp.FCMP(S7));
	DIS("fcmp d7, #0.0", fp.FCMP(D7));
	DIS("fcmpe s7, s3", fp.FCMPE(S7, S3));
	DIS("fcmpe d7, d3", fp.FCMPE(D7, D3));
	DIS("fcmpe s7, #0.0", fp.FCMPE(S7));
	DIS("fcmpe d7, #0.0", fp.FCMPE(D7));
	DIS("fcsel s0, s1, s2, cs", fp.FCSEL(S0, S1, S2, CC_CS));
	DIS("fcsel d0, d1, d2, le", fp.FCSEL(D0, D1, D2, CC_LE));
	DIS("fccmp s1, s2, #4, eq", fp.FCCMP(S1, S2, 0x4, CC_EQ));
	DIS("fccmp d1, d2, #15, gt", fp.FCCMP(D1, D2, 0xF, CC_GT));
	DIS("fccmpe s1, s2, #0, ne", fp.FCCMPE(S1, S2, 0x0, CC_NEQ));
	DIS("fccmpe d1, d2, #8, mi", fp.FCCMPE(D1, D2, 0x8, CC_MI));

	// Permute
	DIS("uzp1.8 q1, q2, q3", fp.UZP1(8, Q1, Q2, Q3));
	DIS("uzp1.32 d1, d2, d3", fp.UZP1(32, D1, D2, D3));
	DIS("trn1.16 q1, q2, q3", fp.TRN1(16, Q1, Q2, Q3));
	DIS("trn1.64 q1, q2, q3", fp.TRN1(64, Q1, Q2, Q3));
	DIS("zip1.32 q1, q2, q3", fp.ZIP1(32, Q1, Q2, Q3));
	DIS("zip1.8 d1, d2, d3", fp.ZIP1(8, D1, D2, D3));
	DIS("uzp2.16 d1, d2, d3", fp.UZP2(16, D1, D2, D3));
	DIS("uzp2.64 q1, q2, q3", fp.UZP2(64, Q1, Q2, Q3));
	DIS("trn2.8 q1, q2, q3", fp.TRN2(8, Q1, Q2, Q3));
	DIS("trn2.32 d1, d2, d3", fp.TRN2(32, D1, D2, D3));
	DIS("zip2.16 q1, q2, q3", fp.ZIP2(16, Q1, Q2, Q3));
	DIS("zip2.64 q1, q2, q3", fp.ZIP2(64, Q1, Q2, Q3));
	DIS("ext q1, q2, q3, #15", fp.EXT(Q1, Q2, Q3, 15));
	DIS("ext d1, d2, d3, #4", fp.EXT(D1, D2, D3, 4));

	// Shift by immediate
	DIS("sshll.16.8 q1, d2, #3", fp.SSHLL(8, Q1, D2, 3));
	DIS("sshll.64.32 q1, d2, #31", fp.SSHLL(32, Q1, D2, 31));
	DIS("sshll2.32.16 q1, q2, #15", fp.SSHLL2(16, Q1, Q2, 15));
	DIS("sshll2.16.8 q1, q2, #1", fp.SSHLL2(8, Q1, Q2, 1));
	DIS("ushll.32.16 q1, d2, #4", fp.USHLL(16, Q1, D2, 4));
	DIS("uxtl.64.32 q1, d2", fp.USHLL(32, Q1, D2, 0));
	DIS("ushll2.16.8 q1, q2, #7", fp.USHLL2(8, Q1, Q2, 7));
	DIS("ushll2.64.32 q1, q2, #2", fp.USHLL2(32, Q1, Q2, 2));
	DIS("shll.16.8 q1, d2, #8", fp.SHLL(8, Q1, D2));
	DIS("shll.64.32 q1, d2, #32", fp.SHLL(32, Q1, D2));
	DIS("shll2.32.16 q1, q2, #16", fp.SHLL2(16, Q1, Q2));
	DIS("shll2.16.8 q1, q2, #8", fp.SHLL2(8, Q1, Q2));
	DIS("shrn.32.64 d0, q3, #8", fp.SHRN(32, D0, Q3, 8));
	DIS("usra.32 q1, q2, #21", fp.USRA(32, Q1, Q2, 21));
	DIS("usra.16 d1, d2, #3", fp.USRA(16, D1, D2, 3));
	DIS("shrn.8.16 d0, q3, #4", fp.SHRN(8, D0, Q3, 4));
	DIS("shrn2.16.32 q0, q3, #16", fp.SHRN2(16, Q0, Q3, 16));
	DIS("shrn2.32.64 q0, q3, #1", fp.SHRN2(32, Q0, Q3, 1));
	DIS("sxtl.16.8 q1, d2", fp.SXTL(8, Q1, D2));
	DIS("sxtl.64.32 q1, d2", fp.SXTL(32, Q1, D2));
	DIS("sxtl2.32.16 q1, q2", fp.SXTL2(16, Q1, Q2));
	DIS("sxtl2.16.8 q1, q2", fp.SXTL2(8, Q1, Q2));
	DIS("uxtl.16.8 q1, d8", fp.UXTL(8, Q1, D8));
	DIS("uxtl.32.16 q1, d8", fp.UXTL(16, Q1, D8));
	DIS("uxtl2.64.32 q1, q2", fp.UXTL2(32, Q1, Q2));
	DIS("uxtl2.16.8 q1, q2", fp.UXTL2(8, Q1, Q2));
	DIS("shl.32 d0, d3, #18", fp.SHL(32, D0, D3, 18));
	DIS("shl.8 q0, q3, #7", fp.SHL(8, Q0, Q3, 7));
	DIS("ushr.16 q0, q3, #7", fp.USHR(16, Q0, Q3, 7));
	DIS("ushr.64 q0, q3, #1", fp.USHR(64, Q0, Q3, 1));
	DIS("sshr.64 q0, q3, #38", fp.SSHR(64, Q0, Q3, 38));
	DIS("sshr.8 d0, d3, #1", fp.SSHR(8, D0, D3, 1));

	// Vector x indexed element
	DIS("fmul q0, q1, q2.4s[3]", fp.FMUL(32, Q0, Q1, Q2, 3));
	DIS("fmul q0, q1, q2.2d[1]", fp.FMUL(64, Q0, Q1, Q2, 1));
	DIS("fmla d0, d1, d2.2s[1]", fp.FMLA(32, D0, D1, D2, 1));
	DIS("fmla q0, q1, q31.2d[0]", fp.FMLA(64, Q0, Q1, Q31, 0));

	// ABI helpers
	DIS("stp x29, x30, [sp, #-48]!; mov x29, sp; str x19, [x29, #16]; str d8, [x29, #32]", fp.ABI_PushRegisters(1 << 19, 1 << 8));
	DIS("ldp x29, x30, [sp]; ldr x19, [sp, #16]; ldr d8, [sp, #32]; add sp, sp, #48", fp.ABI_PopRegisters(1 << 19, 1 << 8));
}

bool TestArm64Disasm() {
	// Page aligned, so ADRP's page-relative target comes out the same everywhere.
	alignas(4096) static u32 code[4096];
	const u8 *base = (const u8 *)code;
	ARM64XEmitter emitter(base, (u8 *)code);
	ARM64FloatEmitter fp(&emitter);
	DisasmChecker checker{ emitter, base };

	TestIntegerInstructions(checker, emitter, base);
	emitter.SetCodePointer(base + 0x2000, (u8 *)base + 0x2000);
	TestFloatInstructions(checker, emitter, fp);

	printf("%d of %d ARM64 disassembly checks failed\n", checker.failures, checker.checks);
	return checker.failures == 0;
}
