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

#include "RegCache.h"
#include "ArmEmitter.h"

using namespace ArmGen;

ArmRegCache::ArmRegCache(MIPSState *mips)
	: mips_(mips)
{
	emit = 0;

}

void ArmRegCache::Init(ARMXEmitter *emitter)
{
	emit = emitter;
	ARMReg *PPCRegs = GetMIPSAllocationOrder(NUMMIPSREG);
	ARMReg *Regs = GetAllocationOrder(NUMARMREG);
	for(int a = 0; a < 32; ++a)
	{
		// This gives us the memory locations of the gpr registers so we can
		// load them.
		regs[a].location = (u8*)&mips_->r[a]; 	
	}
	for(int a = 0; a < NUMMIPSREG; ++a)
	{
		ArmCRegs[a].MIPSReg = 33;
		ArmCRegs[a].Reg = PPCRegs[a];
		ArmCRegs[a].LastLoad = 0;
	}
	for(int a = 0; a < NUMARMREG; ++a)
	{
		ArmRegs[a].Reg = Regs[a];
		ArmRegs[a].free = true;
	}
}

void ArmRegCache::Start(MIPSAnalyst::AnalysisResults &stats)
{
	for(int a = 0; a < NUMMIPSREG; ++a)
	{
		ArmCRegs[a].MIPSReg = 33;
		ArmCRegs[a].LastLoad = 0;
	}
}

ARMReg *ArmRegCache::GetMIPSAllocationOrder(int &count)
{
	// This will return us the allocation order of the registers we can use on
	// the MIPS side.
	static ARMReg allocationOrder[] = 
	{
		R0, R1, R2, R3, R4, R5, R6, R7, R8, R9
	};
	count = sizeof(allocationOrder) / sizeof(const int);
	return allocationOrder;
}
ARMReg *ArmRegCache::GetAllocationOrder(int &count)
{
	// This will return us the allocation order of the registers we can use on
	// the host side.
	static ARMReg allocationOrder[] = 
	{
		R14, R12, R11, R10
	};
	count = sizeof(allocationOrder) / sizeof(const int);
	return allocationOrder;
}

ARMReg ArmRegCache::GetReg(bool AutoLock)
{
	for (int a = 0; a < NUMARMREG; ++a)
	{
		if(ArmRegs[a].free)
		{
			// Alright, this one is free
			if (AutoLock)
				ArmRegs[a].free = false;
			return ArmRegs[a].Reg;
		}
	}
	// Uh Oh, we have all them locked....
	_assert_msg_(JIT, false, "All available registers are locked dumb dumb");
}
void ArmRegCache::Lock(ARMReg Reg)
{
	for (int RegNum = 0; RegNum < NUMARMREG; ++RegNum)
	{
		if(ArmRegs[RegNum].Reg == Reg)
		{
			_assert_msg_(JIT, ArmRegs[RegNum].free, "This register is already locked");
			ArmRegs[RegNum].free = false;
		}
	}
	_assert_msg_(JIT, false, "Register %d can't be used with lock", Reg);
}
void ArmRegCache::Unlock(ARMReg R0, ARMReg R1, ARMReg R2, ARMReg R3)
{
	for (int RegNum = 0; RegNum < NUMARMREG; ++RegNum)
	{
		if(ArmRegs[RegNum].Reg == R0)
		{
			_assert_msg_(JIT, !ArmRegs[RegNum].free, "This register is already unlocked");
			ArmRegs[RegNum].free = true;
		}
		if( R1 != INVALID_REG && ArmRegs[RegNum].Reg == R1) ArmRegs[RegNum].free = true;
		if( R2 != INVALID_REG && ArmRegs[RegNum].Reg == R2) ArmRegs[RegNum].free = true;
		if( R3 != INVALID_REG && ArmRegs[RegNum].Reg == R3) ArmRegs[RegNum].free = true;
	}
}

ARMReg ArmRegCache::R(int preg)
{
	u32 HighestUsed = 0;
	u8 Num = 0;
	for (int a = 0; a < NUMMIPSREG; ++a){
		++ArmCRegs[a].LastLoad;
		if (ArmCRegs[a].LastLoad > HighestUsed)
		{
			HighestUsed = ArmCRegs[a].LastLoad;
			Num = a;
		}
	}
	// Check if already Loaded
	for (int a = 0; a < NUMMIPSREG; ++a) {
		if (ArmCRegs[a].MIPSReg == preg)
		{
			ArmCRegs[a].LastLoad = 0;
			return ArmCRegs[a].Reg;
		}
	}
	// Check if we have a free register
	for (u8 a = 0; a < NUMMIPSREG; ++a)
		if (ArmCRegs[a].MIPSReg == 33)
		{
			emit->ARMABI_MOVI2R(ArmCRegs[a].Reg, (u32)&mips_->r);
			emit->LDR(ArmCRegs[a].Reg, ArmCRegs[a].Reg, preg * 4);
			ArmCRegs[a].MIPSReg = preg;
			ArmCRegs[a].LastLoad = 0;
			return ArmCRegs[a].Reg;
		}
		// Alright, we couldn't get a free space, dump that least used register
		// Note that this is incredibly dangerous if references to the register
		// are still floating around out there!
		ARMReg rA = GetReg(false);
		emit->ARMABI_MOVI2R(rA, (u32)&mips_->r);
		emit->STR(rA, ArmCRegs[Num].Reg, ArmCRegs[Num].MIPSReg * 4);
		emit->LDR(ArmCRegs[Num].Reg, rA, preg * 4);
		ArmCRegs[Num].MIPSReg = preg;
		ArmCRegs[Num].LastLoad = 0;
		return ArmCRegs[Num].Reg;		 
}

void ArmRegCache::Flush()
{
	// Maybe we should keep this pointer around permanently?
	emit->MOVW(R14, (u32)&mips_->r);
	emit->MOVT(R14, (u32)&mips_->r, true);

	for (int a = 0; a < NUMMIPSREG; ++a) {
		if (ArmCRegs[a].MIPSReg != 33)
		{
			emit->STR(R14, ArmCRegs[a].Reg, ArmCRegs[a].MIPSReg * 4);
			ArmCRegs[a].MIPSReg = 33;
			ArmCRegs[a].LastLoad = 0;
		}
	}
}

