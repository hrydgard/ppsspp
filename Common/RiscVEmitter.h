// Copyright (c) 2022- PPSSPP Project.

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
#include "Common/CodeBlock.h"
#include "Common/Common.h"

namespace RiscVGen {

enum RiscVReg {
	X0 = 0, X1, X2, X3, X4, X5, X6, X7,
	X8, X9, X10, X11, X12, X13, X14, X15,
	X16, X17, X18, X19, X20, X21, X22, X23,
	X24, X25, X26, X27, X28, X29, X30, X31,

	R_ZERO = 0,
	R_RA = 1,
	R_SP = 2,
	R_GP = 3,
	R_TP = 4,
	R_FP = 8,

	F0 = 0x20, F1, F2, F3, F4, F5, F6, F7,
	F8, F9, F10, F11, F12, F13, F14, F15,
	F16, F17, F18, F19, F20, F21, F22, F23,
	F24, F25, F26, F27, F28, F29, F30, F31,
};

enum class FixupBranchType {
	B,
	J,
};

enum class Fence {
	I = 0b1000,
	O = 0b0100,
	R = 0b0010,
	W = 0b0001,
	RW = R | W,
	IO = I | O,
};
ENUM_CLASS_BITOPS(Fence);

struct FixupBranch {
	FixupBranch(const u8 *p, FixupBranchType t) : ptr(p), type(t) {}

	const u8 *ptr;
	FixupBranchType type;
};

class RiscVEmitter {
public:
	RiscVEmitter() {}
	RiscVEmitter(const u8 *codePtr, u8 *writablePtr);
	virtual ~RiscVEmitter() {}

	void SetCodePointer(const u8 *ptr, u8 *writePtr);
	const u8 *GetCodePointer() const;
	u8 *GetWritableCodePtr();

	void ReserveCodeSpace(u32 bytes);
	const u8 *AlignCode16();
	const u8 *AlignCodePage();
	void FlushIcache();
	void FlushIcacheSection(const u8 *start, const u8 *end);

	void SetJumpTarget(const FixupBranch &branch);
	bool BInRange(const void *func) const;
	bool JInRange(const void *func) const;

	void LUI(RiscVReg rd, s32 simm32);
	void AUIPC(RiscVReg rd, s32 simm32);

	void JAL(RiscVReg rd, const void *dst);
	void JALR(RiscVReg rd, RiscVReg rs1, s32 simm12);
	FixupBranch JAL(RiscVReg rd);

	// Psuedo-instructions for convenience/clarity.
	void J(const void *dst) {
		JAL(R_ZERO, dst);
	}
	void JR(RiscVReg rs1, u32 simm12 = 0) {
		JALR(R_ZERO, rs1, simm12);
	}
	void RET() {
		JR(R_RA);
	}
	FixupBranch J() {
		return JAL(R_ZERO);
	}

	void BEQ(RiscVReg rs1, RiscVReg rs2, const void *dst);
	void BNE(RiscVReg rs1, RiscVReg rs2, const void *dst);
	void BLT(RiscVReg rs1, RiscVReg rs2, const void *dst);
	void BGE(RiscVReg rs1, RiscVReg rs2, const void *dst);
	void BLTU(RiscVReg rs1, RiscVReg rs2, const void *dst);
	void BGEU(RiscVReg rs1, RiscVReg rs2, const void *dst);
	FixupBranch BEQ(RiscVReg rs1, RiscVReg rs2);
	FixupBranch BNE(RiscVReg rs1, RiscVReg rs2);
	FixupBranch BLT(RiscVReg rs1, RiscVReg rs2);
	FixupBranch BGE(RiscVReg rs1, RiscVReg rs2);
	FixupBranch BLTU(RiscVReg rs1, RiscVReg rs2);
	FixupBranch BGEU(RiscVReg rs1, RiscVReg rs2);

	void LB(RiscVReg rd, RiscVReg rs1, s32 simm12);
	void LH(RiscVReg rd, RiscVReg rs1, s32 simm12);
	void LW(RiscVReg rd, RiscVReg rs1, s32 simm12);
	void LBU(RiscVReg rd, RiscVReg rs1, s32 simm12);
	void LHU(RiscVReg rd, RiscVReg rs1, s32 simm12);

	void SB(RiscVReg rs2, RiscVReg rs1, s32 simm12);
	void SH(RiscVReg rs2, RiscVReg rs1, s32 simm12);
	void SW(RiscVReg rs2, RiscVReg rs1, s32 simm12);

	void ADDI(RiscVReg rd, RiscVReg rs1, s32 simm12);
	void SLTI(RiscVReg rd, RiscVReg rs1, s32 simm12);
	void SLTIU(RiscVReg rd, RiscVReg rs1, s32 simm12);
	void XORI(RiscVReg rd, RiscVReg rs1, s32 simm12);
	void ORI(RiscVReg rd, RiscVReg rs1, s32 simm12);
	void ANDI(RiscVReg rd, RiscVReg rs1, s32 simm12);

	void NOP() {
		ADDI(R_ZERO, R_ZERO, 0);
	}
	void MV(RiscVReg rd, RiscVReg rs1) {
		ADDI(rd, rs1, 0);
	}
	void NOT(RiscVReg rd, RiscVReg rs1) {
		XORI(rd, rs1, -1);
	}

	void SLLI(RiscVReg rd, RiscVReg rs1, u32 shamt);
	void SRLI(RiscVReg rd, RiscVReg rs1, u32 shamt);
	void SRAI(RiscVReg rd, RiscVReg rs1, u32 shamt);

	void ADD(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SUB(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SLL(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SLT(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SLTU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void XOR(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SRL(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SRA(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void OR(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void AND(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);

	void NEG(RiscVReg rd, RiscVReg rs) {
		SUB(rd, R_ZERO, rs);
	}

	void FENCE(Fence predecessor, Fence successor);
	void FENCE_TSO();

	void ECALL();
	void EBREAK();

	// 64-bit instructions - oens ending in W sign extend result to 32 bits.
	void LWU(RiscVReg rd, RiscVReg rs1, s32 simm12);
	void LD(RiscVReg rd, RiscVReg rs1, s32 simm12);
	void SD(RiscVReg rs2, RiscVReg rs1, s32 simm12);
	void ADDIW(RiscVReg rd, RiscVReg rs1, s32 simm12);
	void SLLIW(RiscVReg rd, RiscVReg rs1, u32 shamt);
	void SRLIW(RiscVReg rd, RiscVReg rs1, u32 shamt);
	void SRAIW(RiscVReg rd, RiscVReg rs1, u32 shamt);
	void ADDW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SUBW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SLLW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SRLW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SRAW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);

	void NEGW(RiscVReg rd, RiscVReg rs) {
		SUBW(rd, R_ZERO, rs);
	}

private:
	void SetJumpTarget(const FixupBranch &branch, const void *dst);
	bool BInRange(const void *src, const void *dst) const;
	bool JInRange(const void *src, const void *dst) const;

	inline void Write32(u32 value) {
		*(u32 *)writable_ = value;
		code_ += 4;
		writable_ += 4;
	}
	inline void Write16(u16 value) {
		*(u16 *)writable_ = value;
		code_ += 2;
		writable_ += 2;
	}

	const u8 *code_ = nullptr;
	u8 *writable_ = nullptr;
	const u8 *lastCacheFlushEnd_ = nullptr;
};

class MIPSCodeBlock : public CodeBlock<RiscVEmitter> {
private:
	void PoisonMemory(int offset) override;
};

};
