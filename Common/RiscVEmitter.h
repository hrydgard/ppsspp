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
#include <cstring>
#include <type_traits>
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
	CB,
	CJ,
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

enum class Atomic {
	NONE = 0b00,
	ACQUIRE = 0b10,
	RELEASE = 0b01,
	SEQUENTIAL = 0b11,
};

enum class Round {
	NEAREST_EVEN = 0b000,
	TOZERO = 0b001,
	DOWN = 0b010,
	UP = 0b011,
	NEAREST_MAX = 0b100,
	DYNAMIC = 0b111,
};

enum class FConv {
	W = 0x0000,
	WU = 0x0001,
	L = 0x0002,
	LU = 0x0003,

	S = 0x1000,
	D = 0x1001,
	Q = 0x1003,
};

enum class FMv {
	X,
	W,
	D,
};

enum class Csr {
	FFlags = 0x001,
	FRm = 0x002,
	FCsr = 0x003,

	Cycle = 0xC00,
	Time = 0xC01,
	InstRet = 0xC02,
	CycleH = 0xC80,
	TimeH = 0xC81,
	InstRetH = 0xC82,
};

struct FixupBranch {
	FixupBranch() {}
	FixupBranch(const u8 *p, FixupBranchType t) : ptr(p), type(t) {}
	~FixupBranch();

	const u8 *ptr = nullptr;
	FixupBranchType type = FixupBranchType::B;
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

	void SetJumpTarget(FixupBranch &branch);
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

	void LB(RiscVReg rd, RiscVReg addr, s32 simm12);
	void LH(RiscVReg rd, RiscVReg addr, s32 simm12);
	void LW(RiscVReg rd, RiscVReg addr, s32 simm12);
	void LBU(RiscVReg rd, RiscVReg addr, s32 simm12);
	void LHU(RiscVReg rd, RiscVReg addr, s32 simm12);

	void SB(RiscVReg rs2, RiscVReg addr, s32 simm12);
	void SH(RiscVReg rs2, RiscVReg addr, s32 simm12);
	void SW(RiscVReg rs2, RiscVReg addr, s32 simm12);

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

	// The temp reg is only possibly used for 64-bit values.
	template <typename T>
	void LI(RiscVReg rd, const T &v, RiscVReg temp = R_ZERO) {
		_assert_msg_(rd != R_ZERO, "LI to X0");
		_assert_msg_(rd < F0 && temp < F0, "LI to non-GPR");

		uint64_t value = AsImmediate<T, std::is_signed<T>::value>(v);
		SetRegToImmediate(rd, value, temp);
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
	void LWU(RiscVReg rd, RiscVReg addr, s32 simm12);
	void LD(RiscVReg rd, RiscVReg addr, s32 simm12);
	void SD(RiscVReg rs2, RiscVReg addr, s32 simm12);
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

	// Integer multiplication and division.
	void MUL(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void MULH(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void MULHSU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void MULHU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void DIV(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void DIVU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void REM(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void REMU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	// 64-bit only multiply and divide.
	void MULW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void DIVW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void DIVUW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void REMW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void REMUW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);

	// Atomic memory operations.
	void LR(int bits, RiscVReg rd, RiscVReg addr, Atomic ordering);
	void SC(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg addr, Atomic ordering);
	void AMOSWAP(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg addr, Atomic ordering);
	void AMOADD(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg addr, Atomic ordering);
	void AMOAND(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg addr, Atomic ordering);
	void AMOOR(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg addr, Atomic ordering);
	void AMOXOR(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg addr, Atomic ordering);
	void AMOMIN(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg addr, Atomic ordering);
	void AMOMAX(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg addr, Atomic ordering);
	void AMOMINU(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg addr, Atomic ordering);
	void AMOMAXU(int bits, RiscVReg rd, RiscVReg rs2, RiscVReg addr, Atomic ordering);

	// Floating point (same funcs for single/double/quad, if supported.)
	void FL(int bits, RiscVReg rd, RiscVReg addr, s32 simm12);
	void FS(int bits, RiscVReg rs2, RiscVReg addr, s32 simm12);
	void FLW(RiscVReg rd, RiscVReg addr, s32 simm12) {
		FL(32, rd, addr, simm12);
	}
	void FSW(RiscVReg rs2, RiscVReg addr, s32 simm12) {
		FS(32, rs2, addr, simm12);
	}

	void FMADD(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, RiscVReg rs3, Round rm = Round::DYNAMIC);
	void FMSUB(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, RiscVReg rs3, Round rm = Round::DYNAMIC);
	void FNMSUB(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, RiscVReg rs3, Round rm = Round::DYNAMIC);
	void FNMADD(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, RiscVReg rs3, Round rm = Round::DYNAMIC);

	void FADD(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, Round rm = Round::DYNAMIC);
	void FSUB(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, Round rm = Round::DYNAMIC);
	void FMUL(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, Round rm = Round::DYNAMIC);
	void FDIV(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2, Round rm = Round::DYNAMIC);
	void FSQRT(int bits, RiscVReg rd, RiscVReg rs1, Round rm = Round::DYNAMIC);

	void FSGNJ(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void FSGNJN(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void FSGNJX(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2);

	void FMV(int bits, RiscVReg rd, RiscVReg rs) {
		FSGNJ(bits, rd, rs, rs);
	}
	void FNEG(int bits, RiscVReg rd, RiscVReg rs) {
		FSGNJN(bits, rd, rs, rs);
	}
	void FABS(int bits, RiscVReg rd, RiscVReg rs) {
		FSGNJX(bits, rd, rs, rs);
	}

	void FMIN(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void FMAX(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2);

	void FCVT(FConv to, FConv from, RiscVReg rd, RiscVReg rs1, Round rm = Round::DYNAMIC);
	void FMV(FMv to, FMv from, RiscVReg rd, RiscVReg rs1);

	void FEQ(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void FLT(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void FLE(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void FCLASS(int bits, RiscVReg rd, RiscVReg rs1);

	// Control state register manipulation.
	void CSRRW(RiscVReg rd, Csr csr, RiscVReg rs1);
	void CSRRS(RiscVReg rd, Csr csr, RiscVReg rs1);
	void CSRRC(RiscVReg rd, Csr csr, RiscVReg rs1);
	void CSRRWI(RiscVReg rd, Csr csr, u8 uimm5);
	void CSRRSI(RiscVReg rd, Csr csr, u8 uimm5);
	void CSRRCI(RiscVReg rd, Csr csr, u8 uimm5);

	void FRRM(RiscVReg rd) {
		CSRRS(rd, Csr::FRm, R_ZERO);
	}
	void FSRM(RiscVReg rs) {
		CSRRW(R_ZERO, Csr::FRm, rs);
	}
	void FSRMI(RiscVReg rd, Round rm) {
		_assert_msg_(rm != Round::DYNAMIC, "Cannot set FRm to DYNAMIC");
		CSRRWI(rd, Csr::FRm, (uint8_t)rm);
	}
	void FSRMI(Round rm) {
		FSRMI(R_ZERO, rm);
	}

	// Compressed instructions.
	void C_ADDI4SPN(RiscVReg rd, u32 nzuimm10);
	void C_FLD(RiscVReg rd, RiscVReg addr, u8 uimm8);
	void C_LW(RiscVReg rd, RiscVReg addr, u8 uimm7);
	void C_FLW(RiscVReg rd, RiscVReg addr, u8 uimm7);
	void C_LD(RiscVReg rd, RiscVReg addr, u8 uimm8);
	void C_FSD(RiscVReg rs2, RiscVReg addr, u8 uimm8);
	void C_SW(RiscVReg rs2, RiscVReg addr, u8 uimm7);
	void C_FSW(RiscVReg rs2, RiscVReg addr, u8 uimm7);
	void C_SD(RiscVReg rs2, RiscVReg addr, u8 uimm8);

	void C_NOP();
	void C_ADDI(RiscVReg rd, s8 nzsimm6);
	void C_JAL(const void *dst);
	FixupBranch C_JAL();
	void C_ADDIW(RiscVReg rd, s8 simm6);
	void C_LI(RiscVReg rd, s8 simm6);
	void C_ADDI16SP(s32 nzsimm10);
	void C_LUI(RiscVReg rd, s32 nzsimm18);
	void C_SRLI(RiscVReg rd, u8 nzuimm6);
	void C_SRAI(RiscVReg rd, u8 nzuimm6);
	void C_ANDI(RiscVReg rd, s8 simm6);
	void C_SUB(RiscVReg rd, RiscVReg rs2);
	void C_XOR(RiscVReg rd, RiscVReg rs2);
	void C_OR(RiscVReg rd, RiscVReg rs2);
	void C_AND(RiscVReg rd, RiscVReg rs2);
	void C_SUBW(RiscVReg rd, RiscVReg rs2);
	void C_ADDW(RiscVReg rd, RiscVReg rs2);
	void C_J(const void *dst);
	void C_BEQZ(RiscVReg rs1, const void *dst);
	void C_BNEZ(RiscVReg rs1, const void *dst);
	FixupBranch C_J();
	FixupBranch C_BEQZ(RiscVReg rs1);
	FixupBranch C_BNEZ(RiscVReg rs1);

	void C_SLLI(RiscVReg rd, u8 nzuimm6);
	void C_FLDSP(RiscVReg rd, u32 uimm9);
	void C_LWSP(RiscVReg rd, u8 uimm8);
	void C_FLWSP(RiscVReg rd, u8 uimm8);
	void C_LDSP(RiscVReg rd, u32 uimm9);
	void C_JR(RiscVReg rs1);
	void C_MV(RiscVReg rd, RiscVReg rs2);
	void C_EBREAK();
	void C_JALR(RiscVReg rs1);
	void C_ADD(RiscVReg rd, RiscVReg rs2);
	void C_FSDSP(RiscVReg rs2, u32 uimm9);
	void C_SWSP(RiscVReg rs2, u8 uimm8);
	void C_FSWSP(RiscVReg rs2, u8 uimm8);
	void C_SDSP(RiscVReg rs2, u32 uimm9);

	bool CBInRange(const void *func) const;
	bool CJInRange(const void *func) const;

	bool SetAutoCompress(bool flag) {
		bool prev = autoCompress_;
		autoCompress_ = flag;
		return prev;
	}
	bool AutoCompress() const;

private:
	void SetJumpTarget(FixupBranch &branch, const void *dst);
	bool BInRange(const void *src, const void *dst) const;
	bool JInRange(const void *src, const void *dst) const;
	bool CBInRange(const void *src, const void *dst) const;
	bool CJInRange(const void *src, const void *dst) const;

	void SetRegToImmediate(RiscVReg rd, uint64_t value, RiscVReg temp);

	template <typename T, bool extend>
	uint64_t AsImmediate(const T &v) {
		static_assert(std::is_trivial<T>::value, "Immediate argument must be a simple type");
		static_assert(sizeof(T) <= 8, "Immediate argument size should be 8, 16, 32, or 64 bits");

		// Copy the type to allow floats and avoid endian issues.
		if (sizeof(T) == 8) {
			uint64_t value;
			memcpy(&value, &v, sizeof(value));
			return value;
		} else if (sizeof(T) == 4) {
			uint32_t value;
			memcpy(&value, &v, sizeof(value));
			if (extend)
				return (int64_t)(int32_t)value;
			return value;
		} else if (sizeof(T) == 2) {
			uint16_t value;
			memcpy(&value, &v, sizeof(value));
			if (extend)
				return (int64_t)(int16_t)value;
			return value;
		} else if (sizeof(T) == 1) {
			uint8_t value;
			memcpy(&value, &v, sizeof(value));
			if (extend)
				return (int64_t)(int8_t)value;
			return value;
		}
		return (uint64_t)v;
	}

	inline void Write32(u32 value) {
		Write16(value & 0x0000FFFF);
		Write16(value >> 16);
	}
	inline void Write16(u16 value) {
		*(u16 *)writable_ = value;
		code_ += 2;
		writable_ += 2;
	}

	const u8 *code_ = nullptr;
	u8 *writable_ = nullptr;
	const u8 *lastCacheFlushEnd_ = nullptr;
	bool autoCompress_ = false;
};

class MIPSCodeBlock : public CodeBlock<RiscVEmitter> {
private:
	void PoisonMemory(int offset) override;
};

};
