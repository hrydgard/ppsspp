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

#include "Common.h"
#include "ArmEmitter.h"
#include "CPUDetect.h"

#include <assert.h>
#include <stdarg.h>

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
	__clear_cache (startcode, code);
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

void ARMXEmitter::BKPT(u16 arg)
{
	Write32(condition | 0x01200070 | (arg << 4 & 0x000FFF00) | (arg & 0x0000000F));
}
void ARMXEmitter::YIELD()
{
	Write32(condition | 0x0320F001);
}

void ARMXEmitter::B (Operand2 op2)
{
	Write32(condition | (10 << 24) | op2.Imm24());
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
void ARMXEmitter::BL(const void *fnptr)
{
	s32 distance = (s32)fnptr - (s32(code) + 8);
        _assert_msg_(DYNA_REC, distance > -33554432
                     && distance <=  33554432,
                     "BL out of range (%p calls %p)", code, fnptr);
	Write32(condition | 0x0B000000 | ((distance >> 2) & 0x00FFFFFF));
}
void ARMXEmitter::BLX(ARMReg src)
{
	Write32(condition | 0x12FFF30 | src);
}

void ARMXEmitter::BX(ARMReg src)
{
	Write32(condition | (18 << 20) | (0xFFF << 8) | (1 << 4) | src);
}
void ARMXEmitter::SetJumpTarget(FixupBranch const &branch)
{
	s32 distance =  (s32(code) + 4) - (s32)branch.ptr;
     _assert_msg_(DYNA_REC, distance > -33554432
                     && distance <=  33554432,
                     "SetJumpTarget out of range (%p calls %p)", code,
					 branch.ptr);
	printf("Jumping to %08x\n", distance);
	if(branch.type == 0) // B
		*(u32*)branch.ptr = (u32)(branch.condition | (10 << 24) | (distance &
		0x00FFFFFF)); 
	else // BL
		*(u32*)branch.ptr =	(u32)(branch.condition | 0x0B000000 | ((distance >> 2)
		& 0x00FFFFFF));
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

void ARMXEmitter::WriteDataOp(u32 op, ARMReg dest, ARMReg src, Operand2 op2)
{
	Write32(condition | (op << 20) | (src << 16) | (dest << 12) | op2.Imm12Mod()); 
}
void ARMXEmitter::WriteDataOp(u32 op, ARMReg dest, ARMReg src, ARMReg op2)
{
	Write32(condition | (op << 20) | (src << 16) | (dest << 12) | op2);
}
void ARMXEmitter::WriteDataOp(u32 op, ARMReg dest, ARMReg src)
{
	Write32(condition | (op << 20) | (dest << 12) | src);
}


// Data Operations
void ARMXEmitter::AND (ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(32, dest, src, op2);}
void ARMXEmitter::ANDS(ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(33, dest, src, op2);}
void ARMXEmitter::AND (ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp( 0, dest, src, op2);}
void ARMXEmitter::ANDS(ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp( 1, dest, src, op2);}
void ARMXEmitter::EOR (ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(34, dest, src, op2);}
void ARMXEmitter::EORS(ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(35, dest, src, op2);}
void ARMXEmitter::EOR (ARMReg dest, ARMReg src, ARMReg op2)	  { WriteDataOp( 2, dest, src, op2);}
void ARMXEmitter::EORS(ARMReg dest, ARMReg src, ARMReg op2)	  { WriteDataOp( 3, dest, src, op2);}
void ARMXEmitter::SUB (ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(36, dest, src, op2);}
void ARMXEmitter::SUBS(ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(37, dest, src, op2);}
void ARMXEmitter::SUB (ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp( 4, dest, src, op2);}
void ARMXEmitter::SUBS(ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp( 5, dest, src, op2);}
void ARMXEmitter::RSB (ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(38, dest, src, op2);}
void ARMXEmitter::RSBS(ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(39, dest, src, op2);}
void ARMXEmitter::RSB (ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp( 6, dest, src, op2);}
void ARMXEmitter::RSBS(ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp( 7, dest, src, op2);}
void ARMXEmitter::ADD (ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(40, dest, src, op2);}
void ARMXEmitter::ADDS(ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(41, dest, src, op2);}
void ARMXEmitter::ADD (ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp( 8, dest, src, op2);}
void ARMXEmitter::ADDS(ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp( 9, dest, src, op2);}
void ARMXEmitter::ADC (ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(42, dest, src, op2);}
void ARMXEmitter::ADCS(ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(43, dest, src, op2);}
void ARMXEmitter::ADC (ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp(10, dest, src, op2);}
void ARMXEmitter::ADCS(ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp(11, dest, src, op2);}
void ARMXEmitter::SBC (ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(44, dest, src, op2);}
void ARMXEmitter::SBCS(ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(45, dest, src, op2);}
void ARMXEmitter::SBC (ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp(12, dest, src, op2);}
void ARMXEmitter::SBCS(ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp(13, dest, src, op2);}
void ARMXEmitter::RSC (ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(46, dest, src, op2);}
void ARMXEmitter::RSCS(ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(47, dest, src, op2);}
void ARMXEmitter::RSC (ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp(14, dest, src, op2);}
void ARMXEmitter::RSCS(ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp(15, dest, src, op2);}
void ARMXEmitter::TST (             ARMReg src, Operand2 op2) { WriteDataOp(49, R0  , src, op2);}
void ARMXEmitter::TST (				ARMReg src, ARMReg op2)   { WriteDataOp(17, R0	, src, op2);}
void ARMXEmitter::TEQ (             ARMReg src, Operand2 op2) { WriteDataOp(51, R0  , src, op2);}
void ARMXEmitter::TEQ (				ARMReg src, ARMReg op2)   { WriteDataOp(19, R0	, src, op2);}
void ARMXEmitter::CMP (             ARMReg src, Operand2 op2) { WriteDataOp(53, R0  , src, op2);}
void ARMXEmitter::CMP (             ARMReg src, ARMReg op2)   { WriteDataOp(21, R0  , src, op2);}
void ARMXEmitter::CMN (             ARMReg src, Operand2 op2) { WriteDataOp(55, R0  , src, op2);}
void ARMXEmitter::CMN (             ARMReg src, ARMReg op2)   { WriteDataOp(23, R0  , src, op2);}
void ARMXEmitter::ORR (ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(56, dest, src, op2);}
void ARMXEmitter::ORRS(ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(57, dest, src, op2);}
void ARMXEmitter::ORR (ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp(24, dest, src, op2);}
void ARMXEmitter::ORRS(ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp(25, dest, src, op2);}
void ARMXEmitter::MOV (ARMReg dest,             Operand2 op2) { WriteDataOp(58, dest, R0 , op2);}
void ARMXEmitter::MOVS(ARMReg dest,             Operand2 op2) { WriteDataOp(59, dest, R0 , op2);}
void ARMXEmitter::MOV (ARMReg dest, ARMReg src				) { WriteDataOp(26, dest, R0 , src);}
void ARMXEmitter::MOVS(ARMReg dest, ARMReg src				) { WriteDataOp(27, dest, R0 , src);}
void ARMXEmitter::BIC (ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(60, dest, src, op2);}
void ARMXEmitter::BICS(ARMReg dest, ARMReg src, Operand2 op2) { WriteDataOp(61, dest, src, op2);}
void ARMXEmitter::BIC (ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp(28, dest, src, op2);}
void ARMXEmitter::BICS(ARMReg dest, ARMReg src, ARMReg op2)   { WriteDataOp(29, dest, src, op2);}
void ARMXEmitter::MVN (ARMReg dest,             Operand2 op2) { WriteDataOp(62, dest, R0 , op2);}
void ARMXEmitter::MVNS(ARMReg dest,             Operand2 op2) { WriteDataOp(63, dest, R0 , op2);} 
void ARMXEmitter::MVN (ARMReg dest,             ARMReg op2)   { WriteDataOp(30, dest, R0 , op2);} 
void ARMXEmitter::MVNS(ARMReg dest,             ARMReg op2)   { WriteDataOp(31, dest, R0 , op2);} 

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
// Memory Load/Store operations
void ARMXEmitter::WriteMoveOp(u32 op, ARMReg dest, Operand2 op2, bool TopBits)
{
	Write32(condition | (op << 20) | (dest << 12) | (TopBits ? op2.Imm16High() : op2.Imm16Low()));
}
void ARMXEmitter::MOVT(ARMReg dest, 			Operand2 op2, bool TopBits) 
{
	 WriteMoveOp( 52, dest, op2, TopBits);
}
void ARMXEmitter::MOVW(ARMReg dest, 			Operand2 op2) { WriteMoveOp( 48, dest, op2);}

void ARMXEmitter::WriteStoreOp(u32 op, ARMReg dest, ARMReg src, Operand2 op2)
{
	Write32(condition | (op << 20) | (dest << 16) | (src << 12) | op2.Imm12());
}
void ARMXEmitter::STR (ARMReg dest, ARMReg src, Operand2 op) { WriteStoreOp(0x40, dest, src, op);}
void ARMXEmitter::STRB(ARMReg dest, ARMReg src, Operand2 op) { WriteStoreOp(0x44, dest, src, op);}
void ARMXEmitter::STR (ARMReg dest, ARMReg base, ARMReg offset, bool Index,
bool Add)
{
	Write32(condition | (0x60 << 20) | (Index << 24) | (Add << 23) | (base << 12) | (dest << 16) | offset);
}
void ARMXEmitter::LDR (ARMReg dest, ARMReg src, Operand2 op) { WriteStoreOp(0x41, dest, src, op);}
void ARMXEmitter::LDRB(ARMReg dest, ARMReg src, Operand2 op) { WriteStoreOp(0x45, dest, src, op);}
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
	_assert_msg_(DYNA_REC, Regnum > 1, "Doesn't support only one register");
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
	_assert_msg_(DYNA_REC, Regnum > 1, "Doesn't support only one register");
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
