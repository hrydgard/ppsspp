// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include "../MIPS.h"
#include "../MIPSAnalyst.h"
#include "ArmEmitter.h"

using namespace ArmGen;

#define CTXREG (R10)
#define MEMBASEREG (R11)
#define SCRATCHREG1 (R0)
#define SCRATCHREG2 (R14)
#define DOWNCOUNTREG (R7)

// R1 to R6: mapped MIPS regs
// R8 = flags (maybe we could do better here?)
// R9 = code pointers
// R10 = MIPS context
// R11 = base pointer
// R14 = scratch (actually LR)

enum {
	TOTAL_MAPPABLE_MIPSREGS = 36,
};

typedef int MIPSReg;

struct RegARM {
	MIPSGPReg mipsReg;  // if -1, no mipsreg attached.
	bool isDirty;  // Should the register be written back?
};

enum RegMIPSLoc {
	ML_IMM,
	ML_ARMREG,
	// In an arm reg, but as a pre-adjusted pointer, not the actual reg.
	ML_ARMREG_AS_PTR,
	// In an arm reg, but also has a known immediate value.
	ML_ARMREG_IMM,
	ML_MEM,
};

struct RegMIPS {
	// Where is this MIPS register?
	RegMIPSLoc loc;
	// Data (only one of these is used, depending on loc. Could make a union).
	u32 imm;
	ARMReg reg;  // reg index
	bool spillLock;  // if true, this register cannot be spilled.
	// If loc == ML_MEM, it's back in its location in the CPU context struct.
};

#undef MAP_DIRTY
#undef MAP_NOINIT
// Initing is the default so the flag is reversed.
enum {
	MAP_DIRTY = 1,
	MAP_NOINIT = 2,
};

namespace MIPSComp {
	struct ArmJitOptions;
}

class ArmRegCache {
public:
	ArmRegCache(MIPSState *mips, MIPSComp::ArmJitOptions *options);
	~ArmRegCache() {}

	void Init(ARMXEmitter *emitter);
	void Start(MIPSAnalyst::AnalysisResults &stats);

	// Protect the arm register containing a MIPS register from spilling, to ensure that
	// it's being kept allocated.
	void SpillLock(MIPSGPReg reg, MIPSGPReg reg2 = MIPS_REG_INVALID, MIPSGPReg reg3 = MIPS_REG_INVALID, MIPSGPReg reg4 = MIPS_REG_INVALID);
	void ReleaseSpillLock(MIPSGPReg reg);
	void ReleaseSpillLocks();

	void SetImm(MIPSGPReg reg, u32 immVal);
	bool IsImm(MIPSGPReg reg) const;
	u32 GetImm(MIPSGPReg reg) const;
	// Optimally set a register to an imm value (possibly using another register.)
	void SetRegImm(ARMReg reg, u32 imm);

	// Returns an ARM register containing the requested MIPS register.
	ARMReg MapReg(MIPSGPReg reg, int mapFlags = 0);
	ARMReg MapRegAsPointer(MIPSGPReg reg);  // read-only, non-dirty.

	bool IsMappedAsPointer(MIPSGPReg reg);

	void MapInIn(MIPSGPReg rd, MIPSGPReg rs);
	void MapDirtyIn(MIPSGPReg rd, MIPSGPReg rs, bool avoidLoad = true);
	void MapDirtyInIn(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, bool avoidLoad = true);
	void MapDirtyDirtyIn(MIPSGPReg rd1, MIPSGPReg rd2, MIPSGPReg rs, bool avoidLoad = true);
	void MapDirtyDirtyInIn(MIPSGPReg rd1, MIPSGPReg rd2, MIPSGPReg rs, MIPSGPReg rt, bool avoidLoad = true);
	void FlushArmReg(ARMReg r);
	void FlushR(MIPSGPReg r);
	void FlushBeforeCall();
	void FlushAll();
	void DiscardR(MIPSGPReg r);

	ARMReg R(MIPSGPReg preg); // Returns a cached register, while checking that it's NOT mapped as a pointer
	ARMReg RPtr(MIPSGPReg preg); // Returns a cached register, while checking that it's mapped as a pointer

	void SetEmitter(ARMXEmitter *emitter) { emit_ = emitter; }

	// For better log output only.
	void SetCompilerPC(u32 compilerPC) { compilerPC_ = compilerPC; }

	int GetMipsRegOffset(MIPSGPReg r);

private:
	const ARMReg *GetMIPSAllocationOrder(int &count);
	void MapRegTo(ARMReg reg, MIPSGPReg mipsReg, int mapFlags);
	int FlushGetSequential(MIPSGPReg startMipsReg, bool allowFlushImm);
	ARMReg FindBestToSpill(bool unusedOnly);
		
	MIPSState *mips_;
	MIPSComp::ArmJitOptions *options_;
	ARMXEmitter *emit_;
	u32 compilerPC_;

	enum {
		NUM_ARMREG = 16,
		NUM_MIPSREG = TOTAL_MAPPABLE_MIPSREGS,
	};

	RegARM ar[NUM_ARMREG];
	RegMIPS mr[NUM_MIPSREG];
};
