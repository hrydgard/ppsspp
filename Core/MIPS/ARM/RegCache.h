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

#ifndef _JITARMREGCACHE_H
#define _JITARMREGCACHE_H

#include "ArmEmitter.h"
#include "../MIPS.h"
#include "../MIPSAnalyst.h"
#include "ArmABI.h"

using namespace ArmGen;

// This ARM Register cache actually pre loads the most used registers before
// the block to increase speed since every memory load requires two
// instructions to load it. We are going to use R0-RMAX as registers for the
// use of MIPS Registers.
// Allocation order as follows
#define ARMREGS 16
// Allocate R0 to R9 for MIPS first.
// For General registers on the host side, start with R14 and go down as we go
// R13 is reserved for our stack pointer, don't ever use that. Unless you save
// it
// So we have R14, R12, R11, R10 to work with instructions

struct MIPSCachedReg
{
	const u8 *location;
};

struct JRCPPC
{
	u32 MIPSReg; // Tied to which MIPS Register
	ARMReg Reg; // Tied to which ARM Register
	u32 LastLoad;
};
struct JRCReg
{
	ARMReg Reg; // Which reg this is.
	bool free;
};
class ArmRegCache
{
private:
	MIPSCachedReg regs[32];
	JRCPPC ArmCRegs[ARMREGS];
	JRCReg ArmRegs[ARMREGS]; // Four registers remaining

	int NUMMIPSREG;  //   + LO, HI, ...
	int NUMARMREG;

	ARMReg *GetAllocationOrder(int &count);
	ARMReg *GetMIPSAllocationOrder(int &count);

	MIPSState *mips_;

protected:
	ARMXEmitter *emit;

public:
	ArmRegCache(MIPSState *mips);
	~ArmRegCache() {}

	void Init(ARMXEmitter *emitter);
	void Start(MIPSAnalyst::AnalysisResults &stats);

	void SetEmitter(ARMXEmitter *emitter) {emit = emitter;}

	// TODO: Add a way to lock MIPS registers so they aren't kicked out when you don't expect it.

	ARMReg GetReg(bool AutoLock = true); // Return a ARM register we can use.
	void Lock(ARMReg R0);
	void Unlock(ARMReg R0, ARMReg R1 = INVALID_REG, ARMReg R2 = INVALID_REG, ARMReg R3 = INVALID_REG);
	void Flush();
	ARMReg R(int preg); // Returns a cached register

};




#endif
