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
#include "ArmABI.h"

using namespace ArmGen;
// Shared code between Win32 and Unix32
void ARMXEmitter::ARMABI_CallFunction(void *func) 
{
	ARMABI_MOVIMM32(R8, (u32)func);	
	PUSH(1, _LR);
	BLX(R8);
	POP(1, _LR);
}
void ARMXEmitter::ARMABI_PushAllCalleeSavedRegsAndAdjustStack() {
	// Note: 4 * 4 = 16 bytes, so alignment is preserved.
	PUSH(4, R0, R1, R2, R3);
}

void ARMXEmitter::ARMABI_PopAllCalleeSavedRegsAndAdjustStack() {
	POP(4, R3, R4, R5, R6);
}
void ARMXEmitter::ARMABI_MOVIMM32(ARMReg reg, u32 val)
{
	// TODO: We can do this in less instructions if we check for if it is
	// smaller than a 32bit variable. Like if it is a 8bit or 14bit(?)
	// variable it should be able to be moved to just a single MOV instruction
	// but for now, we are just taking the long route out and using the MOVW
	// and MOVT
	Operand2 Val(val);
	MOVW(reg, Val);
  MOVT(reg, Val, true);
  return;
}
// Moves IMM to memory location
void ARMXEmitter::ARMABI_MOVIMM32(Operand2 op, u32 val)
{
	// This moves imm to a memory location
	Operand2 Val(val);
	MOVW(R10, Val); MOVT(R10, Val, true);
	MOVW(R11, op); MOVT(R11, op, true);
	STR(R11, R10); // R10 is what we want to store
	
}
// NZCVQ is stored in the lower five bits of the Flags variable
// GE values are in the lower four bits of the GEval variable

void ARMXEmitter::UpdateAPSR(bool NZCVQ, u8 Flags, bool GE, u8 GEval)
{
	if(NZCVQ && GE)
	{
		// Can't update GE with the other ones with a immediate
		// Got to use a scratch register
		u32 IMM = (Flags << 27) | ((GEval & 0xF) << 16);
		ARMABI_MOVIMM32(R8, IMM);
		_MSR(true, true, R8);
	}
	else
	{
		if(NZCVQ)
		{
			Operand2 value(Flags << 1, 3);
			_MSR(true, false, value);
		}
		else if(GE)
		{
			Operand2 value(GEval << 2, 9);
			_MSR(false, true, value);
		}
		else
			; // Okay?

	}
}
