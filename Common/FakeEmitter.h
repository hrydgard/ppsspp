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

// WARNING - THIS LIBRARY IS NOT THREAD SAFE!!!

#ifndef _DOLPHIN_FAKE_CODEGEN_
#define _DOLPHIN_FAKE_CODEGEN_

#include <stdint.h>

#include "Common/CommonTypes.h"
#include "Common/CodeBlock.h"

// VCVT flags
#define TO_FLOAT      0
#define TO_INT        1 << 0
#define IS_SIGNED     1 << 1
#define ROUND_TO_ZERO 1 << 2

namespace FakeGen
{
enum FakeReg
{
	// GPRs
	R0 = 0, R1, R2, R3, R4, R5,
	R6, R7, R8, R9, R10, R11,

	// SPRs
	// R13 - R15 are SP, LR, and PC.
	// Almost always referred to by name instead of register number
	R12 = 12, R13 = 13, R14 = 14, R15 = 15,
	R_IP = 12, R_SP = 13, R_LR = 14, R_PC = 15,


	// VFP single precision registers
	S0, S1, S2, S3, S4, S5, S6,
	S7, S8, S9, S10, S11, S12, S13,
	S14, S15, S16, S17, S18, S19, S20,
	S21, S22, S23, S24, S25, S26, S27,
	S28, S29, S30, S31,

	// VFP Double Precision registers
	D0, D1, D2, D3, D4, D5, D6, D7,
	D8, D9, D10, D11, D12, D13, D14, D15,
	D16, D17, D18, D19, D20, D21, D22, D23,
	D24, D25, D26, D27, D28, D29, D30, D31,
	
	// ASIMD Quad-Word registers
	Q0, Q1, Q2, Q3, Q4, Q5, Q6, Q7,
	Q8, Q9, Q10, Q11, Q12, Q13, Q14, Q15,

	// for NEON VLD/VST instructions
	REG_UPDATE = R13,
	INVALID_REG = 0xFFFFFFFF
};

enum CCFlags
{
	CC_EQ = 0, // Equal
	CC_NEQ, // Not equal
	CC_CS, // Carry Set
	CC_CC, // Carry Clear
	CC_MI, // Minus (Negative)
	CC_PL, // Plus
	CC_VS, // Overflow
	CC_VC, // No Overflow
	CC_HI, // Unsigned higher
	CC_LS, // Unsigned lower or same
	CC_GE, // Signed greater than or equal
	CC_LT, // Signed less than
	CC_GT, // Signed greater than
	CC_LE, // Signed less than or equal
	CC_AL, // Always (unconditional) 14
	CC_HS = CC_CS, // Alias of CC_CS  Unsigned higher or same
	CC_LO = CC_CC, // Alias of CC_CC  Unsigned lower
};
const u32 NO_COND = 0xE0000000;

enum ShiftType
{
	ST_LSL = 0,
	ST_ASL = 0,
	ST_LSR = 1,
	ST_ASR = 2,
	ST_ROR = 3,
	ST_RRX = 4
};
enum IntegerSize
{
	I_I8 = 0, 
	I_I16,
	I_I32,
	I_I64
};

enum
{
	NUMGPRs = 13,
};

class FakeXEmitter;

enum OpType
{
	TYPE_IMM = 0,
	TYPE_REG,
	TYPE_IMMSREG,
	TYPE_RSR,
	TYPE_MEM
};

// This is no longer a proper operand2 class. Need to split up.
class Operand2
{
	friend class FakeXEmitter;
protected:
	u32 Value;

private:
	OpType Type;

	// IMM types
	u8	Rotation; // Only for u8 values

	// Register types
	u8 IndexOrShift;
	ShiftType Shift;
public:
	OpType GetType()
	{
		return Type;
	}
	Operand2() {} 
	Operand2(u32 imm, OpType type = TYPE_IMM) : IndexOrShift(), Shift()
	{ 
		Type = type; 
		Value = imm; 
		Rotation = 0;
	}

	Operand2(FakeReg Reg) : IndexOrShift(), Shift()
	{
		Type = TYPE_REG;
		Value = Reg;
		Rotation = 0;
	}
	Operand2(u8 imm, u8 rotation) : IndexOrShift(), Shift()
	{
		Type = TYPE_IMM;
		Value = imm;
		Rotation = rotation;
	}
	Operand2(FakeReg base, ShiftType type, FakeReg shift) : Rotation(0) // RSR
	{
		Type = TYPE_RSR;
		_assert_msg_(type != ST_RRX, "Invalid Operand2: RRX does not take a register shift amount");
		IndexOrShift = shift;
		Shift = type;
		Value = base;
	}

	Operand2(FakeReg base, ShiftType type, u8 shift) : Rotation(0) // For IMM shifted register
	{
		if(shift == 32) shift = 0;
		switch (type)
		{
		case ST_LSL:
			_assert_msg_(shift < 32, "Invalid Operand2: LSL %u", shift);
			break;
		case ST_LSR:
			_assert_msg_(shift <= 32, "Invalid Operand2: LSR %u", shift);
			if (!shift)
				type = ST_LSL;
			if (shift == 32)
				shift = 0;
			break;
		case ST_ASR:
			_assert_msg_(shift < 32, "Invalid Operand2: ASR %u", shift);
			if (!shift)
				type = ST_LSL;
			if (shift == 32)
				shift = 0;
			break;
		case ST_ROR:
			_assert_msg_(shift < 32, "Invalid Operand2: ROR %u", shift);
			if (!shift)
				type = ST_LSL;
			break;
		case ST_RRX:
			_assert_msg_(shift == 0, "Invalid Operand2: RRX does not take an immediate shift amount");
			type = ST_ROR;
			break;
		}
		IndexOrShift = shift;
		Shift = type;
		Value = base;
		Type = TYPE_IMMSREG;
	}
	u32 GetData()
	{
		switch(Type)
		{
		case TYPE_IMM:
			return Imm12Mod(); // This'll need to be changed later
		case TYPE_REG:
			return Rm();
		case TYPE_IMMSREG:
			return IMMSR();
		case TYPE_RSR:
			return RSR();
		default:
			_assert_msg_(false, "GetData with Invalid Type");
			return 0;
		}
	}
	u32 IMMSR() // IMM shifted register
	{
		_assert_msg_(Type == TYPE_IMMSREG, "IMMSR must be imm shifted register");
		return ((IndexOrShift & 0x1f) << 7 | (Shift << 5) | Value);
	}
	u32 RSR() // Register shifted register
	{
		_assert_msg_(Type == TYPE_RSR, "RSR must be RSR Of Course");
		return (IndexOrShift << 8) | (Shift << 5) | 0x10 | Value;
	}
	u32 Rm()
	{
		_assert_msg_(Type == TYPE_REG, "Rm must be with Reg");
		return Value;
	}

	u32 Imm5()
	{
		_assert_msg_(Type == TYPE_IMM, "Imm5 not IMM value");
		return ((Value & 0x0000001F) << 7);
	}
	u32 Imm8()
	{
		_assert_msg_(Type == TYPE_IMM, "Imm8Rot not IMM value");
		return Value & 0xFF;
	}
	u32 Imm8Rot() // IMM8 with Rotation
	{
		_assert_msg_(Type == TYPE_IMM, "Imm8Rot not IMM value");
		_assert_msg_((Rotation & 0xE1) != 0, "Invalid Operand2: immediate rotation %u", Rotation);
		return (1 << 25) | (Rotation << 7) | (Value & 0x000000FF);
	}
	u32 Imm12()
	{
		_assert_msg_(Type == TYPE_IMM, "Imm12 not IMM");
		return (Value & 0x00000FFF);
	}

	u32 Imm12Mod()
	{
		// This is an IMM12 with the top four bits being rotation and the
		// bottom eight being an IMM. This is for instructions that need to
		// expand a 8bit IMM to a 32bit value and gives you some rotation as
		// well.
		// Each rotation rotates to the right by 2 bits
		_assert_msg_(Type == TYPE_IMM, "Imm12Mod not IMM");
		return ((Rotation & 0xF) << 8) | (Value & 0xFF);
	}
	u32 Imm16()
	{
		_assert_msg_(Type == TYPE_IMM, "Imm16 not IMM");
		return ( (Value & 0xF000) << 4) | (Value & 0x0FFF);
	}
	u32 Imm16Low()
	{
		return Imm16();
	}
	u32 Imm16High() // Returns high 16bits
	{
		_assert_msg_(Type == TYPE_IMM, "Imm16 not IMM");
		return ( ((Value >> 16) & 0xF000) << 4) | ((Value >> 16) & 0x0FFF);
	}
	u32 Imm24()
	{
		_assert_msg_(Type == TYPE_IMM, "Imm16 not IMM");
		return (Value & 0x0FFFFFFF);
	}
};

// Use these when you don't know if an imm can be represented as an operand2.
// This lets you generate both an optimal and a fallback solution by checking
// the return value, which will be false if these fail to find a Operand2 that
// represents your 32-bit imm value.
bool TryMakeOperand2(u32 imm, Operand2 &op2);
bool TryMakeOperand2_AllowInverse(u32 imm, Operand2 &op2, bool *inverse);
bool TryMakeOperand2_AllowNegation(s32 imm, Operand2 &op2, bool *negated);

// Use this only when you know imm can be made into an Operand2.
Operand2 AssumeMakeOperand2(u32 imm);

inline Operand2 R(FakeReg Reg)	{ return Operand2(Reg, TYPE_REG); }
inline Operand2 IMM(u32 Imm)	{ return Operand2(Imm, TYPE_IMM); }
inline Operand2 Mem(void *ptr)	{ return Operand2((u32)(uintptr_t)ptr, TYPE_IMM); }
//usage: struct {int e;} s; STRUCT_OFFSET(s,e)
#define STRUCT_OFF(str,elem) ((u32)((u32)&(str).elem-(u32)&(str)))


struct FixupBranch
{
	u8 *ptr;
	u32 condition; // Remembers our codition at the time
	int type; //0 = B 1 = BL
};

typedef const u8* JumpTarget;

// XXX: Stop polluting the global namespace
const u32 I_8 = (1 << 0);
const u32 I_16 = (1 << 1);
const u32 I_32 = (1 << 2);
const u32 I_64 = (1 << 3);
const u32 I_SIGNED = (1 << 4);
const u32 I_UNSIGNED = (1 << 5);
const u32 F_32 = (1 << 6);
const u32 I_POLYNOMIAL = (1 << 7); // Only used in VMUL/VMULL

u32 EncodeVd(FakeReg Vd);
u32 EncodeVn(FakeReg Vn);
u32 EncodeVm(FakeReg Vm);

u32 encodedSize(u32 value);

// Subtracts the base from the register to give us the real one
FakeReg SubBase(FakeReg Reg);

// See A.7.1 in the Fakev7-A
// VMUL F32 scalars can only be up to D15[0], D15[1] - higher scalars cannot be individually addressed
FakeReg DScalar(FakeReg dreg, int subScalar);
FakeReg QScalar(FakeReg qreg, int subScalar);

enum NEONAlignment {
	ALIGN_NONE = 0,
	ALIGN_64 = 1,
	ALIGN_128 = 2,
	ALIGN_256 = 3
};


class NEONXEmitter;

class FakeXEmitter
{
	friend struct OpArg;  // for Write8 etc
private:
	u8 *code, *startcode;
	u8 *lastCacheFlushEnd;
	u32 condition;

protected:
	inline void Write32(u32 value) {*(u32*)code = value; code+=4;}

public:
	FakeXEmitter() : code(0), startcode(0), lastCacheFlushEnd(0) {
		condition = CC_AL << 28;
	}
	FakeXEmitter(u8 *code_ptr) {
		code = code_ptr;
		lastCacheFlushEnd = code_ptr;
		startcode = code_ptr;
		condition = CC_AL << 28;
	}
	virtual ~FakeXEmitter() {}

	void SetCodePointer(u8 *ptr, u8 *writePtr) {}
	const u8 *GetCodePointer() const { return nullptr; }

	void SetCodePtr(u8 *ptr) {}
	void ReserveCodeSpace(u32 bytes) {}
	const u8 *AlignCode16() { return nullptr; }
	const u8 *AlignCodePage() { return nullptr; }
	const u8 *GetCodePtr() const { return nullptr; }
	void FlushIcache() {}
	void FlushIcacheSection(u8 *start, u8 *end) {}
	u8 *GetWritableCodePtr() { return nullptr; }

	CCFlags GetCC() { return CCFlags(condition >> 28); }
	void SetCC(CCFlags cond = CC_AL) {}

	// Special purpose instructions

	// Do nothing
	void NOP(int count = 1) {} //nop padding - TODO: fast nop slides, for amd and intel (check their manuals)

#ifdef CALL
#undef CALL
#endif

	void QuickCallFunction(FakeReg scratchreg, const void *func);
	template <typename T> void QuickCallFunction(FakeReg scratchreg, T func) {
		QuickCallFunction(scratchreg, (const void *)func);
	}
};  // class FakeXEmitter


// Everything that needs to generate machine code should inherit from this.
// You get memory management for free, plus, you can use all the MOV etc functions without
// having to prefix them with gen-> or something similar.
class FakeXCodeBlock : public CodeBlock<FakeXEmitter> {
public:
	void PoisonMemory(int offset) override {
	}
};

}  // namespace

#endif // _DOLPHIN_FAKE_CODEGEN_
