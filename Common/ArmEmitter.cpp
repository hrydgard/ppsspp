// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "Common.h"
#include "ArmEmitter.h"
#include "CPUDetect.h"

#include <assert.h>
#include <stdarg.h>

// For cache flushing on Symbian/Blackberry
#ifdef __SYMBIAN32__
#include <e32std.h>
#endif

#ifdef BLACKBERRY
#include <sys/mman.h>
#endif

namespace ArmGen
{

void ARMXEmitter::SetCodePtr(u8 *ptr)
{
	code = ptr;
	startcode = code;
}

const u8 *ARMXEmitter::GetCodePtr() const
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
	ReserveCodeSpace((-(s32)code) & 15);
	return code;
}

const u8 *ARMXEmitter::AlignCodePage()
{
	ReserveCodeSpace((-(s32)code) & 4095);
	return code;
}

void ARMXEmitter::Flush()
{
#ifdef __SYMBIAN32__
    User::IMB_Range( startcode, code );
#elif defined(BLACKBERRY)
	msync(startcode, code-startcode, MS_SYNC | MS_INVALIDATE_ICACHE);
#else
	__builtin___clear_cache (startcode, code);
#endif
	SLEEP(0);
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
	Write32( 0xF1010000 | (BE << 9));
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
	s32 distance = (s32)fnptr - (s32(code) + 8);
        _assert_msg_(DYNA_REC, distance > -33554432
                     && distance <=  33554432,
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
	s32 distance =  (s32(code) - 8)  - (s32)branch.ptr;
     _assert_msg_(DYNA_REC, distance > -33554432
                     && distance <=  33554432,
                     "SetJumpTarget out of range (%p calls %p)", code,
					 branch.ptr);
	if(branch.type == 0) // B
		*(u32*)branch.ptr = (u32)(branch.condition | (10 << 24) | ((distance >> 2) &
		0x00FFFFFF)); 
	else // BL
		*(u32*)branch.ptr =	(u32)(branch.condition | 0x0B000000 | ((distance >> 2)
		& 0x00FFFFFF));
}
void ARMXEmitter::B (const void *fnptr)
{
	s32 distance = (s32)fnptr - (s32(code) + 8);
        _assert_msg_(DYNA_REC, distance > -33554432
                     && distance <=  33554432,
                     "B out of range (%p calls %p)", code, fnptr);

	Write32(condition | 0x0A000000 | ((distance >> 2) & 0x00FFFFFF));
}

void ARMXEmitter::B(ARMReg src)
{
	Write32(condition | (18 << 20) | (0xFFF << 8) | (1 << 4) | src);
}

void ARMXEmitter::BL(const void *fnptr)
{
	s32 distance = (s32)fnptr - (s32(code) + 8);
        _assert_msg_(DYNA_REC, distance > -33554432
                     && distance <=  33554432,
                     "BL out of range (%p calls %p)", code, fnptr);
	Write32(condition | 0x0B000000 | ((distance >> 2) & 0x00FFFFFF));
}
void ARMXEmitter::BL(ARMReg src)
{
	Write32(condition | 0x12FFF30 | src);
}
void ARMXEmitter::PUSH(const int num, ...)
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
							"MVN"
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
	u32 op = InstOps[Op][Rm.GetType()]; // Type always decided by last operand
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
		_assert_msg_(DYNA_REC, false, "%s not yet support %d", InstNames[Op], Rm.GetType()); 
	Write32(condition | (op << 21) | (SetFlags ? (1 << 20) : 0) | Rn << 16 | Rd << 12 | Data);
}

// Data Operations
void ARMXEmitter::LSL (ARMReg dest, ARMReg src, Operand2 op2) { WriteShiftedDataOp(0, false, dest, src, op2);}
void ARMXEmitter::LSLS(ARMReg dest, ARMReg src, Operand2 op2) { WriteShiftedDataOp(0, true, dest, src, op2);}
void ARMXEmitter::LSL (ARMReg dest, ARMReg src, ARMReg op2)	  { WriteShiftedDataOp(1, false, dest, src, op2);} 
void ARMXEmitter::LSLS(ARMReg dest, ARMReg src, ARMReg op2)	  { WriteShiftedDataOp(1, true, dest, src, op2);}
void ARMXEmitter::MUL (ARMReg dest,	ARMReg src, ARMReg op2)
{
	Write32(condition | (dest << 16) | (src << 8) | (9 << 4) | op2);
}
void ARMXEmitter::MULS(ARMReg dest,	ARMReg src, ARMReg op2)
{
	Write32(condition | (1 << 20) | (dest << 16) | (src << 8) | (9 << 4) | op2);
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
void ARMXEmitter::REV (ARMReg dest, ARMReg src				) 
{
	Write32(condition | (107 << 20) | (15 << 16) | (dest << 12) | (243 << 4) | src);
}

void ARMXEmitter::_MSR (bool nzcvq, bool g,		Operand2 op2)
{
	Write32(condition | (0x320F << 12) | (nzcvq << 19) | (g << 18) | op2.Imm12Mod());
}
void ARMXEmitter::_MSR (bool nzcvq, bool g,		ARMReg src)
{
	Write32(condition | (0x120F << 12) | (nzcvq << 19) | (g << 18) | src);
}
void ARMXEmitter::MRS (ARMReg dest)
{
	Write32(condition | (16 << 20) | (15 << 16) | (dest << 12));
}

void ARMXEmitter::WriteStoreOp(u32 op, ARMReg dest, ARMReg src, Operand2 op2)
{
	if (op2.GetData() == 0) // Don't index
		Write32(condition | (op << 20) | (dest << 16) | (src << 12) | op2.Imm12());
	else
		Write32(condition | (op << 20) | (3 << 23) | (dest << 16) | (src << 12) | op2.Imm12()); 
}
void ARMXEmitter::STR (ARMReg dest, ARMReg src, Operand2 op) { WriteStoreOp(0x40, dest, src, op);}
void ARMXEmitter::STRB(ARMReg dest, ARMReg src, Operand2 op) { WriteStoreOp(0x44, dest, src, op);}
void ARMXEmitter::STR (ARMReg dest, ARMReg base, ARMReg offset, bool Index, bool Add)
{
	Write32(condition | (0x60 << 20) | (Index << 24) | (Add << 23) | (dest << 16) | (base << 12) | offset);
}
void ARMXEmitter::LDREX(ARMReg dest, ARMReg base)
{
	Write32(condition | (25 << 20) | (base << 16) | (dest << 12) | 0xF9F);
}
void ARMXEmitter::STREX(ARMReg dest, ARMReg base, ARMReg op)
{
	_assert_msg_(DYNA_REC, (dest != base && dest != op), "STREX dest can't be other two registers");
	Write32(condition | (24 << 20) | (base << 16) | (dest << 12) | (0xF9 << 4) | op);
}
void ARMXEmitter::DMB ()
{
	Write32(0xF57FF05E);
}

void ARMXEmitter::LDR (ARMReg dest, ARMReg src, Operand2 op) { WriteStoreOp(0x41, src, dest, op);}
void ARMXEmitter::LDRB(ARMReg dest, ARMReg src, Operand2 op) { WriteStoreOp(0x45, src, dest, op);}

void ARMXEmitter::LDR (ARMReg dest, ARMReg base, ARMReg offset, bool Index,
bool Add)
{
	Write32(condition | (0x61 << 20) | (Index << 24) | (Add << 23) | (base << 16) | (dest << 12) | offset);
}
void ARMXEmitter::WriteRegStoreOp(u32 op, ARMReg dest, bool WriteBack, u16 RegList)
{
	Write32(condition | (op << 20) | (WriteBack << 21) | (dest << 16) | RegList);
}
void ARMXEmitter::STMFD(ARMReg dest, bool WriteBack, const int Regnum, ...)
{
	u16 RegList = 0;
	u8 Reg;
	int i;
	va_list vl;
	va_start(vl, Regnum);
	for (i=0;i<Regnum;i++)
	{
		Reg = va_arg(vl, u32);
		RegList |= (1 << Reg);
	}
	va_end(vl);
	WriteRegStoreOp(0x90, dest, WriteBack, RegList);
}
void ARMXEmitter::LDMFD(ARMReg dest, bool WriteBack, const int Regnum, ...)
{
	u16 RegList = 0;
	u8 Reg;
	int i;
	va_list vl;
	va_start(vl, Regnum);
	for (i=0;i<Regnum;i++)
	{
		Reg = va_arg(vl, u32);
		RegList |= (1 << Reg);
	}
	va_end(vl);
	WriteRegStoreOp(0x89, dest, WriteBack, RegList);
}
// helper routines for setting pointers
void ARMXEmitter::CallCdeclFunction3(void* fnptr, u32 arg0, u32 arg1, u32 arg2)
{
}

void ARMXEmitter::CallCdeclFunction4(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3)
{
}

void ARMXEmitter::CallCdeclFunction5(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
}

void ARMXEmitter::CallCdeclFunction6(void* fnptr, u32 arg0, u32 arg1, u32 arg2, u32 arg3, u32 arg4, u32 arg5)
{
}
}
