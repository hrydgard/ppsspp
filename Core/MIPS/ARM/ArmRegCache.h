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

#include "ArmEmitter.h"
#include "../MIPS.h"
#include "../MIPSAnalyst.h"
#include "ArmABI.h"

using namespace ArmGen;

// R2 to R9: mapped MIPS regs
// R11 = base pointer
// R12 = MIPS context
// R14 = code pointers

// Special MIPS registers: 
enum {
	MIPSREG_HI = 32,
	MIPSREG_LO = 33,
	TOTAL_MAPPABLE_MIPSREGS = 34,
};

typedef int MIPSReg;

struct RegARM {
	int mipsReg;  // if -1, no mipsreg attached.
	bool isDirty;  // Should the register be written back?
	bool allocLock;  // if true, this ARM register cannot be used for allocation.
	bool spillLock;  // if true, this register cannot be spilled.
};

enum RegMIPSLoc {
	ML_IMM,
	ML_ARMREG,
	ML_MEM,
};

struct RegMIPS {
	// Where is this MIPS register?
	RegMIPSLoc loc;
	// Data (only one of these is used, depending on loc. Could make a union).
	u32 imm;
	ARMReg reg;
	// If loc == ML_MEM, it's back in its location in the CPU context struct.
};

enum {
	MAP_DIRTY = 1,
	MAP_INITVAL = 2,
};

class ArmRegCache
{
public:
	ArmRegCache(MIPSState *mips);
	~ArmRegCache() {}

	void Init(ARMXEmitter *emitter);
	void Start(MIPSAnalyst::AnalysisResults &stats);

	// void AllocLock(ARMReg reg, ARMReg reg2 = INVALID_REG, ARMReg reg3 = INVALID_REG);
	// void ReleaseAllocLock(ARMReg reg);

	// Protect the arm register containing a MIPS register from spilling, to ensure that
	// it's being kept allocated.
	void SpillLock(MIPSReg reg, MIPSReg reg2 = -1, MIPSReg reg3 = -1);
	void ReleaseSpillLocks();

	void SetImm(MIPSReg reg, u32 immVal);
	bool IsImm(MIPSReg reg) const;
	u32 GetImm(MIPSReg reg) const;

	// Returns an ARM register containing the requested MIPS register.
	ARMReg MapReg(MIPSReg reg, int mapFlags = 0);
	void FlushArmReg(ARMReg r);
	void FlushMipsReg(MIPSReg r);

	void FlushAll();

	ARMReg R(int preg); // Returns a cached register

	void SetEmitter(ARMXEmitter *emitter) { emit = emitter; }

private:
	int GetMipsRegOffset(MIPSReg r);
	MIPSState *mips_;
	ARMXEmitter *emit;

	enum {
		NUM_ARMREG = 16,
		NUM_MIPSREG = TOTAL_MAPPABLE_MIPSREGS,
	};

	RegARM ar[16];
	RegMIPS mr[32];
};
