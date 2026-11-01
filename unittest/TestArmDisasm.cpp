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

// Runs every instruction the ARM32 emitter can produce through our disassembler (ext/disarm.cpp),
// two examples each. The expected strings were checked against llvm-mc, which
// Tools/arm_disasm_check.py automates: it sets ARM_DISASM_DUMP to make this test print every line.

#include <cstdlib>
#include <cstring>
#include <string>

#include "Common/ArmEmitter.h"
#include "Common/CPUDetect.h"
#include "ext/disarm.h"

#include "UnitTest.h"

#undef ADD
#undef RET

using namespace ArmGen;

namespace {

struct DisasmChecker {
	ARMXEmitter &emit;
	const u8 *base;
	const u8 *start = nullptr;
	int failures = 0;
	int checks = 0;
	bool dumpAll = getenv("ARM_DISASM_DUMP") != nullptr;

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
			ArmDis((u32)(p - base), w, buf, sizeof(buf), false);
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
			// Machine readable, for Tools/arm_disasm_check.py.
			printf("DIS|%d|%x|%s|%s|%s|%s\n", line, (int)(start - base), words.c_str(), actual.c_str(), expected, code);
		}
		if (fail) {
			failures++;
		}
	}
};

}  // namespace

#define DIS(expected, ...) do { c.Begin(); __VA_ARGS__; c.Check(expected, #__VA_ARGS__, __LINE__); } while (false)

static void TestIntegerInstructions(DisasmChecker &c, ARMXEmitter &e, const u8 *base) {
	// Branches. The buffer is at address 0 as far as the disassembler is concerned.
	e.SetCodePointer((u8 *)base + 0x100, (u8 *)base + 0x100);
	DIS("B 0x00000180", e.B(base + 0x180));
	DIS("B 0x00000040", e.B(base + 0x40));
	DIS("BL 0x00002000", e.BL(base + 0x2000));
	DIS("BL 0x00000000", e.BL(base));
	DIS("BEQ 0x00000200", e.B_CC(CC_EQ, base + 0x200));
	DIS("BLE 0x00000080", e.B_CC(CC_LE, base + 0x80));
	DIS("B 0x0000011c", FixupBranch b = e.B(); e.SetJumpTarget(b));
	DIS("BL 0x00000120", FixupBranch b = e.BL(); e.SetJumpTarget(b));
	DIS("BHI 0x00000124", FixupBranch b = e.B_CC(CC_HI); e.SetJumpTarget(b));
	DIS("BLLT 0x0000012c; MOV r0, r0", FixupBranch b = e.BL_CC(CC_LT); e.NOP(); e.SetJumpTarget(b));
	DIS("BX r0", e.B(R0));
	DIS("BX lr", e.B(R_LR));
	DIS("BLX r3", e.BL(R3));
	DIS("BLX r12", e.BL(R12));
	DIS("BL 0x00001000", e.QuickCallFunction(R12, base + 0x1000));
	DIS("MOV r0, r4; BL 0x00001000", e.QuickCallFunctionR(R12, base + 0x1000, R4));
	DIS("BNE 0x00000200", e.SetCC(CC_NEQ); e.B(base + 0x200); e.SetCC());
	DIS("BLXGE r2", e.SetCC(CC_GE); e.BL(R2); e.SetCC());

	// Misc
	DIS("MOV r0, r0", e.NOP());
	DIS("MOV r0, r0; MOV r0, r0", e.NOP(2));
	DIS("SETEND be", e.SETEND(true));
	DIS("SETEND le", e.SETEND(false));
	DIS("BKPT #1", e.BKPT(1));
	DIS("BKPT #4660", e.BKPT(0x1234));
	DIS("YIELD", e.YIELD());
	DIS("YIELDEQ", e.SetCC(CC_EQ); e.YIELD(); e.SetCC());
	DIS("SVC #0x0", e.SVC(IMM(0)));
	DIS("SVC #0x123456", e.SVC(IMM(0x123456)));
	DIS("DMB st", e.DMB());
	DIS("MRS r0, apsr", e.MRS(R0));
	DIS("MRS r11, apsr", e.MRS(R11));
	DIS("MSR APSR_nzcvq, r1", e._MSR(true, false, R1));
	DIS("MSR APSR_nzcvqg, r9", e._MSR(true, true, R9));
	DIS("MSR APSR_nzcvq, #0xf0000000", e._MSR(true, false, Operand2(0xF0, 4)));
	DIS("MSR APSR_g, #7", e._MSR(false, true, IMM(7)));

	// Data processing, every operation with an immediate, a register and a shifted register.
	DIS("AND r0, r1, #255", e.AND(R0, R1, IMM(0xFF)));
	DIS("AND r2, r3, r4, lsl #3", e.AND(R2, R3, Operand2(R4, ST_LSL, 3)));
	DIS("ANDS r0, r1, r2", e.ANDS(R0, R1, R2));
	DIS("ANDS r5, r6, #0xff000000", e.ANDS(R5, R6, Operand2(0xFF, 4)));
	DIS("EOR r0, r1, #1", e.EOR(R0, R1, IMM(1)));
	DIS("EOR r10, r11, r12, asr r1", e.EOR(R10, R11, Operand2(R12, ST_ASR, R1)));
	DIS("EORS r0, r1, r2", e.EORS(R0, R1, R2));
	DIS("EORS r3, r3, r4, ror #7", e.EORS(R3, R3, Operand2(R4, ST_ROR, 7)));
	DIS("SUB r0, r1, #4", e.SUB(R0, R1, IMM(4)));
	DIS("SUB sp, sp, #0x100", e.SUB(R_SP, R_SP, Operand2(0x40, 15)));
	DIS("SUBS r0, r0, #1", e.SUBS(R0, R0, IMM(1)));
	DIS("SUBS r1, r2, r3", e.SUBS(R1, R2, Operand2(R3, ST_LSR, 32)));
	DIS("RSB r0, r1, #0", e.RSB(R0, R1, IMM(0)));
	DIS("RSB r0, r1, r2, lsl r3", e.RSB(R0, R1, Operand2(R2, ST_LSL, R3)));
	DIS("RSBS r4, r5, #32", e.RSBS(R4, R5, IMM(32)));
	DIS("RSBS r4, r5, r6", e.RSBS(R4, R5, R6));
	DIS("ADD r0, r1, r2", e.ADD(R0, R1, R2));
	DIS("ADD r0, pc, #8", e.ADD(R0, R_PC, IMM(8)));
	DIS("ADDS r0, r1, r2, rrx", e.ADDS(R0, R1, Operand2(R2, ST_RRX, 0)));
	DIS("ADDS r8, r9, #255", e.ADDS(R8, R9, IMM(255)));
	DIS("ADC r0, r1, r2", e.ADC(R0, R1, R2));
	DIS("ADC r3, r4, #0", e.ADC(R3, R4, IMM(0)));
	DIS("ADCS r0, r1, r2, asr #31", e.ADCS(R0, R1, Operand2(R2, ST_ASR, 31)));
	DIS("ADCS r0, r1, #3", e.ADCS(R0, R1, IMM(3)));
	DIS("SBC r0, r1, r2", e.SBC(R0, R1, R2));
	DIS("SBC r6, r7, #0xc000000f", e.SBC(R6, R7, Operand2(0x3F, 1)));
	DIS("SBCS r0, r1, #10", e.SBCS(R0, R1, IMM(10)));
	DIS("SBCS r0, r1, r2, lsr r3", e.SBCS(R0, R1, Operand2(R2, ST_LSR, R3)));
	DIS("RSC r0, r1, r2", e.RSC(R0, R1, R2));
	DIS("RSC r0, r1, #100", e.RSC(R0, R1, IMM(100)));
	DIS("RSCS r0, r1, r2, lsl #1", e.RSCS(R0, R1, Operand2(R2, ST_LSL, 1)));
	DIS("RSCS r11, r12, #0", e.RSCS(R11, R12, IMM(0)));
	DIS("TST r0, #1", e.TST(R0, IMM(1)));
	DIS("TST r1, r2, lsl #16", e.TST(R1, Operand2(R2, ST_LSL, 16)));
	DIS("TEQ r0, r1", e.TEQ(R0, R1));
	DIS("TEQ r3, #128", e.TEQ(R3, IMM(0x80)));
	DIS("CMP r0, #0", e.CMP(R0, IMM(0)));
	DIS("CMP r1, r2", e.CMP(R1, R2));
	DIS("CMN r0, #1", e.CMN(R0, IMM(1)));
	DIS("CMN r7, r8, asr r9", e.CMN(R7, Operand2(R8, ST_ASR, R9)));
	DIS("ORR r0, r1, #16", e.ORR(R0, R1, IMM(0x10)));
	DIS("ORR r0, r1, r2, ror r3", e.ORR(R0, R1, Operand2(R2, ST_ROR, R3)));
	DIS("ORRS r0, r1, r2", e.ORRS(R0, R1, R2));
	DIS("ORRS r0, r0, #0x10000", e.ORRS(R0, R0, Operand2(0x01, 8)));
	DIS("MOV r0, r1", e.MOV(R0, R1));
	DIS("MOV r0, #42", e.MOV(R0, IMM(42)));
	DIS("MOV pc, lr", e.MOV(R_PC, R_LR));
	DIS("LSL r2, r3, #5", e.MOV(R2, Operand2(R3, ST_LSL, 5)));
	DIS("LSR r2, r3, r4", e.MOV(R2, Operand2(R3, ST_LSR, R4)));
	DIS("RRX r2, r3", e.MOV(R2, Operand2(R3, ST_RRX, 0)));
	DIS("MOVS r0, r1", e.MOVS(R0, R1));
	DIS("MOVS r5, #0", e.MOVS(R5, IMM(0)));
	DIS("BIC r0, r1, #15", e.BIC(R0, R1, IMM(0xF)));
	DIS("BIC r0, r1, #0xff00", e.BIC(R0, R1, Operand2(0xFF, 12)));
	DIS("BICS r0, r1, r2", e.BICS(R0, R1, R2));
	DIS("BICS r0, r1, r2, asr #2", e.BICS(R0, R1, Operand2(R2, ST_ASR, 2)));
	DIS("MVN r0, #0", e.MVN(R0, IMM(0)));
	DIS("MVN r0, r1", e.MVN(R0, R1));
	DIS("MVNS r0, r1, lsl #2", e.MVNS(R0, Operand2(R1, ST_LSL, 2)));
	DIS("MVNS r3, #192", e.MVNS(R3, IMM(0xC0)));
	DIS("MOVW r0, #0x1234", e.MOVW(R0, IMM(0x1234)));
	DIS("MOVW r12, #0xffff", e.MOVW(R12, IMM(0xFFFF)));
	DIS("MOVT r0, #0x5678", e.MOVT(R0, IMM(0x5678)));
	DIS("MOVT r1, #0x89ab", e.MOVT(R1, IMM(0x89AB0000), true));
	DIS("ADDLT r0, r1, #1", e.SetCC(CC_LT); e.ADD(R0, R1, IMM(1)); e.SetCC());
	DIS("MOVSHI r1, r2", e.SetCC(CC_HI); e.MOVS(R1, R2); e.SetCC());

	// Shifts
	DIS("LSL r0, r1, #3", e.LSL(R0, R1, IMM(3)));
	DIS("LSL r0, r1, r2", e.LSL(R0, R1, R2));
	DIS("LSLS r0, r1, #31", e.LSLS(R0, R1, IMM(31)));
	DIS("LSLS r4, r5, r6", e.LSLS(R4, R5, R6));
	DIS("LSR r0, r1, #1", e.LSR(R0, R1, IMM(1)));
	DIS("LSR r0, r1, r2", e.LSR(R0, R1, R2));
	DIS("LSRS r0, r1, #16", e.LSRS(R0, R1, IMM(16)));
	DIS("LSRS r0, r1, r12", e.LSRS(R0, R1, R12));
	DIS("ASR r0, r1, #31", e.ASR(R0, R1, IMM(31)));
	DIS("ASR r0, r1, r2", e.ASR(R0, R1, R2));
	DIS("ASRS r0, r1, #8", e.ASRS(R0, R1, IMM(8)));
	DIS("ASRS r7, r8, r9", e.ASRS(R7, R8, R9));

	// Multiplies and divides
	DIS("MUL r0, r2, r1", e.MUL(R0, R1, R2));
	DIS("MUL r12, r11, r10", e.MUL(R12, R10, R11));
	DIS("MULS r0, r2, r1", e.MULS(R0, R1, R2));
	DIS("MULS r3, r5, r4", e.MULS(R3, R4, R5));
	DIS("UMULL r0, r1, r2, r3", e.UMULL(R0, R1, R2, R3));
	DIS("UMULL r4, r5, r6, r7", e.UMULL(R4, R5, R6, R7));
	DIS("SMULL r0, r1, r2, r3", e.SMULL(R0, R1, R2, R3));
	DIS("SMULL r8, r9, r10, r11", e.SMULL(R8, R9, R10, R11));
	DIS("UMLAL r0, r1, r2, r3", e.UMLAL(R0, R1, R2, R3));
	DIS("UMLAL r4, r12, r6, r7", e.UMLAL(R4, R12, R6, R7));
	DIS("SMLAL r0, r1, r2, r3", e.SMLAL(R0, R1, R2, R3));
	DIS("SMLAL r2, r3, r0, r1", e.SMLAL(R2, R3, R0, R1));
	DIS("UDIV r0, r1, r2", e.UDIV(R0, R1, R2));
	DIS("UDIV r3, r4, r5", e.UDIV(R3, R4, R5));
	DIS("SDIV r0, r1, r2", e.SDIV(R0, R1, R2));
	DIS("SDIV r6, r7, r8", e.SDIV(R6, R7, R8));

	// Bitfields, extension and the like
	DIS("SXTB r0, r1", e.SXTB(R0, R1));
	DIS("SXTB r5, r12", e.SXTB(R5, R12));
	DIS("SXTH r0, r1", e.SXTH(R0, R1));
	DIS("SXTH r2, r3, ror #16", e.SXTH(R2, R3, 2));
	DIS("SXTAH r0, r1, r2", e.SXTAH(R0, R1, R2));
	DIS("SXTAH r0, r1, r2, ror #8", e.SXTAH(R0, R1, R2, 1));
	DIS("BFI r0, r1, #4, #8", e.BFI(R0, R1, 4, 8));
	DIS("BFI r2, r3, #0, #32", e.BFI(R2, R3, 0, 32));
	DIS("BFC r0, #4, #8", e.BFC(R0, 4, 8));
	DIS("BFC r5, #16, #16", e.BFC(R5, 16, 16));
	DIS("UBFX r0, r1, #4, #8", e.UBFX(R0, R1, 4, 8));
	DIS("UBFX r2, r3, #0, #1", e.UBFX(R2, R3, 0, 1));
	DIS("SBFX r0, r1, #4, #8", e.SBFX(R0, R1, 4, 8));
	DIS("SBFX r2, r3, #31, #1", e.SBFX(R2, R3, 31, 1));
	DIS("CLZ r0, r1", e.CLZ(R0, R1));
	DIS("CLZ r12, r11", e.CLZ(R12, R11));
	DIS("RBIT r0, r1", e.RBIT(R0, R1));
	DIS("RBIT r7, r8", e.RBIT(R7, R8));
	DIS("REV r0, r1", e.REV(R0, R1));
	DIS("REV r9, r10", e.REV(R9, R10));
	DIS("REV16 r0, r1", e.REV16(R0, R1));
	DIS("REV16 r3, r4", e.REV16(R3, R4));

	// Loads and stores
	DIS("LDR r3, [r7]", e.LDR(R3, R7));
	DIS("LDR r0, [r1, #4]", e.LDR(R0, R1, IMM(4)));
	DIS("LDR r0, [r1, #-8]", e.LDR(R0, R1, Operand2((u32)-8)));
	DIS("LDR r0, [r1, r2]", e.LDR(R0, R1, R2));
	DIS("LDR r0, [r1, -r2]", e.LDR(R0, R1, R2, false));
	DIS("LDR r0, [r1, r2, lsl #2]", e.LDR(R0, R1, Operand2(R2, ST_LSL, 2)));
	DIS("LDRB r0, [r1, #1]", e.LDRB(R0, R1, IMM(1)));
	DIS("LDRB r0, [r1, r2]", e.LDRB(R0, R1, R2));
	DIS("LDRH r0, [r1, #2]", e.LDRH(R0, R1, IMM(2)));
	DIS("LDRH r0, [r1, -r2]", e.LDRH(R0, R1, R2, false));
	DIS("LDRSB r0, [r1, #255]", e.LDRSB(R0, R1, IMM(0xFF)));
	DIS("LDRSB r0, [r1, r2]", e.LDRSB(R0, R1, R2));
	DIS("LDRSH r0, [r1, #-2]", e.LDRSH(R0, R1, Operand2((u32)-2)));
	DIS("LDRSH r0, [r1, r2]", e.LDRSH(R0, R1, R2));
	DIS("STR r0, [r1, #4095]", e.STR(R0, R1, IMM(4095)));
	DIS("STR r0, [sp, -r2, asr #1]", e.STR(R0, R_SP, Operand2(R2, ST_ASR, 1), false));
	DIS("STRB r0, [r1]", e.STRB(R0, R1));
	DIS("STRB r0, [r1, r2]", e.STRB(R0, R1, R2));
	DIS("STRH r0, [r1, #16]", e.STRH(R0, R1, IMM(0x10)));
	DIS("STRH r0, [r1, r2]", e.STRH(R0, R1, R2));
	DIS("PLD [r0]", e.PLD(R0, 0));
	DIS("PLDW [r1, #-64]", e.PLD(R1, -64, true));
	DIS("LDREX r0, [r1]", e.LDREX(R0, R1));
	DIS("LDREX r4, [r5]", e.LDREX(R4, R5));
	DIS("STREX r0, r2, [r1]", e.STREX(R0, R1, R2));
	DIS("STREX r3, r5, [r4]", e.STREX(R3, R4, R5));

	// Load and store multiple
	DIS("PUSH {r4, lr}", e.PUSH(2, R4, R_LR));
	DIS("STMDB sp!, {r4}", e.PUSH(1, R4));
	DIS("POP {r4, r5, pc}", e.POP(3, R4, R5, R_PC));
	DIS("LDMIA sp!, {r0}", e.POP(1, R0));
	DIS("PUSH {r0, r1}", e.STMFD(R_SP, true, 2, R0, R1));
	DIS("STMDB r2, {r0, r1}", e.STMFD(R2, false, 2, R0, R1));
	DIS("POP {r0, r1}", e.LDMFD(R_SP, true, 2, R0, R1));
	DIS("LDMIA r3, {r12}", e.LDMFD(R3, false, 1, R12));
	DIS("STMIA r0!, {r1, r2}", e.STMIA(R0, true, 2, R1, R2));
	DIS("STMIA r0, {r1, r2, r3}", e.STMIA(R0, false, 3, R1, R2, R3));
	DIS("LDMIA r0!, {r1, r2}", e.LDMIA(R0, true, 2, R1, R2));
	DIS("LDMIA r0, {r4, r11}", e.LDMIA(R0, false, 2, R4, R11));
	DIS("STMDB r0!, {r1, r2}", e.STM(R0, false, true, true, 2, R1, R2));
	DIS("STMIB r0, {r1, r2}", e.STM(R0, true, true, false, 2, R1, R2));
	DIS("LDMDA r0, {r1, r2}", e.LDM(R0, false, false, false, 2, R1, R2));
	DIS("LDMIB r0!, {r1, r2}", e.LDM(R0, true, true, true, 2, R1, R2));
	DIS("STMIA r0!, {r4, r5, r6, r7}", e.STMBitmask(R0, true, false, true, 0x00F0));
	DIS("PUSH {r4, r5, r6, r7, r8, r9, r10, r11, lr}", e.STMBitmask(R_SP, false, true, true, 0x4FF0));
	DIS("LDMIA r0, {r1, r2, r3}", e.LDMBitmask(R0, true, false, false, 0x000E));
	DIS("POP {r4, r5, r6, r7, r8, r9, r10, r11, pc}", e.LDMBitmask(R_SP, true, false, true, 0x8FF0));

	// Helpers that pick an encoding
	DIS("MOVW r0, #0x5678; MOVT r0, #0x1234", e.MOVI2R(R0, 0x12345678));
	DIS("MVN r0, #255", e.MOVI2R(R0, 0xFFFFFF00));
	DIS("MOVW r1, #0x1234", e.MOVI2R(R1, 0x1234));
	DIS("MOVW r2, #0x0; MOVT r2, #0xabcd", e.MOVI2R(R2, 0xABCD0000, false));
	DIS("MOVW r3, #0x4000; MOVT r3, #0x880", e.MOVP2R(R3, (void *)(uintptr_t)0x08804000));
	DIS("ADD r0, r1, #0x100", e.ADDI2R(R0, R1, 0x100, R12));
	DIS("MOVW r12, #0x2345; MOVT r12, #0x1; ADD r0, r1, r12", e.ADDI2R(R0, R1, 0x12345, R12));
	DIS("ADD r0, r1, #0x1200; ADD r0, r0, #52", e.TryADDI2R(R0, R1, 0x1234));
	DIS("SUB r0, r1, #4", e.SUBI2R(R0, R1, 4, R12));
	DIS("SUB r0, r1, #0x1200; SUB r0, r0, #52", e.SUBI2R(R0, R1, 0x1234, R12));
	DIS("AND r0, r1, #0xff00", e.ANDI2R(R0, R1, 0xFF00, R12));
	DIS("BIC r0, r1, #0xf000", e.ANDI2R(R0, R1, 0xFFFF0FFF, R12));
#if PPSSPP_ARCH(ARMV7)
	// ARMv7 builds have UBFX, MOVW and MOVT.
	DIS("UBFX r0, r1, #0, #17", e.ANDI2R(R0, R1, 0x0001FFFF, R12));
	DIS("MOVW r12, #0x5678; MOVT r12, #0x1234; AND r0, r1, r12", e.ANDI2R(R0, R1, 0x12345678, R12));
#else
	DIS("BIC r0, r1, #0xfe0000; BIC r0, r0, #0xff000000", e.ANDI2R(R0, R1, 0x0001FFFF, R12));
	DIS("BIC r0, r1, #135; BIC r0, r0, #0xa900; BIC r0, r0, #0xcb0000; BIC r0, r0, #0xed000000", e.ANDI2R(R0, R1, 0x12345678, R12));
#endif
	DIS("ORR r0, r1, #0x80000000", e.ORI2R(R0, R1, 0x80000000, R12));
	DIS("ORR r0, r1, #240; ORR r0, r0, #0xf00000", e.ORI2R(R0, R1, 0x00F000F0, R12));
#if PPSSPP_ARCH(ARMV7)
	DIS("MOVW r12, #0x5678; MOVT r12, #0x1234; ORR r0, r1, r12", e.ORI2R(R0, R1, 0x12345678, R12));
#else
	DIS("ORR r0, r1, #0x278; ORR r0, r0, #0x5400; ORR r0, r0, #0x2340000; ORR r0, r0, #0x10000008", e.ORI2R(R0, R1, 0x12345678, R12));
#endif
	DIS("EOR r0, r1, #0x3fc", e.EORI2R(R0, R1, 0x3FC, R12));
	DIS("MOVW r12, #0x1234; EOR r0, r1, r12", e.EORI2R(R0, R1, 0x1234, R12));
	DIS("CMP r0, #16", e.CMPI2R(R0, 0x10, R12));
	DIS("CMN r0, #16", e.CMPI2R(R0, (u32)-0x10, R12));
	DIS("MOVW r12, #0x1234; CMP r0, r12", e.CMPI2R(R0, 0x1234, R12));
	DIS("TST r0, #0x100", e.TSTI2R(R0, 0x100, R12));
	DIS("MOVW r12, #0x101; TST r0, r12", e.TSTI2R(R0, 0x101, R12));
	DIS("MOV r0, #255; ORR r0, r0, #0xff0000", e.TrySetValue_TwoOp(R0, 0x00FF00FF));
	DIS("MOV r0, #1; ORR r0, r0, #0x80000000", e.TrySetValue_TwoOp(R0, 0x80000001));
	// From a fixed offset, since the code before it is shorter on ARMv7.
	e.SetCodePointer((u8 *)base + 0xFF8, (u8 *)base + 0xFF8);
	DIS("NOP; NOP", e.NopAlignCode16());
	DIS("BKPT #0; BKPT #0", e.ReserveCodeSpace(8));
}

static void TestFloatInstructions(DisasmChecker &c, ARMXEmitter &e) {
	// VFP loads and stores
	DIS("VLDR s3, [r8, #48]", e.VLDR(S3, R8, 48));
	DIS("VLDR d17, [r0, #-1020]", e.VLDR(D17, R0, -1020));
	DIS("VSTR s5, [r12, #-36]", e.VSTR(S5, R12, -36));
	DIS("VSTR d0, [sp]", e.VSTR(D0, R_SP, 0));
	DIS("VLDMIA r0, {s0-s3}", e.VLDMIA(R0, false, S0, 4));
	DIS("VLDMIA r1!, {d8-d15}", e.VLDMIA(R1, true, D8, 8));
	DIS("VSTMIA r0!, {s4}", e.VSTMIA(R0, true, S4, 1));
	DIS("VSTMIA r2, {d16-d31}", e.VSTMIA(R2, false, D16, 16));
	DIS("VLDMDB r0!, {s8-s9}", e.VLDMDB(R0, true, S8, 2));
	DIS("VLDMDB r3!, {d2-d4}", e.VLDMDB(R3, true, D2, 3));
	DIS("VSTMDB r0!, {d0-d1}", e.VSTMDB(R0, true, D0, 2));
	DIS("VSTMDB r4!, {s30-s31}", e.VSTMDB(R4, true, S30, 2));
	DIS("VPUSH {d8-d15}", e.VPUSH(D8, 8));
	DIS("VPUSH {s16}", e.VPUSH(S16, 1));
	DIS("VPOP {d8-d15}", e.VPOP(D8, 8));
	DIS("VPOP {s0-s2}", e.VPOP(S0, 3));

	// VFP arithmetic, single and double
	DIS("VADD.f32 s1, s2, s3", e.VADD(S1, S2, S3));
	DIS("VADD.f64 d1, d2, d3", e.VADD(D1, D2, D3));
	DIS("VSUB.f32 s1, s2, s3", e.VSUB(S1, S2, S3));
	DIS("VSUB.f64 d20, d21, d31", e.VSUB(D20, D21, D31));
	DIS("VMUL.f32 s7, s8, s9", e.VMUL(S7, S8, S9));
	DIS("VMUL.f64 d0, d16, d1", e.VMUL(D0, D16, D1));
	DIS("VNMUL.f32 s7, s8, s9", e.VNMUL(S7, S8, S9));
	DIS("VNMUL.f64 d7, d8, d9", e.VNMUL(D7, D8, D9));
	DIS("VMLA.f32 s7, s8, s9", e.VMLA(S7, S8, S9));
	DIS("VMLA.f64 d5, d6, d7", e.VMLA(D5, D6, D7));
	DIS("VMLS.f32 s0, s1, s2", e.VMLS(S0, S1, S2));
	DIS("VMLS.f64 d1, d2, d3", e.VMLS(D1, D2, D3));
	DIS("VNMLA.f32 s7, s8, s9", e.VNMLA(S7, S8, S9));
	DIS("VNMLA.f64 d1, d2, d3", e.VNMLA(D1, D2, D3));
	DIS("VNMLS.f32 s7, s8, s9", e.VNMLS(S7, S8, S9));
	DIS("VNMLS.f64 d30, d31, d29", e.VNMLS(D30, D31, D29));
	DIS("VDIV.f32 s1, s2, s3", e.VDIV(S1, S2, S3));
	DIS("VDIV.f64 d1, d2, d3", e.VDIV(D1, D2, D3));
	DIS("VSQRT.f32 s1, s2", e.VSQRT(S1, S2));
	DIS("VSQRT.f64 d10, d11", e.VSQRT(D10, D11));
	DIS("VABS.f32 s1, s2", e.VABS(S1, S2));
	DIS("VABS.f64 d3, d4", e.VABS(D3, D4));
	DIS("VNEG.f32 s1, s2", e.VNEG(S1, S2));
	DIS("VNEG.f64 d18, d19", e.VNEG(D18, D19));
	DIS("VCMP.f32 s1, s2", e.VCMP(S1, S2));
	DIS("VCMP.f64 d1, d2", e.VCMP(D1, D2));
	DIS("VCMP.f32 s5, #0.0", e.VCMP(S5));
	DIS("VCMP.f64 d6, #0.0", e.VCMP(D6));
	DIS("VCMPE.f32 s1, s2", e.VCMPE(S1, S2));
	DIS("VCMPE.f64 d3, d4", e.VCMPE(D3, D4));
	DIS("VCMPE.f32 s7, #0.0", e.VCMPE(S7));
	DIS("VCMPE.f64 d8, #0.0", e.VCMPE(D8));
	DIS("VADDNE.f32 s0, s1, s2", e.SetCC(CC_NEQ); e.VADD(S0, S1, S2); e.SetCC());

	// VFP moves
	DIS("VMOV.f32 s1, s2", e.VMOV(S1, S2));
	DIS("VMOV.f32 s25, s21", e.VMOV(S25, S21));
	DIS("VMOV.f64 d1, d2", e.VMOV(D1, D2));
	DIS("VMOV.f64 d26, d30", e.VMOV(D26, D30));
	DIS("VMOV s9, r3", e.VMOV(S9, R3));
	DIS("VMOV s31, r0", e.VMOV(S31, R0));
	DIS("VMOV r9, s3", e.VMOV(R9, S3));
	DIS("VMOV r0, s30", e.VMOV(R0, S30));
	DIS("VMOV d0, r1, r2", e.VMOV(D0, R1, R2));
	DIS("VMOV d17, r3, r4", e.VMOV(D17, R3, R4));
	DIS("VMOV r1, r2, d0", e.VMOV(R1, R2, D0));
	DIS("VMOV r5, r6, d31", e.VMOV(R5, R6, D31));
	DIS("VMOV.32 d1[0], r2", e.VMOV(D1, R2, false));
	DIS("VMOV.32 d20[1], r3", e.VMOV(D20, R3, true));
	DIS("VMOV.f32 s0, #1.0", e.VMOV(S0, IMM(0x70)));
	DIS("VMOV.f64 d1, #-0.5", e.VMOV(D1, IMM(0xE0)));
	DIS("VMOV.f32 s0, #1.0", e.MOVI2F(S0, 1.0f, R12));
	DIS("MOVW r12, #0xcccd; MOVT r12, #0x3dcc; VMOV s1, r12", e.MOVI2F(S1, 0.1f, R12));
	DIS("VMOV.f32 s2, #-2.0", e.MOVI2F(S2, 2.0f, R12, true));
	DIS("MOV r0, #0x3f800000", e.MOVI2FR(R0, 1.0f));
	DIS("MOV r1, #0xbf000000", e.MOVI2FR(R1, -0.5f));
	DIS("MVN r12, #0x80000000; VMOV s3, r12", e.MOVIU2F(S3, 0x7FFFFFFF, R12));
	DIS("VMRS r1, fpscr", e.VMRS(R1));
	DIS("VMRS r12, fpscr", e.VMRS(R12));
	DIS("VMSR fpscr, r7", e.VMSR(R7));
	DIS("VMSR fpscr, r0", e.VMSR(R0));
	DIS("VMRS APSR_nzcv, fpscr", e.VMRS_APSR());
	DIS("VMRSEQ APSR_nzcv, fpscr", e.SetCC(CC_EQ); e.VMRS_APSR(); e.SetCC());

	// VFP conversions
	DIS("VCVTR.s32.f32 s0, s1", e.VCVT(S0, S1, TO_INT | IS_SIGNED));
	DIS("VCVT.s32.f32 s0, s1", e.VCVT(S0, S1, TO_INT | IS_SIGNED | ROUND_TO_ZERO));
	DIS("VCVTR.u32.f32 s2, s3", e.VCVT(S2, S3, TO_INT));
	DIS("VCVT.u32.f32 s2, s3", e.VCVT(S2, S3, TO_INT | ROUND_TO_ZERO));
	DIS("VCVT.f32.s32 s4, s5", e.VCVT(S4, S5, TO_FLOAT | IS_SIGNED));
	DIS("VCVT.f32.u32 s4, s5", e.VCVT(S4, S5, TO_FLOAT));
	DIS("VCVT.f64.f32 d0, s1", e.VCVT(D0, S1, TO_FLOAT));
	DIS("VCVT.f32.f64 s0, d1", e.VCVT(S0, D1, TO_FLOAT));
	DIS("VCVT.s32.f64 s0, d1", e.VCVT(S0, D1, TO_INT | IS_SIGNED | ROUND_TO_ZERO));
	DIS("VCVT.f64.s32 d0, s1", e.VCVT(D0, S1, TO_INT | IS_SIGNED));
	DIS("VCVT.f64.u32 d5, s7", e.VCVT(D5, S7, TO_INT));
	DIS("VCVT.u32.f64 s6, d20", e.VCVT(S6, D20, TO_INT | ROUND_TO_ZERO));
}

static void TestNeonInstructions(DisasmChecker &c, ARMXEmitter &e) {
	// Three registers of the same length
	DIS("VADD.i8 d3, d4, d19", e.VADD(I_8, D3, D4, D19));
	DIS("VADD.i32 q3, q4, q15", e.VADD(I_32, Q3, Q4, Q15));
	DIS("VADD.f32 d3, d4, d19", e.VADD(F_32, D3, D4, D19));
	DIS("VADD.f32 q11, q11, q10", e.VADD(F_32, Q11, Q11, Q10));
	DIS("VADD.i8 d3, d4, d5", e.VADD(I_I8, D3, D4, D5));
	DIS("VADD.i64 q1, q2, q3", e.VADD(I_I64, Q1, Q2, Q3));
	DIS("VSUB.i16 q5, q6, q15", e.VSUB(I_16, Q5, Q6, Q15));
	DIS("VSUB.i64 d0, d1, d2", e.VSUB(I_64, D0, D1, D2));
	DIS("VSUB.f32 q1, q2, q3", e.VSUB(F_32, Q1, Q2, Q3));
	DIS("VSUB.f32 d1, d2, d3", e.VSUB(F_32, D1, D2, D3));
	DIS("VSUB.i32 q1, q2, q9", e.VSUB(I_I32, Q1, Q2, Q9));
	DIS("VSUB.i8 q4, q5, q6", e.VSUB(I_I8, Q4, Q5, Q6));
	DIS("VMUL.f32 q1, q2, q3", e.VMUL(F_32, Q1, Q2, Q3));
	DIS("VMUL.f32 q13, q15, q14", e.VMUL(F_32, Q13, Q15, Q14));
	DIS("VMUL.i16 d0, d1, d2", e.VMUL(I_16, D0, D1, D2));
	DIS("VMUL.p8 q0, q1, q2", e.VMUL(I_8 | I_POLYNOMIAL, Q0, Q1, Q2));
	DIS("VMLA.f32 q1, q2, q3", e.VMLA(F_32, Q1, Q2, Q3));
	DIS("VMLA.i32 d1, d2, d3", e.VMLA(I_32, D1, D2, D3));
	DIS("VMLS.f32 q1, q2, q3", e.VMLS(F_32, Q1, Q2, Q3));
	DIS("VMLS.i16 q1, q2, q3", e.VMLS(I_16, Q1, Q2, Q3));
	DIS("VFMA.f32 q1, q2, q3", e.VFMA(F_32, Q1, Q2, Q3));
	DIS("VFMA.f32 d1, d2, d3", e.VFMA(F_32, D1, D2, D3));
	DIS("VFMS.f32 q1, q2, q3", e.VFMS(F_32, Q1, Q2, Q3));
	DIS("VFMS.f32 d16, d17, d18", e.VFMS(F_32, D16, D17, D18));
	DIS("VABA.s8 d0, d1, d2", e.VABA(I_8 | I_SIGNED, D0, D1, D2));
	DIS("VABA.u32 q0, q1, q2", e.VABA(I_32 | I_UNSIGNED, Q0, Q1, Q2));
	DIS("VABD.s16 d0, d1, d2", e.VABD(I_16 | I_SIGNED, D0, D1, D2));
	DIS("VABD.u8 q0, q1, q2", e.VABD(I_8 | I_UNSIGNED, Q0, Q1, Q2));
	DIS("VABD.f32 d0, d1, d2", e.VABD(F_32, D0, D1, D2));
	DIS("VABD.f32 q0, q1, q2", e.VABD(F_32, Q0, Q1, Q2));
	DIS("VABD.f32 q0, q1, q2", e.VABD(I_I32, Q0, Q1, Q2));
	DIS("VABD.f32 d0, d1, d2", e.VABD(I_I32, D0, D1, D2));
	DIS("VACGE.f32 d0, d1, d2", e.VACGE(D0, D1, D2));
	DIS("VACGE.f32 q0, q1, q2", e.VACGE(Q0, Q1, Q2));
	DIS("VACGT.f32 d0, d1, d2", e.VACGT(D0, D1, D2));
	DIS("VACGT.f32 q0, q1, q2", e.VACGT(Q0, Q1, Q2));
	DIS("VACGE.f32 d0, d2, d1", e.VACLE(D0, D1, D2));
	DIS("VACGE.f32 q0, q2, q1", e.VACLE(Q0, Q1, Q2));
	DIS("VACGT.f32 d0, d2, d1", e.VACLT(D0, D1, D2));
	DIS("VACGT.f32 q0, q2, q1", e.VACLT(Q0, Q1, Q2));
	DIS("VCEQ.i8 d0, d1, d2", e.VCEQ(I_8, D0, D1, D2));
	DIS("VCEQ.f32 q0, q1, q2", e.VCEQ(F_32, Q0, Q1, Q2));
	DIS("VCGE.s16 d0, d1, d2", e.VCGE(I_16 | I_SIGNED, D0, D1, D2));
	DIS("VCGE.u32 q0, q1, q2", e.VCGE(I_32 | I_UNSIGNED, Q0, Q1, Q2));
	DIS("VCGE.f32 q0, q1, q2", e.VCGE(F_32, Q0, Q1, Q2));
	DIS("VCGT.u8 d0, d1, d2", e.VCGT(I_8 | I_UNSIGNED, D0, D1, D2));
	DIS("VCGT.f32 q0, q1, q2", e.VCGT(F_32, Q0, Q1, Q2));
	DIS("VCGE.s32 d0, d2, d1", e.VCLE(I_32 | I_SIGNED, D0, D1, D2));
	DIS("VCGE.f32 q0, q2, q1", e.VCLE(F_32, Q0, Q1, Q2));
	DIS("VCGT.u32 q8, q4, q8", e.VCLT(I_32 | I_UNSIGNED, Q8, Q8, Q4));
	DIS("VCGT.f32 d0, d2, d1", e.VCLT(F_32, D0, D1, D2));
	DIS("VTST.8 d0, d1, d2", e.VTST(I_8, D0, D1, D2));
	DIS("VTST.32 q0, q1, q2", e.VTST(I_32, Q0, Q1, Q2));
	DIS("VHADD.s8 d0, d1, d2", e.VHADD(I_8 | I_SIGNED, D0, D1, D2));
	DIS("VHADD.u16 q0, q1, q2", e.VHADD(I_16 | I_UNSIGNED, Q0, Q1, Q2));
	DIS("VHSUB.s32 d0, d1, d2", e.VHSUB(I_32 | I_SIGNED, D0, D1, D2));
	DIS("VHSUB.u8 q0, q1, q2", e.VHSUB(I_8 | I_UNSIGNED, Q0, Q1, Q2));
	DIS("VRHADD.s8 d0, d1, d2", e.VRHADD(I_8 | I_SIGNED, D0, D1, D2));
	DIS("VRHADD.u16 q0, q1, q2", e.VRHADD(I_16 | I_UNSIGNED, Q0, Q1, Q2));
	DIS("VQADD.s8 d0, d1, d2", e.VQADD(I_8 | I_SIGNED, D0, D1, D2));
	DIS("VQADD.u64 q0, q1, q2", e.VQADD(I_64 | I_UNSIGNED, Q0, Q1, Q2));
	DIS("VQSUB.s16 d0, d1, d2", e.VQSUB(I_16 | I_SIGNED, D0, D1, D2));
	DIS("VQSUB.u32 q0, q1, q2", e.VQSUB(I_32 | I_UNSIGNED, Q0, Q1, Q2));
	DIS("VMAX.s8 d0, d1, d2", e.VMAX(I_8 | I_SIGNED, D0, D1, D2));
	DIS("VMAX.u32 q0, q1, q2", e.VMAX(I_32 | I_UNSIGNED, Q0, Q1, Q2));
	DIS("VMAX.f32 q3, q4, q9", e.VMAX(F_32, Q3, Q4, Q9));
	DIS("VMAX.f32 d3, d4, d5", e.VMAX(F_32, D3, D4, D5));
	DIS("VMIN.s16 d0, d1, d2", e.VMIN(I_16 | I_SIGNED, D0, D1, D2));
	DIS("VMIN.u8 q0, q1, q2", e.VMIN(I_8 | I_UNSIGNED, Q0, Q1, Q2));
	DIS("VMIN.f32 d3, d4, d19", e.VMIN(F_32, D3, D4, D19));
	DIS("VMIN.f32 q3, q4, q5", e.VMIN(F_32, Q3, Q4, Q5));
	DIS("VPADD.f32 d0, d0, d0", e.VPADD(F_32, D0, D0, D0));
	DIS("VPADD.i16 d1, d2, d3", e.VPADD(I_16, D1, D2, D3));
	DIS("VPMAX.f32 d0, d1, d2", e.VPMAX(F_32, D0, D1, D2));
	DIS("VPMAX.u8 d0, d1, d2", e.VPMAX(I_8 | I_UNSIGNED, D0, D1, D2));
	DIS("VPMIN.f32 d0, d1, d2", e.VPMIN(F_32, D0, D1, D2));
	DIS("VPMIN.s32 d0, d1, d2", e.VPMIN(I_32 | I_SIGNED, D0, D1, D2));
	DIS("VQDMULH.s16 d0, d1, d2", e.VQDMULH(I_16, D0, D1, D2));
	DIS("VQDMULH.s32 q0, q1, q2", e.VQDMULH(I_32, Q0, Q1, Q2));
	DIS("VQRDMULH.s16 d0, d1, d2", e.VQRDMULH(I_16, D0, D1, D2));
	DIS("VQRDMULH.s32 q0, q1, q2", e.VQRDMULH(I_32, Q0, Q1, Q2));
	DIS("VRECPS.f32 d0, d1, d2", e.VRECPS(D0, D1, D2));
	DIS("VRECPS.f32 q0, q1, q2", e.VRECPS(Q0, Q1, Q2));
	DIS("VRSQRTS.f32 d0, d1, d2", e.VRSQRTS(D0, D1, D2));
	DIS("VRSQRTS.f32 q0, q1, q2", e.VRSQRTS(Q0, Q1, Q2));
	DIS("VSHL.s8 d0, d1, d2", e.VSHL(I_8 | I_SIGNED, D0, D1, D2));
	DIS("VSHL.u32 q0, q1, q2", e.VSHL(I_32 | I_UNSIGNED, Q0, Q1, Q2));
	DIS("VRSHL.s16 d0, d2, d1", e.VRSHL(I_16 | I_SIGNED, D0, D1, D2));
	DIS("VRSHL.u64 q0, q2, q1", e.VRSHL(I_64 | I_UNSIGNED, Q0, Q1, Q2));
	DIS("VQSHL.s8 d0, d2, d1", e.VQSHL(I_8 | I_SIGNED, D0, D1, D2));
	DIS("VQSHL.u32 q0, q2, q1", e.VQSHL(I_32 | I_UNSIGNED, Q0, Q1, Q2));
	DIS("VQRSHL.s16 d0, d2, d1", e.VQRSHL(I_16 | I_SIGNED, D0, D1, D2));
	DIS("VQRSHL.u8 q0, q2, q1", e.VQRSHL(I_8 | I_UNSIGNED, Q0, Q1, Q2));

	// Bitwise
	DIS("VAND q1, q2, q3", e.VAND(Q1, Q2, Q3));
	DIS("VAND d1, d2, d3", e.VAND(D1, D2, D3));
	DIS("VBIC q1, q2, q3", e.VBIC(Q1, Q2, Q3));
	DIS("VBIC d1, d2, d3", e.VBIC(D1, D2, D3));
	DIS("VEOR q0, q1, q2", e.VEOR(Q0, Q1, Q2));
	DIS("VEOR d0, d0, d0", e.VEOR(D0, D0, D0));
	DIS("VORR q1, q2, q3", e.VORR(Q1, Q2, Q3));
	DIS("VORR d1, d2, d3", e.VORR(D1, D2, D3));
	DIS("VORN q1, q2, q3", e.VORN(Q1, Q2, Q3));
	DIS("VORN d1, d2, d3", e.VORN(D1, D2, D3));
	DIS("VBIF q1, q2, q3", e.VBIF(Q1, Q2, Q3));
	DIS("VBIF d1, d2, d3", e.VBIF(D1, D2, D3));
	DIS("VBIT q1, q2, q3", e.VBIT(Q1, Q2, Q3));
	DIS("VBIT d1, d2, d3", e.VBIT(D1, D2, D3));
	DIS("VBSL q1, q2, q3", e.VBSL(Q1, Q2, Q3));
	DIS("VBSL d1, d2, d3", e.VBSL(D1, D2, D3));
	DIS("VMOV q14, q2", e.VMOV(Q14, Q2));
	DIS("VMOV q1, q2", e.VMOV_neon(Q1, Q2));

	// Three registers of different lengths
	DIS("VADDL.s8 q0, d1, d2", e.VADDL(I_8 | I_SIGNED, Q0, D1, D2));
	DIS("VADDL.u32 q8, d20, d21", e.VADDL(I_32 | I_UNSIGNED, Q8, D20, D21));
	DIS("VADDW.s16 q0, q1, d4", e.VADDW(I_16 | I_SIGNED, Q0, Q1, D4));
	DIS("VADDW.u8 q0, q1, d4", e.VADDW(I_8 | I_UNSIGNED, Q0, Q1, D4));
	DIS("VSUBL.s8 q0, d1, d2", e.VSUBL(I_8 | I_SIGNED, Q0, D1, D2));
	DIS("VSUBL.u16 q0, d1, d2", e.VSUBL(I_16 | I_UNSIGNED, Q0, D1, D2));
	DIS("VSUBW.s32 q0, q1, d4", e.VSUBW(I_32 | I_SIGNED, Q0, Q1, D4));
	DIS("VSUBW.u8 q0, q1, d4", e.VSUBW(I_8 | I_UNSIGNED, Q0, Q1, D4));
	DIS("VADDHN.i16 d0, q1, q2", e.VADDHN(I_16, D0, Q1, Q2));
	DIS("VADDHN.i64 d0, q1, q2", e.VADDHN(I_64, D0, Q1, Q2));
	DIS("VRADDHN.i16 d0, q1, q2", e.VRADDHN(I_16, D0, Q1, Q2));
	DIS("VRADDHN.i32 d0, q1, q2", e.VRADDHN(I_32, D0, Q1, Q2));
	DIS("VSUBHN.i32 d0, q1, q2", e.VSUBHN(I_32, D0, Q1, Q2));
	DIS("VSUBHN.i64 d0, q1, q2", e.VSUBHN(I_64, D0, Q1, Q2));
	DIS("VRSUBHN.i16 d0, q1, q2", e.VRSUBHN(I_16, D0, Q1, Q2));
	DIS("VRSUBHN.i64 d0, q1, q2", e.VRSUBHN(I_64, D0, Q1, Q2));
	DIS("VABAL.s8 q0, d2, d3", e.VABAL(I_8 | I_SIGNED, Q0, D2, D3));
	DIS("VABAL.u16 q0, d2, d3", e.VABAL(I_16 | I_UNSIGNED, Q0, D2, D3));
	DIS("VABDL.s32 q0, d2, d3", e.VABDL(I_32 | I_SIGNED, Q0, D2, D3));
	DIS("VABDL.u8 q0, d2, d3", e.VABDL(I_8 | I_UNSIGNED, Q0, D2, D3));
	DIS("VMLAL.s8 q0, d2, d3", e.VMLAL(I_8 | I_SIGNED, Q0, D2, D3));
	DIS("VMLAL.u32 q0, d2, d3", e.VMLAL(I_32 | I_UNSIGNED, Q0, D2, D3));
	DIS("VMLSL.s16 q0, d2, d3", e.VMLSL(I_16 | I_SIGNED, Q0, D2, D3));
	DIS("VMLSL.u8 q0, d2, d3", e.VMLSL(I_8 | I_UNSIGNED, Q0, D2, D3));
	DIS("VMULL.s8 q0, d2, d3", e.VMULL(I_8 | I_SIGNED, Q0, D2, D3));
	DIS("VMULL.u16 q0, d2, d3", e.VMULL(I_16 | I_UNSIGNED, Q0, D2, D3));
	DIS("VMULL.p8 q0, d2, d3", e.VMULL(I_8 | I_POLYNOMIAL, Q0, D2, D3));
	DIS("VQDMLAL.s16 q0, d2, d3", e.VQDMLAL(I_16, Q0, D2, D3));
	DIS("VQDMLAL.s32 q0, d2, d3", e.VQDMLAL(I_32, Q0, D2, D3));
	DIS("VQDMLSL.s16 q0, d2, d3", e.VQDMLSL(I_16, Q0, D2, D3));
	DIS("VQDMLSL.s32 q0, d2, d3", e.VQDMLSL(I_32, Q0, D2, D3));
	DIS("VQDMULL.s16 q0, d2, d3", e.VQDMULL(I_16, Q0, D2, D3));
	DIS("VQDMULL.s32 q0, d2, d3", e.VQDMULL(I_32, Q0, D2, D3));

	// Two registers and a scalar
	DIS("VMUL.f32 q12, q8, d0[0]", e.VMUL_scalar(F_32, Q12, Q8, DScalar(D0, 0)));
	DIS("VMUL.f32 q1, q2, d7[1]", e.VMUL_scalar(F_32, Q1, Q2, DScalar(D7, 1)));
	DIS("VMUL.f32 d1, d2, d14[0]", e.VMUL_scalar(F_32, D1, D2, QScalar(Q7, 0)));
	DIS("VMUL.f32 d1, d2, d15[1]", e.VMUL_scalar(F_32, D1, D2, QScalar(Q7, 3)));
	DIS("VMLA.f32 q8, q13, d8[1]", e.VMLA_scalar(F_32, Q8, Q13, DScalar(D8, 1)));
	DIS("VMLA.f32 q1, q2, d7[0]", e.VMLA_scalar(F_32, Q1, Q2, DScalar(D7, 0)));
	DIS("VMLA.f32 d1, d2, d7[0]", e.VMLA_scalar(F_32, D1, D2, XScalar(Q3, 2)));
	DIS("VMLA.f32 d1, d2, d3[1]", e.VMLA_scalar(F_32, D1, D2, XScalar(D3, 1)));

	// Two registers, miscellaneous
	DIS("VABS.f32 q1, q2", e.VABS(F_32, Q1, Q2));
	DIS("VABS.s8 d1, d2", e.VABS(I_8, D1, D2));
	DIS("VNEG.f32 q1, q2", e.VNEG(F_32, Q1, Q2));
	DIS("VNEG.s32 d1, d2", e.VNEG(I_32, D1, D2));
	DIS("VMVN q1, q13", e.VMVN(Q1, Q13));
	DIS("VMVN d1, d2", e.VMVN(D1, D2));
	DIS("VCEQ.i16 d0, d1, #0", e.VCEQ(I_16, D0, D1));
	DIS("VCEQ.f32 q0, q1, #0", e.VCEQ(F_32, Q0, Q1));
	DIS("VCGE.s8 d0, d1, #0", e.VCGE(I_8, D0, D1));
	DIS("VCGE.f32 q0, q1, #0", e.VCGE(F_32, Q0, Q1));
	DIS("VCGT.s32 q0, q1, #0", e.VCGT(I_32, Q0, Q1));
	DIS("VCGT.f32 d0, d1, #0", e.VCGT(F_32, D0, D1));
	DIS("VCLE.s16 d0, d1, #0", e.VCLE(I_16, D0, D1));
	DIS("VCLE.f32 q0, q1, #0", e.VCLE(F_32, Q0, Q1));
	DIS("VCLT.s8 q0, q1, #0", e.VCLT(I_8, Q0, Q1));
	DIS("VCLT.f32 d0, d1, #0", e.VCLT(F_32, D0, D1));
	DIS("VCLS.s8 d0, d1", e.VCLS(I_8, D0, D1));
	DIS("VCLS.s32 q0, q1", e.VCLS(I_32, Q0, Q1));
	DIS("VCLZ.i16 d0, d1", e.VCLZ(I_16, D0, D1));
	DIS("VCLZ.i32 q0, q1", e.VCLZ(I_32, Q0, Q1));
	DIS("VCNT.8 d0, d1", e.VCNT(I_8, D0, D1));
	DIS("VCNT.8 q0, q1", e.VCNT(I_8, Q0, Q1));
	DIS("VPADAL.s8 d0, d1", e.VPADAL(I_8 | I_SIGNED, D0, D1));
	DIS("VPADAL.u32 q0, q1", e.VPADAL(I_32 | I_UNSIGNED, Q0, Q1));
	DIS("VPADDL.s16 d0, d1", e.VPADDL(I_16 | I_SIGNED, D0, D1));
	DIS("VPADDL.u8 q0, q1", e.VPADDL(I_8 | I_UNSIGNED, Q0, Q1));
	DIS("VQABS.s8 d0, d1", e.VQABS(I_8, D0, D1));
	DIS("VQABS.s32 q0, q1", e.VQABS(I_32, Q0, Q1));
	DIS("VQNEG.s16 d0, d1", e.VQNEG(I_16, D0, D1));
	DIS("VQNEG.s8 q0, q1", e.VQNEG(I_8, Q0, Q1));
	DIS("VRECPE.f32 q0, q1", e.VRECPE(F_32, Q0, Q1));
	DIS("VRECPE.u32 d0, d1", e.VRECPE(I_32, D0, D1));
	DIS("VRSQRTE.f32 q0, q1", e.VRSQRTE(F_32, Q0, Q1));
	DIS("VRSQRTE.u32 d0, d1", e.VRSQRTE(I_32, D0, D1));
	DIS("VSWP q0, q1", e.VSWP(Q0, Q1));
	DIS("VSWP d0, d1", e.VSWP(D0, D1));
	DIS("VTRN.8 d0, d1", e.VTRN(I_8, D0, D1));
	DIS("VTRN.32 q0, q1", e.VTRN(I_32, Q0, Q1));
	DIS("VUZP.16 d0, d1", e.VUZP(I_16, D0, D1));
	DIS("VUZP.8 q0, q1", e.VUZP(I_8, Q0, Q1));
	DIS("VZIP.8 d0, d1", e.VZIP(I_8, D0, D1));
	DIS("VZIP.16 q0, q1", e.VZIP(I_16, Q0, Q1));
	DIS("VREV64.8 d0, d1", e.VREV64(I_8, D0, D1));
	DIS("VREV64.32 q0, q1", e.VREV64(I_32, Q0, Q1));
	DIS("VREV32.16 d0, d1", e.VREV32(I_16, D0, D1));
	DIS("VREV32.8 q0, q1", e.VREV32(I_8, Q0, Q1));
	DIS("VREV16.8 d0, d1", e.VREV16(I_8, D0, D1));
	DIS("VREV16.8 q2, q3", e.VREV16(I_8, Q2, Q3));
	DIS("VMOVN.i32 d1, q2", e.VMOVN(I_32, D1, Q2));
	DIS("VMOVN.i16 d0, q15", e.VMOVN(I_16, D0, Q15));
	DIS("VQMOVN.s32 d1, q2", e.VQMOVN(I_32 | I_SIGNED, D1, Q2));
	DIS("VQMOVN.u64 d1, q2", e.VQMOVN(I_64 | I_UNSIGNED, D1, Q2));
	DIS("VQMOVUN.s16 d1, q2", e.VQMOVUN(I_16, D1, Q2));
	DIS("VQMOVUN.s32 d1, q2", e.VQMOVUN(I_32, D1, Q2));
	DIS("VCVT.f32.u32 q0, q1", e.VCVT(F_32 | I_UNSIGNED, Q0, Q1));
	DIS("VCVT.f32.s32 d0, d1", e.VCVT(F_32 | I_SIGNED, D0, D1));
	DIS("VCVT.u32.f32 q2, q2", e.VCVT(I_32 | I_UNSIGNED, Q2, Q2));
	DIS("VCVT.s32.f32 d2, d3", e.VCVT(I_32 | I_SIGNED, D2, D3));
	DIS("VCVT.f32.f16 q0, d2", e.VCVTF32F16(Q0, D2));
	DIS("VCVT.f32.f16 q8, d15", e.VCVTF32F16(Q8, D15));
	DIS("VCVT.f16.f32 d0, q1", e.VCVTF16F32(D0, Q1));
	DIS("VCVT.f16.f32 d15, q8", e.VCVTF16F32(D15, Q8));

	// VEXT, VDUP and moves to and from core registers
	DIS("VEXT.8 d0, d1, d2, #3", e.VEXT(D0, D1, D2, 3));
	DIS("VEXT.8 q0, q1, q2, #15", e.VEXT(Q0, Q1, Q2, 15));
	DIS("VDUP.32 q14, d30[1]", e.VDUP(F_32, Q14, D30, 1));
	DIS("VDUP.32 d14, d30[1]", e.VDUP(F_32, D14, D30, 1));
	DIS("VDUP.8 d0, d1[7]", e.VDUP(I_8, D0, D1, 7));
	DIS("VDUP.16 q0, d1[3]", e.VDUP(I_16, Q0, D1, 3));
	DIS("VDUP.32 q0, r0", e.VDUP(I_32, Q0, R0));
	DIS("VDUP.32 q5, r3", e.VDUP(I_32, Q5, R3));
	DIS("VDUP.16 q2, r7", e.VDUP(I_16, Q2, R7));
	DIS("VDUP.32 d0, r0", e.VDUP(I_32, D0, R0));
	DIS("VDUP.16 d3, r5", e.VDUP(I_16, D3, R5));
	DIS("VDUP.8 q1, r2", e.VDUP(I_8, Q1, R2));
	DIS("VMOV.32 d5[1], r4", e.VMOV_neon(F_32, D5, R4, 1));
	DIS("VMOV.8 d0[6], r1", e.VMOV_neon(I_8, D0, R1, 6));
	DIS("VMOV.16 d20[3], r1", e.VMOV_neon(I_16, D20, R1, 3));
	DIS("VMOV.32 r2, d4[0]", e.VMOV_neon(I_32, R2, D4, 0));
	DIS("VMOV.u8 r2, d4[5]", e.VMOV_neon(I_8 | I_UNSIGNED, R2, D4, 5));
	DIS("VMOV.s16 r2, d17[2]", e.VMOV_neon(I_16 | I_SIGNED, R2, D17, 2));

	// Immediates
	DIS("VMOV.i32 d0, #0xf3", e.VMOV_imm(I_32, D0, VIMM___x___x, 0xF3));
	DIS("VMOV.i32 q1, #0x12000000", e.VMOV_imm(I_32, Q1, VIMMx___x___, 0x12));
	DIS("VMOV.i16 d0, #0x80", e.VMOV_imm(I_16, D0, VIMM_x_x_x_x, 0x80));
	DIS("VMOV.i16 q0, #0x7f00", e.VMOV_imm(I_16, Q0, VIMMx_x_x_x_, 0x7F));
	DIS("VMOV.i8 q0, #0xf3", e.VMOV_imm(I_8, Q0, VIMMxxxxxxxx, 0xF3));
	DIS("VMOV.i64 d1, #0xff000000000000ff", e.VMOV_imm(I_64, D1, VIMMbits2bytes, 0x81));
	DIS("VMOV.f32 q0, #1.0", e.VMOV_immf(Q0, 1.0f));
	DIS("VMOV.f32 d0, #-1.0", e.VMOV_immf(D0, -1.0f));
	DIS("VMOV.f32 q2, #1.5", e.VMOV_immf(Q2, 1.5f));
	DIS("VEOR q3, q3, q3", e.VMOV_immf(Q3, 0.0f));
	DIS("VORR.i32 d0, #0xf300", e.VORR_imm(I_32, D0, VIMM__x___x_, 0xF3));
	DIS("VORR.i16 q0, #0x1", e.VORR_imm(I_16, Q0, VIMM_x_x_x_x, 0x01));
	DIS("VBIC.i32 q0, #0xf30000", e.VBIC_imm(I_32, Q0, VIMM_x___x__, 0xF3));
	DIS("VBIC.i16 d0, #0xff00", e.VBIC_imm(I_16, D0, VIMMx_x_x_x_, 0xFF));
	DIS("VMVN.i32 d0, #0xf3", e.VMVN_imm(I_32, D0, VIMM___x___x, 0xF3));
	DIS("VMVN.i16 q0, #0x2", e.VMVN_imm(I_16, Q0, VIMM_x_x_x_x, 0x02));
	DIS("VMOV.i32 q0, #0xab", e.VMOV_neon(I_32, Q0, (u32)0x000000AB));
	DIS("VMOV.i32 d0, #0xab00", e.VMOV_neon(I_32, D0, (u32)0x0000AB00));
	DIS("VMOV.i32 d0, #0xab0000", e.VMOV_neon(I_32, D0, (u32)0x00AB0000));
	DIS("VMOV.i32 d0, #0xab000000", e.VMOV_neon(I_32, D0, (u32)0xAB000000));
	DIS("VMOV.i16 d0, #0xab", e.VMOV_neon(I_16, D0, (u32)0x00AB));
	DIS("VMOV.i16 q0, #0xab00", e.VMOV_neon(I_16, Q0, (u32)0xAB00));
	DIS("VMOV.i32 d0, #0xabff", e.VMOV_neon(I_32, D0, (u32)0x0000ABFF));
	DIS("VMOV.i32 d0, #0xabffff", e.VMOV_neon(I_32, D0, (u32)0x00ABFFFF));
	DIS("VMOV.i8 q0, #0x5a", e.VMOV_neon(I_8, Q0, (u32)0x5A));
	DIS("VMOV.f32 q0, #1.0", e.VMOV_neon(I_32, Q0, (u32)0x3F800000));
	DIS("VMOV.i16 q0, #0xff00", e.VMOV_neon(I_32, Q0, (u32)0xFF00FF00));
	DIS("VMOV.i32 q1, #0x40000000", e.VMOV_neon(F_32, Q1, 2.0f));
	DIS("VMOV.f32 q0, #1.0", e.MOVI2F_neon(Q0, 1.0f, R12));
	DIS("MOVW r12, #0xcccd; MOVT r12, #0x3dcc; VDUP.32 d0, r12", e.MOVI2F_neon(D0, 0.1f, R12));

	// Shifts by immediate
	DIS("VSHL.i8 d1, d2, #7", e.VSHL(I_8, D1, D2, 7));
	DIS("VSHL.i64 q1, q2, #33", e.VSHL(I_64, Q1, Q2, 33));
	DIS("VSHLL.i16 q0, d0, #16", e.VSHLL(I_16, Q0, D0, 16));
	DIS("VSHLL.i8 q0, d0, #8", e.VSHLL(I_8, Q0, D0, 8));
	DIS("VSHLL.s8 q0, d0, #4", e.VSHLL(I_8, Q0, D0, 4));
	DIS("VSHLL.s32 q1, d2, #17", e.VSHLL(I_32, Q1, D2, 17));
	DIS("VSHLL.u16 q1, d2, #3", e.VSHLL(I_16 | I_UNSIGNED, Q1, D2, 3));
	DIS("VSHR.u32 q1, q2, #19", e.VSHR(I_32 | I_UNSIGNED, Q1, Q2, 19));
	DIS("VSHR.s64 q1, q2, #36", e.VSHR(I_64, Q1, Q2, 36));
	DIS("VSHR.s8 d1, d2, #8", e.VSHR(I_8 | I_SIGNED, D1, D2, 8));
	DIS("VSHRN.i32 d0, q0, #16", e.VSHRN(I_32, D0, Q0, 16));
	DIS("VSHRN.i64 d1, q2, #24", e.VSHRN(I_64, D1, Q2, 24));
	DIS("VSHRN.i32 d1, q2, #8", e.VSHRN(I_32, D1, Q2, 8));
	DIS("VSHRN.i16 d1, q2, #1", e.VSHRN(I_16, D1, Q2, 1));
	DIS("VMOVL.u16 q0, d0", e.VMOVL(I_16 | I_UNSIGNED, Q0, D0));
	DIS("VMOVL.s32 q0, d0", e.VMOVL(I_32 | I_SIGNED, Q0, D0));
	DIS("VMOVL.u8 q8, d31", e.VMOVL(I_8 | I_UNSIGNED, Q8, D31));

	// Element and structure loads and stores
	DIS("VLD1.32 {d19-d20}, [r3]", e.VLD1(I_32, D19, R3, 2, ALIGN_NONE, R_PC));
	DIS("VLD1.32 {d0-d3}, [r0:128]!", e.VLD1(F_32, D0, R0, 4, ALIGN_128, REG_UPDATE));
	DIS("VLD1.8 {d16-d18}, [r1:64], r2", e.VLD1(I_8, D16, R1, 3, ALIGN_64, R2));
	DIS("VLD1.64 {d2}, [r4]", e.VLD1(I_64, D2, R4, 1));
	DIS("VST1.32 {d23}, [r9]", e.VST1(I_32, D23, R9, 1, ALIGN_NONE, R_PC));
	DIS("VST1.16 {d0-d1}, [sp:128]!", e.VST1(I_16, D0, R_SP, 2, ALIGN_128, REG_UPDATE));
	DIS("VST1.32 {d8-d11}, [r0:256]", e.VST1(F_32, D8, R0, 4, ALIGN_256));
	DIS("VLD1.32 {d8[0]}, [r3]", e.VLD1_lane(F_32, D8, R3, 0, false));
	DIS("VLD1.8 {d8[2]}, [r3]", e.VLD1_lane(I_8, D8, R3, 2, false));
	DIS("VLD1.16 {d0[3]}, [r1:16]!", e.VLD1_lane(I_16, D0, R1, 3, true, REG_UPDATE));
	DIS("VLD1.32 {d3[1]}, [r1:32], r2", e.VLD1_lane(I_32, Q1, R1, 3, true, R2));
	DIS("VST1.32 {d8[1]}, [r3]", e.VST1_lane(I_32, D8, R3, 1, false));
	DIS("VST1.8 {d5[2]}, [r3]", e.VST1_lane(I_8, Q2, R3, 10, false));
	DIS("VLD1.32 {d0[], d1[]}, [r0:32]", e.VLD1_all_lanes(F_32, Q0, R0, true));
	DIS("VLD1.16 {d2[]}, [r1]!", e.VLD1_all_lanes(I_16, D2, R1, false, REG_UPDATE));
	DIS("VLD1.8 {d2[]}, [r1], r3", e.VLD1_all_lanes(I_8, D2, R1, false, R3));
}

bool TestArmDisasm() {
	// Aligned, so that NopAlignCode16() pads the same everywhere.
	alignas(16) static u32 code[4096];
	const u8 *base = (const u8 *)code;
	ARMXEmitter emitter((u8 *)code);
	DisasmChecker checker{ emitter, base };

	// These assert on CPU features, which we don't care about when only encoding.
	bool vfpv4 = cpu_info.bVFPv4;
	bool idiva = cpu_info.bIDIVa;
	cpu_info.bVFPv4 = true;
	cpu_info.bIDIVa = true;

	TestIntegerInstructions(checker, emitter, base);
	emitter.SetCodePointer((u8 *)base + 0x2000, (u8 *)base + 0x2000);
	TestFloatInstructions(checker, emitter);
	TestNeonInstructions(checker, emitter);

	cpu_info.bVFPv4 = vfpv4;
	cpu_info.bIDIVa = idiva;

	printf("%d of %d ARM disassembly checks failed\n", checker.failures, checker.checks);
	return checker.failures == 0;
}
