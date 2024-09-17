// Copyright (c) 2014- PPSSPP Project.

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

#include <functional>
#include <stdint.h>

#include "Common/CodeBlock.h"
#include "Common/CommonTypes.h"

namespace MIPSGen {

enum MIPSReg {
	R_ZERO = 0,
	R_AT,
	V0, V1,

	A0 = 4, A1 = 5, A2 = 6, A3 = 7, A4 = 8, A5 = 9, A6 = 10, A7 = 11,
	// Alternate names depending on ABI.
	T0 = 8, T1 = 9, T2 = 10, T3 = 11,

	T4, T5, T6, T7,
	S0, S1, S2, S3, S4, S5, S6, S7,
	T8, T9,
	K0, K1,
	R_GP, R_SP, R_FP,
	R_RA,

	F_BASE = 32,
	F0 = 32, F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,
	F16, F17, F18, F19, F20, F21, F22, F23, F24, F25, F26, F27, F28, F29, F30, F31,

	INVALID_REG = 0xFFFFFFFF
};

enum {
	// All 32 except: ZERO, K0/K1 (kernel), RA.  The rest are only convention.
	NUMGPRs = 32 - 1 - 2 - 1,
	NUMFPRs = 32,
};

enum FixupBranchType {
	// 16-bit immediate jump/branch (to pc + (simm16 + 1 ops).)
	BRANCH_16,
	// 26-bit immediate jump/branch (to pc's 4 high bits + imm * 4.)
	BRANCH_26,
};

// Beware of delay slots.
struct FixupBranch {
	u8 *ptr;
	FixupBranchType type;
};

class MIPSEmitter {
public:
	MIPSEmitter() : code_(0), lastCacheFlushEnd_(0) {
	}
	MIPSEmitter(u8 *code_ptr) : code_(code_ptr), lastCacheFlushEnd_(code_ptr) {
		SetCodePointer(code_ptr, code_ptr);
	}
	virtual ~MIPSEmitter() {
	}

	void SetCodePointer(const u8 *ptr, u8 *writePtr);
	const u8* GetCodePointer() const;

	void ReserveCodeSpace(u32 bytes);
	const u8 *AlignCode16();
	const u8 *AlignCodePage();
	const u8 *GetCodePtr() const;
	u8 *GetWritableCodePtr();
	void FlushIcache();
	void FlushIcacheSection(u8 *start, u8 *end);

	// 20 bits valid in code.
	void BREAK(u32 code);

	void NOP() {
		SLL(R_ZERO, R_ZERO, 0);
	}

	// Note for all branches and jumps:
	// MIPS has DELAY SLOTS.  This emitter makes it so if you forget that, you'll be safe.
	// If you want to run something inside a delay slot, emit the instruction inside a closure.
	//
	// Example:                        Translates to:
	//    J(&myFunc);                  J(&myFunc);
	//    ADDU(V0, V0, V1);            NOP();
	//                                 ADDU(V0, V0, V1);
	//
	//    J(&myFunc, [&] {             J(&myFunc);
	//        ADDU(V0, V0, V1);        ADDU(V0, V0, V1);
	//    });
	//
	// This applies to all J*() and B*() functions (except BREAK(), which is not a branch func.)

	FixupBranch J(std::function<void ()> delaySlot = nullptr);
	void J(const void *func, std::function<void ()> delaySlot = nullptr);
	FixupBranch JAL(std::function<void ()> delaySlot = nullptr);
	void JAL(const void *func, std::function<void ()> delaySlot = nullptr);
	void JR(MIPSReg rs, std::function<void ()> delaySlot = nullptr);
	void JRRA(std::function<void ()> delaySlot = nullptr) {
		JR(R_RA, delaySlot);
	}
	void JALR(MIPSReg rd, MIPSReg rs, std::function<void ()> delaySlot = nullptr);
	void JALR(MIPSReg rs, std::function<void ()> delaySlot = nullptr) {
		JALR(R_RA, rs, delaySlot);
	}

	inline FixupBranch B(std::function<void ()> delaySlot = nullptr) {
		return BEQ(R_ZERO, R_ZERO, delaySlot);
	}
	inline void B(const void *func, std::function<void ()> delaySlot = nullptr) {
		return BEQ(R_ZERO, R_ZERO, func, delaySlot);
	}
	FixupBranch BLTZ(MIPSReg rs, std::function<void ()> delaySlot = nullptr);
	void BLTZ(MIPSReg rs, const void *func, std::function<void ()> delaySlot = nullptr);
	FixupBranch BEQ(MIPSReg rs, MIPSReg rt, std::function<void ()> delaySlot = nullptr);
	void BEQ(MIPSReg rs, MIPSReg rt, const void *func, std::function<void ()> delaySlot = nullptr);
	FixupBranch BNE(MIPSReg rs, MIPSReg rt, std::function<void ()> delaySlot = nullptr);
	void BNE(MIPSReg rs, MIPSReg rt, const void *func, std::function<void ()> delaySlot = nullptr);
	inline FixupBranch BEQZ(MIPSReg rs, std::function<void ()> delaySlot = nullptr) {
		return BEQ(rs, R_ZERO, delaySlot);
	}
	inline void BEQZ(MIPSReg rs, const void *func, std::function<void ()> delaySlot = nullptr) {
		return BEQ(rs, R_ZERO, func, delaySlot);
	}
	inline FixupBranch BNEZ(MIPSReg rs, std::function<void ()> delaySlot = nullptr) {
		return BNE(rs, R_ZERO, delaySlot);
	}
	inline void BNEZ(MIPSReg rs, const void *func, std::function<void ()> delaySlot = nullptr) {
		return BNE(rs, R_ZERO, func, delaySlot);
	}
	FixupBranch BLEZ(MIPSReg rs, std::function<void ()> delaySlot = nullptr);
	void BLEZ(MIPSReg rs, const void *func, std::function<void ()> delaySlot = nullptr);
	FixupBranch BGTZ(MIPSReg rs, std::function<void ()> delaySlot = nullptr);
	void BGTZ(MIPSReg rs, const void *func, std::function<void ()> delaySlot = nullptr);

	void SetJumpTarget(const FixupBranch &branch);
	bool BInRange(const void *func);
	bool JInRange(const void *func);

	// R_AT is the stereotypical scratch reg, but it is not likely to be used.
	void QuickCallFunction(MIPSReg scratchreg, const void *func);
	template <typename T> void QuickCallFunction(MIPSReg scratchreg, T func) {
		QuickCallFunction(scratchreg, (const void *)func);
	}

	void LB(MIPSReg dest, MIPSReg base, s16 offset);
	void LH(MIPSReg dest, MIPSReg base, s16 offset);
	void LW(MIPSReg dest, MIPSReg base, s16 offset);
	void SB(MIPSReg value, MIPSReg base, s16 offset);
	void SH(MIPSReg dest, MIPSReg base, s16 offset);
	void SW(MIPSReg value, MIPSReg base, s16 offset);

	// These exist for the sole purpose of making compilation fail if you try to load/store from R+R.
	void LB(MIPSReg dest, MIPSReg base, MIPSReg invalid);
	void LH(MIPSReg dest, MIPSReg base, MIPSReg invalid);
	void LW(MIPSReg dest, MIPSReg base, MIPSReg invalid);
	void SB(MIPSReg value, MIPSReg base, MIPSReg invalid);
	void SH(MIPSReg dest, MIPSReg base, MIPSReg invalid);
	void SW(MIPSReg value, MIPSReg base, MIPSReg invalid);

	void SLL(MIPSReg rd, MIPSReg rt, u8 sa);
	void SRL(MIPSReg rd, MIPSReg rt, u8 sa);
	void SRA(MIPSReg rd, MIPSReg rt, u8 sa);
	void SLLV(MIPSReg rd, MIPSReg rt, MIPSReg rs);
	void SRLV(MIPSReg rd, MIPSReg rt, MIPSReg rs);
	void SRAV(MIPSReg rd, MIPSReg rt, MIPSReg rs);

	void SLT(MIPSReg rd, MIPSReg rt, MIPSReg rs);
	void SLTU(MIPSReg rd, MIPSReg rt, MIPSReg rs);
	void SLTI(MIPSReg rd, MIPSReg rt, s16 imm);
	// Note: very importantly, *sign* extends imm before an unsigned compare.
	void SLTIU(MIPSReg rt, MIPSReg rs, s16 imm);

	// ADD/SUB/ADDI intentionally omitted.  They are just versions that trap.
	void ADDU(MIPSReg rd, MIPSReg rs, MIPSReg rt);
	void SUBU(MIPSReg rd, MIPSReg rs, MIPSReg rt);
	void ADDIU(MIPSReg rt, MIPSReg rs, s16 imm);
	void SUBIU(MIPSReg rt, MIPSReg rs, s16 imm) {
		ADDIU(rt, rs, -imm);
	}

	void AND(MIPSReg rd, MIPSReg rs, MIPSReg rt);
	void OR(MIPSReg rd, MIPSReg rs, MIPSReg rt);
	void XOR(MIPSReg rd, MIPSReg rs, MIPSReg rt);
	void ANDI(MIPSReg rt, MIPSReg rs, s16 imm);
	void ORI(MIPSReg rt, MIPSReg rs, s16 imm);
	void XORI(MIPSReg rt, MIPSReg rs, s16 imm);

	// Clears the lower bits.  On MIPS64, the result is sign extended.
	void LUI(MIPSReg rt, s16 imm);

	void INS(MIPSReg rt, MIPSReg rs, s8 pos, s8 size);
	void EXT(MIPSReg rt, MIPSReg rs, s8 pos, s8 size);

	// MIPS64 only.  Transparently uses DSLL32 to shift 32-63 bits.
	void DSLL(MIPSReg rd, MIPSReg rt, u8 sa);

	void MOVI2R(MIPSReg reg, u64 val);
	void MOVI2R(MIPSReg reg, s64 val) {
		MOVI2R(reg, (u64)val);
	}
	void MOVI2R(MIPSReg reg, u32 val);
	void MOVI2R(MIPSReg reg, s32 val) {
		MOVI2R(reg, (u32)val);
	}
	template <class T> void MOVP2R(MIPSReg reg, T *val) {
		if (sizeof(uintptr_t) > sizeof(u32)) {
			MOVI2R(reg, (u64)(intptr_t)(const void *)val);
		} else {
			MOVI2R(reg, (u32)(intptr_t)(const void *)val);
		}
	}

protected:
	inline void Write32(u32 value) {
		*code32_++ = value;
	}

	// Less parenthesis.
	inline void Write32Fields(u8 pos1, u32 v1) {
		*code32_++ = (v1 << pos1);
	}
	inline void Write32Fields(u8 pos1, u32 v1, u8 pos2, u32 v2) {
		*code32_++ = (v1 << pos1) | (v2 << pos2);
	}
	inline void Write32Fields(u8 pos1, u32 v1, u8 pos2, u32 v2, u8 pos3, u32 v3) {
		*code32_++ = (v1 << pos1) | (v2 << pos2) | (v3 << pos3);
	}
	inline void Write32Fields(u8 pos1, u32 v1, u8 pos2, u32 v2, u8 pos3, u32 v3, u8 pos4, u32 v4) {
		*code32_++ = (v1 << pos1) | (v2 << pos2) | (v3 << pos3) | (v4 << pos4);
	}
	inline void Write32Fields(u8 pos1, u32 v1, u8 pos2, u32 v2, u8 pos3, u32 v3, u8 pos4, u32 v4, u8 pos5, u32 v5) {
		*code32_++ = (v1 << pos1) | (v2 << pos2) | (v3 << pos3) | (v4 << pos5) | (v5 << pos5);
	}
	inline void Write32Fields(u8 pos1, u32 v1, u8 pos2, u32 v2, u8 pos3, u32 v3, u8 pos4, u32 v4, u8 pos5, u32 v5, u8 pos6, u32 v6) {
		*code32_++ = (v1 << pos1) | (v2 << pos2) | (v3 << pos3) | (v4 << pos5) | (v5 << pos5) | (v6 << pos6);
	}

	static void SetJumpTarget(const FixupBranch &branch, const void *dst);
	static bool BInRange(const void *src, const void *dst);
	static bool JInRange(const void *src, const void *dst);
	FixupBranch MakeFixupBranch(FixupBranchType type) const;
	void ApplyDelaySlot(std::function<void ()> delaySlot);

private:
	union {
		u8 *code_;
		u32 *code32_;
	};
	u8 *lastCacheFlushEnd_;
};

// Everything that needs to generate machine code should inherit from this.
// You get memory management for free, plus, you can use all the LUI etc functions without
// having to prefix them with gen-> or something similar.
class MIPSCodeBlock : public CodeBlock<MIPSEmitter> {
public:
	void PoisonMemory(int offset) override;

protected:
	u8 *region;
	size_t region_size;
};

};
