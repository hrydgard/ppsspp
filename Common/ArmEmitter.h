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

#pragma once

#include <vector>
#include <cstdint>

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include "Common/ArmCommon.h"
#include "Common/CodeBlock.h"

// VCVT flags
#define TO_FLOAT      0
#define TO_INT        1 << 0
#define IS_SIGNED     1 << 1
#define ROUND_TO_ZERO 1 << 2

namespace ArmGen
{
enum ARMReg
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

class ARMXEmitter;

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
	friend class ARMXEmitter;
protected:
	u32 Value;

private:
	OpType Type;

	// IMM types
	u8	Rotation = 0; // Only for u8 values

	// Register types
	u8 IndexOrShift = 0;
	ShiftType Shift = ST_LSL;
public:
	OpType GetType() const {
		return Type;
	}
	Operand2() {
		Type = TYPE_IMM;
		Value = 0;
	}
	Operand2(u32 imm, OpType type = TYPE_IMM) {
		Type = type;
		Value = imm;
	}

	Operand2(ARMReg Reg) {
		Type = TYPE_REG;
		Value = Reg;
	}
	Operand2(u8 imm, u8 rotation) {
		Type = TYPE_IMM;
		Value = imm;
		Rotation = rotation;
	}
	Operand2(ARMReg base, ShiftType type, ARMReg shift) // RSR
	{
		Type = TYPE_RSR;
		_assert_msg_(type != ST_RRX, "Invalid Operand2: RRX does not take a register shift amount");
		IndexOrShift = shift;
		Shift = type;
		Value = base;
	}

	Operand2(ARMReg base, ShiftType type, u8 shift)// For IMM shifted register
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
	u32 Rm() const
	{
		_assert_msg_(Type == TYPE_REG, "Rm must be with Reg");
		return Value;
	}

	u32 Imm5() const
	{
		_assert_msg_((Type == TYPE_IMM), "Imm5 not IMM value");
		return ((Value & 0x0000001F) << 7);
	}
	u32 Imm8() const
	{
		_assert_msg_((Type == TYPE_IMM), "Imm8Rot not IMM value");
		return Value & 0xFF;
	}
	u32 Imm8Rot() const // IMM8 with Rotation
	{
		_assert_msg_((Type == TYPE_IMM), "Imm8Rot not IMM value");
		_assert_msg_((Rotation & 0xE1) != 0, "Invalid Operand2: immediate rotation %u", Rotation);
		return (1 << 25) | (Rotation << 7) | (Value & 0x000000FF);
	}
	u32 Imm12() const
	{
		_assert_msg_((Type == TYPE_IMM), "Imm12 not IMM");
		return (Value & 0x00000FFF);
	}

	u32 Imm12Mod() const
	{
		// This is an IMM12 with the top four bits being rotation and the
		// bottom eight being an IMM. This is for instructions that need to
		// expand a 8bit IMM to a 32bit value and gives you some rotation as
		// well.
		// Each rotation rotates to the right by 2 bits
		_assert_msg_((Type == TYPE_IMM), "Imm12Mod not IMM");
		return ((Rotation & 0xF) << 8) | (Value & 0xFF);
	}
	u32 Imm16() const
	{
		_assert_msg_((Type == TYPE_IMM), "Imm16 not IMM");
		return ( (Value & 0xF000) << 4) | (Value & 0x0FFF);
	}
	u32 Imm16Low() const
	{
		return Imm16();
	}
	u32 Imm16High() const // Returns high 16bits
	{
		_assert_msg_((Type == TYPE_IMM), "Imm16 not IMM");
		return ( ((Value >> 16) & 0xF000) << 4) | ((Value >> 16) & 0x0FFF);
	}
	u32 Imm24() const
	{
		_assert_msg_((Type == TYPE_IMM), "Imm16 not IMM");
		return (Value & 0x0FFFFFFF);
	}
	// NEON and ASIMD specific
	u32 Imm8ASIMD() const
	{
		_assert_msg_((Type == TYPE_IMM), "Imm8ASIMD not IMM");
		return  ((Value & 0x80) << 17) | ((Value & 0x70) << 12) | (Value & 0xF);
	}
	u32 Imm8VFP() const
	{
		_assert_msg_((Type == TYPE_IMM), "Imm8VFP not IMM");
		return ((Value & 0xF0) << 12) | (Value & 0xF);
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

inline Operand2 R(ARMReg Reg)	{ return Operand2(Reg, TYPE_REG); }
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

struct LiteralPool
{
	intptr_t loc;
	u8* ldr_address;
	u32 val;
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

enum VIMMMode {
	VIMM___x___x = 0x0, // 0000 VMOV
	VIMM__x___x_ = 0x2, // 0010
	VIMM_x___x__ = 0x4, // 0100
	VIMMx___x___ = 0x6, // 0110
	VIMM_x_x_x_x = 0x8, // 1000
	VIMMx_x_x_x_ = 0xA, // 1010
	VIMM__x1__x1 = 0xC, // 1100
	VIMM_x11_x11 = 0xD, // 1101
	VIMMxxxxxxxx = 0xE, // 1110  // op == 0
	VIMMf000f000 = 0xF, // 1111  // op == 0     ( really   aBbbbbbc defgh 00000000 00000000 ) where B = NOT b
	VIMMbits2bytes = 0x1E,   // Bit replication into bytes! Easily created 111111111 00000000 masks!
};

u32 EncodeVd(ARMReg Vd);
u32 EncodeVn(ARMReg Vn);
u32 EncodeVm(ARMReg Vm);

u32 encodedSize(u32 value);

// Subtracts the base from the register to give us the real one
ARMReg SubBase(ARMReg Reg);

inline bool IsQ(ARMReg r) {
	return r >= Q0 && r <= Q15;
}

inline bool IsD(ARMReg r) {
	return r >= D0 && r <= D31;
}

// See A.7.1 in the ARMv7-A
// VMUL F32 scalars can only be up to D15[0], D15[1] - higher scalars cannot be individually addressed
ARMReg DScalar(ARMReg dreg, int subScalar);
ARMReg QScalar(ARMReg qreg, int subScalar);
inline ARMReg XScalar(ARMReg reg, int subScalar) {
	if (IsQ(reg))
		return QScalar(reg, subScalar);
	else
		return DScalar(reg, subScalar);
}

const char *ARMRegAsString(ARMReg reg);

// Get the two halves of a Q register.
inline ARMReg D_0(ARMReg q) {
	if (q >= Q0 && q <= Q15) {
		return ARMReg(D0 + (q - Q0) * 2);
	} else if (q >= D0 && q <= D31) {
		return q;
	} else {
		return INVALID_REG;
	}
}
inline ARMReg D_1(ARMReg q) {
	return ARMReg(D0 + (q - Q0) * 2 + 1);
}

enum NEONAlignment {
	ALIGN_NONE = 0,
	ALIGN_64 = 1,
	ALIGN_128 = 2,
	ALIGN_256 = 3
};


class NEONXEmitter;

class ARMXEmitter
{
	friend struct OpArg;  // for Write8 etc
	friend class NEONXEmitter;
private:
	u8 *code, *startcode;
	u8 *lastCacheFlushEnd;
	u32 condition;
	std::vector<LiteralPool> currentLitPool;

	void WriteStoreOp(u32 Op, ARMReg Rt, ARMReg Rn, Operand2 op2, bool RegAdd);
	void WriteRegStoreOp(u32 op, ARMReg dest, bool WriteBack, u16 RegList);
	void WriteVRegStoreOp(u32 op, ARMReg dest, bool Double, bool WriteBack, ARMReg firstreg, u8 numregs);
	void WriteShiftedDataOp(u32 op, bool SetFlags, ARMReg dest, ARMReg src, ARMReg op2);
	void WriteShiftedDataOp(u32 op, bool SetFlags, ARMReg dest, ARMReg src, Operand2 op2);
	void WriteSignedMultiply(u32 Op, u32 Op2, u32 Op3, ARMReg dest, ARMReg r1, ARMReg r2);

	void WriteVFPDataOp(u32 Op, ARMReg Vd, ARMReg Vn, ARMReg Vm);

	void Write4OpMultiply(u32 op, ARMReg destLo, ARMReg destHi, ARMReg rn, ARMReg rm);

	// New Ops
	void WriteInstruction(u32 op, ARMReg Rd, ARMReg Rn, Operand2 Rm, bool SetFlags = false);

	void WriteVLDST1(bool load, u32 Size, ARMReg Vd, ARMReg Rn, int regCount, NEONAlignment align, ARMReg Rm);
	void WriteVLDST1_lane(bool load, u32 Size, ARMReg Vd, ARMReg Rn, int lane, bool aligned, ARMReg Rm);

	void WriteVimm(ARMReg Vd, int cmode, u8 imm, int op);

	void EncodeShiftByImm(u32 Size, ARMReg Vd, ARMReg Vm, int shiftAmount, u8 opcode, bool quad, bool inverse, bool halve);

protected:
	inline void Write32(u32 value) {*(u32*)code = value; code+=4;}

public:
	ARMXEmitter() : code(0), startcode(0), lastCacheFlushEnd(0) {
		condition = CC_AL << 28;
	}
	ARMXEmitter(u8 *code_ptr) {
		code = code_ptr;
		lastCacheFlushEnd = code_ptr;
		startcode = code_ptr;
		condition = CC_AL << 28;
	}
	virtual ~ARMXEmitter() {}

	void SetCodePointer(u8 *ptr, u8 *writePtr);
	const u8 *GetCodePointer() const;

	void ReserveCodeSpace(u32 bytes);
	const u8 *AlignCode16();
	const u8 *AlignCodePage();
	const u8 *NopAlignCode16();

	void FlushIcache();
	void FlushIcacheSection(u8 *start, u8 *end);
	u8 *GetWritableCodePtr();

	void FlushLitPool();
	void AddNewLit(u32 val);
	bool TrySetValue_TwoOp(ARMReg reg, u32 val);

	CCFlags GetCC() const { return CCFlags(condition >> 28); }
	void SetCC(CCFlags cond = CC_AL);

	// Special purpose instructions

	// Dynamic Endian Switching
	void SETEND(bool BE);
	// Debug Breakpoint
	void BKPT(u16 arg);

	// Hint instruction
	void YIELD();

	// Do nothing
	void NOP(int count = 1); //nop padding - TODO: fast nop slides, for amd and intel (check their manuals)

#ifdef CALL
#undef CALL
#endif

	// Branching
	FixupBranch B();
	FixupBranch B_CC(CCFlags Cond);
	void B_CC(CCFlags Cond, const void *fnptr);
	FixupBranch BL();
	FixupBranch BL_CC(CCFlags Cond);
	void SetJumpTarget(FixupBranch const &branch);

	void B (const void *fnptr);
	void B (ARMReg src);
	void BL(const void *fnptr);
	void BL(ARMReg src);
	bool BLInRange(const void *fnptr) const;

	void PUSH(const int num, ...);
	void POP(const int num, ...);

	// New Data Ops
	void AND (ARMReg Rd, ARMReg Rn, Operand2 Rm);
	void ANDS(ARMReg Rd, ARMReg Rn, Operand2 Rm);
	void EOR (ARMReg dest, ARMReg src, Operand2 op2);
	void EORS(ARMReg dest, ARMReg src, Operand2 op2);
	void SUB (ARMReg dest, ARMReg src, Operand2 op2);
	void SUBS(ARMReg dest, ARMReg src, Operand2 op2);
	void RSB (ARMReg dest, ARMReg src, Operand2 op2);
	void RSBS(ARMReg dest, ARMReg src, Operand2 op2);
	void ADD (ARMReg dest, ARMReg src, Operand2 op2);
	void ADDS(ARMReg dest, ARMReg src, Operand2 op2);
	void ADC (ARMReg dest, ARMReg src, Operand2 op2);
	void ADCS(ARMReg dest, ARMReg src, Operand2 op2);
	void LSL (ARMReg dest, ARMReg src, Operand2 op2);
	void LSL (ARMReg dest, ARMReg src, ARMReg op2);
	void LSLS(ARMReg dest, ARMReg src, Operand2 op2);
	void LSLS(ARMReg dest, ARMReg src, ARMReg op2);
	void LSR (ARMReg dest, ARMReg src, Operand2 op2);
	void LSRS(ARMReg dest, ARMReg src, Operand2 op2);
	void LSR (ARMReg dest, ARMReg src, ARMReg op2);
	void LSRS(ARMReg dest, ARMReg src, ARMReg op2);
	void ASR (ARMReg dest, ARMReg src, Operand2 op2);
	void ASRS(ARMReg dest, ARMReg src, Operand2 op2);
	void ASR (ARMReg dest, ARMReg src, ARMReg op2);
	void ASRS(ARMReg dest, ARMReg src, ARMReg op2);

	void SBC (ARMReg dest, ARMReg src, Operand2 op2);
	void SBCS(ARMReg dest, ARMReg src, Operand2 op2);
	void RBIT(ARMReg dest, ARMReg src);
	void REV (ARMReg dest, ARMReg src);
	void REV16 (ARMReg dest, ARMReg src);
	void RSC (ARMReg dest, ARMReg src, Operand2 op2);
	void RSCS(ARMReg dest, ARMReg src, Operand2 op2);
	void TST (             ARMReg src, Operand2 op2);
	void TEQ (             ARMReg src, Operand2 op2);
	void CMP (             ARMReg src, Operand2 op2);
	void CMN (             ARMReg src, Operand2 op2);
	void ORR (ARMReg dest, ARMReg src, Operand2 op2);
	void ORRS(ARMReg dest, ARMReg src, Operand2 op2);
	void MOV (ARMReg dest,             Operand2 op2);
	void MOVS(ARMReg dest,             Operand2 op2);
	void BIC (ARMReg dest, ARMReg src, Operand2 op2);   // BIC = ANDN
	void BICS(ARMReg dest, ARMReg src, Operand2 op2);
	void MVN (ARMReg dest,             Operand2 op2);
	void MVNS(ARMReg dest,             Operand2 op2);
	void MOVW(ARMReg dest,             Operand2 op2);
	void MOVT(ARMReg dest, Operand2 op2, bool TopBits = false);

	// UDIV and SDIV are only available on CPUs that have 
	// the idiva hardare capacity
	void UDIV(ARMReg dest, ARMReg dividend, ARMReg divisor);
	void SDIV(ARMReg dest, ARMReg dividend, ARMReg divisor);

	void MUL (ARMReg dest,	ARMReg src, ARMReg op2);
	void MULS(ARMReg dest,	ARMReg src, ARMReg op2);

	void UMULL(ARMReg destLo, ARMReg destHi, ARMReg rn, ARMReg rm);
	void SMULL(ARMReg destLo, ARMReg destHi, ARMReg rn, ARMReg rm);

	void UMLAL(ARMReg destLo, ARMReg destHi, ARMReg rn, ARMReg rm);
	void SMLAL(ARMReg destLo, ARMReg destHi, ARMReg rn, ARMReg rm);

	void SXTB(ARMReg dest, ARMReg op2);
	void SXTH(ARMReg dest, ARMReg op2, u8 rotation = 0);
	void SXTAH(ARMReg dest, ARMReg src, ARMReg op2, u8 rotation = 0);
	void BFI(ARMReg rd, ARMReg rn, u8 lsb, u8 width);
	void BFC(ARMReg rd, u8 lsb, u8 width);
	void UBFX(ARMReg dest, ARMReg op2, u8 lsb, u8 width);
	void SBFX(ARMReg dest, ARMReg op2, u8 lsb, u8 width);
	void CLZ(ARMReg rd, ARMReg rm);
	void PLD(ARMReg rd, int offset, bool forWrite = false);

	// Using just MSR here messes with our defines on the PPC side of stuff (when this code was in dolphin...)
	// Just need to put an underscore here, bit annoying.
	void _MSR (bool nzcvq, bool g, Operand2 op2);
	void _MSR (bool nzcvq, bool g, ARMReg src);
	void MRS  (ARMReg dest);

	// Memory load/store operations
	void LDR  (ARMReg dest, ARMReg base, Operand2 op2 = 0, bool RegAdd = true);
	void LDRB (ARMReg dest, ARMReg base, Operand2 op2 = 0, bool RegAdd = true);
	void LDRH (ARMReg dest, ARMReg base, Operand2 op2 = 0, bool RegAdd = true);
	void LDRSB(ARMReg dest, ARMReg base, Operand2 op2 = 0, bool RegAdd = true);
	void LDRSH(ARMReg dest, ARMReg base, Operand2 op2 = 0, bool RegAdd = true);
	void STR  (ARMReg result, ARMReg base, Operand2 op2 = 0, bool RegAdd = true);
	void STRB (ARMReg result, ARMReg base, Operand2 op2 = 0, bool RegAdd = true);
	void STRH (ARMReg result, ARMReg base, Operand2 op2 = 0, bool RegAdd = true);

	void STMFD(ARMReg dest, bool WriteBack, const int Regnum, ...);
	void LDMFD(ARMReg dest, bool WriteBack, const int Regnum, ...);
	void STMIA(ARMReg dest, bool WriteBack, const int Regnum, ...);
	void LDMIA(ARMReg dest, bool WriteBack, const int Regnum, ...);
	void STM(ARMReg dest, bool Add, bool Before, bool WriteBack, const int Regnum, ...);
	void LDM(ARMReg dest, bool Add, bool Before, bool WriteBack, const int Regnum, ...);
	void STMBitmask(ARMReg dest, bool Add, bool Before, bool WriteBack, const u16 RegList);
	void LDMBitmask(ARMReg dest, bool Add, bool Before, bool WriteBack, const u16 RegList);

	// Exclusive Access operations
	void LDREX(ARMReg dest, ARMReg base);
	// result contains the result if the instruction managed to store the value
	void STREX(ARMReg result, ARMReg base, ARMReg op);
	void DMB ();
	void SVC(Operand2 op);

	// NEON and ASIMD instructions
	// None of these will be created with conditional since ARM
	// is deprecating conditional execution of ASIMD instructions.
	// ASIMD instructions don't even have a conditional encoding.

	// NEON Only
	void VABD(IntegerSize size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VADD(IntegerSize size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VSUB(IntegerSize size, ARMReg Vd, ARMReg Vn, ARMReg Vm);

	// VFP Only
	void VLDMIA(ARMReg dest, bool WriteBack, ARMReg firstreg, int numregs);
	void VSTMIA(ARMReg dest, bool WriteBack, ARMReg firstreg, int numregs);
	void VLDMDB(ARMReg dest, bool WriteBack, ARMReg firstreg, int numregs);
	void VSTMDB(ARMReg dest, bool WriteBack, ARMReg firstreg, int numregs);
	void VPUSH(ARMReg firstvreg, int numvregs) {
		VSTMDB(R_SP, true, firstvreg, numvregs);
	}
	void VPOP(ARMReg firstvreg, int numvregs) {
		VLDMIA(R_SP, true, firstvreg, numvregs);
	}
	void VLDR(ARMReg Dest, ARMReg Base, s16 offset);
	void VSTR(ARMReg Src,  ARMReg Base, s16 offset);
	void VCMP(ARMReg Vd, ARMReg Vm);
	void VCMPE(ARMReg Vd, ARMReg Vm);
	// Compares against zero
	void VCMP(ARMReg Vd);
	void VCMPE(ARMReg Vd);

	void VNMLA(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VNMLS(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VNMUL(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VDIV(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VSQRT(ARMReg Vd, ARMReg Vm);

	// NEON and VFP
	void VADD(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VSUB(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VABS(ARMReg Vd, ARMReg Vm);
	void VNEG(ARMReg Vd, ARMReg Vm);
	void VMUL(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMLA(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMLS(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMOV(ARMReg Dest, Operand2 op2);
	void VMOV(ARMReg Dest, ARMReg Src, bool high);
	void VMOV(ARMReg Dest, ARMReg Src);
	// Either Vd, Rt, Rt2 or Rt, Rt2, Vd.
	void VMOV(ARMReg Dest, ARMReg Src1, ARMReg Src2);
	void VCVT(ARMReg Dest, ARMReg Src, int flags);

	// NEON, need to check for this (supported if VFP4 is supported)
	void VCVTF32F16(ARMReg Dest, ARMReg Src);
	void VCVTF16F32(ARMReg Dest, ARMReg Src);

	void VABA(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VABAL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VABD(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VABDL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VABS(u32 Size, ARMReg Vd, ARMReg Vm);
	void VACGE(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VACGT(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VACLE(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VACLT(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VADD(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VADDHN(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VADDL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VADDW(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VBIF(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VBIT(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VBSL(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VCEQ(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VCEQ(u32 Size, ARMReg Vd, ARMReg Vm);
	void VCGE(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VCGE(u32 Size, ARMReg Vd, ARMReg Vm);
	void VCGT(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VCGT(u32 Size, ARMReg Vd, ARMReg Vm);
	void VCLE(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VCLE(u32 Size, ARMReg Vd, ARMReg Vm);
	void VCLS(u32 Size, ARMReg Vd, ARMReg Vm);
	void VCLT(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VCLT(u32 Size, ARMReg Vd, ARMReg Vm);
	void VCLZ(u32 Size, ARMReg Vd, ARMReg Vm);
	void VCNT(u32 Size, ARMReg Vd, ARMReg Vm);
	void VDUP(u32 Size, ARMReg Vd, ARMReg Vm, u8 index);
	void VDUP(u32 Size, ARMReg Vd, ARMReg Rt);
	void VEXT(ARMReg Vd, ARMReg Vn, ARMReg Vm, u8 index);
	void VFMA(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VFMS(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VHADD(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VHSUB(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMAX(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMIN(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);

	// Three registers
	void VMLA(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMLS(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMLAL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMLSL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMUL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMULL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQDMLAL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQDMLSL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQDMULH(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQDMULL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQRDMULH(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);

	// Two registers and a scalar
	// These two are super useful for matrix multiplication
	void VMUL_scalar(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMLA_scalar(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	
	// TODO:
	/*
	void VMLS_scalar(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMLAL_scalar(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMLSL_scalar(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VMULL_scalar(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQDMLAL_scalar(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQDMLSL_scalar(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQDMULH_scalar(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQDMULL_scalar(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQRDMULH_scalar(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	*/

  // Vector bitwise. These don't have an element size for obvious reasons.
	void VAND(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VBIC(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VEOR(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VORN(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VORR(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	inline void VMOV_neon(ARMReg Dest, ARMReg Src) {
		VORR(Dest, Src, Src);
	}
	void VMOV_neon(u32 Size, ARMReg Vd, u32 imm);
	void VMOV_neon(u32 Size, ARMReg Vd, float imm) {
		_dbg_assert_msg_(Size == F_32, "Expecting F_32 immediate for VMOV_neon float arg.");
		union {
			float f;
			u32 u;
		} val;
		val.f = imm;
		VMOV_neon(I_32, Vd, val.u);
	}
	void VMOV_neon(u32 Size, ARMReg Vd, ARMReg Rt, int lane);

	void VNEG(u32 Size, ARMReg Vd, ARMReg Vm);
	void VMVN(ARMReg Vd, ARMReg Vm);
	void VPADAL(u32 Size, ARMReg Vd, ARMReg Vm);
	void VPADD(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VPADDL(u32 Size, ARMReg Vd, ARMReg Vm);
	void VPMAX(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VPMIN(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQABS(u32 Size, ARMReg Vd, ARMReg Vm);
	void VQADD(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQNEG(u32 Size, ARMReg Vd, ARMReg Vm);
	void VQRSHL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQSHL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VQSUB(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VRADDHN(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VRECPE(u32 Size, ARMReg Vd, ARMReg Vm);
	void VRECPS(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VRHADD(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VRSHL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VRSQRTE(u32 Size, ARMReg Vd, ARMReg Vm);
	void VRSQRTS(ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VRSUBHN(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VSHL(u32 Size, ARMReg Vd, ARMReg Vm, ARMReg Vn);  // Register shift
	void VSUB(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VSUBHN(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VSUBL(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VSUBW(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VSWP(ARMReg Vd, ARMReg Vm);
	void VTRN(u32 Size, ARMReg Vd, ARMReg Vm);
	void VTST(u32 Size, ARMReg Vd, ARMReg Vn, ARMReg Vm);
	void VUZP(u32 Size, ARMReg Vd, ARMReg Vm);
	void VZIP(u32 Size, ARMReg Vd, ARMReg Vm);
	void VREVX(u32 size, u32 Size, ARMReg Vd, ARMReg Vm);
	void VREV64(u32 Size, ARMReg Vd, ARMReg Vm);
	void VREV32(u32 Size, ARMReg Vd, ARMReg Vm);
	void VREV16(u32 Size, ARMReg Vd, ARMReg Vm);


	// NEON immediate instructions


	void VMOV_imm(u32 Size, ARMReg Vd, VIMMMode type, int imm);
	void VMOV_immf(ARMReg Vd, float value);  // This only works with a select few values (1.0f and -1.0f).

	void VORR_imm(u32 Size, ARMReg Vd, VIMMMode type, int imm);
	void VMVN_imm(u32 Size, ARMReg Vd, VIMMMode type, int imm);
	void VBIC_imm(u32 Size, ARMReg Vd, VIMMMode type, int imm);

	// Widening and narrowing moves
	void VMOVL(u32 Size, ARMReg Vd, ARMReg Vm);
	void VMOVN(u32 Size, ARMReg Vd, ARMReg Vm);
	void VQMOVN(u32 Size, ARMReg Vd, ARMReg Vm);
	void VQMOVUN(u32 Size, ARMReg Vd, ARMReg Vm);

	// Shifts by immediate
	void VSHL(u32 Size, ARMReg Vd, ARMReg Vm, int shiftAmount);
	void VSHLL(u32 Size, ARMReg Vd, ARMReg Vm, int shiftAmount);  // widening
	void VSHR(u32 Size, ARMReg Vd, ARMReg Vm, int shiftAmount);
	void VSHRN(u32 Size, ARMReg Vd, ARMReg Vm, int shiftAmount);  // narrowing

	// Vector VCVT
	void VCVT(u32 DestSize, ARMReg Dest, ARMReg Src);


	// Notes:
	// Rm == R_PC  is interpreted as no offset, otherwise, effective address is sum of Rn and Rm
	// Rm == R13  is interpreted as   VLD1,   ....  [Rn]!    Added a REG_UPDATE pseudo register.

	// Load/store multiple registers full of elements (a register is a D register)
	// Specifying alignment when it can be guaranteed is documented to improve load/store performance.
	// For example, when loading a set of four 64-bit registers that we know is 32-byte aligned, we should specify ALIGN_256.
	void VLD1(u32 Size, ARMReg Vd, ARMReg Rn, int regCount, NEONAlignment align = ALIGN_NONE, ARMReg Rm = R_PC);
	void VST1(u32 Size, ARMReg Vd, ARMReg Rn, int regCount, NEONAlignment align = ALIGN_NONE, ARMReg Rm = R_PC);

	// Load/store single lanes of D registers
	void VLD1_lane(u32 Size, ARMReg Vd, ARMReg Rn, int lane, bool aligned, ARMReg Rm = R_PC);
	void VST1_lane(u32 Size, ARMReg Vd, ARMReg Rn, int lane, bool aligned, ARMReg Rm = R_PC);

	// Load one value into all lanes of a D or a Q register (either supported, all formats should work). 
	void VLD1_all_lanes(u32 Size, ARMReg Vd, ARMReg Rn, bool aligned, ARMReg Rm = R_PC);

	/*
	// Deinterleave two loads... or something. TODO
	void VLD2(u32 Size, ARMReg Vd, ARMReg Rn, int regCount, NEONAlignment align = ALIGN_NONE, ARMReg Rm = R_PC);
	void VST2(u32 Size, ARMReg Vd, ARMReg Rn, int regCount, NEONAlignment align = ALIGN_NONE, ARMReg Rm = R_PC);

	void VLD2_lane(u32 Size, ARMReg Vd, ARMReg Rn, int lane, ARMReg Rm = R_PC);
	void VST2_lane(u32 Size, ARMReg Vd, ARMReg Rn, int lane, ARMReg Rm = R_PC);

	void VLD3(u32 Size, ARMReg Vd, ARMReg Rn, int regCount, NEONAlignment align = ALIGN_NONE, ARMReg Rm = R_PC);
	void VST3(u32 Size, ARMReg Vd, ARMReg Rn, int regCount, NEONAlignment align = ALIGN_NONE, ARMReg Rm = R_PC);

	void VLD3_lane(u32 Size, ARMReg Vd, ARMReg Rn, int lane, ARMReg Rm = R_PC);
	void VST3_lane(u32 Size, ARMReg Vd, ARMReg Rn, int lane, ARMReg Rm = R_PC);

	void VLD4(u32 Size, ARMReg Vd, ARMReg Rn, int regCount, NEONAlignment align = ALIGN_NONE, ARMReg Rm = R_PC);
	void VST4(u32 Size, ARMReg Vd, ARMReg Rn, int regCount, NEONAlignment align = ALIGN_NONE, ARMReg Rm = R_PC);

	void VLD4_lane(u32 Size, ARMReg Vd, ARMReg Rn, int lane, ARMReg Rm = R_PC);
	void VST4_lane(u32 Size, ARMReg Vd, ARMReg Rn, int lane, ARMReg Rm = R_PC);
	*/

	void VMRS_APSR();
	void VMRS(ARMReg Rt);
	void VMSR(ARMReg Rt);

	void QuickCallFunction(ARMReg scratchreg, const void *func);
	template <typename T> void QuickCallFunction(ARMReg scratchreg, T func) {
		QuickCallFunction(scratchreg, (const void *)func);
	}

	// Wrapper around MOVT/MOVW with fallbacks.
	void MOVI2R(ARMReg reg, u32 val, bool optimize = true);
	void MOVI2FR(ARMReg dest, float val, bool negate = false);
	void MOVI2F(ARMReg dest, float val, ARMReg tempReg, bool negate = false);
	void MOVI2F_neon(ARMReg dest, float val, ARMReg tempReg, bool negate = false);

	// Load pointers without casting
	template <class T> void MOVP2R(ARMReg reg, T *val) {
		MOVI2R(reg, (u32)(uintptr_t)(void *)val);
	}

	void MOVIU2F(ARMReg dest, u32 val, ARMReg tempReg, bool negate = false) {
		union {
			u32 u;
			float f;
		} v = {val};
		MOVI2F(dest, v.f, tempReg, negate);
	}

	void ADDI2R(ARMReg rd, ARMReg rs, u32 val, ARMReg scratch);
	bool TryADDI2R(ARMReg rd, ARMReg rs, u32 val);
	void SUBI2R(ARMReg rd, ARMReg rs, u32 val, ARMReg scratch);
	bool TrySUBI2R(ARMReg rd, ARMReg rs, u32 val);
	void ANDI2R(ARMReg rd, ARMReg rs, u32 val, ARMReg scratch);
	bool TryANDI2R(ARMReg rd, ARMReg rs, u32 val);
	void CMPI2R(ARMReg rs, u32 val, ARMReg scratch);
	bool TryCMPI2R(ARMReg rs, u32 val);
	void TSTI2R(ARMReg rs, u32 val, ARMReg scratch);
	bool TryTSTI2R(ARMReg rs, u32 val);
	void ORI2R(ARMReg rd, ARMReg rs, u32 val, ARMReg scratch);
	bool TryORI2R(ARMReg rd, ARMReg rs, u32 val);
	void EORI2R(ARMReg rd, ARMReg rs, u32 val, ARMReg scratch);
	bool TryEORI2R(ARMReg rd, ARMReg rs, u32 val);
};  // class ARMXEmitter


// Everything that needs to generate machine code should inherit from this.
// You get memory management for free, plus, you can use all the MOV etc functions without
// having to prefix them with gen-> or something similar.

class ARMXCodeBlock : public CodeBlock<ARMXEmitter> {
public:
	void PoisonMemory(int offset) override;
};

// VFP Specific
struct VFPEnc {
	s16 opc1;
	s16 opc2;
};
extern const VFPEnc VFPOps[16][2];
extern const char *VFPOpNames[16];

}  // namespace
