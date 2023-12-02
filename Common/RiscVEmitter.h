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

	V0 = 0x40, V1, V2, V3, V4, V5, V6, V7,
	V8, V9, V10, V11, V12, V13, V14, V15,
	V16, V17, V18, V19, V20, V21, V22, V23,
	V24, V25, V26, V27, V28, V29, V30, V31,

	INVALID_REG = 0xFFFFFFFF,
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
	H = 0x1002,
	Q = 0x1003,
};

enum class FMv {
	X,
	H,
	W,
	D,
};

enum class Csr {
	FFlags = 0x001,
	FRm = 0x002,
	FCsr = 0x003,

	VStart = 0x008,
	VXSat = 0x009,
	VXRm = 0x00A,
	VCsr = 0x00F,
	VL = 0xC20,
	VType = 0xC21,
	VLenB = 0xC22,

	Cycle = 0xC00,
	Time = 0xC01,
	InstRet = 0xC02,
	CycleH = 0xC80,
	TimeH = 0xC81,
	InstRetH = 0xC82,
};

enum class VLMul {
	M1 = 0b000,
	M2 = 0b001,
	M4 = 0b010,
	M8 = 0b011,
	MF8 = 0b101,
	MF4 = 0b110,
	MF2 = 0b111,
};

enum class VSew {
	E8 = 0b000,
	E16 = 0b001,
	E32 = 0b010,
	E64 = 0b011,
};

enum class VTail {
	U = 0,
	A = 1,
};

enum class VMask {
	U = 0,
	A = 1,
};

struct VType {
	constexpr VType(VSew sew, VTail vt, VMask vm)
		: value(((uint32_t)sew << 3) | ((uint32_t)vt << 6) | ((uint32_t)vm << 7)) {
	}
	constexpr VType(VSew sew, VLMul lmul, VTail vt, VMask vm)
		: value((uint32_t)lmul | ((uint32_t)sew << 3) | ((uint32_t)vt << 6) | ((uint32_t)vm << 7)) {
	}

	VType(int bits, VLMul lmul, VTail vt, VMask vm) {
		VSew sew = VSew::E8;
		switch (bits) {
		case 8: sew = VSew::E8; break;
		case 16: sew = VSew::E16; break;
		case 32: sew = VSew::E32; break;
		case 64: sew = VSew::E64; break;
		default: _assert_msg_(false, "Invalid vtype width"); break;
		}
		value = (uint32_t)lmul | ((uint32_t)sew << 3) | ((uint32_t)vt << 6) | ((uint32_t)vm << 7);
	}

	uint32_t value;
};

enum class VUseMask {
	V0_T = 0,
	NONE = 1,
};

struct FixupBranch {
	FixupBranch() {}
	FixupBranch(const u8 *p, FixupBranchType t) : ptr(p), type(t) {}
	FixupBranch(FixupBranch &&other);
	FixupBranch(const FixupBranch &) = delete;
	~FixupBranch();

	FixupBranch &operator =(FixupBranch &&other);
	FixupBranch &operator =(const FixupBranch &other) = delete;

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

	void QuickJAL(RiscVReg scratchreg, RiscVReg rd, const u8 *dst);
	void QuickJ(RiscVReg scratchreg, const u8 *dst) {
		QuickJAL(scratchreg, R_ZERO, dst);
	}
	void QuickCallFunction(const u8 *func, RiscVReg scratchreg = R_RA) {
		QuickJAL(scratchreg, R_RA, func);
	}
	template <typename T>
	void QuickCallFunction(T *func, RiscVReg scratchreg = R_RA) {
		static_assert(std::is_function<T>::value, "QuickCallFunction without function");
		QuickCallFunction((const u8 *)func, scratchreg);
	}

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
	void SNEZ(RiscVReg rd, RiscVReg rs) {
		SLTU(rd, R_ZERO, rs);
	}
	void SEQZ(RiscVReg rd, RiscVReg rs) {
		SLTIU(rd, rs, 1);
	}

	void FENCE(Fence predecessor, Fence successor);
	void FENCE_TSO();

	void ECALL();
	void EBREAK();

	// 64-bit instructions - ones ending in W sign extend result to 32 bits.
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

	// Additional floating point (Zfa.)
	bool CanFLI(int bits, double v) const;
	bool CanFLI(int bits, uint32_t pattern) const;
	bool CanFLI(int bits, float v) const {
		return CanFLI(bits, (double)v);
	}
	void FLI(int bits, RiscVReg rd, double v);
	void FLI(int bits, RiscVReg rd, uint32_t pattern);
	void FLI(int bits, RiscVReg rd, float v) {
		FLI(bits, rd, (double)v);
	}
	void FMINM(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void FMAXM(int bits, RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void FROUND(int bits, RiscVReg rd, RiscVReg rs1, Round rm = Round::DYNAMIC);

	// Convenience helper for FLI support.
	void QuickFLI(int bits, RiscVReg rd, double v, RiscVReg scratchReg);
	void QuickFLI(int bits, RiscVReg rd, uint32_t pattern, RiscVReg scratchReg);
	void QuickFLI(int bits, RiscVReg rd, float v, RiscVReg scratchReg);

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

	// Vector instructions.
	void VSETVLI(RiscVReg rd, RiscVReg rs1, VType vtype);
	void VSETIVLI(RiscVReg rd, u8 uimm5, VType vtype);
	void VSETVL(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);

	// Load contiguous registers, unordered.
	void VLE_V(int dataBits, RiscVReg vd, RiscVReg rs1, VUseMask vm = VUseMask::NONE) {
		VLSEGE_V(1, dataBits, vd, rs1, vm);
	}
	// Load registers with stride (note: rs2/stride can be X0/zero to broadcast.)
	void VLSE_V(int dataBits, RiscVReg vd, RiscVReg rs1, RiscVReg rs2, VUseMask vm = VUseMask::NONE) {
		VLSSEGE_V(1, dataBits, vd, rs1, rs2, vm);
	}
	// Load indexed registers (gather), unordered.
	void VLUXEI_V(int indexBits, RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE) {
		VLUXSEGEI_V(1, indexBits, vd, rs1, vs2, vm);
	}
	// Load indexed registers (gather), ordered.
	void VLOXEI_V(int indexBits, RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE) {
		VLOXSEGEI_V(1, indexBits, vd, rs1, vs2, vm);
	}
	// Load mask (force 8 bit, EMUL=1, TA)
	void VLM_V(RiscVReg vd, RiscVReg rs1);
	// Load but ignore faults after first element.
	void VLEFF_V(int dataBits, RiscVReg vd, RiscVReg rs1, VUseMask vm = VUseMask::NONE) {
		VLSEGEFF_V(1, dataBits, vd, rs1, vm);
	}
	// Load fields into subsequent registers (destructure.)
	void VLSEGE_V(int fields, int dataBits, RiscVReg vd, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VLSSEGE_V(int fields, int dataBits, RiscVReg vd, RiscVReg rs1, RiscVReg rs2, VUseMask vm = VUseMask::NONE);
	void VLUXSEGEI_V(int fields, int indexBits, RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VLOXSEGEI_V(int fields, int indexBits, RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VLSEGEFF_V(int fields, int dataBits, RiscVReg vd, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	// Load entire registers (implementation dependent size.)
	void VLR_V(int regs, int hintBits, RiscVReg vd, RiscVReg rs1);

	void VSE_V(int dataBits, RiscVReg vs3, RiscVReg rs1, VUseMask vm = VUseMask::NONE) {
		VSSEGE_V(1, dataBits, vs3, rs1, vm);
	}
	void VSSE_V(int dataBits, RiscVReg vs3, RiscVReg rs1, RiscVReg rs2, VUseMask vm = VUseMask::NONE) {
		VSSSEGE_V(1, dataBits, vs3, rs1, rs2, vm);
	}
	void VSUXEI_V(int indexBits, RiscVReg vs3, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE) {
		VSUXSEGEI_V(1, indexBits, vs3, rs1, vs2, vm);
	}
	void VSOXEI_V(int indexBits, RiscVReg vs3, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE) {
		VSOXSEGEI_V(1, indexBits, vs3, rs1, vs2, vm);
	}
	void VSM_V(RiscVReg vs3, RiscVReg rs1);
	void VSSEGE_V(int fields, int dataBits, RiscVReg vs3, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VSSSEGE_V(int fields, int dataBits, RiscVReg vs3, RiscVReg rs1, RiscVReg rs2, VUseMask vm = VUseMask::NONE);
	void VSUXSEGEI_V(int fields, int indexBits, RiscVReg vs3, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VSOXSEGEI_V(int fields, int indexBits, RiscVReg vs3, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VSR_V(int regs, RiscVReg vs3, RiscVReg rs1);

	void VADD_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VADD_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VADD_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VSUB_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VSUB_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VRSUB_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VRSUB_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VNEG_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE) {
		VRSUB_VX(vd, vs2, X0, vm);
	}

	void VWADDU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWADDU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VWSUBU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWSUBU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VWADD_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWADD_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VWSUB_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWSUB_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VWADDU_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWADDU_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VWSUBU_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWSUBU_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VWADD_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWADD_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VWSUB_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWSUB_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	void VZEXT_V(int frac, RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VSEXT_V(int frac, RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);

	// vmask must be V0, provided for clarity/reminder.
	void VADC_VVM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, RiscVReg vmask);
	void VADC_VXM(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, RiscVReg vmask);
	void VADC_VIM(RiscVReg vd, RiscVReg vs2, s8 simm5, RiscVReg vmask);
	void VMADC_VVM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, RiscVReg vmask);
	void VMADC_VXM(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, RiscVReg vmask);
	void VMADC_VIM(RiscVReg vd, RiscVReg vs2, s8 simm5, RiscVReg vmask);
	void VMADC_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1);
	void VMADC_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1);
	void VMADC_VI(RiscVReg vd, RiscVReg vs2, s8 simm5);
	void VSBC_VVM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, RiscVReg vmask);
	void VSBC_VXM(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, RiscVReg vmask);
	void VMSBC_VVM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, RiscVReg vmask);
	void VMSBC_VXM(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, RiscVReg vmask);
	void VMSBC_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1);
	void VMSBC_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1);

	void VAND_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VAND_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VAND_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VOR_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VOR_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VOR_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VXOR_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VXOR_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VXOR_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VNOT_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE) {
		VXOR_VI(vd, vs2, -1, vm);
	}

	void VSLL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VSLL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VSLL_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm = VUseMask::NONE);
	void VSRL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VSRL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VSRL_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm = VUseMask::NONE);
	void VSRA_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VSRA_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VSRA_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm = VUseMask::NONE);
	void VNSRL_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VNSRL_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VNSRL_WI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm = VUseMask::NONE);
	void VNSRA_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VNSRA_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VNSRA_WI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm = VUseMask::NONE);

	// Using a mask creates an AND condition, assuming vtype has MU not MA.
	// Note: VV and VI don't have all comparison ops, VX does (there's no GE/GEU at all, though.)
	void VMSEQ_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMSNE_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMSLTU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMSLT_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMSLEU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMSLE_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMSEQ_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMSNE_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMSLTU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMSLT_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMSLEU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMSLE_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMSGTU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMSGT_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMSEQ_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VMSNE_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VMSLEU_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VMSLE_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VMSGTU_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VMSGT_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);

	void VMINU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMINU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMIN_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMIN_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMAXU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMAXU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMAX_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMAX_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	void VMUL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMUL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMULH_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMULH_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMULHU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMULHU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	// Takes vs2 as signed, vs1 as unsigned.
	void VMULHSU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMULHSU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	void VDIVU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VDIVU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VDIV_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VDIV_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VREMU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VREMU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VREM_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VREM_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	void VWMUL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWMUL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VWMULU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWMULU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VWMULSU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWMULSU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	// Multiply and add - vd += vs1 * vs2.
	void VMACC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VMACC_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Multiply and sub - vd -= vs1 * vs2.
	void VNMSAC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VNMSAC_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Multiply and add - vd = vd * vs1 + vs2.
	void VMADD_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VMADD_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Multiply and sub - vd = -(vd * vs1) + vs2.
	void VNMSUB_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VNMSUB_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Widening multiply and add - vd(wide) += vs1 * vs2.
	void VWMACCU_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VWMACCU_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Widening multiply and add - vd(wide) += vs1 * vs2.
	void VWMACC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VWMACC_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Widening multiply and add - vd(wide) += S(vs1) * U(vs2).
	void VWMACCSU_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VWMACCSU_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Widening multiply and add - vd(wide) += U(rs1) * S(vs2).
	void VWMACCUS_VX(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);

	// Masked bits (1) take vs1/rs1/simm5, vmask must be V0.
	void VMERGE_VVM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, RiscVReg vmask);
	void VMERGE_VXM(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, RiscVReg vmask);
	void VMERGE_VIM(RiscVReg vd, RiscVReg vs2, s8 simm5, RiscVReg vmask);

	// Simple register copy, can be used as a hint to internally prepare size if vd == vs1.
	void VMV_VV(RiscVReg vd, RiscVReg vs1);
	// These broadcast a value to all lanes of vd.
	void VMV_VX(RiscVReg vd, RiscVReg rs1);
	void VMV_VI(RiscVReg vd, s8 simm5);

	void VSADDU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VSADDU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VSADDU_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VSADD_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VSADD_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VSADD_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VSSUBU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VSSUBU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VSSUB_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VSSUB_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	void VAADDU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VAADDU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VAADD_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VAADD_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VASUBU_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VASUBU_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VASUB_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VASUB_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	// Fixed-point multiply, sra's product by SEW-1 before writing result.
	void VSMUL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VSMUL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	void VSSRL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VSSRL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VSSRL_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VSSRA_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VSSRA_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VSSRA_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);

	void VNCLIPU_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VNCLIPU_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VNCLIPU_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);
	void VNCLIP_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VNCLIP_WX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VNCLIP_VI(RiscVReg vd, RiscVReg vs2, s8 simm5, VUseMask vm = VUseMask::NONE);

	void VFADD_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFADD_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VFSUB_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFSUB_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VFRSUB_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	void VFWADD_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFWADD_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VFWSUB_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFWSUB_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VFWADD_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFWADD_WF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VFWSUB_WV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFWSUB_WF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	void VFMUL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFMUL_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VFDIV_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFDIV_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VFRDIV_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	void VFWMUL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFWMUL_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	// Fused multiply and accumulate: vd = +vd + vs1 * vs2.
	void VFMACC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFMACC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Fused multiply and accumulate, negated: vd = -vd - vs1 * vs2.
	void VFNMACC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFNMACC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Fused multiply and subtract accumuluator: vd = -vd + vs1 * vs2.
	void VFMSAC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFMSAC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Fused multiply and subtract accumuluator, negated: vd = +vd - vs1 * vs2.
	void VFNMSAC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFNMSAC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Fused multiply and add: vd = +(vs1 * vd) + vs2.
	void VFMADD_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFMADD_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Fused multiply and add, negated: vd = -(vs1 * vd) - vs2.
	void VFNMADD_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFNMADD_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Fused multiply and subtract: vd = +(vs1 * vd) - vs2.
	void VFMSUB_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFMSUB_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Fused multiply and subtract, negated: vd = -(vs1 * vd) + vs2.
	void VFNMSUB_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFNMSUB_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);

	// Fused widening multiply and accumulate: vd(wide) = +vd + vs1 * vs2.
	void VFWMACC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFWMACC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Fused widening multiply and accumulate, negated: vd(wide) = -vd - vs1 * vs2.
	void VFWNMACC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFWNMACC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Fused widening multiply and subtract accumulator: vd(wide) = -vd + vs1 * vs2.
	void VFWMSAC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFWMSAC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	// Fused widening multiply and subtract accumulator, negated: vd(wide) = +vd - vs1 * vs2.
	void VFWNMSAC_VV(RiscVReg vd, RiscVReg vs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFWNMSAC_VF(RiscVReg vd, RiscVReg rs1, RiscVReg vs2, VUseMask vm = VUseMask::NONE);

	void VFSQRT_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFRSQRT7_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFREC7_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);

	void VFMIN_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFMIN_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VFMAX_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFMAX_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	void VFSGNJ_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFSGNJ_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VFSGNJN_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFSGNJN_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VFSGNJX_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFSGNJX_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	void VMFEQ_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMFEQ_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMFNE_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMFNE_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMFLT_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMFLT_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMFLE_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VMFLE_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMFGT_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VMFGE_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	void VFCLASS_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);

	// vmask must be V0, takes rs1 where mask bits are set (1).
	void VFMERGE_VFM(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, RiscVReg vmask);
	// Broadcast/splat.
	void VFMV_VF(RiscVReg vd, RiscVReg rs1);

	void VFCVT_XU_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFCVT_X_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFCVT_RTZ_XU_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFCVT_RTZ_X_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFCVT_F_XU_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFCVT_F_X_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);

	void VFWCVT_XU_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFWCVT_X_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFWCVT_RTZ_XU_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFWCVT_RTZ_X_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFWCVT_F_XU_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFWCVT_F_X_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFWCVT_F_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);

	void VFNCVT_XU_F_W(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFNCVT_X_F_W(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFNCVT_RTZ_XU_F_W(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFNCVT_RTZ_X_F_W(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFNCVT_F_XU_W(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFNCVT_F_X_W(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFNCVT_F_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFNCVT_ROD_F_F_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);

	void VREDSUM_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VREDMAXU_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VREDMAX_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VREDMINU_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VREDMIN_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VREDAND_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VREDOR_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VREDXOR_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWREDSUMU_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWREDSUM_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);

	void VFREDOSUM_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFREDUSUM_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFREDMAX_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFREDMIN_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFWREDOSUM_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VFWREDUSUM_VS(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);

	void VMAND_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1);
	void VMNAND_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1);
	void VMANDN_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1);
	void VMXOR_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1);
	void VMOR_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1);
	void VMNOR_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1);
	void VMORN_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1);
	void VMXNOR_MM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1);
	void VMMV_M(RiscVReg vd, RiscVReg vs1) {
		VMAND_MM(vd, vs1, vs1);
	}
	void VMCLR_M(RiscVReg vd, RiscVReg vs1) {
		VMXOR_MM(vd, vs1, vs1);
	}
	void VMSET_M(RiscVReg vd, RiscVReg vs1) {
		VMXNOR_MM(vd, vs1, vs1);
	}
	void VMNOT_M(RiscVReg vd, RiscVReg vs1) {
		VMNAND_MM(vd, vs1, vs1);
	}

	void VCPOP_M(RiscVReg rd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VFIRST_M(RiscVReg rd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VMSBF_M(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VMSIF_M(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VMSOF_M(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VIOTA_M(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VID_M(RiscVReg vd, VUseMask vm = VUseMask::NONE);

	void VMV_X_S(RiscVReg rd, RiscVReg vs2);
	void VMV_S_X(RiscVReg vd, RiscVReg rs1);
	void VFMV_F_S(RiscVReg rd, RiscVReg vs2);
	void VFMV_S_F(RiscVReg vd, RiscVReg rs1);

	void VSLIDEUP_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VSLIDEUP_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm = VUseMask::NONE);
	void VSLIDEDOWN_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VSLIDEDOWN_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm = VUseMask::NONE);
	void VSLIDE1UP_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VFSLIDE1UP_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VSLIDE1DOWN_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VFSLIDE1DOWN_VF(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);

	void VRGATHER_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VRGATHEREI16_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VRGATHER_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VRGATHER_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm = VUseMask::NONE);

	void VCOMPRESS_VM(RiscVReg vd, RiscVReg vs2, RiscVReg vs1);
	void VMVR_V(int regs, RiscVReg vd, RiscVReg vs2);

	void VANDN_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VANDN_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VBREV_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VBREV8_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VREV8_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VCLZ_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VCTZ_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VCPOP_V(RiscVReg vd, RiscVReg vs2, VUseMask vm = VUseMask::NONE);
	void VROL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VROL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VROR_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VROR_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VROR_VI(RiscVReg vd, RiscVReg vs2, u8 uimm6, VUseMask vm = VUseMask::NONE);
	void VWSLL_VV(RiscVReg vd, RiscVReg vs2, RiscVReg vs1, VUseMask vm = VUseMask::NONE);
	void VWSLL_VX(RiscVReg vd, RiscVReg vs2, RiscVReg rs1, VUseMask vm = VUseMask::NONE);
	void VWSLL_VI(RiscVReg vd, RiscVReg vs2, u8 uimm5, VUseMask vm = VUseMask::NONE);

	// Bitmanip instructions.
	void ADD_UW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SH_ADD(int shift, RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SH_ADD_UW(int shift, RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SLLI_UW(RiscVReg rd, RiscVReg rs1, u32 shamt);
	void ANDN(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void ORN(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void XNOR(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void CLZ(RiscVReg rd, RiscVReg rs);
	void CLZW(RiscVReg rd, RiscVReg rs);
	void CTZ(RiscVReg rd, RiscVReg rs);
	void CTZW(RiscVReg rd, RiscVReg rs);
	void CPOP(RiscVReg rd, RiscVReg rs);
	void CPOPW(RiscVReg rd, RiscVReg rs);
	void MAX(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void MAXU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void MIN(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void MINU(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void SEXT_B(RiscVReg rd, RiscVReg rs);
	void SEXT_H(RiscVReg rd, RiscVReg rs);
	void SEXT_W(RiscVReg rd, RiscVReg rs) {
		ADDIW(rd, rs, 0);
	}
	void ZEXT_B(RiscVReg rd, RiscVReg rs) {
		ANDI(rd, rs, 0xFF);
	}
	void ZEXT_H(RiscVReg rd, RiscVReg rs);
	void ZEXT_W(RiscVReg rd, RiscVReg rs) {
		ADD_UW(rd, rs, R_ZERO);
	}
	void ROL(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void ROLW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void ROR(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void RORW(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void RORI(RiscVReg rd, RiscVReg rs1, u32 shamt);
	void RORIW(RiscVReg rd, RiscVReg rs1, u32 shamt);
	void ORC_B(RiscVReg rd, RiscVReg rs);
	void REV8(RiscVReg rd, RiscVReg rs);
	void CLMUL(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void CLMULH(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void CLMULR(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void BCLR(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void BCLRI(RiscVReg rd, RiscVReg rs1, u32 shamt);
	void BEXT(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void BEXTI(RiscVReg rd, RiscVReg rs1, u32 shamt);
	void BINV(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void BINVI(RiscVReg rd, RiscVReg rs1, u32 shamt);
	void BSET(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void BSETI(RiscVReg rd, RiscVReg rs1, u32 shamt);

	void CZERO_EQZ(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);
	void CZERO_NEZ(RiscVReg rd, RiscVReg rs1, RiscVReg rs2);

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

	void C_LBU(RiscVReg rd, RiscVReg rs1, u8 uimm2);
	void C_LHU(RiscVReg rd, RiscVReg rs1, u8 uimm2);
	void C_LH(RiscVReg rd, RiscVReg rs1, u8 uimm2);
	void C_SB(RiscVReg rs2, RiscVReg rs1, u8 uimm2);
	void C_SH(RiscVReg rs2, RiscVReg rs1, u8 uimm2);
	void C_ZEXT_B(RiscVReg rd);
	void C_SEXT_B(RiscVReg rd);
	void C_ZEXT_H(RiscVReg rd);
	void C_SEXT_H(RiscVReg rd);
	void C_ZEXT_W(RiscVReg rd);
	void C_SEXT_W(RiscVReg rd) {
		C_ADDIW(rd, 0);
	}
	void C_NOT(RiscVReg rd);
	void C_MUL(RiscVReg rd, RiscVReg rs2);

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

protected:
	const u8 *code_ = nullptr;
	u8 *writable_ = nullptr;
	const u8 *lastCacheFlushEnd_ = nullptr;
	bool autoCompress_ = false;
};

class RiscVCodeBlock : public CodeBlock<RiscVEmitter> {
private:
	void PoisonMemory(int offset) override;
};

};
