// Copyright (c) 2025- PPSSPP Project.

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

namespace LoongArch64Gen {

enum LoongArch64Reg {
    // General-purpose Registers (64-bit)
    // https://loongson.github.io/LoongArch-Documentation/LoongArch-Vol1-EN.html#_general_purpose_registers
	R0 = 0, R1, R2, R3, R4, R5, R6, R7,
	R8, R9, R10, R11, R12, R13, R14, R15,
	R16, R17, R18, R19, R20, R21, R22, R23,
	R24, R25, R26, R27, R28, R29, R30, R31,

    R_ZERO = 0,
	R_RA = 1,
    R_SP = 3,

    // Floating-point Registers (64-bit)
    // https://loongson.github.io/LoongArch-Documentation/LoongArch-Vol1-EN.html#floating-point-registers
	F0 = 0x20, F1, F2, F3, F4, F5, F6, F7,
	F8, F9, F10, F11, F12, F13, F14, F15,
	F16, F17, F18, F19, F20, F21, F22, F23,
	F24, F25, F26, F27, F28, F29, F30, F31,

    // FP register f0 and LSX register v0 and LASX register x0 share the lowest 64 bits
    // LSX register v0 and LASX register x0 share the lowest 128 bits
    // https://jia.je/unofficial-loongarch-intrinsics-guide/

    // LSX Registers (128-bit)
    V0 = 0x40, V1, V2, V3, V4, V5, V6, V7,
	V8, V9, V10, V11, V12, V13, V14, V15,
	V16, V17, V18, V19, V20, V21, V22, V23,
	V24, V25, V26, V27, V28, V29, V30, V31,

    // LASX Registers
    X0 = 0x60, X1, X2, X3, X4, X5, X6, X7,
	X8, X9, X10, X11, X12, X13, X14, X15,
	X16, X17, X18, X19, X20, X21, X22, X23,
	X24, X25, X26, X27, X28, X29, X30, X31,

    INVALID_REG = 0xFFFFFFFF,
};

enum LoongArch64CFR {
    // Condition Flag Register
    // The length of CFR is 1 bit.
    FCC0 = 0, FCC1, FCC2, FCC3, FCC4, FCC5, FCC6, FCC7,
};

enum LoongArch64FCSR {
    // Floating-point Control and Status Register
    // The length of FCSR0 is 29 bits.
    // FCSR1-FCSR3 are aliases of some fields in fcsr0.
    FCSR0 = 0, FCSR1, FCSR2, FCSR3,
};

enum class FixupBranchType {
	B,
	J,
    BZ,
};

enum class LoongArch64Fcond {
    // Conditions used in FCMP instruction
    CAF = 0x0,
    CUN = 0x8,
    CEQ = 0x4,
    CUEQ = 0xC,
    CLT = 0x2,
    CULT = 0xA,
    CLE = 0x6,
    CULE = 0xE,
    CNE = 0x10,
    COR = 0x14,
    CUNE = 0x18,
    SAF = 0x1,
    SUN = 0x9,
    SEQ = 0x5,
    SUEQ = 0xD,
    SLT = 0x3,
    SULT = 0xB,
    SLE = 0x7,
    SULE = 0xF,
    SNE = 0x11,
    SOR = 0x15,
    SUNE = 0x19,
};

static inline LoongArch64Reg DecodeReg(LoongArch64Reg reg) { return (LoongArch64Reg)(reg & 0x1F); }
static inline bool IsGPR(LoongArch64Reg reg) { return (reg & ~0x1F) == 0; }
static inline bool IsFPR(LoongArch64Reg reg) { return (reg & ~0x1F) == 0x20; }
static inline bool IsVPR(LoongArch64Reg reg) { return (reg & ~0x1F) == 0x40; }
static inline bool IsXPR(LoongArch64Reg reg) { return (reg & ~0x1F) == 0x60; }
static inline bool IsCFR(LoongArch64CFR cfr) { return ((int)cfr < 8); }
static inline bool IsFCSR(LoongArch64FCSR fcsr) { return ((int)fcsr < 4); }
inline LoongArch64Reg EncodeRegToV(LoongArch64Reg reg) { return (LoongArch64Reg)(DecodeReg(reg) + V0); }

struct FixupBranch {
	FixupBranch() {}
	FixupBranch(const u8 *p, FixupBranchType t) : ptr(p), type(t) {}
	FixupBranch(FixupBranch &&other);
	FixupBranch(const FixupBranch &) = delete;
	~FixupBranch();

	FixupBranch &operator =(FixupBranch &&other);
	FixupBranch &operator =(const FixupBranch &other) = delete;

    // Pointer to executable code address.
	const u8 *ptr = nullptr;
	FixupBranchType type = FixupBranchType::B;
};

class LoongArch64Emitter {
public:
    LoongArch64Emitter() {}
    LoongArch64Emitter(const u8 *codePtr, u8 *writablePtr);
    virtual ~LoongArch64Emitter() {}

	void SetCodePointer(const u8 *ptr, u8 *writePtr);
	const u8 *GetCodePointer() const;
    u8 *GetWritableCodePtr();

    void ReserveCodeSpace(u32 bytes);
	const u8 *AlignCode16();
	const u8 *AlignCodePage();
	void FlushIcache();
	void FlushIcacheSection(const u8 *start, const u8 *end);

    void SetJumpTarget(FixupBranch &branch);
    bool BranchInRange(const void *func) const;
	bool JumpInRange(const void *func) const;
    bool BranchZeroInRange(const void *func) const;

    void QuickJump(LoongArch64Reg scratchreg, LoongArch64Reg rd, const u8 *dst);
	void QuickJ(LoongArch64Reg scratchreg, const u8 *dst) {
		QuickJump(scratchreg, R_ZERO, dst);
	}
	void QuickCallFunction(const u8 *func, LoongArch64Reg scratchreg = R_RA) {
		QuickJump(scratchreg, R_RA, func);
	}
	template <typename T>
	void QuickCallFunction(T *func, LoongArch64Reg scratchreg = R_RA) {
		static_assert(std::is_function<T>::value, "QuickCallFunction without function");
		QuickCallFunction((const u8 *)func, scratchreg);
	}

    // https://loongson.github.io/LoongArch-Documentation/LoongArch-Vol1-EN.html
    // https://github.com/loongson-community/loongarch-opcodes/

    // Basic Integer Instructions

    // Arithmetic Operation Instructions
    void ADD_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void ADD_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void SUB_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void SUB_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK

    void ADDI_W(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12
    void ADDI_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12
    void ADDU16I_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si16); // DJSk16

    void ALSL_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa2); // DJKUa2pp1
    void ALSL_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa2); // DJKUa2pp1
    void ALSL_WU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa2); // DJKUa2pp1

    void LU12I_W(LoongArch64Reg rd, s32 si20); // DSj20
    void LU32I_D(LoongArch64Reg rd, s32 si20); // DSj20
    void LU52I_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12

    void SLT(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void SLTU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK

    void SLTI(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12
    void SLTUI(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12

    void PCADDI(LoongArch64Reg rd, s32 si20); // DSj20
    void PCADDU12I(LoongArch64Reg rd, s32 si20); // DSj20
    void PCADDU18I(LoongArch64Reg rd, s32 si20); // DSj20
    void PCALAU12I(LoongArch64Reg rd, s32 si20); // DSj20

    void AND(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void OR(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void NOR(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void XOR(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void ANDN(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void ORN(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK

    void ANDI(LoongArch64Reg rd, LoongArch64Reg rj, u16 ui12); // DJUk12
    void ORI(LoongArch64Reg rd, LoongArch64Reg rj, u16 ui12); // DJUk12
    void XORI(LoongArch64Reg rd, LoongArch64Reg rj, u16 ui12); // DJUk12

    void NOP() {
        ANDI(R_ZERO, R_ZERO, 0);
    }
    void MOVE(LoongArch64Reg rd, LoongArch64Reg rj) {
        OR(rd, rj, R_ZERO);
    }

    template <typename T>
	void LI(LoongArch64Reg rd, const T &v) {
		_assert_msg_(rd != R_ZERO, "LI to X0");
		_assert_msg_(rd < F0, "LI to non-GPR");

		uint64_t value = AsImmediate<T, std::is_signed<T>::value>(v);
		SetRegToImmediate(rd, value);
	}

    void MUL_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void MULH_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void MULH_WU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void MUL_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void MULH_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void MULH_DU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK

    void MULW_D_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void MULW_D_WU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK

    void DIV_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void MOD_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void DIV_WU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void MOD_WU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void DIV_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void MOD_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void DIV_DU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void MOD_DU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK

    // Bit-shift Instructions
    void SLL_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void SRL_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void SRA_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void ROTR_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK

    void SLLI_W(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui5); // DJUk5
    void SRLI_W(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui5); // DJUk5
    void SRAI_W(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui5); // DJUk5
    void ROTRI_W(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui5); // DJUk5

    void SLL_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void SRL_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void SRA_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void ROTR_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK

    void SLLI_D(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui6); // DJUk6
    void SRLI_D(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui6); // DJUk6
    void SRAI_D(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui6); // DJUk6
    void ROTRI_D(LoongArch64Reg rd, LoongArch64Reg rj, u8 ui6); // DJUk6

    // Bit-manipulation Instructions
    void EXT_W_B(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void EXT_W_H(LoongArch64Reg rd, LoongArch64Reg rj); // DJ

    void CLO_W(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void CLO_D(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void CLZ_W(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void CLZ_D(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void CTO_W(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void CTO_D(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void CTZ_W(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void CTZ_D(LoongArch64Reg rd, LoongArch64Reg rj); // DJ

    void BYTEPICK_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa2); // DJKUa2
    void BYTEPICK_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk, u8 sa3); // DJKUa3

    void REVB_2H(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void REVB_4H(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void REVB_2W(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void REVB_D(LoongArch64Reg rd, LoongArch64Reg rj); // DJ

    void BITREV_4B(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void BITREV_8B(LoongArch64Reg rd, LoongArch64Reg rj); // DJ

    void BITREV_W(LoongArch64Reg rd, LoongArch64Reg rj); //DJ
    void BITREV_D(LoongArch64Reg rd, LoongArch64Reg rj); // DJ

    void BSTRINS_W(LoongArch64Reg rd, LoongArch64Reg rj, u8 msbw, u8 lsbw); // DJUk5Um5
    void BSTRINS_D(LoongArch64Reg RD, LoongArch64Reg RJ, u8 msbd, u8 lsbd); // DJUk6Um6

    void BSTRPICK_W(LoongArch64Reg RD, LoongArch64Reg RJ, u8 msbd, u8 lsbd); // DJUk5Um5
    void BSTRPICK_D(LoongArch64Reg RD, LoongArch64Reg RJ, u8 msbd, u8 lsbd); // DJUk6Um6

    void MASKEQZ(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void MASKNEZ(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK

    // Branch Instructions
    void BEQ(LoongArch64Reg rj, LoongArch64Reg rd, const void *dst); // JDSk16ps2
    void BNE(LoongArch64Reg rj, LoongArch64Reg rd, const void *dst); // JDSk16ps2
    void BLT(LoongArch64Reg rj, LoongArch64Reg rd, const void *dst); // JDSk16ps2
    void BGE(LoongArch64Reg rj, LoongArch64Reg rd, const void *dst); // JDSk16ps2
    void BLTU(LoongArch64Reg rj, LoongArch64Reg rd, const void *dst); // JDSk16ps2
    void BGEU(LoongArch64Reg rj, LoongArch64Reg rd, const void *dst); // JDSk16ps2
    FixupBranch BEQ(LoongArch64Reg rj, LoongArch64Reg rd);
    FixupBranch BNE(LoongArch64Reg rj, LoongArch64Reg rd);
    FixupBranch BLT(LoongArch64Reg rj, LoongArch64Reg rd);
    FixupBranch BGE(LoongArch64Reg rj, LoongArch64Reg rd);
    FixupBranch BLTU(LoongArch64Reg rj, LoongArch64Reg rd);
    FixupBranch BGEU(LoongArch64Reg rj, LoongArch64Reg rd);

    void BEQZ(LoongArch64Reg rj, const void *dst); // JSd5k16ps2
    void BNEZ(LoongArch64Reg rj, const void *dst); // JSd5k16ps2
    FixupBranch BEQZ(LoongArch64Reg rj);
    FixupBranch BNEZ(LoongArch64Reg rj);

    void B(const void *dst); // Sd10k16ps2
    void BL(const void *dst); // Sd10k16ps2
    FixupBranch B();
    FixupBranch BL();

    void JIRL(LoongArch64Reg rd, LoongArch64Reg rj, s32 offs16); // DJSk16ps2

    void JR(LoongArch64Reg rj) {
        JIRL(R_ZERO, rj, 0);
    }
    void RET() {
        JIRL(R_ZERO, R_RA, 0);
    }

    // Common Memory Access Instructions
    void LD_B(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12
    void LD_H(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12
    void LD_W(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12
    void LD_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12
    void LD_BU(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12
    void LD_HU(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12
    void LD_WU(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12
    void ST_B(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12
    void ST_H(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12
    void ST_W(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12
    void ST_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si12); // DJSk12

    void LDX_B(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void LDX_H(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void LDX_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void LDX_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void LDX_BU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void LDX_HU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void LDX_WU(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void STX_B(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void STX_H(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void STX_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void STX_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK

    void LDPTR_W(LoongArch64Reg rd, LoongArch64Reg rj, s16 si14); // DJSk14ps2
    void LDPTR_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si14); // DJSk14ps2
    void STPTR_W(LoongArch64Reg rd, LoongArch64Reg rj, s16 si14); // DJSk14ps2
    void STPTR_D(LoongArch64Reg rd, LoongArch64Reg rj, s16 si14); // DJSk14ps2

    void PRELD(u32 hint, LoongArch64Reg rj, s16 si12); // Ud5JSk12
    void PRELDX(u32 hint, LoongArch64Reg rj, LoongArch64Reg rk); // Ud5JK

    // Bound Check Memory Access Instructions
    void LDGT_B(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void LDGT_H(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void LDGT_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void LDGT_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void LDLE_B(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void LDLE_H(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void LDLE_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void LDLE_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void STGT_B(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void STGT_H(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void STGT_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void STGT_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void STLE_B(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void STLE_H(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void STLE_W(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK
    void STLE_D(LoongArch64Reg rd, LoongArch64Reg rj, LoongArch64Reg rk); // DJK

    // Atomic Memory Access Instructions
    void AMSWAP_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMSWAP_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMSWAP_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMSWAP_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ

    void AMADD_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMADD_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMADD_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMADD_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ

    void AMAND_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMAND_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMAND_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMAND_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ

    void AMOR_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMOR_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMOR_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMOR_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ

    void AMXOR_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMXOR_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMXOR_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMXOR_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ

    void AMMAX_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMMAX_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMMAX_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMMAX_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ

    void AMMIN_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMMIN_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMMIN_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMMIN_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ

    void AMMAX_WU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMMAX_DB_WU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMMAX_DU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMMAX_DB_DU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ

    void AMMIN_WU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMMIN_DB_WU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMMIN_DU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMMIN_DB_DU(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ

    void AMSWAP_B(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMSWAP_DB_B(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMSWAP_H(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMSWAP_DB_H(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ

    void AMADD_B(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMADD_DB_B(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMADD_H(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMADD_DB_H(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ

    void AMCAS_B(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMCAS_DB_B(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMCAS_H(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMCAS_DB_H(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMCAS_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMCAS_DB_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMCAS_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void AMCAS_DB_D(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ

    // CRC Check Instructions
    void CRC_W_B_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void CRC_W_H_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void CRC_W_W_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void CRC_W_D_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void CRCC_W_B_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void CRCC_W_H_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void CRCC_W_W_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ
    void CRCC_W_D_W(LoongArch64Reg rd, LoongArch64Reg rk, LoongArch64Reg rj); // DKJ

    // Other Miscellaneous Instructions
    void SYSCALL(u16 code); // Ud15
    void BREAK(u16 code); // Ud15

    void ASRTLE_D(LoongArch64Reg rj, LoongArch64Reg rk); // JK
    void ASRTGT_D(LoongArch64Reg rj, LoongArch64Reg rk); // JK

    void RDTIMEL_W(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void RDTIMEH_W(LoongArch64Reg rd, LoongArch64Reg rj); // DJ
    void RDTIME_D(LoongArch64Reg rd, LoongArch64Reg rj); // DJ

    void CPUCFG(LoongArch64Reg rd, LoongArch64Reg rj); // DJ

    // Basic Floating-Point Instructions
    // Floating-Point Arithmetic Operation Instructions
    void FADD_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FADD_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FSUB_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FSUB_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FMUL_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FMUL_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FDIV_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FDIV_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk

    void FMADD_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa); // FdFjFkFa
    void FMADD_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa); // FdFjFkFa
    void FMSUB_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa); // FdFjFkFa
    void FMSUB_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa); // FdFjFkFa
    void FNMADD_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa); // FdFjFkFa
    void FNMADD_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa); // FdFjFkFa
    void FNMSUB_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa); // FdFjFkFa
    void FNMSUB_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Reg fa); // FdFjFkFa

    void FMAX_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FMAX_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FMIN_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FMIN_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk

    void FMAXA_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FMAXA_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FMINA_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FMINA_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk

    void FABS_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FABS_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FNEG_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FNEG_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj

    void FSQRT_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FSQRT_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FRECIP_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FRECIP_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FRSQRT_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FRSQRT_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj

    void FSCALEB_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FSCALEB_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FLOGB_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FLOGB_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FCOPYSIGN_S(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk
    void FCOPYSIGN_D(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk); // FdFjFk

    void FCLASS_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FCLASS_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj

    void FRECIPE_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FRECIPE_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FRSQRTE_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FRSQRTE_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj

    void FCMP_COND_S(LoongArch64CFR cd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Fcond cond); // CdFjFkFcond
    void FCMP_COND_D(LoongArch64CFR cd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64Fcond cond); // CdFjFkFcond

    // Floating-Point Conversion Instructions
    void FCVT_S_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FCVT_D_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj

    void FFINT_S_W(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FFINT_S_L(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FFINT_D_W(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FFINT_D_L(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINT_W_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINT_W_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINT_L_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINT_L_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj

    void FTINTRM_W_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRM_W_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRM_L_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRM_L_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRP_W_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRP_W_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRP_L_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRP_L_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRZ_W_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRZ_W_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRZ_L_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRZ_L_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRNE_W_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRNE_W_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRNE_L_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FTINTRNE_L_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj

    void FRINT_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FRINT_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj

    // Floating-Point Move Instructions
    void FMOV_S(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj
    void FMOV_D(LoongArch64Reg fd, LoongArch64Reg fj); // FdFj

    void FSEL(LoongArch64Reg fd, LoongArch64Reg fj, LoongArch64Reg fk, LoongArch64CFR ca); // FdFjFkCa

    void MOVGR2FR_W(LoongArch64Reg fd, LoongArch64Reg rj); // FdJ
    void MOVGR2FR_D(LoongArch64Reg fd, LoongArch64Reg rj); // FdJ
    void MOVGR2FRH_W(LoongArch64Reg fd, LoongArch64Reg rj); // FdJ

    void MOVFR2GR_S(LoongArch64Reg fd, LoongArch64Reg rj); // DFj
    void MOVFR2GR_D(LoongArch64Reg fd, LoongArch64Reg rj); // DFj
    void MOVFRH2GR_S(LoongArch64Reg fd, LoongArch64Reg rj); // DFj

    void MOVGR2FCSR(LoongArch64FCSR fcsr, LoongArch64Reg rj); // JUd5
    void MOVFCSR2GR(LoongArch64Reg rd, LoongArch64FCSR fcsr); // DUj5

    void MOVFR2CF(LoongArch64CFR cd, LoongArch64Reg fj); // CdFj
    void MOVCF2FR(LoongArch64Reg fd, LoongArch64CFR cj); // FdCj

    void MOVGR2CF(LoongArch64CFR cd, LoongArch64Reg rj); // CdJ
    void MOVCF2GR(LoongArch64Reg rd, LoongArch64CFR cj); // DCj

    // Floating-Point Branch Instructions
    void BCEQZ(LoongArch64CFR cj, s32 offs21); // CjSd5k16ps2
    void BCNEZ(LoongArch64CFR cj, s32 offs21); // CjSd5k16ps2

    // Floating-Point Common Memory Access Instructions
    void FLD_S(LoongArch64Reg fd, LoongArch64Reg rj, s16 si12); // FdJSk12
    void FLD_D(LoongArch64Reg fd, LoongArch64Reg rj, s16 si12); // FdJSk12
    void FST_S(LoongArch64Reg fd, LoongArch64Reg rj, s16 si12); // FdJSk12
    void FST_D(LoongArch64Reg fd, LoongArch64Reg rj, s16 si12); // FdJSk12

    void FLDX_S(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk); // FdJK
    void FLDX_D(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk); // FdJK
    void FSTX_S(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk); // FdJK
    void FSTX_D(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk); // FdJK

    // Floating-Point Bound Check Memory Access Instructions
    void FLDGT_S(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk); // FdJK
    void FLDGT_D(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk); // FdJK
    void FLDLE_S(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk); // FdJK
    void FLDLE_D(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk); // FdJK
    void FSTGT_S(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk); // FdJK
    void FSTGT_D(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk); // FdJK
    void FSTLE_S(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk); // FdJK
    void FSTLE_D(LoongArch64Reg fd, LoongArch64Reg rj, LoongArch64Reg rk); // FdJK

    void QuickFLI(int bits, LoongArch64Reg fd, double v, LoongArch64Reg scratchReg);
	void QuickFLI(int bits, LoongArch64Reg fd, uint32_t pattern, LoongArch64Reg scratchReg);
	void QuickFLI(int bits, LoongArch64Reg fd, float v, LoongArch64Reg scratchReg);

    // LoongArch SX 128-bit SIMD instructions
    void VFMADD_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va); // VdVjVkVa
    void VFMADD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va); // VdVjVkVa
    void VFMSUB_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va); // VdVjVkVa
    void VFMSUB_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va); // VdVjVkVa
    void VFNMADD_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va); // VdVjVkVa
    void VFNMADD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va); // VdVjVkVa
    void VFNMSUB_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va); // VdVjVkVa
    void VFNMSUB_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va); // VdVjVkVa
    void VFCMP_CAF_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SAF_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CLT_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SLT_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CEQ_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SEQ_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CLE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SLE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CUN_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SUN_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CULT_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SULT_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CUEQ_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SUEQ_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CULE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SULE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CNE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SNE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_COR_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SOR_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CUNE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SUNE_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CAF_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SAF_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CLT_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SLT_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CEQ_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SEQ_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CLE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SLE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CUN_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SUN_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CULT_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SULT_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CUEQ_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SUEQ_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CULE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SULE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CNE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SNE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_COR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SOR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_CUNE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCMP_SUNE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VBITSEL_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va); // VdVjVkVa
    void VSHUF_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk, LoongArch64Reg va); // VdVjVkVa
    void VLD(LoongArch64Reg vd, LoongArch64Reg rj, s16 si12); // VdJSk12
    void VST(LoongArch64Reg vd, LoongArch64Reg rj, s16 si12); // VdJSk12
    void VLDREPL_D(LoongArch64Reg vd, LoongArch64Reg rj, s16 si9); // VdJSk9
    void VLDREPL_W(LoongArch64Reg vd, LoongArch64Reg rj, s16 si10); // VdJSk10
    void VLDREPL_H(LoongArch64Reg vd, LoongArch64Reg rj, s16 si11); // VdJSk11
    void VLDREPL_B(LoongArch64Reg vd, LoongArch64Reg rj, s16 si12); // VdJSk12
    void VSTELM_D(LoongArch64Reg vd, LoongArch64Reg rj, s16 si8, u8 idx1); // VdJSk8Un1
    void VSTELM_W(LoongArch64Reg vd, LoongArch64Reg rj, s16 si8, u8 idx2); // VdJSk8Un2
    void VSTELM_H(LoongArch64Reg vd, LoongArch64Reg rj, s16 si8, u8 idx3); // VdJSk8Un3
    void VSTELM_B(LoongArch64Reg vd, LoongArch64Reg rj, s16 si8, u8 idx4); // VdJSk8Un4
    void VLDX(LoongArch64Reg vd, LoongArch64Reg rj, LoongArch64Reg rk); // VdJK
    void VSTX(LoongArch64Reg vd, LoongArch64Reg rj, LoongArch64Reg rk); // VdJK
    void VSEQ_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSEQ_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSEQ_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSEQ_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLE_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLE_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLE_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLE_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLE_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLE_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLE_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLT_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLT_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLT_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLT_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLT_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLT_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLT_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLT_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUB_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUB_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUB_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUB_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWEV_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWEV_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWEV_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWEV_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWEV_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWEV_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWEV_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWEV_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWOD_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWOD_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWOD_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWOD_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWOD_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWOD_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWOD_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWOD_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWEV_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWEV_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWEV_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWEV_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWEV_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWEV_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWEV_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWEV_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWOD_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWOD_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWOD_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWOD_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWOD_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWOD_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWOD_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUBWOD_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWEV_H_BU_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWEV_W_HU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWEV_D_WU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWEV_Q_DU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWOD_H_BU_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWOD_W_HU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWOD_D_WU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDWOD_Q_DU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSADD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSADD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSADD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSADD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSUB_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSUB_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSUB_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSUB_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSADD_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSADD_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSADD_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSADD_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSUB_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSUB_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSUB_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSUB_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHADDW_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHADDW_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHADDW_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHADDW_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHSUBW_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHSUBW_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHSUBW_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHSUBW_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHADDW_HU_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHADDW_WU_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHADDW_DU_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHADDW_QU_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHSUBW_HU_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHSUBW_WU_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHSUBW_DU_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VHSUBW_QU_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDA_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDA_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDA_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADDA_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VABSD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VABSD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VABSD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VABSD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VABSD_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VABSD_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VABSD_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VABSD_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVG_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVG_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVG_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVG_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVG_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVG_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVG_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVG_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVGR_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVGR_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVGR_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVGR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVGR_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVGR_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVGR_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VAVGR_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMAX_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMAX_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMAX_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMAX_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMIN_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMIN_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMIN_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMIN_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMAX_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMAX_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMAX_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMAX_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMIN_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMIN_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMIN_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMIN_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMUL_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMUL_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMUL_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMUL_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMUH_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMUH_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMUH_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMUH_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMUH_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMUH_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMUH_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMUH_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWEV_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWEV_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWEV_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWEV_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWOD_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWOD_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWOD_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWOD_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWEV_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWEV_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWEV_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWEV_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWOD_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWOD_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWOD_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWOD_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWEV_H_BU_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWEV_W_HU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWEV_D_WU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWEV_Q_DU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWOD_H_BU_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWOD_W_HU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWOD_D_WU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMULWOD_Q_DU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMSUB_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMSUB_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMSUB_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMSUB_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWEV_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWEV_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWEV_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWEV_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWOD_H_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWOD_W_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWOD_D_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWOD_Q_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWEV_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWEV_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWEV_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWEV_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWOD_H_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWOD_W_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWOD_D_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWOD_Q_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWEV_H_BU_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWEV_W_HU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWEV_D_WU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWEV_Q_DU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWOD_H_BU_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWOD_W_HU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWOD_D_WU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMADDWOD_Q_DU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VDIV_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VDIV_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VDIV_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VDIV_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMOD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMOD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMOD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMOD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VDIV_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VDIV_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VDIV_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VDIV_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMOD_BU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMOD_HU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMOD_WU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VMOD_DU(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLL_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLL_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLL_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSLL_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRL_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRL_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRL_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRL_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRA_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRA_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRA_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRA_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VROTR_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VROTR_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VROTR_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VROTR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRLR_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRLR_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRLR_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRLR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRAR_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRAR_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRAR_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRAR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRLN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRLN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRLN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRAN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRAN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRAN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRLRN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRLRN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRLRN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRARN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRARN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSRARN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRLN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRLN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRLN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRAN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRAN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRAN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRLRN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRLRN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRLRN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRARN_B_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRARN_H_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRARN_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRLN_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRLN_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRLN_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRAN_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRAN_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRAN_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRLRN_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRLRN_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRLRN_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRARN_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRARN_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSSRARN_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VBITCLR_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VBITCLR_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VBITCLR_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VBITCLR_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VBITSET_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VBITSET_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VBITSET_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VBITSET_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VBITREV_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VBITREV_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VBITREV_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VBITREV_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPACKEV_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPACKEV_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPACKEV_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPACKEV_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPACKOD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPACKOD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPACKOD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPACKOD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VILVL_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VILVL_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VILVL_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VILVL_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VILVH_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VILVH_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VILVH_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VILVH_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPICKEV_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPICKEV_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPICKEV_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPICKEV_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPICKOD_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPICKOD_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPICKOD_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VPICKOD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VREPLVE_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg rk); // VdVjK
    void VREPLVE_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg rk); // VdVjK
    void VREPLVE_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg rk); // VdVjK
    void VREPLVE_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg rk); // VdVjK
    void VAND_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VOR_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VXOR_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VNOR_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VANDN_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VORN_V(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFRSTP_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFRSTP_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VADD_Q(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSUB_Q(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSIGNCOV_B(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSIGNCOV_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSIGNCOV_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSIGNCOV_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFADD_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFADD_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFSUB_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFSUB_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFMUL_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFMUL_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFDIV_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFDIV_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFMAX_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFMAX_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFMIN_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFMIN_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFMAXA_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFMAXA_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFMINA_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFMINA_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCVT_H_S(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFCVT_S_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFFINT_S_L(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFTINT_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFTINTRM_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFTINTRP_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFTINTRZ_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VFTINTRNE_W_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSHUF_H(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSHUF_W(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSHUF_D(LoongArch64Reg vd, LoongArch64Reg vj, LoongArch64Reg vk); // VdVjVk
    void VSEQI_B(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VSEQI_H(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VSEQI_W(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VSEQI_D(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VSLEI_B(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VSLEI_H(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VSLEI_W(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VSLEI_D(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VSLEI_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSLEI_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSLEI_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSLEI_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSLTI_B(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VSLTI_H(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VSLTI_W(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VSLTI_D(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VSLTI_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSLTI_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSLTI_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSLTI_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VADDI_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VADDI_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VADDI_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VADDI_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSUBI_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSUBI_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSUBI_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSUBI_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VBSLL_V(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VBSRL_V(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VMAXI_B(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VMAXI_H(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VMAXI_W(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VMAXI_D(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VMINI_B(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VMINI_H(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VMINI_W(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VMINI_D(LoongArch64Reg vd, LoongArch64Reg vj, s8 si5); // VdVjSk5
    void VMAXI_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VMAXI_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VMAXI_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VMAXI_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VMINI_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VMINI_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VMINI_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VMINI_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VFRSTPI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VFRSTPI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VCLO_B(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VCLO_H(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VCLO_W(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VCLO_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VCLZ_B(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VCLZ_H(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VCLZ_W(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VCLZ_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VPCNT_B(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VPCNT_H(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VPCNT_W(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VPCNT_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VNEG_B(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VNEG_H(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VNEG_W(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VNEG_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VMSKLTZ_B(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VMSKLTZ_H(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VMSKLTZ_W(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VMSKLTZ_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VMSKGEZ_B(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VMSKNZ_B(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VSETEQZ_V(LoongArch64CFR cd, LoongArch64Reg vj); // CdVj
    void VSETNEZ_V(LoongArch64CFR cd, LoongArch64Reg vj); // CdVj
    void VSETANYEQZ_B(LoongArch64CFR cd, LoongArch64Reg vj); // CdVj
    void VSETANYEQZ_H(LoongArch64CFR cd, LoongArch64Reg vj); // CdVj
    void VSETANYEQZ_W(LoongArch64CFR cd, LoongArch64Reg vj); // CdVj
    void VSETANYEQZ_D(LoongArch64CFR cd, LoongArch64Reg vj); // CdVj
    void VSETALLNEZ_B(LoongArch64CFR cd, LoongArch64Reg vj); // CdVj
    void VSETALLNEZ_H(LoongArch64CFR cd, LoongArch64Reg vj); // CdVj
    void VSETALLNEZ_W(LoongArch64CFR cd, LoongArch64Reg vj); // CdVj
    void VSETALLNEZ_D(LoongArch64CFR cd, LoongArch64Reg vj); // CdVj
    void VFLOGB_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFLOGB_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFCLASS_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFCLASS_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFSQRT_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFSQRT_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRECIP_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRECIP_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRSQRT_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRSQRT_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRECIPE_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRECIPE_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRSQRTE_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRSQRTE_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRINT_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRINT_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRINTRM_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRINTRM_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRINTRP_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRINTRP_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRINTRZ_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRINTRZ_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRINTRNE_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFRINTRNE_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFCVTL_S_H(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFCVTH_S_H(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFCVTL_D_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFCVTH_D_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFFINT_S_W(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFFINT_S_WU(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFFINT_D_L(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFFINT_D_LU(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFFINTL_D_W(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFFINTH_D_W(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINT_W_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINT_L_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRM_W_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRM_L_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRP_W_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRP_L_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRZ_W_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRZ_L_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRNE_W_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRNE_L_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINT_WU_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINT_LU_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRZ_WU_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRZ_LU_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTL_L_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTH_L_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRML_L_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRMH_L_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRPL_L_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRPH_L_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRZL_L_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRZH_L_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRNEL_L_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VFTINTRNEH_L_S(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VEXTH_H_B(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VEXTH_W_H(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VEXTH_D_W(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VEXTH_Q_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VEXTH_HU_BU(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VEXTH_WU_HU(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VEXTH_DU_WU(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VEXTH_QU_DU(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VREPLGR2VR_B(LoongArch64Reg vd, LoongArch64Reg rj); // VdJ
    void VREPLGR2VR_H(LoongArch64Reg vd, LoongArch64Reg rj); // VdJ
    void VREPLGR2VR_W(LoongArch64Reg vd, LoongArch64Reg rj); // VdJ
    void VREPLGR2VR_D(LoongArch64Reg vd, LoongArch64Reg rj); // VdJ
    void VROTRI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VROTRI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VROTRI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VROTRI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSRLRI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VSRLRI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSRLRI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSRLRI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSRARI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VSRARI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSRARI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSRARI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VINSGR2VR_B(LoongArch64Reg vd, LoongArch64Reg rj, u8 ui4); // VdJUk4
    void VINSGR2VR_H(LoongArch64Reg vd, LoongArch64Reg rj, u8 ui3); // VdJUk3
    void VINSGR2VR_W(LoongArch64Reg vd, LoongArch64Reg rj, u8 ui2); // VdJUk2
    void VINSGR2VR_D(LoongArch64Reg vd, LoongArch64Reg rj, u8 ui1); // VdJUk1
    void VPICKVE2GR_B(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui4); // DVjUk4
    void VPICKVE2GR_H(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui3); // DVjUk3
    void VPICKVE2GR_W(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui2); // DVjUk2
    void VPICKVE2GR_D(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui1); // DVjUk1
    void VPICKVE2GR_BU(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui4); // DVjUk4
    void VPICKVE2GR_HU(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui3); // DVjUk3
    void VPICKVE2GR_WU(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui2); // DVjUk2
    void VPICKVE2GR_DU(LoongArch64Reg rd, LoongArch64Reg vj, u8 ui1); // DVjUk1
    void VREPLVEI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VREPLVEI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VREPLVEI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui2); // VdVjUk2
    void VREPLVEI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui1); // VdVjUk1
    void VSLLWIL_H_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VSLLWIL_W_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSLLWIL_D_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VEXTL_Q_D(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VSLLWIL_HU_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VSLLWIL_WU_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSLLWIL_DU_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VEXTL_QU_DU(LoongArch64Reg vd, LoongArch64Reg vj); // VdVj
    void VBITCLRI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VBITCLRI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VBITCLRI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VBITCLRI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VBITSETI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VBITSETI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VBITSETI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VBITSETI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VBITREVI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VBITREVI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VBITREVI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VBITREVI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSAT_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VSAT_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSAT_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSAT_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSAT_BU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VSAT_HU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSAT_WU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSAT_DU(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSLLI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VSLLI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSLLI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSLLI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSRLI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VSRLI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSRLI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSRLI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSRAI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui3); // VdVjUk3
    void VSRAI_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSRAI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSRAI_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSRLNI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSRLNI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSRLNI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSRLNI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7); // VdVjUk7
    void VSRLRNI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSRLRNI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSRLRNI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSRLRNI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7); // VdVjUk7
    void VSSRLNI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSSRLNI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSSRLNI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSSRLNI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7); // VdVjUk7
    void VSSRLNI_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSSRLNI_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSSRLNI_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSSRLNI_DU_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7); // VdVjUk7
    void VSSRLRNI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSSRLRNI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSSRLRNI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSSRLRNI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7); // VdVjUk7
    void VSSRLRNI_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSSRLRNI_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSSRLRNI_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSSRLRNI_DU_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7); // VdVjUk7
    void VSRANI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSRANI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSRANI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSRANI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7); // VdVjUk7
    void VSRARNI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSRARNI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSRARNI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSRARNI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7); // VdVjUk7
    void VSSRANI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSSRANI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSSRANI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSSRANI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7); // VdVjUk7
    void VSSRANI_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSSRANI_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSSRANI_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSSRANI_DU_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7); // VdVjUk7
    void VSSRARNI_B_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSSRARNI_H_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSSRARNI_W_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSSRARNI_D_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7); // VdVjUk7
    void VSSRARNI_BU_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui4); // VdVjUk4
    void VSSRARNI_HU_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui5); // VdVjUk5
    void VSSRARNI_WU_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui6); // VdVjUk6
    void VSSRARNI_DU_Q(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui7); // VdVjUk7
    void VEXTRINS_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8
    void VEXTRINS_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8
    void VEXTRINS_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8
    void VEXTRINS_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8
    void VSHUF4I_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8
    void VSHUF4I_H(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8
    void VSHUF4I_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8
    void VSHUF4I_D(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8
    void VBITSELI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8
    void VANDI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8
    void VORI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8
    void VXORI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8
    void VNORI_B(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8
    void VLDI(LoongArch64Reg vd, s16 i13); // VdSj13
    void VPERMI_W(LoongArch64Reg vd, LoongArch64Reg vj, u8 ui8); // VdVjUk8

private:
    void SetJumpTarget(FixupBranch &branch, const void *dst);
	bool BranchInRange(const void *src, const void *dst) const;
	bool JumpInRange(const void *src, const void *dst) const;
    bool BranchZeroInRange(const void *src, const void *dst) const;

    void SetRegToImmediate(LoongArch64Reg rd, uint64_t value);

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
        *(u32 *)writable_ = value;
		code_ += 4;
		writable_ += 4;
	}

protected:
	const u8 *code_ = nullptr;
	u8 *writable_ = nullptr;
    const u8 *lastCacheFlushEnd_ = nullptr;
};

class LoongArch64CodeBlock : public CodeBlock<LoongArch64Emitter> {
private:
    void PoisonMemory(int offset) override;
};

};
