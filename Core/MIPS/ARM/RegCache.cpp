// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "../MIPS.h"
#include "../MIPSTables.h"
#include "../MIPSAnalyst.h"
#include "Jit.h"
#include "Asm.h"
#include "RegCache.h"
#include "CommonFuncs.h"

using namespace ArmGen;

RegCache::RegCache() : emit(0) {
	memset(locks, 0, sizeof(locks));
	memset(xlocks, 0, sizeof(xlocks));
	memset(saved_locks, 0, sizeof(saved_locks));
	memset(saved_xlocks, 0, sizeof(saved_xlocks));
	memset(regs, 0, sizeof(regs));
	memset(xregs, 0, sizeof(xregs));
	memset(saved_regs, 0, sizeof(saved_regs));
	memset(saved_xregs, 0, sizeof(saved_xregs));
}

static const int allocationOrder[] = 
{
	R2, R3, R4, R5, R6, R7, R8, R10, R11	 // omitting R9?
};

void RegCache::Start(MIPSState *mips, MIPSAnalyst::AnalysisResults &stats)
{
	for (int i = 0; i < NUMARMREGS; i++)
	{
		xregs[i].free = true;
		xregs[i].dirty = false;
		xlocks[i] = false;
	}
	for (int i = 0; i < 32; i++)
	{
		regs[i].location = GetDefaultLocation(i);
		regs[i].away = false;
	}
	
	// todo: sort to find the most popular regs
	/*
	int maxPreload = 2;
	for (int i = 0; i < 32; i++)
	{
		if (stats.numReads[i] > 2 || stats.numWrites[i] >= 2)
		{
			LoadToX64(i, true, false); //stats.firstRead[i] <= stats.firstWrite[i], false);
			maxPreload--;
			if (!maxPreload)
				break;
		}
	}*/
	//Find top regs - preload them (load bursts ain't bad)
	//But only preload IF written OR reads >= 3
}

// these are powerpc reg indices
void RegCache::Lock(int p1, int p2, int p3, int p4)
{
	locks[p1] = true;
	if (p2 != 0xFF) locks[p2] = true;
	if (p3 != 0xFF) locks[p3] = true;
	if (p4 != 0xFF) locks[p4] = true;
}

// these are x64 reg indices
void RegCache::LockX(int x1, int x2, int x3, int x4)
{
	if (xlocks[x1]) {
		PanicAlert("RegCache: x %i already locked!", x1);
	}
	xlocks[x1] = true;
	if (x2 != 0xFF) xlocks[x2] = true;
	if (x3 != 0xFF) xlocks[x3] = true;
	if (x4 != 0xFF) xlocks[x4] = true;
}

bool RegCache::IsFreeX(int xreg) const
{
	return xregs[xreg].free && !xlocks[xreg];
}

void RegCache::UnlockAll()
{
	for (int i = 0; i < 32; i++)
		locks[i] = false;
}

void RegCache::UnlockAllX()
{
	for (int i = 0; i < NUMARMREGS; i++)
		xlocks[i] = false;
}

ARMReg RegCache::GetFreeXReg()
{
	int aCount;
	const int *aOrder = GetAllocationOrder(aCount);
	for (int i = 0; i < aCount; i++)
	{
		ARMReg xr = (ARMReg)aOrder[i];
		if (!xlocks[xr] && xregs[xr].free)
		{
			return (ARMReg)xr;
		}
	}
	//Okay, not found :( Force grab one

	//TODO - add a pass to grab xregs whose ppcreg is not used in the next 3 instructions
	for (int i = 0; i < aCount; i++)
	{
		ARMReg xr = (ARMReg)aOrder[i];
		if (xlocks[xr]) 
			continue;
		int preg = xregs[xr].ppcReg;
		if (!locks[preg])
		{
			StoreFromRegister(preg);
			return xr;
		}
	}
	//Still no dice? Die!
	_assert_msg_(DYNA_REC, 0, "Regcache ran out of regs");
	return (ARMReg) -1;
}

void RegCache::SaveState()
{
	memcpy(saved_locks, locks, sizeof(locks));
	memcpy(saved_xlocks, xlocks, sizeof(xlocks));
	memcpy(saved_regs, regs, sizeof(regs));
	memcpy(saved_xregs, xregs, sizeof(xregs));
}

void RegCache::LoadState()
{
	memcpy(xlocks, saved_xlocks, sizeof(xlocks));
	memcpy(locks, saved_locks, sizeof(locks));
	memcpy(regs, saved_regs, sizeof(regs));
	memcpy(xregs, saved_xregs, sizeof(xregs));
}

void RegCache::FlushR(ARMReg reg)
{
	if (reg >= NUMARMREGS)
		PanicAlert("Flushing non existent reg");
	if (!xregs[reg].free)
	{
		StoreFromRegister(xregs[reg].ppcReg);
	}
}

int RegCache::SanityCheck() const
{
	for (int i = 0; i < 32; i++) {
		if (regs[i].away) {
			if (regs[i].location.IsSimpleReg()) {
				ARMReg simple = regs[i].location.GetSimpleReg();
				if (xlocks[simple])
					return 1;
				if (xregs[simple].ppcReg != i)
					return 2;
			}
			else if (regs[i].location.IsImm())
				return 3;
		}
	}
	return 0;
}

void RegCache::DiscardRegContentsIfCached(int preg)
{
	if (regs[preg].away && regs[preg].location.IsSimpleReg())
	{
		ARMReg xr = regs[preg].location.GetSimpleReg();
		xregs[xr].free = true;
		xregs[xr].dirty = false;
		xregs[xr].ppcReg = -1;
		regs[preg].away = false;
		regs[preg].location = GetDefaultLocation(preg);
	}
}


void GPRRegCache::SetImmediate32(int preg, u32 immValue)
{
	//if (regs[preg].away == true && regs[preg].location.IsImm() && regs[preg].location.offset == immValue)
	//	return;
	DiscardRegContentsIfCached(preg);
	regs[preg].away = true;
	regs[preg].location.SetImm32(immValue);
}

void GPRRegCache::Start(MIPSState *mips, MIPSAnalyst::AnalysisResults &stats)
{
	RegCache::Start(mips, stats);
}

void FPURegCache::Start(MIPSState *mips, MIPSAnalyst::AnalysisResults &stats)
{
	RegCache::Start(mips, stats);
}

const int *GPRRegCache::GetAllocationOrder(int &count)
{
	count = sizeof(allocationOrder) / sizeof(const int);
	return allocationOrder;
}

const int *FPURegCache::GetAllocationOrder(int &count)
{
	static const int allocationOrder[] = 
	{
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
	};
	count = sizeof(allocationOrder) / sizeof(int);
	return allocationOrder;
}

Location GPRRegCache::GetDefaultLocation(int reg) const
{
	Location loc;
	loc.SetM(&mips->r[reg]);
	return loc;
}

Location FPURegCache::GetDefaultLocation(int reg) const
{
	Location loc;
	loc.SetM(&mips->f[reg]);
	return loc;
}

void RegCache::KillImmediate(int preg, bool doLoad, bool makeDirty)
{
	if (regs[preg].away)
	{
		if (regs[preg].location.IsImm())
			BindToRegister(preg, doLoad, makeDirty);
		else if (regs[preg].location.IsSimpleReg())
			xregs[RX(preg)].dirty |= makeDirty;
	}
}

void GPRRegCache::BindToRegister(int i, bool doLoad, bool makeDirty)
{
	if (!regs[i].away && regs[i].location.IsImm())
		PanicAlert("Bad immediate");

	if (!regs[i].away || (regs[i].away && regs[i].location.IsImm()))
	{
		ARMReg xr = GetFreeXReg();
		if (xregs[xr].dirty) PanicAlert("Xreg already dirty");
		if (xlocks[xr]) PanicAlert("GetFreeXReg returned locked register");
		xregs[xr].free = false;
		xregs[xr].ppcReg = i;
		xregs[xr].dirty = makeDirty || regs[i].location.IsImm();
		Location newloc;
		newloc.SetReg(xr);
		
		//if (doLoad)
		//	emit->MOV(32, newloc, regs[i].location);
		for (int j = 0; j < 32; j++)
		{
			if (i != j && regs[j].location.IsSimpleReg() && regs[j].location.GetSimpleReg() == xr)
			{
				PanicAlert("");
			}
		}
		regs[i].away = true;
		regs[i].location = newloc;
	}
	else
	{
		// reg location must be simplereg; memory locations
		// and immediates are taken care of above.
		xregs[RX(i)].dirty |= makeDirty;
	}
	if (xlocks[RX(i)]) {
		PanicAlert("Seriously WTF, this reg should have been flushed");
	}
}

void GPRRegCache::StoreFromRegister(int i)
{
	if (regs[i].away)
	{
		bool doStore;
		if (regs[i].location.IsSimpleReg())
		{
			ARMReg xr = RX(i);
			xregs[xr].free = true;
			xregs[xr].ppcReg = -1;
			doStore = xregs[xr].dirty;
			xregs[xr].dirty = false;
		}
		else
		{
			//must be immediate - do nothing
			doStore = true;
		}
		Location newLoc = GetDefaultLocation(i);
		//if (doStore)
		//	emit->MOV(32, newLoc, regs[i].location);
		regs[i].location = newLoc;
		regs[i].away = false;
	}
}

void FPURegCache::BindToRegister(int i, bool doLoad, bool makeDirty)
{
	_assert_msg_(DYNA_REC, !regs[i].location.IsImm(), "WTF - load - imm");
	if (!regs[i].away)
	{
		// Reg is at home in the memory register file. Let's pull it out.
		ARMReg xr = GetFreeXReg();
		_assert_msg_(DYNA_REC, xr < NUMARMREGS, "WTF - load - invalid reg");
		xregs[xr].ppcReg = i;
		xregs[xr].free = false;
		xregs[xr].dirty = makeDirty;
		Location newloc;
		newloc.SetReg(xr);
		if (doLoad)
		{
			//if (!regs[i].location.IsImm() && (regs[i].location.offset & 0xF))
			//{
			//	PanicAlert("WARNING - misaligned fp register location %i", i);
			//}
			//emit->MOVAPD(xr, regs[i].location);
		}
		regs[i].location = newloc;
		regs[i].away = true;
	} else {
		// There are no immediates in the FPR reg file, so we already had this in a register. Make dirty as necessary.
		xregs[RX(i)].dirty |= makeDirty;
	}
}

void FPURegCache::StoreFromRegister(int i)
{
	_assert_msg_(DYNA_REC, !regs[i].location.IsImm(), "WTF - store - imm");
	if (regs[i].away)
	{
		ARMReg xr = regs[i].location.GetSimpleReg();
		_assert_msg_(DYNA_REC, xr < NUMARMREGS, "WTF - store - invalid reg");
		xregs[xr].free = true;
		xregs[xr].dirty = false;
		xregs[xr].ppcReg = -1;
		Location newLoc = GetDefaultLocation(i);
		// emit->MOVAPD(newLoc, xr);
		regs[i].location = newLoc;
		regs[i].away = false;
	}
	else
	{
	//	_assert_msg_(DYNA_REC,0,"already stored");
	}
}

void RegCache::Flush(FlushMode mode)
{
	for (int i = 0; i < NUMARMREGS; i++) {
		if (xlocks[i])
			PanicAlert("Someone forgot to unlock X64 reg %i.", i);
	}
	for (int i = 0; i < 32; i++)
	{
		if (locks[i])
		{
			PanicAlert("Somebody forgot to unlock PPC reg %i.", i);
		}
		if (regs[i].away)
		{
			if (regs[i].location.IsSimpleReg())
			{
				ARMReg xr = RX(i);
				StoreFromRegister(i);
				xregs[xr].dirty = false;
			}
			else if (regs[i].location.IsImm())
			{
				StoreFromRegister(i);
			}
			else
			{
				_assert_msg_(DYNA_REC,0,"Jit64 - Flush unhandled case, reg %i PC: %08x", i, mips->pc);
			}
		}
	}
}
