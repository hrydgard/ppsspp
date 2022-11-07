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

#include "Core/MIPS/MIPS.h"
#include "ArmEmitter.h"

namespace ArmJitConstants {

const ArmGen::ARMReg JITBASEREG = ArmGen::R9;
const ArmGen::ARMReg CTXREG = ArmGen::R10;
const ArmGen::ARMReg MEMBASEREG = ArmGen::R11;
const ArmGen::ARMReg SCRATCHREG1 = ArmGen::R0;
const ArmGen::ARMReg SCRATCHREG2 = ArmGen::R14;
const ArmGen::ARMReg DOWNCOUNTREG = ArmGen::R7;

enum {
	TOTAL_MAPPABLE_MIPSREGS = 36,
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

// Initing is the default so the flag is reversed.
enum {
	MAP_DIRTY = 1,
	MAP_NOINIT = 2 | MAP_DIRTY,
};

}

namespace MIPSAnalyst {
struct AnalysisResults;
};

// R1 to R6: mapped MIPS regs
// R8 = flags (maybe we could do better here?)
// R9 = code pointers
// R10 = MIPS context
// R11 = base pointer
// R14 = scratch (actually LR)


typedef int MIPSReg;

struct RegARM {
	MIPSGPReg mipsReg;  // if -1, no mipsreg attached.
	bool isDirty;  // Should the register be written back?
};

struct RegMIPS {
	// Where is this MIPS register?
	ArmJitConstants::RegMIPSLoc loc;
	// Data (both or only one may be used, depending on loc.)
	u32 imm;
	ArmGen::ARMReg reg;  // reg index
	bool spillLock;  // if true, this register cannot be spilled.
	// If loc == ML_MEM, it's back in its location in the CPU context struct.
};

namespace MIPSComp {
	struct JitOptions;
	struct JitState;
}

class ArmRegCache {
public:
	ArmRegCache(MIPSState *mipsState, MIPSComp::JitState *js, MIPSComp::JitOptions *jo);
	~ArmRegCache() {}

	void Init(ArmGen::ARMXEmitter *emitter);
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
	void SetRegImm(ArmGen::ARMReg reg, u32 imm);

	// Returns an ARM register containing the requested MIPS register.
	ArmGen::ARMReg MapReg(MIPSGPReg reg, int mapFlags = 0);
	ArmGen::ARMReg MapRegAsPointer(MIPSGPReg reg);  // read-only, non-dirty.

	bool IsMapped(MIPSGPReg reg);
	bool IsMappedAsPointer(MIPSGPReg reg);

	void MapInIn(MIPSGPReg rd, MIPSGPReg rs);
	void MapDirtyIn(MIPSGPReg rd, MIPSGPReg rs, bool avoidLoad = true);
	void MapDirtyInIn(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, bool avoidLoad = true);
	void MapDirtyDirtyIn(MIPSGPReg rd1, MIPSGPReg rd2, MIPSGPReg rs, bool avoidLoad = true);
	void MapDirtyDirtyInIn(MIPSGPReg rd1, MIPSGPReg rd2, MIPSGPReg rs, MIPSGPReg rt, bool avoidLoad = true);
	void FlushArmReg(ArmGen::ARMReg r);
	void FlushR(MIPSGPReg r);
	void FlushBeforeCall();
	void FlushAll();
	void DiscardR(MIPSGPReg r);

	ArmGen::ARMReg R(MIPSGPReg preg); // Returns a cached register, while checking that it's NOT mapped as a pointer
	ArmGen::ARMReg RPtr(MIPSGPReg preg); // Returns a cached register, while checking that it's mapped as a pointer

	void SetEmitter(ArmGen::ARMXEmitter *emitter) { emit_ = emitter; }

	// For better log output only.
	void SetCompilerPC(u32 compilerPC) { compilerPC_ = compilerPC; }

	int GetMipsRegOffset(MIPSGPReg r);

private:
	const ArmGen::ARMReg *GetMIPSAllocationOrder(int &count);
	void MapRegTo(ArmGen::ARMReg reg, MIPSGPReg mipsReg, int mapFlags);
	int FlushGetSequential(MIPSGPReg startMipsReg, bool allowFlushImm);
	ArmGen::ARMReg FindBestToSpill(bool unusedOnly, bool *clobbered);
		
	MIPSState *mips_;
	ArmGen::ARMXEmitter *emit_;
	MIPSComp::JitState *js_;
	MIPSComp::JitOptions *jo_;
	u32 compilerPC_;

	enum {
		NUM_ARMREG = 16,
		NUM_MIPSREG = ArmJitConstants::TOTAL_MAPPABLE_MIPSREGS,
	};

	RegARM ar[NUM_ARMREG];
	RegMIPS mr[NUM_MIPSREG];
};
