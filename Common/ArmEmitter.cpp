// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "ppsspp_config.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if PPSSPP_PLATFORM(IOS)
#include <libkern/OSCacheControl.h>
#include <sys/mman.h>
#endif

#include "Common/Log.h"
#include "Common/MemoryUtil.h"
#include "Common/ArmEmitter.h"
#include "Common/CPUDetect.h"

#ifdef _WIN32
#include "CommonWindows.h"
#endif

// Want it in release builds too
#ifdef __ANDROID__
#undef _dbg_assert_msg_
#define _dbg_assert_msg_ _assert_msg_
#endif

namespace ArmGen
{

inline u32 RotR(u32 a, int amount) {
	if (!amount) return a;
	return (a >> amount) | (a << (32 - amount));
}

inline u32 RotL(u32 a, int amount) {
	if (!amount) return a;
	return (a << amount) | (a >> (32 - amount));
}

bool TryMakeOperand2(u32 imm, Operand2 &op2) {
	// Just brute force it.
	for (int i = 0; i < 16; i++) {
		int mask = RotR(0xFF, i * 2);
		if ((imm & mask) == imm) {
			op2 = Operand2((u8)(RotL(imm, i * 2)), (u8)i);
			return true;
		}
	}
	return false;
}

bool TryMakeOperand2_AllowInverse(u32 imm, Operand2 &op2, bool *inverse)
{
	if (!TryMakeOperand2(imm, op2)) {
		*inverse = true;
		return TryMakeOperand2(~imm, op2);
	} else {
		*inverse = false;
		return true;
	}
}

bool TryMakeOperand2_AllowNegation(s32 imm, Operand2 &op2, bool *negated)
{
	if (!TryMakeOperand2(imm, op2)) {
		*negated = true;
		return TryMakeOperand2(-imm, op2);
	} else {
		*negated = false;
		return true;
	}
}

Operand2 AssumeMakeOperand2(u32 imm) {
	Operand2 op2;
	bool result = TryMakeOperand2(imm, op2);
	_dbg_assert_msg_(result, "Could not make assumed Operand2.");
	if (!result) {
		// Make double sure that we get it logged.
		ERROR_LOG(JIT, "Could not make assumed Operand2.");
	}
	return op2;
}

bool ARMXEmitter::TrySetValue_TwoOp(ARMReg reg, u32 val)
{
	int ops = 0;
	for (int i = 0; i < 16; i++)
	{
		if ((val >> (i*2)) & 0x3)
		{
			ops++;
			i+=3;
		}
	}
	if (ops > 2)
		return false;

	bool first = true;
	for (int i = 0; i < 16; i++, val >>=2) {
		if (val & 0x3) {
			first ? MOV(reg, Operand2((u8)val, (u8)((16-i) & 0xF)))
				  : ORR(reg, reg, Operand2((u8)val, (u8)((16-i) & 0xF)));
			first = false;
			i+=3;
			val >>= 6;
		}
	}
	return true;
}

bool TryMakeFloatIMM8(u32 val, Operand2 &op2)
{
	if ((val & 0x0007FFFF) == 0)
	{
		// VFP Encoding for Imms: <7> Not(<6>) Repeat(<6>,5) <5:0> Zeros(19)
		bool bit6 = (val & 0x40000000) == 0x40000000;
		bool canEncode = true;
		for (u32 mask = 0x20000000; mask >= 0x02000000; mask >>= 1)
		{
			if (((val & mask) == mask) == bit6)
				canEncode = false;
		}
		if (canEncode)
		{
			u32 imm8 = (val & 0x80000000) >> 24; // sign bit
			imm8 |= (!bit6 << 6);
			imm8 |= (val & 0x01F80000) >> 19;
			op2 = IMM(imm8);
			return true;
		}
	}

	return false;
}

void ARMXEmitter::MOVI2FR(ARMReg dest, float val, bool negate)
{
	union {float f; u32 u;} conv;
	conv.f = negate ? -val : val;
	MOVI2R(dest, conv.u);
}

void ARMXEmitter::MOVI2F(ARMReg dest, float val, ARMReg tempReg, bool negate)
{
	union {float f; u32 u;} conv;
	conv.f = negate ? -val : val;
	// Try moving directly first if mantisse is empty
	Operand2 op2;
	if (cpu_info.bVFPv3 && TryMakeFloatIMM8(conv.u, op2))
		VMOV(dest, op2);
	else
	{
		MOVI2R(tempReg, conv.u);
		VMOV(dest, tempReg);
	}
	// Otherwise, possible to use a literal pool and VLDR directly (+- 1020)
}

void ARMXEmitter::MOVI2F_neon(ARMReg dest, float val, ARMReg tempReg, bool negate)
{
	union {float f; u32 u;} conv;
	conv.f = negate ? -val : val;
	// Try moving directly first if mantisse is empty
	Operand2 op2;
	if (cpu_info.bVFPv3 && TryMakeFloatIMM8(conv.u, op2))
		VMOV_neon(F_32, dest, conv.u);
	else
	{
		MOVI2R(tempReg, conv.u);
		VDUP(F_32, dest, tempReg);
	}
	// Otherwise, possible to use a literal pool and VLD1 directly (+- 1020)
}

void ARMXEmitter::ADDI2R(ARMReg rd, ARMReg rs, u32 val, ARMReg scratch)
{
	if (!TryADDI2R(rd, rs, val)) {
		MOVI2R(scratch, val);
		ADD(rd, rs, scratch);
	}
}

bool ARMXEmitter::TryADDI2R(ARMReg rd, ARMReg rs, u32 val)
{
	if (val == 0) {
		if (rd != rs)
			MOV(rd, rs);
		return true;
	}
	Operand2 op2;
	bool negated;
	if (TryMakeOperand2_AllowNegation(val, op2, &negated)) {
		if (!negated)
			ADD(rd, rs, op2);
		else
			SUB(rd, rs, op2);
		return true;
	} else {
		// Try 16-bit additions and subtractions - easy to test for.
		// Should also try other rotations...
		if ((val & 0xFFFF0000) == 0) {
			// Decompose into two additions.
			ADD(rd, rs, Operand2((u8)(val >> 8), 12));   // rotation right by 12*2 == rotation left by 8
			ADD(rd, rd, Operand2((u8)(val), 0));
			return true;
		} else if ((((u32)-(s32)val) & 0xFFFF0000) == 0) {
			val = (u32)-(s32)val;
			SUB(rd, rs, Operand2((u8)(val >> 8), 12));
			SUB(rd, rd, Operand2((u8)(val), 0));
			return true;
		} else {
			return false;
		}
	}
}

void ARMXEmitter::SUBI2R(ARMReg rd, ARMReg rs, u32 val, ARMReg scratch)
{
	if (!TrySUBI2R(rd, rs, val)) {
		MOVI2R(scratch, val);
		SUB(rd, rs, scratch);
	}
}

bool ARMXEmitter::TrySUBI2R(ARMReg rd, ARMReg rs, u32 val)
{
	// Just add a negative.
	return TryADDI2R(rd, rs, (u32)-(s32)val);
}

void ARMXEmitter::ANDI2R(ARMReg rd, ARMReg rs, u32 val, ARMReg scratch)
{
	if (!TryANDI2R(rd, rs, val)) {
		MOVI2R(scratch, val);
		AND(rd, rs, scratch);
	}
}

bool ARMXEmitter::TryANDI2R(ARMReg rd, ARMReg rs, u32 val)
{
	Operand2 op2;
	bool inverse;
	if (val == 0) {
		// Avoid the ALU, may improve pipeline.
		MOV(rd, 0);
		return true;
	} else if (TryMakeOperand2_AllowInverse(val, op2, &inverse)) {
		if (!inverse) {
			AND(rd, rs, op2);
		} else {
			BIC(rd, rs, op2);
		}
		return true;
	} else {
#if PPSSPP_ARCH(ARMV7)
		// Check if we have a single pattern of sequential bits.
		int seq = -1;
		for (int i = 0; i < 32; ++i) {
			if (((val >> i) & 1) == 0) {
				if (seq == -1) {
					// The width is all bits previous to this, set to 1.
					seq = i;
				}
			} else if (seq != -1) {
				// Uh oh, more than one sequence.
				seq = -2;
			}
		}

		if (seq > 0) {
			UBFX(rd, rs, 0, seq);
			return true;
		}
#endif

		int ops = 0;
		for (int i = 0; i < 32; i += 2) {
			u8 bits = RotR(val, i) & 0xFF;
			// If either low bit is not set, we need to use a BIC for them.
			if ((bits & 3) != 3) {
				++ops;
				i += 8 - 2;
			}
		}

		// The worst case is 4 (e.g. 0x55555555.)
#if PPSSPP_ARCH(ARMV7)
		if (ops > 3) {
			return false;
		}
#endif
		bool first = true;
		for (int i = 0; i < 32; i += 2) {
			u8 bits = RotR(val, i) & 0xFF;
			if ((bits & 3) != 3) {
				u8 rotation = i == 0 ? 0 : 16 - i / 2;
				if (first) {
					BIC(rd, rs, Operand2(~bits, rotation));
					first = false;
				} else {
					BIC(rd, rd, Operand2(~bits, rotation));
				}
				// Well, we took care of these other bits while we were at it.
				i += 8 - 2;
			}
		}
		return true;
	}
}

void ARMXEmitter::CMPI2R(ARMReg rs, u32 val, ARMReg scratch)
{
	if (!TryCMPI2R(rs, val)) {
		MOVI2R(scratch, val);
		CMP(rs, scratch);
	}
}

bool ARMXEmitter::TryCMPI2R(ARMReg rs, u32 val)
{
	Operand2 op2;
	bool negated;
	if (TryMakeOperand2_AllowNegation(val, op2, &negated)) {
		if (!negated)
			CMP(rs, op2);
		else
			CMN(rs, op2);
		return true;
	} else {
		return false;
	}
}

void ARMXEmitter::TSTI2R(ARMReg rs, u32 val, ARMReg scratch)
{
	if (!TryTSTI2R(rs, val)) {
		MOVI2R(scratch, val);
		TST(rs, scratch);
	}
}

bool ARMXEmitter::TryTSTI2R(ARMReg rs, u32 val)
{
	Operand2 op2;
	if (TryMakeOperand2(val, op2)) {
		TST(rs, op2);
		return true;
	} else {
		return false;
	}
}

void ARMXEmitter::ORI2R(ARMReg rd, ARMReg rs, u32 val, ARMReg scratch)
{
	if (!TryORI2R(rd, rs, val)) {
		MOVI2R(scratch, val);
		ORR(rd, rs, scratch);
	}
}

bool ARMXEmitter::TryORI2R(ARMReg rd, ARMReg rs, u32 val)
{
	Operand2 op2;
	if (val == 0) {
		// Avoid the ALU, may improve pipeline.
		if (rd != rs) {
			MOV(rd, rs);
		}
		return true;
	} else if (TryMakeOperand2(val, op2)) {
		ORR(rd, rs, op2);
		return true;
	} else {
		int ops = 0;
		for (int i = 0; i < 32; i += 2) {
			u8 bits = RotR(val, i) & 0xFF;
			// If either low bit is set, we need to use a ORR for them.
			if ((bits & 3) != 0) {
				++ops;
				i += 8 - 2;
			}
		}

		// The worst case is 4 (e.g. 0x55555555.)  But MVN can make it 2.  Not sure if better.
		bool inversed;
		if (TryMakeOperand2_AllowInverse(val, op2, &inversed) && ops >= 3) {
			return false;
#if PPSSPP_ARCH(ARMV7)
		} else if (ops > 3) {
			return false;
#endif
		}

		bool first = true;
		for (int i = 0; i < 32; i += 2) {
			u8 bits = RotR(val, i) & 0xFF;
			if ((bits & 3) != 0) {
				u8 rotation = i == 0 ? 0 : 16 - i / 2;
				if (first) {
					ORR(rd, rs, Operand2(bits, rotation));
					first = false;
				} else {
					ORR(rd, rd, Operand2(bits, rotation));
				}
				// Well, we took care of these other bits while we were at it.
				i += 8 - 2;
			}
		}
		return true;
	}
}

void ARMXEmitter::EORI2R(ARMReg rd, ARMReg rs, u32 val, ARMReg scratch)
{
	if (!TryEORI2R(rd, rs, val)) {
		MOVI2R(scratch, val);
		EOR(rd, rs, scratch);
	}
}

bool ARMXEmitter::TryEORI2R(ARMReg rd, ARMReg rs, u32 val)
{
	Operand2 op2;
	if (val == 0) {
		if (rd != rs) {
			MOV(rd, rs);
		}
		return true;
	} else if (TryMakeOperand2(val, op2)) {
		EOR(rd, rs, op2);
		return true;
	} else {
		return false;
	}
}

void ARMXEmitter::FlushLitPool()
{
	for (LiteralPool& pool : currentLitPool) {
		// Search for duplicates
		for (LiteralPool& old_pool : currentLitPool) {
			if (old_pool.val == pool.val)
				pool.loc = old_pool.loc;
		}

		// Write the constant to Literal Pool
		if (!pool.loc)
		{
			pool.loc = (intptr_t)code;
			Write32(pool.val);
		}
		s32 offset = (s32)(pool.loc - (intptr_t)pool.ldr_address - 8);

		// Backpatch the LDR
		*(u32*)pool.ldr_address |= (offset >= 0) << 23 | abs(offset);
	}
	// TODO: Save a copy of previous pools in case they are still in range.
	currentLitPool.clear();
}

void ARMXEmitter::AddNewLit(u32 val)
{
	LiteralPool pool_item;
	pool_item.loc = 0;
	pool_item.val = val;
	pool_item.ldr_address = code;
	currentLitPool.push_back(pool_item);
}

void ARMXEmitter::MOVI2R(ARMReg reg, u32 val, bool optimize)
{
	Operand2 op2;
	bool inverse;

#if PPSSPP_ARCH(ARMV7)
	// Unused
	if (!optimize)
	{
		// For backpatching on ARMv7
		MOVW(reg, val & 0xFFFF);
		MOVT(reg, val, true);
		return;
	}
#endif

	if (TryMakeOperand2_AllowInverse(val, op2, &inverse)) {
		inverse ? MVN(reg, op2) : MOV(reg, op2);
	} else {
#if PPSSPP_ARCH(ARMV7)
		// Use MOVW+MOVT for ARMv7+
		MOVW(reg, val & 0xFFFF);
		if(val & 0xFFFF0000)
			MOVT(reg, val, true);
#else
		if (!TrySetValue_TwoOp(reg,val)) {
			bool first = true;
			for (int i = 0; i < 32; i += 2) {
				u8 bits = RotR(val, i) & 0xFF;
				if ((bits & 3) != 0) {
					u8 rotation = i == 0 ? 0 : 16 - i / 2;
					if (first) {
						MOV(reg, Operand2(bits, rotation));
						first = false;
					} else {
						ORR(reg, reg, Operand2(bits, rotation));
					}
					// Well, we took care of these other bits while we were at it.
					i += 8 - 2;
				}
			}
			// Use literal pool for ARMv6.
			// Disabled for now as it is crashfing since Vertex Decoder JIT
//			AddNewLit(val);
//			LDR(reg, R_PC); // To be backpatched later
		}
#endif
	}
}

static const char *const armRegStrings[] = {
	"r0","r1","r2","r3",
	"r4","r5","r6","r7",
	"r8","r9","r10","r11",
	"r12","r13","r14","PC",

	"s0", "s1", "s2", "s3",
	"s4", "s5", "s6", "s7",
	"s8", "s9", "s10", "s11",
	"s12", "s13", "s14", "s15",

	"s16", "s17", "s18", "s19",
	"s20", "s21", "s22", "s23",
	"s24", "s25", "s26", "s27",
	"s28", "s29", "s30", "s31",

	"d0", "d1", "d2", "d3",
	"d4", "d5", "d6", "d7",
	"d8", "d9", "d10", "d11",
	"d12", "d13", "d14", "d15",

	"d16", "d17", "d18", "d19",
	"d20", "d21", "d22", "d23",
	"d24", "d25", "d26", "d27",
	"d28", "d29", "d30", "d31",

	"q0", "q1", "q2", "q3",
	"q4", "q5", "q6", "q7",
	"q8", "q9", "q10", "q11",
	"q12", "q13", "q14", "q15",
};

const char *ARMRegAsString(ARMReg reg) {
	if ((unsigned int)reg >= sizeof(armRegStrings)/sizeof(armRegStrings[0]))
		return "(bad)";
	return armRegStrings[(int)reg];
}

void ARMXEmitter::QuickCallFunction(ARMReg reg, const void *func) {
	if (BLInRange(func)) {
		BL(func);
	} else {
		MOVP2R(reg, func);
		BL(reg);
	}
}

void ARMXEmitter::SetCodePointer(u8 *ptr, u8 *writePtr)
{
	code = ptr;
	startcode = code;
	lastCacheFlushEnd = ptr;
}

const u8 *ARMXEmitter::GetCodePointer() const
{
	return code;
}

u8 *ARMXEmitter::GetWritableCodePtr()
{
	return code;
}

void ARMXEmitter::ReserveCodeSpace(u32 bytes)
{
	for (u32 i = 0; i < bytes/4; i++)
		Write32(0xE1200070); //bkpt 0
}

const u8 *ARMXEmitter::AlignCode16()
{
	ReserveCodeSpace((-(intptr_t)code) & 15);
	return code;
}

const u8 *ARMXEmitter::NopAlignCode16() {
	int bytes = ((-(intptr_t)code) & 15);
	for (int i = 0; i < bytes / 4; i++) {
		Write32(0xE320F000); // one of many possible nops
	}
	return code;
}

const u8 *ARMXEmitter::AlignCodePage()
{
	ReserveCodeSpace((-(intptr_t)code) & 4095);
	return code;
}

void ARMXEmitter::FlushIcache()
{
	FlushIcacheSection(lastCacheFlushEnd, code);
	lastCacheFlushEnd = code;
}

void ARMXEmitter::FlushIcacheSection(u8 *start, u8 *end)
{
#if PPSSPP_PLATFORM(IOS)
	// Header file says this is equivalent to: sys_icache_invalidate(start, end - start);
	sys_cache_control(kCacheFunctionPrepareForExecution, start, end - start);
#elif PPSSPP_PLATFORM(WINDOWS)
	FlushInstructionCache(GetCurrentProcess(), start, end - start);
#elif PPSSPP_ARCH(ARM)

#if defined(__clang__) || defined(__ANDROID__)
	__clear_cache(start, end);
#else
	__builtin___clear_cache(start, end);
#endif

#endif
}

void ARMXEmitter::SetCC(CCFlags cond)
{
	condition = cond << 28;
}

void ARMXEmitter::NOP(int count)
{
	for (int i = 0; i < count; i++) {
		Write32(condition | 0x01A00000);
	}
}

void ARMXEmitter::SETEND(bool BE)
{
	//SETEND is non-conditional
	Write32(0xF1010000 | (BE << 9));
}
void ARMXEmitter::BKPT(u16 arg)
{
	Write32(condition | 0x01200070 | (arg << 4 & 0x000FFF00) | (arg & 0x0000000F));
}
void ARMXEmitter::YIELD()
{
	Write32(condition | 0x0320F001);
}

FixupBranch ARMXEmitter::B()
{
	FixupBranch branch;
	branch.type = 0; // Zero for B
	branch.ptr = code;
	branch.condition = condition;
	//We'll write NOP here for now.
	Write32(condition | 0x01A00000);
	return branch;
}
FixupBranch ARMXEmitter::BL()
{
	FixupBranch branch;
	branch.type = 1; // Zero for B
	branch.ptr = code;
	branch.condition = condition;
	//We'll write NOP here for now.
	Write32(condition | 0x01A00000);
	return branch;
}

FixupBranch ARMXEmitter::B_CC(CCFlags Cond)
{
	FixupBranch branch;
	branch.type = 0; // Zero for B
	branch.ptr = code;
	branch.condition = Cond << 28;
	//We'll write NOP here for now.
	Write32(condition | 0x01A00000);
	return branch;
}
void ARMXEmitter::B_CC(CCFlags Cond, const void *fnptr)
{
	ptrdiff_t distance = (intptr_t)fnptr - ((intptr_t)(code) + 8);
	_assert_msg_(distance > -0x2000000 && distance < 0x2000000,
                     "B_CC out of range (%p calls %p)", code, fnptr);

	Write32((Cond << 28) | 0x0A000000 | ((distance >> 2) & 0x00FFFFFF));
}
FixupBranch ARMXEmitter::BL_CC(CCFlags Cond)
{
	FixupBranch branch;
	branch.type = 1; // Zero for B
	branch.ptr = code;
	branch.condition = Cond << 28;
	//We'll write NOP here for now.
	Write32(condition | 0x01A00000);
	return branch;
}
void ARMXEmitter::SetJumpTarget(FixupBranch const &branch)
{
	ptrdiff_t distance =  ((intptr_t)(code) - 8)  - (intptr_t)branch.ptr;
	_assert_msg_(distance > -0x2000000 && distance < 0x2000000,
	             "SetJumpTarget out of range (%p calls %p)", code, branch.ptr);
	u32 instr = (u32)(branch.condition | ((distance >> 2) & 0x00FFFFFF));
	instr |= branch.type == 0 ? /* B */ 0x0A000000 : /* BL */ 0x0B000000;
	*(u32*)branch.ptr = instr;
}
void ARMXEmitter::B(const void *fnptr)
{
	ptrdiff_t distance = (intptr_t)fnptr - (intptr_t(code) + 8);
	_assert_msg_(distance > -0x2000000 && distance < 0x2000000,
                     "B out of range (%p calls %p)", code, fnptr);

	Write32(condition | 0x0A000000 | ((distance >> 2) & 0x00FFFFFF));
}

void ARMXEmitter::B(ARMReg src)
{
	Write32(condition | 0x012FFF10 | src);
}

bool ARMXEmitter::BLInRange(const void *fnptr) const {
	ptrdiff_t distance = (intptr_t)fnptr - (intptr_t(code) + 8);
	if (distance <= -0x2000000 || distance >= 0x2000000)
		return false;
	else
		return true;
}

void ARMXEmitter::BL(const void *fnptr)
{
	ptrdiff_t distance = (intptr_t)fnptr - (intptr_t(code) + 8);
	_assert_msg_(distance > -0x2000000 && distance < 0x2000000,
                     "BL out of range (%p calls %p)", code, fnptr);
	Write32(condition | 0x0B000000 | ((distance >> 2) & 0x00FFFFFF));
}
void ARMXEmitter::BL(ARMReg src)
{
	Write32(condition | 0x012FFF30 | src);
}

void ARMXEmitter::PUSH(const int num, ...)
{
	u16 RegList = 0;
	u8 Reg;
	int i;
	va_list vl;
	va_start(vl, num);
	for (i = 0; i < num; i++) {
		Reg = va_arg(vl, u32);
		RegList |= (1 << Reg);
	}
	va_end(vl);
	Write32(condition | (2349 << 16) | RegList);
}

void ARMXEmitter::POP(const int num, ...)
{
	u16 RegList = 0;
	u8 Reg;
	int i;
	va_list vl;
	va_start(vl, num);
	for (i=0;i<num;i++)
	{
		Reg = va_arg(vl, u32);
		RegList |= (1 << Reg);
	}
	va_end(vl);
	Write32(condition | (2237 << 16) | RegList);
}

void ARMXEmitter::WriteShiftedDataOp(u32 op, bool SetFlags, ARMReg dest, ARMReg src, Operand2 op2)
{
	Write32(condition | (13 << 21) | (SetFlags << 20) | (dest << 12) | op2.Imm5() | (op << 4) | src);
}
void ARMXEmitter::WriteShiftedDataOp(u32 op, bool SetFlags, ARMReg dest, ARMReg src, ARMReg op2)
{
	Write32(condition | (13 << 21) | (SetFlags << 20) | (dest << 12) | (op2 << 8) | (op << 4) | src);
}

// IMM, REG, IMMSREG, RSR 
// -1 for invalid if the instruction doesn't support that
const s32 InstOps[][4] = {{16, 0, 0, 0}, // AND(s)
                         {17, 1, 1, 1}, // EOR(s)
                         {18, 2, 2, 2}, // SUB(s)
                         {19, 3, 3, 3}, // RSB(s)
                         {20, 4, 4, 4}, // ADD(s)
                         {21, 5, 5, 5}, // ADC(s)
                         {22, 6, 6, 6}, // SBC(s)
                         {23, 7, 7, 7}, // RSC(s)
                         {24, 8, 8, 8}, // TST
                         {25, 9, 9, 9}, // TEQ
                         {26, 10, 10, 10}, // CMP
                         {27, 11, 11, 11}, // CMN
                         {28, 12, 12, 12}, // ORR(s)
                         {29, 13, 13, 13}, // MOV(s)
                         {30, 14, 14, 14}, // BIC(s)
                         {31, 15, 15, 15}, // MVN(s)
                         {24, -1, -1, -1}, // MOVW
                         {26, -1, -1, -1}, // MOVT
                         }; 

const char *InstNames[] = { "AND",
                            "EOR",
                            "SUB",
                            "RSB",
                            "ADD",
                            "ADC",
                            "SBC",
                            "RSC",
                            "TST",
                            "TEQ",
                            "CMP",
                            "CMN",
                            "ORR",
                            "MOV",
                            "BIC",
                            "MVN",
                            "MOVW",
                            "MOVT",
                            };

void ARMXEmitter::AND (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(0, Rd, Rn, Rm); }
void ARMXEmitter::ANDS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(0, Rd, Rn, Rm, true); }
void ARMXEmitter::EOR (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(1, Rd, Rn, Rm); }
void ARMXEmitter::EORS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(1, Rd, Rn, Rm, true); }
void ARMXEmitter::SUB (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(2, Rd, Rn, Rm); }
void ARMXEmitter::SUBS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(2, Rd, Rn, Rm, true); }
void ARMXEmitter::RSB (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(3, Rd, Rn, Rm); }
void ARMXEmitter::RSBS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(3, Rd, Rn, Rm, true); }
void ARMXEmitter::ADD (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(4, Rd, Rn, Rm); }
void ARMXEmitter::ADDS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(4, Rd, Rn, Rm, true); }
void ARMXEmitter::ADC (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(5, Rd, Rn, Rm); }
void ARMXEmitter::ADCS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(5, Rd, Rn, Rm, true); }
void ARMXEmitter::SBC (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(6, Rd, Rn, Rm); }
void ARMXEmitter::SBCS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(6, Rd, Rn, Rm, true); }
void ARMXEmitter::RSC (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(7, Rd, Rn, Rm); }
void ARMXEmitter::RSCS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(7, Rd, Rn, Rm, true); }
void ARMXEmitter::TST (			  ARMReg Rn, Operand2 Rm) { WriteInstruction(8, R0, Rn, Rm, true); }
void ARMXEmitter::TEQ (			  ARMReg Rn, Operand2 Rm) { WriteInstruction(9, R0, Rn, Rm, true); }
void ARMXEmitter::CMP (			  ARMReg Rn, Operand2 Rm) { WriteInstruction(10, R0, Rn, Rm, true); }
void ARMXEmitter::CMN (			  ARMReg Rn, Operand2 Rm) { WriteInstruction(11, R0, Rn, Rm, true); }
void ARMXEmitter::ORR (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(12, Rd, Rn, Rm); }
void ARMXEmitter::ORRS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(12, Rd, Rn, Rm, true); }
void ARMXEmitter::MOV (ARMReg Rd,			 Operand2 Rm) { WriteInstruction(13, Rd, R0, Rm); }
void ARMXEmitter::MOVS(ARMReg Rd,			 Operand2 Rm) { WriteInstruction(13, Rd, R0, Rm, true); }
void ARMXEmitter::BIC (ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(14, Rd, Rn, Rm); }
void ARMXEmitter::BICS(ARMReg Rd, ARMReg Rn, Operand2 Rm) { WriteInstruction(14, Rd, Rn, Rm, true); }
void ARMXEmitter::MVN (ARMReg Rd,			 Operand2 Rm) { WriteInstruction(15, Rd, R0, Rm); }
void ARMXEmitter::MVNS(ARMReg Rd,			 Operand2 Rm) { WriteInstruction(15, Rd, R0, Rm, true); }
void ARMXEmitter::MOVW(ARMReg Rd,			 Operand2 Rm) { WriteInstruction(16, Rd, R0, Rm); }
void ARMXEmitter::MOVT(ARMReg Rd, Operand2 Rm, bool TopBits) { WriteInstruction(17, Rd, R0, TopBits ? Rm.Value >> 16 : Rm); }

void ARMXEmitter::WriteInstruction (u32 Op, ARMReg Rd, ARMReg Rn, Operand2 Rm, bool SetFlags) // This can get renamed later
{
	s32 op = InstOps[Op][Rm.GetType()]; // Type always decided by last operand
	u32 Data = Rm.GetData();
	if (Rm.GetType() == TYPE_IMM)
	{
		switch (Op)
		{
			// MOV cases that support IMM16
			case 16:
			case 17:
				Data = Rm.Imm16();
			break;
			default:
			break;
		}
	}
	if (op == -1)
		_assert_msg_(false, "%s not yet support %d", InstNames[Op], Rm.GetType()); 
	Write32(condition | (op << 21) | (SetFlags ? (1 << 20) : 0) | Rn << 16 | Rd << 12 | Data);
}

// Data Operations
void ARMXEmitter::WriteSignedMultiply(u32 Op, u32 Op2, u32 Op3, ARMReg dest, ARMReg r1, ARMReg r2)
{
	Write32(condition | (0x7 << 24) | (Op << 20) | (dest << 16) | (Op2 << 12) | (r1 << 8) | (Op3 << 5) | (1 << 4) | r2);
}
void ARMXEmitter::UDIV(ARMReg dest, ARMReg dividend, ARMReg divisor)
{
	_assert_msg_(cpu_info.bIDIVa, "Trying to use integer divide on hardware that doesn't support it.");
	WriteSignedMultiply(3, 0xF, 0, dest, divisor, dividend);
}
void ARMXEmitter::SDIV(ARMReg dest, ARMReg dividend, ARMReg divisor)
{
	_assert_msg_(cpu_info.bIDIVa, "Trying to use integer divide on hardware that doesn't support it.");
	WriteSignedMultiply(1, 0xF, 0, dest, divisor, dividend);
}

void ARMXEmitter::LSL (ARMReg dest, ARMReg src, Operand2 op2) { WriteShiftedDataOp(0, false, dest, src, op2);}
void ARMXEmitter::LSLS(ARMReg dest, ARMReg src, Operand2 op2) { WriteShiftedDataOp(0, true, dest, src, op2);}
void ARMXEmitter::LSL (ARMReg dest, ARMReg src, ARMReg op2)   { WriteShiftedDataOp(1, false, dest, src, op2);} 
void ARMXEmitter::LSLS(ARMReg dest, ARMReg src, ARMReg op2)   { WriteShiftedDataOp(1, true, dest, src, op2);}
void ARMXEmitter::LSR (ARMReg dest, ARMReg src, Operand2 op2) {
	_assert_msg_(op2.GetType() != TYPE_IMM || op2.Imm5() != 0, "LSR must have a non-zero shift (use LSL.)"); 
	WriteShiftedDataOp(2, false, dest, src, op2);
}
void ARMXEmitter::LSRS(ARMReg dest, ARMReg src, Operand2 op2) {
	_assert_msg_(op2.GetType() != TYPE_IMM || op2.Imm5() != 0, "LSRS must have a non-zero shift (use LSLS.)");
	WriteShiftedDataOp(2, true, dest, src, op2);
}
void ARMXEmitter::LSR (ARMReg dest, ARMReg src, ARMReg op2)   { WriteShiftedDataOp(3, false, dest, src, op2);}
void ARMXEmitter::LSRS(ARMReg dest, ARMReg src, ARMReg op2)   { WriteShiftedDataOp(3, true, dest, src, op2);}
void ARMXEmitter::ASR (ARMReg dest, ARMReg src, Operand2 op2) {
	_assert_msg_(op2.GetType() != TYPE_IMM || op2.Imm5() != 0, "ASR must have a non-zero shift (use LSL.)");
	WriteShiftedDataOp(4, false, dest, src, op2);
}
void ARMXEmitter::ASRS(ARMReg dest, ARMReg src, Operand2 op2) {
	_assert_msg_(op2.GetType() != TYPE_IMM || op2.Imm5() != 0, "ASRS must have a non-zero shift (use LSLS.)");
	WriteShiftedDataOp(4, true, dest, src, op2);
}
void ARMXEmitter::ASR (ARMReg dest, ARMReg src, ARMReg op2)   { WriteShiftedDataOp(5, false, dest, src, op2);}
void ARMXEmitter::ASRS(ARMReg dest, ARMReg src, ARMReg op2)   { WriteShiftedDataOp(5, true, dest, src, op2);}

void ARMXEmitter::MUL (ARMReg dest,	ARMReg src, ARMReg op2)
{
	Write32(condition | (dest << 16) | (src << 8) | (9 << 4) | op2);
}
void ARMXEmitter::MULS(ARMReg dest,	ARMReg src, ARMReg op2)
{
	Write32(condition | (1 << 20) | (dest << 16) | (src << 8) | (9 << 4) | op2);
}

void ARMXEmitter::Write4OpMultiply(u32 op, ARMReg destLo, ARMReg destHi, ARMReg rm, ARMReg rn) {
	Write32(condition | (op << 20) | (destHi << 16) | (destLo << 12) | (rm << 8) | (9 << 4) | rn);
}

void ARMXEmitter::UMULL(ARMReg destLo, ARMReg destHi, ARMReg rm, ARMReg rn)
{
	Write4OpMultiply(0x8, destLo, destHi, rn, rm);
}

void ARMXEmitter::SMULL(ARMReg destLo, ARMReg destHi, ARMReg rm, ARMReg rn)
{
	Write4OpMultiply(0xC, destLo, destHi, rn, rm);
}

void ARMXEmitter::UMLAL(ARMReg destLo, ARMReg destHi, ARMReg rm, ARMReg rn)
{
	Write4OpMultiply(0xA, destLo, destHi, rn, rm);
}

void ARMXEmitter::SMLAL(ARMReg destLo, ARMReg destHi, ARMReg rm, ARMReg rn)
{
	Write4OpMultiply(0xE, destLo, destHi, rn, rm);
}

void ARMXEmitter::UBFX(ARMReg dest, ARMReg rn, u8 lsb, u8 width)
{
	Write32(condition | (0x7E0 << 16) | ((width - 1) << 16) | (dest << 12) | (lsb << 7) | (5 << 4) | rn);
}

void ARMXEmitter::SBFX(ARMReg dest, ARMReg rn, u8 lsb, u8 width)
{
	Write32(condition | (0x7A0 << 16) | ((width - 1) << 16) | (dest << 12) | (lsb << 7) | (5 << 4) | rn);
}

void ARMXEmitter::CLZ(ARMReg rd, ARMReg rm)
{
	Write32(condition | (0x16F << 16) | (rd << 12) | (0xF1 << 4) | rm);
}

void ARMXEmitter::PLD(ARMReg rn, int offset, bool forWrite) {
	_dbg_assert_msg_(offset < 0x3ff && offset > -0x3ff, "PLD: Max 12 bits of offset allowed");

	bool U = offset >= 0;
	if (offset < 0) offset = -offset;
	bool R = !forWrite;
	// Conditions not allowed
	Write32((0xF5 << 24) | (U << 23) | (R << 22) | (1 << 20) | ((int)rn << 16) | (0xF << 12) | offset);
}


void ARMXEmitter::BFI(ARMReg rd, ARMReg rn, u8 lsb, u8 width)
{
	u32 msb = (lsb + width - 1);
	if (msb > 31) msb = 31;
	Write32(condition | (0x7C0 << 16) | (msb << 16) | (rd << 12) | (lsb << 7) | (1 << 4) | rn);
}

void ARMXEmitter::BFC(ARMReg rd, u8 lsb, u8 width)
{
	u32 msb = (lsb + width - 1);
	if (msb > 31) msb = 31;
	Write32(condition | (0x7C0 << 16) | (msb << 16) | (rd << 12) | (lsb << 7) | (1 << 4) | 15);
}

void ARMXEmitter::SXTB (ARMReg dest, ARMReg op2)
{
	Write32(condition | (0x6AF << 16) | (dest << 12) | (7 << 4) | op2);
}

void ARMXEmitter::SXTH (ARMReg dest, ARMReg op2, u8 rotation)
{
	SXTAH(dest, (ARMReg)15, op2, rotation);
}
void ARMXEmitter::SXTAH(ARMReg dest, ARMReg src, ARMReg op2, u8 rotation) 
{
	// bits ten and 11 are the rotation amount, see 8.8.232 for more
	// information
	Write32(condition | (0x6B << 20) | (src << 16) | (dest << 12) | (rotation << 10) | (7 << 4) | op2);
}
void ARMXEmitter::RBIT(ARMReg dest, ARMReg src)
{
	Write32(condition | (0x6F << 20) | (0xF << 16) | (dest << 12) | (0xF3 << 4) | src);
}
void ARMXEmitter::REV (ARMReg dest, ARMReg src) 
{
	Write32(condition | (0x6BF << 16) | (dest << 12) | (0xF3 << 4) | src);
}
void ARMXEmitter::REV16(ARMReg dest, ARMReg src)
{
	Write32(condition | (0x6BF << 16) | (dest << 12) | (0xFB << 4) | src);
}

void ARMXEmitter::_MSR (bool write_nzcvq, bool write_g,		Operand2 op2)
{
	Write32(condition | (0x320F << 12) | (write_nzcvq << 19) | (write_g << 18) | op2.Imm12Mod());
}
void ARMXEmitter::_MSR (bool write_nzcvq, bool write_g,		ARMReg src)
{
	Write32(condition | (0x120F << 12) | (write_nzcvq << 19) | (write_g << 18) | src);
}
void ARMXEmitter::MRS (ARMReg dest)
{
	Write32(condition | (16 << 20) | (15 << 16) | (dest << 12));
}
void ARMXEmitter::LDREX(ARMReg dest, ARMReg base)
{
	Write32(condition | (25 << 20) | (base << 16) | (dest << 12) | 0xF9F);
}
void ARMXEmitter::STREX(ARMReg result, ARMReg base, ARMReg op)
{
	_assert_msg_((result != base && result != op), "STREX dest can't be other two registers");
	Write32(condition | (24 << 20) | (base << 16) | (result << 12) | (0xF9 << 4) | op);
}
void ARMXEmitter::DMB ()
{
	Write32(0xF57FF05E);
}
void ARMXEmitter::SVC(Operand2 op)
{
	Write32(condition | (0x0F << 24) | op.Imm24());
}

// IMM, REG, IMMSREG, RSR
// -1 for invalid if the instruction doesn't support that
const s32 LoadStoreOps[][4] = {
	{0x40, 0x60, 0x60, -1}, // STR
	{0x41, 0x61, 0x61, -1}, // LDR
	{0x44, 0x64, 0x64, -1}, // STRB
	{0x45, 0x65, 0x65, -1}, // LDRB
	// Special encodings
	{ 0x4,  0x0,  -1, -1}, // STRH
	{ 0x5,  0x1,  -1, -1}, // LDRH
	{ 0x5,  0x1,  -1, -1}, // LDRSB
	{ 0x5,  0x1,  -1, -1}, // LDRSH
};
const char *LoadStoreNames[] = {
	"STR",
	"LDR",
	"STRB",
	"LDRB",
	"STRH",
	"LDRH",
	"LDRSB",
	"LDRSH",
};

void ARMXEmitter::WriteStoreOp(u32 Op, ARMReg Rt, ARMReg Rn, Operand2 Rm, bool RegAdd)
{
	s32 op = LoadStoreOps[Op][Rm.GetType()]; // Type always decided by last operand
	u32 Data;

	// Qualcomm chipsets get /really/ angry if you don't use index, even if the offset is zero.
	// Some of these encodings require Index at all times anyway. Doesn't really matter.
	// bool Index = op2 != 0 ? true : false;
	bool Index = true;
	bool Add = false;

	// Special Encoding (misc addressing mode)
	bool SpecialOp = false;
	bool Half = false;
	bool SignedLoad = false;

	if (op == -1)
		_assert_msg_(false, "%s does not support %d", LoadStoreNames[Op], Rm.GetType()); 

	switch (Op)
	{
		case 4: // STRH
			SpecialOp = true;
			Half = true;
			SignedLoad = false;
		break;
		case 5: // LDRH
			SpecialOp = true;
			Half = true;
			SignedLoad = false;
		break;
		case 6: // LDRSB
			SpecialOp = true;
			Half = false;
			SignedLoad = true;
		break;
		case 7: // LDRSH
			SpecialOp = true;
			Half = true;
			SignedLoad = true;
		break;
	}
	switch (Rm.GetType())
	{
		case TYPE_IMM:
		{
			s32 Temp = (s32)Rm.Value;
			Data = abs(Temp);
			// The offset is encoded differently on this one.
			if (SpecialOp)
				Data = ((Data & 0xF0) << 4) | (Data & 0xF);
			if (Temp >= 0) Add = true;
		}
		break;
		case TYPE_REG:
			Data = Rm.GetData();
			Add = RegAdd;
			break;
		case TYPE_IMMSREG:
			if (!SpecialOp)
			{
				Data = Rm.GetData();
				Add = RegAdd;
				break;
			}
			// Intentional fallthrough: TYPE_IMMSREG not supported for misc addressing.
		default:
			// RSR not supported for any of these
			// We already have the warning above
			BKPT(0x2);
			return;
		break;
	}
	if (SpecialOp)
	{
		// Add SpecialOp things
		Data = (0x9 << 4) | (SignedLoad << 6) | (Half << 5) | Data;
	}
	Write32(condition | (op << 20) | (Index << 24) | (Add << 23) | (Rn << 16) | (Rt << 12) | Data);
}

void ARMXEmitter::LDR (ARMReg dest, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(1, dest, base, op2, RegAdd);}
void ARMXEmitter::LDRB(ARMReg dest, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(3, dest, base, op2, RegAdd);}
void ARMXEmitter::LDRH(ARMReg dest, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(5, dest, base, op2, RegAdd);}
void ARMXEmitter::LDRSB(ARMReg dest, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(6, dest, base, op2, RegAdd);}
void ARMXEmitter::LDRSH(ARMReg dest, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(7, dest, base, op2, RegAdd);}
void ARMXEmitter::STR  (ARMReg result, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(0, result, base, op2, RegAdd);}
void ARMXEmitter::STRH (ARMReg result, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(4, result, base, op2, RegAdd);}
void ARMXEmitter::STRB (ARMReg result, ARMReg base, Operand2 op2, bool RegAdd) { WriteStoreOp(2, result, base, op2, RegAdd);}

#define VA_TO_REGLIST(RegList, Regnum) \
{ \
	u8 Reg; \
	va_list vl; \
	va_start(vl, Regnum); \
	for (int i = 0; i < Regnum; i++) \
	{ \
		Reg = va_arg(vl, u32); \
		RegList |= (1 << Reg); \
	} \
	va_end(vl); \
}

void ARMXEmitter::WriteRegStoreOp(u32 op, ARMReg dest, bool WriteBack, u16 RegList)
{
	Write32(condition | (op << 20) | (WriteBack << 21) | (dest << 16) | RegList);
}
void ARMXEmitter::WriteVRegStoreOp(u32 op, ARMReg Rn, bool Double, bool WriteBack, ARMReg Vd, u8 numregs)
{
	_dbg_assert_msg_(!WriteBack || Rn != R_PC, "VLDM/VSTM cannot use WriteBack with PC (PC is deprecated anyway.)");
	Write32(condition | (op << 20) | (WriteBack << 21) | (Rn << 16) | EncodeVd(Vd) | ((0xA | (int)Double) << 8) | (numregs << (int)Double));
}
void ARMXEmitter::STMFD(ARMReg dest, bool WriteBack, const int Regnum, ...)
{
	u16 RegList = 0;
	VA_TO_REGLIST(RegList, Regnum);
	WriteRegStoreOp(0x80 | 0x10 | 0, dest, WriteBack, RegList);
}
void ARMXEmitter::LDMFD(ARMReg dest, bool WriteBack, const int Regnum, ...)
{
	u16 RegList = 0;
	VA_TO_REGLIST(RegList, Regnum);
	WriteRegStoreOp(0x80 | 0x08 | 1, dest, WriteBack, RegList);
}
void ARMXEmitter::STMIA(ARMReg dest, bool WriteBack, const int Regnum, ...)
{
	u16 RegList = 0;
	VA_TO_REGLIST(RegList, Regnum);
	WriteRegStoreOp(0x80 | 0x08 | 0, dest, WriteBack, RegList);
}
void ARMXEmitter::LDMIA(ARMReg dest, bool WriteBack, const int Regnum, ...)
{
	u16 RegList = 0;
	VA_TO_REGLIST(RegList, Regnum);
	WriteRegStoreOp(0x80 | 0x08 | 1, dest, WriteBack, RegList);
}
void ARMXEmitter::STM(ARMReg dest, bool Add, bool Before, bool WriteBack, const int Regnum, ...)
{
	u16 RegList = 0;
	VA_TO_REGLIST(RegList, Regnum);
	WriteRegStoreOp(0x80 | (Before << 4) | (Add << 3) | 0, dest, WriteBack, RegList);
}
void ARMXEmitter::LDM(ARMReg dest, bool Add, bool Before, bool WriteBack, const int Regnum, ...)
{
	u16 RegList = 0;
	VA_TO_REGLIST(RegList, Regnum);
	WriteRegStoreOp(0x80 | (Before << 4) | (Add << 3) | 1, dest, WriteBack, RegList);
}

void ARMXEmitter::STMBitmask(ARMReg dest, bool Add, bool Before, bool WriteBack, const u16 RegList)
{
	WriteRegStoreOp(0x80 | (Before << 4) | (Add << 3) | 0, dest, WriteBack, RegList);
}
void ARMXEmitter::LDMBitmask(ARMReg dest, bool Add, bool Before, bool WriteBack, const u16 RegList)
{
	WriteRegStoreOp(0x80 | (Before << 4) | (Add << 3) | 1, dest, WriteBack, RegList);
}

#undef VA_TO_REGLIST

// NEON Specific
void ARMXEmitter::VABD(IntegerSize Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_assert_msg_(Vd >= D0, "Pass invalid register to VABD(float)");
	_assert_msg_(cpu_info.bNEON, "Can't use VABD(float) when CPU doesn't support it");
	bool register_quad = Vd >= Q0;

	// Gets encoded as a double register
	Vd = SubBase(Vd);
	Vn = SubBase(Vn);
	Vm = SubBase(Vm);

	Write32((0xF3 << 24) | ((Vd & 0x10) << 18) | (Size << 20) | ((Vn & 0xF) << 16) \
		| ((Vd & 0xF) << 12) | (0xD << 8) | ((Vn & 0x10) << 3) | (register_quad << 6) \
		| ((Vm & 0x10) << 2) | (Vm & 0xF));
}
void ARMXEmitter::VADD(IntegerSize Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_assert_msg_(Vd >= D0, "Pass invalid register to VADD(integer)");
	_assert_msg_(cpu_info.bNEON, "Can't use VADD(integer) when CPU doesn't support it");

	bool register_quad = Vd >= Q0;

	// Gets encoded as a double register
	Vd = SubBase(Vd);
	Vn = SubBase(Vn);
	Vm = SubBase(Vm);

	Write32((0xF2 << 24) | ((Vd & 0x10) << 18) | (Size << 20) | ((Vn & 0xF) << 16) \
		| ((Vd & 0xF) << 12) | (0x8 << 8) | ((Vn & 0x10) << 3) | (register_quad << 6) \
		| ((Vm & 0x10) << 1) | (Vm & 0xF));

}
void ARMXEmitter::VSUB(IntegerSize Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_assert_msg_(Vd >= Q0, "Pass invalid register to VSUB(integer)");
	_assert_msg_(cpu_info.bNEON, "Can't use VSUB(integer) when CPU doesn't support it");

	// Gets encoded as a double register
	Vd = SubBase(Vd);
	Vn = SubBase(Vn);
	Vm = SubBase(Vm);

	Write32((0xF3 << 24) | ((Vd & 0x10) << 18) | (Size << 20) | ((Vn & 0xF) << 16) \
		| ((Vd & 0xF) << 12) | (0x8 << 8) | ((Vn & 0x10) << 3) | (1 << 6) \
		| ((Vm & 0x10) << 2) | (Vm & 0xF));
}

extern const VFPEnc VFPOps[16][2] = {
	{{0xE0, 0xA0}, {  -1,   -1}}, // 0: VMLA
	{{0xE1, 0xA4}, {  -1,   -1}}, // 1: VNMLA
	{{0xE0, 0xA4}, {  -1,   -1}}, // 2: VMLS
	{{0xE1, 0xA0}, {  -1,   -1}}, // 3: VNMLS
	{{0xE3, 0xA0}, {  -1,   -1}}, // 4: VADD
	{{0xE3, 0xA4}, {  -1,   -1}}, // 5: VSUB
	{{0xE2, 0xA0}, {  -1,   -1}}, // 6: VMUL
	{{0xE2, 0xA4}, {  -1,   -1}}, // 7: VNMUL
	{{0xEB, 0xAC}, {  -1 /* 0x3B */,  -1 /* 0x70 */}}, // 8: VABS(Vn(0x0) used for encoding)
	{{0xE8, 0xA0}, {  -1,   -1}}, // 9: VDIV
	{{0xEB, 0xA4}, {  -1 /* 0x3B */,   -1 /* 0x78 */}}, // 10: VNEG(Vn(0x1) used for encoding)
	{{0xEB, 0xAC}, {  -1,   -1}}, // 11: VSQRT (Vn(0x1) used for encoding)
	{{0xEB, 0xA4}, {  -1,   -1}}, // 12: VCMP (Vn(0x4 | #0 ? 1 : 0) used for encoding)
	{{0xEB, 0xAC}, {  -1,   -1}}, // 13: VCMPE (Vn(0x4 | #0 ? 1 : 0) used for encoding)
	{{  -1,   -1}, {0x3B, 0x30}}, // 14: VABSi
	};

const char *VFPOpNames[16] = {
	"VMLA",
	"VNMLA",
	"VMLS",
	"VNMLS",
	"VADD",
	"VSUB",
	"VMUL",
	"VNMUL",
	"VABS",
	"VDIV",
	"VNEG",
	"VSQRT",
	"VCMP",
	"VCMPE",
	"VABSi",
};

u32 EncodeVd(ARMReg Vd)
{
	bool quad_reg = Vd >= Q0;
	bool double_reg = Vd >= D0;

	ARMReg Reg = SubBase(Vd);

	if (quad_reg)
		return ((Reg & 0x10) << 18) | ((Reg & 0xF) << 12);
	else {
		if (double_reg)
			return ((Reg & 0x10) << 18) | ((Reg & 0xF) << 12);
		else
			return ((Reg & 0x1) << 22) | ((Reg & 0x1E) << 11);
	}
}
u32 EncodeVn(ARMReg Vn)
{
	bool quad_reg = Vn >= Q0;
	bool double_reg = Vn >= D0;

	ARMReg Reg = SubBase(Vn);
	if (quad_reg)
		return ((Reg & 0xF) << 16) | ((Reg & 0x10) << 3);
	else {
		if (double_reg)
			return ((Reg & 0xF) << 16) | ((Reg & 0x10) << 3);
		else
			return ((Reg & 0x1E) << 15) | ((Reg & 0x1) << 7);
	}
}
u32 EncodeVm(ARMReg Vm)
{
	bool quad_reg = Vm >= Q0;
	bool double_reg = Vm >= D0;

	ARMReg Reg = SubBase(Vm);

	if (quad_reg)
		return ((Reg & 0x10) << 1) | (Reg & 0xF);
	else {
		if (double_reg)
			return ((Reg & 0x10) << 1) | (Reg & 0xF);
		else
			return ((Reg & 0x1) << 5) | (Reg >> 1);
	}
}

u32 encodedSize(u32 value)
{
	if (value & I_8)
		return 0;
	else if (value & I_16)
		return 1;
	else if ((value & I_32) || (value & F_32))
		return 2;
	else if (value & I_64)
		return 3;
	else
		_dbg_assert_msg_(false, "Passed invalid size to integer NEON instruction");
	return 0;
}

ARMReg SubBase(ARMReg Reg)
{
	if (Reg >= S0)
	{
		if (Reg >= D0)
		{
			if (Reg >= Q0)
				return (ARMReg)((Reg - Q0) * 2); // Always gets encoded as a double register
			return (ARMReg)(Reg - D0);
		}
		return (ARMReg)(Reg - S0);
	}
	return Reg;
}

ARMReg DScalar(ARMReg dreg, int subScalar) {
	int dr = (int)(SubBase(dreg)) & 0xF;
	int scalar = ((subScalar << 4) | dr);
	ARMReg ret =  (ARMReg)(D0 + scalar);
	// ILOG("Scalar: %i D0: %i AR: %i", scalar, (int)D0, (int)ret);
	return ret;
}

// Convert to a DScalar
ARMReg QScalar(ARMReg qreg, int subScalar) {
	int dr = (int)(SubBase(qreg)) & 0xF;
	if (subScalar & 2) {
		dr++;
	}
	int scalar = (((subScalar & 1) << 4) | dr);
	ARMReg ret =  (ARMReg)(D0 + scalar);
	return ret;
}

void ARMXEmitter::WriteVFPDataOp(u32 Op, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	bool quad_reg = Vd >= Q0;
	bool double_reg = Vd >= D0 && Vd < Q0;

	VFPEnc enc = VFPOps[Op][quad_reg];
	if (enc.opc1 == -1 && enc.opc2 == -1)
		_assert_msg_(false, "%s does not support %s", VFPOpNames[Op], quad_reg ? "NEON" : "VFP"); 
	u32 VdEnc = EncodeVd(Vd);
	u32 VnEnc = EncodeVn(Vn);
	u32 VmEnc = EncodeVm(Vm);
	u32 cond = quad_reg ? (0xF << 28) : condition;

	Write32(cond | (enc.opc1 << 20) | VnEnc | VdEnc | (enc.opc2 << 4) | (quad_reg << 6) | (double_reg << 8) | VmEnc);
}
void ARMXEmitter::VMLA(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(0, Vd, Vn, Vm); }
void ARMXEmitter::VNMLA(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(1, Vd, Vn, Vm); }
void ARMXEmitter::VMLS(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(2, Vd, Vn, Vm); }
void ARMXEmitter::VNMLS(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(3, Vd, Vn, Vm); }
void ARMXEmitter::VADD(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(4, Vd, Vn, Vm); }
void ARMXEmitter::VSUB(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(5, Vd, Vn, Vm); }
void ARMXEmitter::VMUL(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(6, Vd, Vn, Vm); }
void ARMXEmitter::VNMUL(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(7, Vd, Vn, Vm); }
void ARMXEmitter::VABS(ARMReg Vd, ARMReg Vm){ WriteVFPDataOp(8, Vd, D0, Vm); }
void ARMXEmitter::VDIV(ARMReg Vd, ARMReg Vn, ARMReg Vm){ WriteVFPDataOp(9, Vd, Vn, Vm); }
void ARMXEmitter::VNEG(ARMReg Vd, ARMReg Vm){ WriteVFPDataOp(10, Vd, D1, Vm); }
void ARMXEmitter::VSQRT(ARMReg Vd, ARMReg Vm){ WriteVFPDataOp(11, Vd, D1, Vm); }
void ARMXEmitter::VCMP(ARMReg Vd, ARMReg Vm){ WriteVFPDataOp(12, Vd, D4, Vm); }
void ARMXEmitter::VCMPE(ARMReg Vd, ARMReg Vm){ WriteVFPDataOp(13, Vd, D4, Vm); }
void ARMXEmitter::VCMP(ARMReg Vd){ WriteVFPDataOp(12, Vd, D5, D0); }
void ARMXEmitter::VCMPE(ARMReg Vd){ WriteVFPDataOp(13, Vd, D5, D0); }

void ARMXEmitter::VLDMIA(ARMReg ptr, bool WriteBack, ARMReg firstvreg, int numvregs)
{
	WriteVRegStoreOp(0x80 | 0x40 | 0x8 | 1, ptr, firstvreg >= D0, WriteBack, firstvreg, numvregs);
}

void ARMXEmitter::VSTMIA(ARMReg ptr, bool WriteBack, ARMReg firstvreg, int numvregs)
{
	WriteVRegStoreOp(0x80 | 0x40 | 0x8, ptr, firstvreg >= D0, WriteBack, firstvreg, numvregs);
}

void ARMXEmitter::VLDMDB(ARMReg ptr, bool WriteBack, ARMReg firstvreg, int numvregs)
{
	_dbg_assert_msg_(WriteBack, "Writeback is required for VLDMDB");
	WriteVRegStoreOp(0x80 | 0x040 | 0x10 | 1, ptr, firstvreg >= D0, WriteBack, firstvreg, numvregs);
}

void ARMXEmitter::VSTMDB(ARMReg ptr, bool WriteBack, ARMReg firstvreg, int numvregs)
{
	_dbg_assert_msg_(WriteBack, "Writeback is required for VSTMDB");
	WriteVRegStoreOp(0x80 | 0x040 | 0x10, ptr, firstvreg >= D0, WriteBack, firstvreg, numvregs);
}

void ARMXEmitter::VLDR(ARMReg Dest, ARMReg Base, s16 offset)
{
	_assert_msg_(Dest >= S0 && Dest <= D31, "Passed Invalid dest register to VLDR");
	_assert_msg_(Base <= R15, "Passed invalid Base register to VLDR");

	bool Add = offset >= 0 ? true : false;
	u32 imm = abs(offset);

	_assert_msg_((imm & 0xC03) == 0, "VLDR: Offset needs to be word aligned and small enough");

	if (imm & 0xC03)
		ERROR_LOG(JIT, "VLDR: Bad offset %08x", imm);

	bool single_reg = Dest < D0;

	Dest = SubBase(Dest);

	if (single_reg)
	{
		Write32(condition | (0xD << 24) | (Add << 23) | ((Dest & 0x1) << 22) | (1 << 20) | (Base << 16) \
			| ((Dest & 0x1E) << 11) | (10 << 8) | (imm >> 2));
	}
	else
	{
		Write32(condition | (0xD << 24) | (Add << 23) | ((Dest & 0x10) << 18) | (1 << 20) | (Base << 16) \
			| ((Dest & 0xF) << 12) | (11 << 8) | (imm >> 2));
	}
}
void ARMXEmitter::VSTR(ARMReg Src, ARMReg Base, s16 offset)
{
	_assert_msg_(Src >= S0 && Src <= D31, "Passed invalid src register to VSTR");
	_assert_msg_(Base <= R15, "Passed invalid base register to VSTR");

	bool Add = offset >= 0 ? true : false;
	u32 imm = abs(offset);

	_assert_msg_((imm & 0xC03) == 0, "VSTR: Offset needs to be word aligned and small enough");

	if (imm & 0xC03)
		ERROR_LOG(JIT, "VSTR: Bad offset %08x", imm);

	bool single_reg = Src < D0;

	Src = SubBase(Src);

	if (single_reg)
	{
		Write32(condition | (0xD << 24) | (Add << 23) | ((Src & 0x1) << 22) | (Base << 16) \
			| ((Src & 0x1E) << 11) | (10 << 8) | (imm >> 2));
	}
	else
	{
		Write32(condition | (0xD << 24) | (Add << 23) | ((Src & 0x10) << 18) | (Base << 16) \
			| ((Src & 0xF) << 12) | (11 << 8) | (imm >> 2));
	}
}

void ARMXEmitter::VMRS_APSR() {
	Write32(condition | 0x0EF10A10 | (15 << 12));
}
void ARMXEmitter::VMRS(ARMReg Rt) {
	Write32(condition | (0xEF << 20) | (1 << 16) | (Rt << 12) | 0xA10);
}
void ARMXEmitter::VMSR(ARMReg Rt) {
	Write32(condition | (0xEE << 20) | (1 << 16) | (Rt << 12) | 0xA10);
}

void ARMXEmitter::VMOV(ARMReg Dest, Operand2 op2)
{
	_assert_msg_(cpu_info.bVFPv3, "VMOV #imm requires VFPv3");
	int sz = Dest >= D0 ? (1 << 8) : 0;
	Write32(condition | (0xEB << 20) | EncodeVd(Dest) | (5 << 9) | sz | op2.Imm8VFP());
}

void ARMXEmitter::VMOV_neon(u32 Size, ARMReg Vd, u32 imm)
{
	_assert_msg_(cpu_info.bNEON, "VMOV_neon #imm requires NEON");
	_assert_msg_(Vd >= D0, "VMOV_neon #imm must target a double or quad");
	bool register_quad = Vd >= Q0;

	int cmode = 0;
	int op = 0;
	Operand2 op2 = IMM(0);

	u32 imm8 = imm & 0xFF;
	imm8 = imm8 | (imm8 << 8) | (imm8 << 16) | (imm8 << 24);

	if (Size == I_8) {
		imm = imm8;
	} else if (Size == I_16) {
		imm &= 0xFFFF;
		imm = imm | (imm << 16);
	}

	if ((imm & 0x000000FF) == imm) {
		op = 0;
		cmode = 0 << 1;
		op2 = IMM(imm);
	} else if ((imm & 0x0000FF00) == imm) {
		op = 0;
		cmode = 1 << 1;
		op2 = IMM(imm >> 8);
	} else if ((imm & 0x00FF0000) == imm) {
		op = 0;
		cmode = 2 << 1;
		op2 = IMM(imm >> 16);
	} else if ((imm & 0xFF000000) == imm) {
		op = 0;
		cmode = 3 << 1;
		op2 = IMM(imm >> 24);
	} else if ((imm & 0x00FF00FF) == imm && (imm >> 16) == (imm & 0x00FF)) {
		op = 0;
		cmode = 4 << 1;
		op2 = IMM(imm & 0xFF);
	} else if ((imm & 0xFF00FF00) == imm && (imm >> 16) == (imm & 0xFF00)) {
		op = 0;
		cmode = 5 << 1;
		op2 = IMM(imm & 0xFF);
	} else if ((imm & 0x0000FFFF) == (imm | 0x000000FF)) {
		op = 0;
		cmode = (6 << 1) | 0;
		op2 = IMM(imm >> 8);
	} else if ((imm & 0x00FFFFFF) == (imm | 0x0000FFFF)) {
		op = 0;
		cmode = (6 << 1) | 1;
		op2 = IMM(imm >> 16);
	} else if (imm == imm8) {
		op = 0;
		cmode = (7 << 1) | 0;
		op2 = IMM(imm & 0xFF);
	} else if (TryMakeFloatIMM8(imm, op2)) {
		op = 0;
		cmode = (7 << 1) | 1;
	} else {
		// 64-bit constant form - technically we could take a u64.
		bool canEncode = true;
		u8 imm8 = 0;
		for (int i = 0, i8 = 0; i < 32; i += 8, ++i8) {
			u8 b = (imm >> i) & 0xFF;
			if (b == 0xFF) {
				imm8 |= 1 << i8;
			} else if (b != 0x00) {
				canEncode = false;
			}
		}
		if (canEncode) {
			// We don't want zeros in the second lane.
			op = 1;
			cmode = 7 << 1;
			op2 = IMM(imm8 | (imm8 << 4));
		} else {
			_assert_msg_(false, "VMOV_neon #imm invalid constant value");
		}
	}

	// No condition allowed.
	Write32((15 << 28) | (0x28 << 20) | EncodeVd(Vd) | (cmode << 8) | (register_quad << 6) | (op << 5) | (1 << 4) | op2.Imm8ASIMD());
}

void ARMXEmitter::VMOV_neon(u32 Size, ARMReg Vd, ARMReg Rt, int lane)
{
	_assert_msg_(cpu_info.bNEON, "VMOV_neon requires NEON");

	int opc1 = 0;
	int opc2 = 0;

	switch (Size & ~(I_SIGNED | I_UNSIGNED))
	{
	case I_8: opc1 = 2 | (lane >> 2); opc2 = lane & 3; break;
	case I_16: opc1 = lane >> 1; opc2 = 1 | ((lane & 1) << 1); break;
	case I_32:
	case F_32:
		_assert_msg_((Size & I_UNSIGNED) == 0, "Cannot use UNSIGNED for I_32 or F_32");
		opc1 = lane & 1;
		break;
	default:
		_assert_msg_(false, "VMOV_neon unsupported size");
	}

	if (Vd < S0 && Rt >= D0 && Rt < Q0)
	{
		// Oh, reading to reg, our params are backwards.
		ARMReg Src = Rt;
		ARMReg Dest = Vd;

		_dbg_assert_msg_((Size & (I_UNSIGNED | I_SIGNED | F_32 | I_32)) != 0, "Must specify I_SIGNED or I_UNSIGNED in VMOV, unless F_32/I_32");
		int U = (Size & I_UNSIGNED) ? (1 << 23) : 0;

		Write32(condition | (0xE1 << 20) | U | (opc1 << 21) | EncodeVn(Src) | (Dest << 12) | (0xB << 8) | (opc2 << 5) | (1 << 4));
	}
	else if (Rt < S0 && Vd >= D0 && Vd < Q0)
	{
		ARMReg Src = Rt;
		ARMReg Dest = Vd;
		Write32(condition | (0xE0 << 20) | (opc1 << 21) | EncodeVn(Dest) | (Src << 12) | (0xB << 8) | (opc2 << 5) | (1 << 4));
	}
	else
		_assert_msg_(false, "VMOV_neon unsupported arguments (Dx -> Rx or Rx -> Dx)");
}

void ARMXEmitter::VMOV(ARMReg Vd, ARMReg Rt, ARMReg Rt2)
{
	_assert_msg_(cpu_info.bVFP | cpu_info.bNEON, "VMOV_neon requires VFP or NEON");

	if (Vd < S0 && Rt < S0 && Rt2 >= D0)
	{
		// Oh, reading to regs, our params are backwards.
		ARMReg Src = Rt2;
		ARMReg Dest1 = Vd;
		ARMReg Dest2 = Rt;
		Write32(condition | (0xC5 << 20) | (Dest2 << 16) | (Dest1 << 12) | (0xB << 8) | EncodeVm(Src) | (1 << 4));
	}
	else if (Vd >= D0 && Rt < S0 && Rt2 < S0)
	{
		ARMReg Dest = Vd;
		ARMReg Src1 = Rt;
		ARMReg Src2 = Rt2;
		Write32(condition | (0xC4 << 20) | (Src2 << 16) | (Src1 << 12) | (0xB << 8) | EncodeVm(Dest) | (1 << 4));
	}
	else
		_assert_msg_(false, "VMOV_neon requires either Dm, Rt, Rt2 or Rt, Rt2, Dm.");
}

void ARMXEmitter::VMOV(ARMReg Dest, ARMReg Src, bool high)
{
	_assert_msg_(Src < S0, "This VMOV doesn't support SRC other than ARM Reg");
	_assert_msg_(Dest >= D0, "This VMOV doesn't support DEST other than VFP");

	Dest = SubBase(Dest);

	Write32(condition | (0xE << 24) | (high << 21) | ((Dest & 0xF) << 16) | (Src << 12) \
		| (0xB << 8) | ((Dest & 0x10) << 3) | (1 << 4));
}

void ARMXEmitter::VMOV(ARMReg Dest, ARMReg Src)
{
	if (Dest == Src) {
		WARN_LOG(JIT, "VMOV %s, %s - same register", ARMRegAsString(Src), ARMRegAsString(Dest));
	}
	if (Dest > R15)
	{
		if (Src < S0)
		{
			if (Dest < D0)
			{
				// Moving to a Neon register FROM ARM Reg
				Dest = (ARMReg)(Dest - S0); 
				Write32(condition | (0xE0 << 20) | ((Dest & 0x1E) << 15) | (Src << 12) \
						| (0xA << 8) | ((Dest & 0x1) << 7) | (1 << 4));
				return;
			}
			else
			{
				// Move 64bit from Arm reg
				_assert_msg_(false, "This VMOV doesn't support moving 64bit ARM to NEON");
				return;
			}
		}
	}
	else
	{
		if (Src > R15)
		{
			if (Src < D0)
			{
				// Moving to ARM Reg from Neon Register
				Src = (ARMReg)(Src - S0);
				Write32(condition | (0xE1 << 20) | ((Src & 0x1E) << 15) | (Dest << 12) \
						| (0xA << 8) | ((Src & 0x1) << 7) | (1 << 4));
				return;
			}
			else
			{
				// Move 64bit To Arm reg
				_assert_msg_(false, "This VMOV doesn't support moving 64bit ARM From NEON");
				return;
			}
		}
		else
		{
			// Move Arm reg to Arm reg
			_assert_msg_(false, "VMOV doesn't support moving ARM registers");
		}
	}
	// Moving NEON registers
	int SrcSize = Src < D0 ? 1 : Src < Q0 ? 2 : 4;
	int DestSize = Dest < D0 ? 1 : Dest < Q0 ? 2 : 4;
	bool Single = DestSize == 1;
	bool Quad = DestSize == 4;

	_assert_msg_(SrcSize == DestSize, "VMOV doesn't support moving different register sizes");
	if (SrcSize != DestSize) {
		ERROR_LOG(JIT, "SrcSize: %i (%s)  DestDize: %i (%s)", SrcSize, ARMRegAsString(Src), DestSize, ARMRegAsString(Dest));
	}

	Dest = SubBase(Dest);
	Src = SubBase(Src);

	if (Single)
	{
		Write32(condition | (0x1D << 23) | ((Dest & 0x1) << 22) | (0x3 << 20) | ((Dest & 0x1E) << 11) \
				| (0x5 << 9) | (1 << 6) | ((Src & 0x1) << 5) | ((Src & 0x1E) >> 1));
	}
	else
	{
		// Double and quad
		if (Quad)
		{
			_assert_msg_(cpu_info.bNEON, "Trying to use quad registers when you don't support ASIMD."); 
			// Gets encoded as a Double register
			Write32((0xF2 << 24) | ((Dest & 0x10) << 18) | (2 << 20) | ((Src & 0xF) << 16) \
				| ((Dest & 0xF) << 12) | (1 << 8) | ((Src & 0x10) << 3) | (1 << 6) \
				| ((Src & 0x10) << 1) | (1 << 4) | (Src & 0xF));

		}
		else
		{
			Write32(condition | (0x1D << 23) | ((Dest & 0x10) << 18) | (0x3 << 20) | ((Dest & 0xF) << 12) \
				| (0x2D << 6) | ((Src & 0x10) << 1) | (Src & 0xF));
		}
	}
}

void ARMXEmitter::VCVT(ARMReg Dest, ARMReg Source, int flags)
{
	bool single_reg = (Dest < D0) && (Source < D0);
	bool single_double = !single_reg && (Source < D0 || Dest < D0);
	bool single_to_double = Source < D0;
	int op  = ((flags & TO_INT) ? (flags & ROUND_TO_ZERO) : (flags & IS_SIGNED)) ? 1 : 0;
	int op2 = ((flags & TO_INT) ? (flags & IS_SIGNED) : 0) ? 1 : 0;
	Dest = SubBase(Dest);
	Source = SubBase(Source);

	if (single_double)
	{
		// S32<->F64
		if (flags & TO_INT)
		{
			if (single_to_double)
			{
				Write32(condition | (0x1D << 23) | ((Dest & 0x10) << 18) | (0x7 << 19) \
					| ((Dest & 0xF) << 12) | (op << 7) | (0x2D << 6) | ((Source & 0x1) << 5) | (Source >> 1));
			} else {
				Write32(condition | (0x1D << 23) | ((Dest & 0x1) << 22) | (0x7 << 19) | ((flags & TO_INT) << 18) | (op2 << 16) \
					| ((Dest & 0x1E) << 11) | (op << 7) | (0x2D << 6) | ((Source & 0x10) << 1) | (Source & 0xF));
			}
		}
		// F32<->F64
		else {
			if (single_to_double)
			{
				Write32(condition | (0x1D << 23) | ((Dest & 0x10) << 18) | (0x3 << 20) | (0x7 << 16) \
					| ((Dest & 0xF) << 12) | (0x2F << 6) | ((Source & 0x1) << 5) | (Source >> 1));
			} else {
				Write32(condition | (0x1D << 23) | ((Dest & 0x1) << 22) | (0x3 << 20) | (0x7 << 16) \
					| ((Dest & 0x1E) << 11) | (0x2B << 6) | ((Source & 0x10) << 1) | (Source & 0xF));
			}
		}
	} else if (single_reg) {
		Write32(condition | (0x1D << 23) | ((Dest & 0x1) << 22) | (0x7 << 19) | ((flags & TO_INT) << 18) | (op2 << 16) \
			| ((Dest & 0x1E) << 11) | (op << 7) | (0x29 << 6) | ((Source & 0x1) << 5) | (Source >> 1));
	} else {
		Write32(condition | (0x1D << 23) | ((Dest & 0x10) << 18) | (0x7 << 19) | ((flags & TO_INT) << 18) | (op2 << 16) \
			| ((Dest & 0xF) << 12) | (1 << 8) | (op << 7) | (0x29 << 6) | ((Source & 0x10) << 1) | (Source & 0xF));
	}
}

void ARMXEmitter::VABA(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);
	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | ((Size & I_UNSIGNED ? 1 : 0) << 24) | EncodeVn(Vn) \
		| (encodedSize(Size) << 20) | EncodeVd(Vd) | (0x71 << 4) | (register_quad << 6) | EncodeVm(Vm));
}

void ARMXEmitter::VABAL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vn >= D0 && Vn < Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vm >= D0 && Vm < Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);

	Write32((0xF2 << 24) | ((Size & I_UNSIGNED ? 1 : 0) << 24) | (1 << 23) | EncodeVn(Vn) \
		| (encodedSize(Size) << 20) | EncodeVd(Vd) | (0x50 << 4) | EncodeVm(Vm));
}

void ARMXEmitter::VABD(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	bool register_quad = Vd >= Q0;

	if (Size & F_32)
		Write32((0xF3 << 24) | (1 << 21) | EncodeVn(Vn) | EncodeVd(Vd) | (0xD << 8) | EncodeVm(Vm));
	else
		Write32((0xF2 << 24) | ((Size & I_UNSIGNED ? 1 : 0) << 24) | EncodeVn(Vn) \
			| (encodedSize(Size) << 20) | EncodeVd(Vd) | (0x70 << 4) | (register_quad << 6) | EncodeVm(Vm));
}

void ARMXEmitter::VABDL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vn >= D0 && Vn < Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vm >= D0 && Vm < Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);

	Write32((0xF2 << 24) | ((Size & I_UNSIGNED ? 1 : 0) << 24) | (1 << 23) | EncodeVn(Vn) \
		| (encodedSize(Size) << 20) | EncodeVd(Vd) | (0x70 << 4) | EncodeVm(Vm));
}

void ARMXEmitter::VABS(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (0xB1 << 16) | (encodedSize(Size) << 18) | EncodeVd(Vd) \
		| ((Size & F_32 ? 1 : 0) << 10) | (0x30 << 4) | (register_quad << 6) | EncodeVm(Vm));
}

void ARMXEmitter::VACGE(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	// Only Float
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | EncodeVn(Vn) | EncodeVd(Vd) \
		| (0xD1 << 4) | (register_quad << 6) | EncodeVm(Vm));
}

void ARMXEmitter::VACGT(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	// Only Float
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (1 << 21) | EncodeVn(Vn) | EncodeVd(Vd) \
		| (0xD1 << 4) | (register_quad << 6) | EncodeVm(Vm));
}

void ARMXEmitter::VACLE(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	VACGE(Vd, Vm, Vn);
}

void ARMXEmitter::VACLT(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	VACGT(Vd, Vn, Vm);
}

void ARMXEmitter::VADD(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	if (Size & F_32)
		Write32((0xF2 << 24) | EncodeVn(Vn) | EncodeVd(Vd) | (0xD << 8) | (register_quad << 6) | EncodeVm(Vm));
	else
		Write32((0xF2 << 24) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) \
			| (0x8 << 8) | (register_quad << 6) | EncodeVm(Vm));
}

void ARMXEmitter::VADDHN(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd < Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vn >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vm >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);

	Write32((0xF2 << 24) | (1 << 23) | (encodedSize(Size) << 20) | EncodeVn(Vn) \
		| EncodeVd(Vd) | (0x80 << 4) | EncodeVm(Vm));
}

void ARMXEmitter::VADDL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vn >= D0 && Vn < Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vm >= D0 && Vm < Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);

	Write32((0xF2 << 24) | ((Size & I_UNSIGNED ? 1 : 0) << 24) | (1 << 23) | (encodedSize(Size) << 20) | EncodeVn(Vn) \
		| EncodeVd(Vd) | EncodeVm(Vm));
}
void ARMXEmitter::VADDW(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vn >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vm >= D0 && Vm < Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);

	Write32((0xF2 << 24) | ((Size & I_UNSIGNED ? 1 : 0) << 24) | (1 << 23) | (encodedSize(Size) << 20) | EncodeVn(Vn) \
		| EncodeVd(Vd) | (1 << 8) | EncodeVm(Vm));
}
void ARMXEmitter::VAND(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Vd == Vn && Vn == Vm), "All operands the same for %s is a nop", __FUNCTION__);
	// _dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);
	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | EncodeVn(Vn) | EncodeVd(Vd) | (0x11 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VBIC(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	//  _dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);
	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (1 << 20) | EncodeVn(Vn) | EncodeVd(Vd) | (0x11 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VEOR(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s: %i", __FUNCTION__, Vd);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	bool register_quad = Vd >= Q0;	

	Write32((0xF3 << 24) | EncodeVn(Vn) | EncodeVd(Vd) | (0x11 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VBIF(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	// _dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);
	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (3 << 20) | EncodeVn(Vn) | EncodeVd(Vd) | (0x11 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VBIT(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	// _dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);
	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (2 << 20) | EncodeVn(Vn) | EncodeVd(Vd) | (0x11 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VBSL(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	// _dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);
	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (1 << 20) | EncodeVn(Vn) | EncodeVd(Vd) | (0x11 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VCEQ(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;
	if (Size & F_32)
		Write32((0xF2 << 24) | EncodeVn(Vn) | EncodeVd(Vd) | (0xE0 << 4) | (register_quad << 6) | EncodeVm(Vm));
	else
		Write32((0xF3 << 24) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) \
			| (0x81 << 4) | (register_quad << 6) | EncodeVm(Vm));

}
void ARMXEmitter::VCEQ(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (0xB << 20) | (encodedSize(Size) << 18) | (1 << 16) \
		| EncodeVd(Vd) | ((Size & F_32 ? 1 : 0) << 10) | (0x10 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VCGE(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;
	if (Size & F_32)
		Write32((0xF3 << 24) | EncodeVn(Vn) | EncodeVd(Vd) | (0xE0 << 4) | (register_quad << 6) | EncodeVm(Vm));
	else
		Write32((0xF2 << 24) | ((Size & I_UNSIGNED ? 1 : 0) << 24)  | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) \
			| (0x31 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VCGE(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;
	Write32((0xF3 << 24) | (0xB << 20) | (encodedSize(Size) << 18) | (1 << 16) \
		| EncodeVd(Vd) | ((Size & F_32 ? 1 : 0) << 10) | (0x8 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VCGT(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;
	if (Size & F_32)
		Write32((0xF3 << 24) | (1 << 21) | EncodeVn(Vn) | EncodeVd(Vd) | (0xE0 << 4) | (register_quad << 6) | EncodeVm(Vm));
	else
		Write32((0xF2 << 24) | ((Size & I_UNSIGNED ? 1 : 0) << 24)  | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) \
			| (0x30 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VCGT(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;
	Write32((0xF3 << 24) | (0xD << 20) | (encodedSize(Size) << 18) | (1 << 16) \
		| EncodeVd(Vd) | ((Size & F_32 ? 1 : 0) << 10) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VCLE(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	VCGE(Size, Vd, Vm, Vn);
}
void ARMXEmitter::VCLE(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;
	Write32((0xF3 << 24) | (0xD << 20) | (encodedSize(Size) << 18) | (1 << 16) \
		| EncodeVd(Vd) | ((Size & F_32 ? 1 : 0) << 10) | (3 << 7) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VCLS(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);

	bool register_quad = Vd >= Q0;
	Write32((0xF3 << 24) | (0xD << 20) | (encodedSize(Size) << 18) \
		| EncodeVd(Vd) | (1 << 10) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VCLT(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	VCGT(Size, Vd, Vm, Vn);
}
void ARMXEmitter::VCLT(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;
	Write32((0xF3 << 24) | (0xD << 20) | (encodedSize(Size) << 18) | (1 << 16) \
		| EncodeVd(Vd) | ((Size & F_32 ? 1 : 0) << 10) | (0x20 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VCLZ(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;
	Write32((0xF3 << 24) | (0xD << 20) | (encodedSize(Size) << 18) \
		| EncodeVd(Vd) | (0x48 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VCNT(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(Size & I_8, "Can only use I_8 with %s", __FUNCTION__);

	bool register_quad = Vd >= Q0;
	Write32((0xF3 << 24) | (0xD << 20) | (encodedSize(Size) << 18) \
		| EncodeVd(Vd) | (0x90 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VDUP(u32 Size, ARMReg Vd, ARMReg Vm, u8 index)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vm >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;
	u32 imm4 = 0;
	if (Size & I_8)
		imm4 = (index << 1) | 1;
	else if (Size & I_16)
		imm4 = (index << 2) | 2;
	else if (Size & (I_32 | F_32))
		imm4 = (index << 3) | 4;
	Write32((0xF3 << 24) | (0xB << 20) | (imm4 << 16)  \
		| EncodeVd(Vd) | (0xC << 8) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VDUP(u32 Size, ARMReg Vd, ARMReg Rt)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Rt < S0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;
	Vd = SubBase(Vd);
	u8 sizeEncoded = 0;
	if (Size & I_8)
		sizeEncoded = 2;
	else if (Size & I_16)
		sizeEncoded = 1;
	else if (Size & I_32)
		sizeEncoded = 0;

	Write32((0xEE << 24) | (0x8 << 20) | ((sizeEncoded & 2) << 21) | (register_quad << 21) \
		| ((Vd & 0xF) << 16) | (Rt << 12) | (0xB1 << 4) | ((Vd & 0x10) << 3) | ((sizeEncoded & 1) << 5));
}
void ARMXEmitter::VEXT(ARMReg Vd, ARMReg Vn, ARMReg Vm, u8 index)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (0xB << 20) | EncodeVn(Vn) | EncodeVd(Vd) | (index & 0xF) \
		| (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VFMA(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Size == F_32, "Passed invalid size to FP-only NEON instruction");
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bVFPv4, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | EncodeVn(Vn) | EncodeVd(Vd) | (0xC1 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VFMS(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Size == F_32, "Passed invalid size to FP-only NEON instruction");
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bVFPv4, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (1 << 21) | EncodeVn(Vn) | EncodeVd(Vd) | (0xC1 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VHADD(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (((Size & I_UNSIGNED) ? 1 : 0) << 23) | (encodedSize(Size) << 20) \
		| EncodeVn(Vn) | EncodeVd(Vd) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VHSUB(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (((Size & I_UNSIGNED) ? 1 : 0) << 23) | (encodedSize(Size) << 20) \
		| EncodeVn(Vn) | EncodeVd(Vd) | (1 << 9) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VMAX(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	if (Size & F_32)
		Write32((0xF2 << 24) | EncodeVn(Vn) | EncodeVd(Vd) | (0xF0 << 4) | (register_quad << 6) | EncodeVm(Vm));
	else
		Write32((0xF2 << 24) | (((Size & I_UNSIGNED) ? 1 : 0) << 23) | (encodedSize(Size) << 20) \
			| EncodeVn(Vn) | EncodeVd(Vd) | (0x60 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VMIN(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	if (Size & F_32)
		Write32((0xF2 << 24) | (2 << 20) | EncodeVn(Vn) | EncodeVd(Vd) | (0xF0 << 4) | (register_quad << 6) | EncodeVm(Vm));
	else
		Write32((0xF2 << 24) | (((Size & I_UNSIGNED) ? 1 : 0) << 23) | (encodedSize(Size) << 20) \
			| EncodeVn(Vn) | EncodeVd(Vd) | (0x61 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VMLA(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	if (Size & F_32)
		Write32((0xF2 << 24) | EncodeVn(Vn) | EncodeVd(Vd) | (0xD1 << 4) | (register_quad << 6) | EncodeVm(Vm));
	else
		Write32((0xF2 << 24) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | (0x90 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VMLS(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	if (Size & F_32)
		Write32((0xF2 << 24) | (1 << 21) | EncodeVn(Vn) | EncodeVd(Vd) | (0xD1 << 4) | (register_quad << 6) | EncodeVm(Vm));
	else
		Write32((0xF2 << 24) | (1 << 24) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | (0x90 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VMLAL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vn >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vm >= D0 && Vm < Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);

	Write32((0xF2 << 24) | ((Size & I_UNSIGNED ? 1 : 0) << 24) | (encodedSize(Size) << 20) \
		| EncodeVn(Vn) | EncodeVd(Vd) | (0x80 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VMLSL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vn >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vm >= D0 && Vm < Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float.", __FUNCTION__);

	Write32((0xF2 << 24) | ((Size & I_UNSIGNED ? 1 : 0) << 24) | (encodedSize(Size) << 20) \
		| EncodeVn(Vn) | EncodeVd(Vd) | (0xA0 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VMUL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	if (Size & F_32)
		Write32((0xF3 << 24) | EncodeVn(Vn) | EncodeVd(Vd) | (0xD1 << 4) | (register_quad << 6) | EncodeVm(Vm));
	else
		Write32((0xF2 << 24) | ((Size & I_POLYNOMIAL) ? (1 << 24) : 0) | (encodedSize(Size) << 20) | \
				EncodeVn(Vn) | EncodeVd(Vd) | (0x91 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VMULL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	Write32((0xF2 << 24) | (1 << 23) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0xC0 << 4) | ((Size & I_POLYNOMIAL) ? 1 << 9 : 0) | EncodeVm(Vm));
}
void ARMXEmitter::VMLA_scalar(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	// No idea if the Non-Q case here works. Not really that interested.
	if (Size & F_32)
		Write32((0xF2 << 24) | (register_quad << 24) | (1 << 23) | (2 << 20) | EncodeVn(Vn) | EncodeVd(Vd) | (0x14 << 4) | EncodeVm(Vm));
	else
		_dbg_assert_msg_(false, "VMLA_scalar only supports float atm");
	//else
	//	Write32((0xF2 << 24) | (1 << 23) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | (0x90 << 4) | (1 << 6) | EncodeVm(Vm));
	// Unsigned support missing
}
void ARMXEmitter::VMUL_scalar(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	int VmEnc = EncodeVm(Vm);
	// No idea if the Non-Q case here works. Not really that interested.
	if (Size & F_32)  // Q flag
		Write32((0xF2 << 24) | (register_quad << 24) | (1 << 23) | (2 << 20) | EncodeVn(Vn) | EncodeVd(Vd) | (0x94 << 4) | VmEnc);
	else
		_dbg_assert_msg_(false, "VMUL_scalar only supports float atm");
	
		// Write32((0xF2 << 24) | ((Size & I_POLYNOMIAL) ? (1 << 24) : 0) | (1 << 23) | (encodedSize(Size) << 20) | 
		// EncodeVn(Vn) | EncodeVd(Vd) | (0x84 << 4) | (register_quad << 6) | EncodeVm(Vm));
	// Unsigned support missing
}

void ARMXEmitter::VMVN(ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF3B << 20)	| \
		EncodeVd(Vd) | (0xB << 7) | (register_quad << 6) | EncodeVm(Vm));
}

void ARMXEmitter::VNEG(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (0xB << 20) | (encodedSize(Size) << 18) | (1 << 16) | \
			EncodeVd(Vd) | ((Size & F_32) ? 1 << 10 : 0) | (0xE << 6) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VORN(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (3 << 20) | EncodeVn(Vn) | EncodeVd(Vd) | (0x11 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VORR(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Vd == Vn && Vn == Vm), "All operands the same for %s is a nop", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (2 << 20) | EncodeVn(Vn) | EncodeVd(Vd) | (0x11 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VPADAL(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (0xB << 20) | (encodedSize(Size) << 18) | EncodeVd(Vd) | \
			(0x60 << 4) | ((Size & I_UNSIGNED) ? 1 << 7 : 0) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VPADD(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	if (Size & F_32)
		Write32((0xF3 << 24) | EncodeVn(Vn) | EncodeVd(Vd) | (0xD0 << 4) | EncodeVm(Vm));
	else
		Write32((0xF2 << 24) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
				(0xB1 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VPADDL(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (0xB << 20) | (encodedSize(Size) << 18) | EncodeVd(Vd) | \
			(0x20 << 4) | (Size & I_UNSIGNED ? 1 << 7 : 0) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VPMAX(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	if (Size & F_32)
		Write32((0xF3 << 24) | EncodeVn(Vn) | EncodeVd(Vd) | (0xF0 << 4) | EncodeVm(Vm));
	else
		Write32((0xF2 << 24) | (Size & I_UNSIGNED ? 1 << 24 : 0) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
				(0xA0 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VPMIN(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	if (Size & F_32)
		Write32((0xF3 << 24) | (1 << 21) | EncodeVn(Vn) | EncodeVd(Vd) | (0xF0 << 4) | EncodeVm(Vm));
	else
		Write32((0xF2 << 24) | (Size & I_UNSIGNED ? 1 << 24 : 0) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
				(0xA1 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VQABS(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (0xB << 20) | (encodedSize(Size) << 18) | EncodeVd(Vd) | \
			(0x70 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VQADD(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x1 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VQDMLAL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	Write32((0xF2 << 24) | (1 << 23) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x90 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VQDMLSL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	Write32((0xF2 << 24) | (1 << 23) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0xB0 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VQDMULH(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	Write32((0xF2 << 24) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0xB0 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VQDMULL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	Write32((0xF2 << 24) | (1 << 23) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0xD0 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VQNEG(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (0xB << 20) | (encodedSize(Size) << 18) | EncodeVd(Vd) | \
			(0x78 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VQRDMULH(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	Write32((0xF3 << 24) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0xB0 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VQRSHL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (Size & I_UNSIGNED ? 1 << 24 : 0) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x51 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VQSHL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (Size & I_UNSIGNED ? 1 << 24 : 0) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x41 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VQSUB(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (Size & I_UNSIGNED ? 1 << 24 : 0) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x21 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VRADDHN(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	Write32((0xF3 << 24) | (1 << 23) | ((encodedSize(Size) - 1) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x40 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VRECPE(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (0xB << 20) | (0xB << 16) | EncodeVd(Vd) | \
			(0x40 << 4) | (Size & F_32 ? 1 << 8 : 0) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VRECPS(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | EncodeVn(Vn) | EncodeVd(Vd) | (0xF1 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VRHADD(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (Size & I_UNSIGNED ? 1 << 24 : 0) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x10 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VRSHL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (Size & I_UNSIGNED ? 1 << 24 : 0) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x50 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VRSQRTE(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;
	Vd = SubBase(Vd);
	Vm = SubBase(Vm);

	Write32((0xF3 << 24) | (0xB << 20) | ((Vd & 0x10) << 18) | (0xB << 16)
			| ((Vd & 0xF) << 12) | (9 << 7) | (Size & F_32 ? (1 << 8) : 0) | (register_quad << 6)
			| ((Vm & 0x10) << 1) | (Vm & 0xF));
}
void ARMXEmitter::VRSQRTS(ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (1 << 21) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0xF1 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VRSUBHN(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	Write32((0xF3 << 24) | (1 << 23) | ((encodedSize(Size) - 1) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x60 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VSHL(u32 Size, ARMReg Vd, ARMReg Vm, ARMReg Vn)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (Size & I_UNSIGNED ? 1 << 24 : 0) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x40 << 4) | (register_quad << 6) | EncodeVm(Vm));
}

static int EncodeSizeShift(u32 Size, int amount, bool inverse, bool halve) {
	int sz = 0;
	switch (Size & 0xF) {
	case I_8: sz = 8; break;
	case I_16: sz = 16; break;
	case I_32: sz = 32; break;
	case I_64: sz = 64; break;
	}
	if (inverse && halve) {
		_dbg_assert_msg_(amount <= sz / 2, "Amount %d too large for narrowing shift (max %d)", amount, sz/2);
		return (sz / 2) + (sz / 2) - amount;
	} else if (inverse) {
		return sz + (sz - amount);
	} else {
		return sz + amount;
	}
}

void ARMXEmitter::EncodeShiftByImm(u32 Size, ARMReg Vd, ARMReg Vm, int shiftAmount, u8 opcode, bool register_quad, bool inverse, bool halve) {
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_(!(Size & F_32), "%s doesn't support float", __FUNCTION__);
	int imm7 = EncodeSizeShift(Size, shiftAmount, inverse, halve);
	int L = (imm7 >> 6) & 1;
	int U = (Size & I_UNSIGNED) ? 1 : 0;
	u32 value = (0xF2 << 24) | (U << 24) | (1 << 23) | ((imm7 & 0x3f) << 16) | EncodeVd(Vd) | (opcode << 8) | (L << 7) | (register_quad << 6) | (1 << 4) | EncodeVm(Vm);
	Write32(value);
}

void ARMXEmitter::VSHL(u32 Size, ARMReg Vd, ARMReg Vm, int shiftAmount) {
	EncodeShiftByImm((Size & ~I_UNSIGNED), Vd, Vm, shiftAmount, 0x5, Vd >= Q0, false, false);
}

void ARMXEmitter::VSHLL(u32 Size, ARMReg Vd, ARMReg Vm, int shiftAmount) {
	if ((u32)shiftAmount == (8 * (Size & 0xF))) {
		// Entirely different encoding (A2) for size == shift! Bleh.
		int sz = 0;
		switch (Size & 0xF) {
		case I_8: sz = 0; break;
		case I_16: sz = 1; break;
		case I_32: sz = 2; break;
		case I_64:
			_dbg_assert_msg_(false, "Cannot VSHLL 64-bit elements");
		}
		int imm6 = 0x32 | (sz << 2);
		u32 value = (0xF3 << 24) | (1 << 23) | (imm6 << 16) | EncodeVd(Vd) | (0x3 << 8) | EncodeVm(Vm);
		Write32(value);
	} else {
		EncodeShiftByImm((Size & ~I_UNSIGNED), Vd, Vm, shiftAmount, 0xA, false, false, false);
	}
}

void ARMXEmitter::VSHR(u32 Size, ARMReg Vd, ARMReg Vm, int shiftAmount) {
	EncodeShiftByImm(Size, Vd, Vm, shiftAmount, 0x0, Vd >= Q0, true, false);
}

void ARMXEmitter::VSHRN(u32 Size, ARMReg Vd, ARMReg Vm, int shiftAmount) {
	// Reduce Size by 1 to encode correctly.
	EncodeShiftByImm(Size, Vd, Vm, shiftAmount, 0x8, false, true, true);
}

void ARMXEmitter::VSUB(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	if (Size & F_32)
		Write32((0xF2 << 24) | (1 << 21) | EncodeVn(Vn) | EncodeVd(Vd) | \
				(0xD0 << 4) | (register_quad << 6) | EncodeVm(Vm));
	else
		Write32((0xF3 << 24) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
				(0x80 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VSUBHN(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	Write32((0xF2 << 24) | (1 << 23) | ((encodedSize(Size) - 1) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x60 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VSUBL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	Write32((0xF2 << 24) | (Size & I_UNSIGNED ? 1 << 24 : 0) | (1 << 23) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x20 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VSUBW(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	Write32((0xF2 << 24) | (Size & I_UNSIGNED ? 1 << 24 : 0) | (1 << 23) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x30 << 4) | EncodeVm(Vm));
}
void ARMXEmitter::VSWP(ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (0xB << 20) | (1 << 17) | EncodeVd(Vd) | \
			(register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VTRN(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (0xB << 20) | (encodedSize(Size) << 18) | (1 << 17) | EncodeVd(Vd) | \
			(1 << 7) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VTST(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF2 << 24) | (encodedSize(Size) << 20) | EncodeVn(Vn) | EncodeVd(Vd) | \
			(0x81 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VUZP(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (0xB << 20) | (encodedSize(Size) << 18) | (1 << 17) | EncodeVd(Vd) | \
			(0x10 << 4) | (register_quad << 6) | EncodeVm(Vm));
}
void ARMXEmitter::VZIP(u32 Size, ARMReg Vd, ARMReg Vm)
{
	 _dbg_assert_msg_(Vd >= D0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);

	bool register_quad = Vd >= Q0;

	Write32((0xF3 << 24) | (0xB << 20) | (encodedSize(Size) << 18) | (1 << 17) | EncodeVd(Vd) | \
			(0x18 << 4) | (register_quad << 6) | EncodeVm(Vm));
}

void ARMXEmitter::VMOVL(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vd >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vm >= D0 && Vm <= D31, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_((Size & (I_UNSIGNED | I_SIGNED)) != 0, "Must specify I_SIGNED or I_UNSIGNED in VMOVL");

	bool unsign = (Size & I_UNSIGNED) != 0;
	int imm3 = 0;
	if (Size & I_8) imm3 = 1;
	if (Size & I_16) imm3 = 2;
	if (Size & I_32) imm3 = 4;

	Write32((0xF2 << 24) | (unsign << 24) | (1 << 23) | (imm3 << 19) | EncodeVd(Vd) | \
		(0xA1 << 4) | EncodeVm(Vm));
}

void ARMXEmitter::VMOVN(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vm >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vd >= D0 && Vd <= D31, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_((Size & I_8) == 0, "%s cannot narrow from I_8", __FUNCTION__);

	// For consistency with assembler syntax and VMOVL - encode one size down.
	int halfSize = encodedSize(Size) - 1;

	Write32((0xF3B << 20) | (halfSize << 18) | (1 << 17) | EncodeVd(Vd) | (1 << 9) | EncodeVm(Vm));
}

void ARMXEmitter::VQMOVN(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vm >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vd >= D0 && Vd <= D31, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_((Size & (I_UNSIGNED | I_SIGNED)) != 0, "Must specify I_SIGNED or I_UNSIGNED in %s NEON", __FUNCTION__);
	_dbg_assert_msg_((Size & I_8) == 0, "%s cannot narrow from I_8", __FUNCTION__);

	int halfSize = encodedSize(Size) - 1;
	int op = (1 << 7) | (Size & I_UNSIGNED ? 1 << 6 : 0);

	Write32((0xF3B << 20) | (halfSize << 18) | (1 << 17) | EncodeVd(Vd) | (1 << 9) | op | EncodeVm(Vm));
}

void ARMXEmitter::VQMOVUN(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_(Vm >= Q0, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(Vd >= D0 && Vd <= D31, "Pass invalid register to %s", __FUNCTION__);
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	_dbg_assert_msg_((Size & I_8) == 0, "%s cannot narrow from I_8", __FUNCTION__);

	int halfSize = encodedSize(Size) - 1;
	int op = (1 << 6);

	Write32((0xF3B << 20) | (halfSize << 18) | (1 << 17) | EncodeVd(Vd) | (1 << 9) | op | EncodeVm(Vm));
}

void ARMXEmitter::VCVT(u32 Size, ARMReg Vd, ARMReg Vm)
{
	_dbg_assert_msg_((Size & (I_UNSIGNED | I_SIGNED)) != 0, "Must specify I_SIGNED or I_UNSIGNED in VCVT NEON");

	bool register_quad = Vd >= Q0;
	bool toInteger = (Size & I_32) != 0;
	bool isUnsigned = (Size & I_UNSIGNED) != 0;
	int op = (toInteger << 1) | (int)isUnsigned;

	Write32((0xF3 << 24) | (0xBB << 16) | EncodeVd(Vd) | (0x3 << 9) | (op << 7) | (register_quad << 6) | EncodeVm(Vm));
}

static int RegCountToType(int nRegs, NEONAlignment align) {
	switch (nRegs) {
	case 1:
		_dbg_assert_msg_(!((int)align & 1), "align & 1 must be == 0");
		return 7;
	case 2:
		_dbg_assert_msg_(!((int)align == 3), "align must be != 3");
		return 10;
	case 3:
		_dbg_assert_msg_(!((int)align & 1), "align & 1 must be == 0");
		return 6;
	case 4:
		return 2;
	default:
		_dbg_assert_msg_(false, "Invalid number of registers passed to vector load/store");
		return 0;
	}
}

void ARMXEmitter::WriteVLDST1(bool load, u32 Size, ARMReg Vd, ARMReg Rn, int regCount, NEONAlignment align, ARMReg Rm)
{
	u32 spacing = RegCountToType(regCount, align); // Only support loading to 1 reg
	// Gets encoded as a double register
	Vd = SubBase(Vd);

	Write32((0xF4 << 24) | ((Vd & 0x10) << 18) | (load << 21) | (Rn << 16)
			| ((Vd & 0xF) << 12) | (spacing << 8) | (encodedSize(Size) << 6)
			| (align << 4) | Rm);
}

void ARMXEmitter::VLD1(u32 Size, ARMReg Vd, ARMReg Rn, int regCount, NEONAlignment align, ARMReg Rm) {
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	WriteVLDST1(true, Size, Vd, Rn, regCount, align, Rm);
}

void ARMXEmitter::VST1(u32 Size, ARMReg Vd, ARMReg Rn, int regCount, NEONAlignment align, ARMReg Rm) {
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	WriteVLDST1(false, Size, Vd, Rn, regCount, align, Rm);
}

void ARMXEmitter::WriteVLDST1_lane(bool load, u32 Size, ARMReg Vd, ARMReg Rn, int lane, bool aligned, ARMReg Rm) 
{
	bool register_quad = Vd >= Q0;

	Vd = SubBase(Vd);
	// Support quad lanes by converting to D lanes
	if (register_quad && lane > 1) {
		Vd = (ARMReg)((int)Vd + 1);
		lane -= 2;
	}
	int encSize = encodedSize(Size);
	int index_align = 0;
	switch (encSize) {
	case 0: index_align = lane << 1; break;
	case 1: index_align = lane << 2; if (aligned) index_align |= 1; break;
	case 2: index_align = lane << 3; if (aligned) index_align |= 3; break;
	default:
		break;
	}

	Write32((0xF4 << 24) | (1 << 23) | ((Vd & 0x10) << 18) | (load << 21) | (Rn << 16)
		| ((Vd & 0xF) << 12) | (encSize << 10)
		| (index_align << 4) | Rm);
}

void ARMXEmitter::VLD1_lane(u32 Size, ARMReg Vd, ARMReg Rn, int lane, bool aligned, ARMReg Rm) {
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	WriteVLDST1_lane(true, Size, Vd, Rn, lane, aligned, Rm);
}

void ARMXEmitter::VST1_lane(u32 Size, ARMReg Vd, ARMReg Rn, int lane, bool aligned, ARMReg Rm) {
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	WriteVLDST1_lane(false, Size, Vd, Rn, lane, aligned, Rm);
}

void ARMXEmitter::VLD1_all_lanes(u32 Size, ARMReg Vd, ARMReg Rn, bool aligned, ARMReg Rm) {
	bool register_quad = Vd >= Q0;

	Vd = SubBase(Vd);

	int T = register_quad;  // two D registers

	Write32((0xF4 << 24) | (1 << 23) | ((Vd & 0x10) << 18) | (1 << 21) | (Rn << 16)
		| ((Vd & 0xF) << 12) | (0xC << 8) | (encodedSize(Size) << 6)
		| (T << 5) | (aligned << 4) | Rm);
}

/*
void ARMXEmitter::VLD2(u32 Size, ARMReg Vd, ARMReg Rn, int regCount, NEONAlignment align, ARMReg Rm)
{
	u32 spacing = 0x8; // Single spaced registers
	// Gets encoded as a double register
	Vd = SubBase(Vd);

	Write32((0xF4 << 24) | ((Vd & 0x10) << 18) | (1 << 21) | (Rn << 16)
			| ((Vd & 0xF) << 12) | (spacing << 8) | (encodedSize(Size) << 6)
			| (align << 4) | Rm);
}
*/

void ARMXEmitter::WriteVimm(ARMReg Vd, int cmode, u8 imm, int op) {
	bool register_quad = Vd >= Q0;

	Write32((0xF28 << 20) | ((imm >> 7) << 24) | (((imm >> 4) & 0x7) << 16) | (imm & 0xF) |
		      EncodeVd(Vd) | (register_quad << 6) | (op << 5) | (1 << 4) | ((cmode & 0xF) << 8));
}

void ARMXEmitter::VMOV_imm(u32 Size, ARMReg Vd, VIMMMode type, int imm) {
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	// Only let through the modes that apply.
	switch (type) {
	case VIMM___x___x:
	case VIMM__x___x_:
	case VIMM_x___x__:
	case VIMMx___x___:
		if (Size != I_32)
			goto error;
		WriteVimm(Vd, (int)type, imm, 0);
		break;
	case VIMM_x_x_x_x:
	case VIMMx_x_x_x_:
		if (Size != I_16)
			goto error;
		WriteVimm(Vd, (int)type, imm, 0);
		break;
	case VIMMxxxxxxxx:  // replicate the byte
		if (Size != I_8)
			goto error;
		WriteVimm(Vd, (int)type, imm, 0);
		break;
	case VIMMbits2bytes:
		if (Size != I_64)
			goto error;
		WriteVimm(Vd, (int)type, imm, 1);
		break;
	default:
		goto error;
	}
	return;

error:
	_dbg_assert_msg_(false, "Bad Size or type specified in %s: Size %i Type %i", __FUNCTION__, (int)Size, type);
}

void ARMXEmitter::VMOV_immf(ARMReg Vd, float value) {  // This only works with a select few values. I've hardcoded 1.0f.
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	u8 bits = 0;

	if (value == 0.0f) {
		VEOR(Vd, Vd, Vd);
		return;
	}

	// TODO: Do something more sophisticated here.
	if (value == 1.5f) {
		bits = 0x78;
	} else if (value == 1.0f) {
		bits = 0x70;
	} else if (value == -1.0f) {
		bits = 0xF0;
	} else {
		_dbg_assert_msg_(false, "%s: Invalid floating point immediate", __FUNCTION__);
	}
	WriteVimm(Vd, VIMMf000f000, bits, 0);
}

void ARMXEmitter::VORR_imm(u32 Size, ARMReg Vd, VIMMMode type, int imm) {
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	// Only let through the modes that apply.
	switch (type) {
	case VIMM___x___x:
	case VIMM__x___x_:
	case VIMM_x___x__:
	case VIMMx___x___:
		if (Size != I_32)
			goto error;
		WriteVimm(Vd, (int)type | 1, imm, 0);
		break;
	case VIMM_x_x_x_x:
	case VIMMx_x_x_x_:
		if (Size != I_16)
			goto error;
		WriteVimm(Vd, (int)type | 1, imm, 0);
		break;
	default:
		goto error;
	}
	return;
error:
	_dbg_assert_msg_(false, "Bad Size or type specified in VORR_imm");
}

void ARMXEmitter::VBIC_imm(u32 Size, ARMReg Vd, VIMMMode type, int imm) {
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	// Only let through the modes that apply.
	switch (type) {
	case VIMM___x___x:
	case VIMM__x___x_:
	case VIMM_x___x__:
	case VIMMx___x___:
		if (Size != I_32)
			goto error;
		WriteVimm(Vd, (int)type | 1, imm, 1);
		break;
	case VIMM_x_x_x_x:
	case VIMMx_x_x_x_:
		if (Size != I_16)
			goto error;
		WriteVimm(Vd, (int)type | 1, imm, 1);
		break;
	default:
		goto error;
	}
	return;
error:
	_dbg_assert_msg_(false, "Bad Size or type specified in VBIC_imm");
}


void ARMXEmitter::VMVN_imm(u32 Size, ARMReg Vd, VIMMMode type, int imm) {
	_dbg_assert_msg_(cpu_info.bNEON, "Can't use %s when CPU doesn't support it", __FUNCTION__);
	// Only let through the modes that apply.
	switch (type) {
	case VIMM___x___x:
	case VIMM__x___x_:
	case VIMM_x___x__:
	case VIMMx___x___:
		if (Size != I_32)
			goto error;
		WriteVimm(Vd, (int)type, imm, 1);
		break;
	case VIMM_x_x_x_x:
	case VIMMx_x_x_x_:
		if (Size != I_16)
			goto error;
		WriteVimm(Vd, (int)type, imm, 1);
		break;
	default:
		goto error;
	}
	return;
error:
	_dbg_assert_msg_(false, "Bad Size or type specified in VMVN_imm");
}


void ARMXEmitter::VREVX(u32 size, u32 Size, ARMReg Vd, ARMReg Vm)
{
	bool register_quad = Vd >= Q0;
	Vd = SubBase(Vd);
	Vm = SubBase(Vm);

	Write32((0xF3 << 24) | (1 << 23) | ((Vd & 0x10) << 18) | (0x3 << 20)
			| (encodedSize(Size) << 18) | ((Vd & 0xF) << 12) | (size << 7)
			| (register_quad << 6) | ((Vm & 0x10) << 1) | (Vm & 0xF));
}

void ARMXEmitter::VREV64(u32 Size, ARMReg Vd, ARMReg Vm)
{
	VREVX(0, Size, Vd, Vm);
}

void ARMXEmitter::VREV32(u32 Size, ARMReg Vd, ARMReg Vm)
{
	VREVX(1, Size, Vd, Vm);
}

void ARMXEmitter::VREV16(u32 Size, ARMReg Vd, ARMReg Vm)
{
	VREVX(2, Size, Vd, Vm);
}

// See page A8-878 in ARMv7-A Architecture Reference Manual

// Dest is a Q register, Src is a D register.
void ARMXEmitter::VCVTF32F16(ARMReg Dest, ARMReg Src) {
	_assert_msg_(cpu_info.bVFPv4, "Can't use half-float conversions when you don't support VFPv4");
	if (Dest < Q0 || Dest > Q15 || Src < D0 || Src > D15) {
		_assert_msg_(cpu_info.bNEON, "Bad inputs to VCVTF32F16"); 
		// Invalid!
	}

	Dest = SubBase(Dest);
	Src = SubBase(Src);
	
	int op = 1;
	Write32((0xF3B6 << 16) | ((Dest & 0x10) << 18) | ((Dest & 0xF) << 12) | 0x600 | (op << 8) | ((Src & 0x10) << 1) | (Src & 0xF));
}

// UNTESTED
// Dest is a D register, Src is a Q register.
void ARMXEmitter::VCVTF16F32(ARMReg Dest, ARMReg Src) {
	_assert_msg_(cpu_info.bVFPv4, "Can't use half-float conversions when you don't support VFPv4");
	if (Dest < D0 || Dest > D15 || Src < Q0 || Src > Q15) {
		_assert_msg_(cpu_info.bNEON, "Bad inputs to VCVTF32F16"); 
		// Invalid!
	}
	Dest = SubBase(Dest);
	Src = SubBase(Src);
	int op = 0;
	Write32((0xF3B6 << 16) | ((Dest & 0x10) << 18) | ((Dest & 0xF) << 12) | 0x600 | (op << 8) | ((Src & 0x10) << 1) | (Src & 0xF));
}

// Always clear code space with breakpoints, so that if someone accidentally executes
// uninitialized, it just breaks into the debugger.
void ARMXCodeBlock::PoisonMemory(int offset) {
	// TODO: this isn't right for ARM!
	memset(region + offset, 0xCC, region_size - offset);
	ResetCodePtr(offset);
}

}
