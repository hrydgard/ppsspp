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
#include "Common/Arm64Emitter.h"

namespace Arm64JitConstants {

const Arm64Gen::ARM64Reg DOWNCOUNTREG = Arm64Gen::W24;
const Arm64Gen::ARM64Reg FLAGTEMPREG = Arm64Gen::X25;
const Arm64Gen::ARM64Reg JITBASEREG = Arm64Gen::X26;
const Arm64Gen::ARM64Reg CTXREG = Arm64Gen::X27;
const Arm64Gen::ARM64Reg MEMBASEREG = Arm64Gen::X28;
const Arm64Gen::ARM64Reg SCRATCH1_64 = Arm64Gen::X16;
const Arm64Gen::ARM64Reg SCRATCH2_64 = Arm64Gen::X17;
const Arm64Gen::ARM64Reg SCRATCH1 = Arm64Gen::W16;
const Arm64Gen::ARM64Reg SCRATCH2 = Arm64Gen::W17;

enum {
	TOTAL_MAPPABLE_MIPSREGS = 36,
};

enum RegMIPSLoc {
	ML_IMM,
	ML_ARMREG,
	// In an arm reg, but an adjusted pointer (not pointerified - unaligned.)
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

}  // namespace

namespace MIPSAnalyst {
struct AnalysisResults;
};

typedef int MIPSReg;

struct RegARM64 {
	MIPSGPReg mipsReg;  // if -1, no mipsreg attached.
	bool isDirty;  // Should the register be written back?
	bool pointerified;  // Has used movk to move the memory base into the top part of the reg. Note - still usable as 32-bit reg!
	bool tempLocked; // Reserved for a temp register.
};

struct RegMIPS {
	// Where is this MIPS register?
	Arm64JitConstants::RegMIPSLoc loc;
	// Data (both or only one may be used, depending on loc.)
	u64 imm;
	Arm64Gen::ARM64Reg reg;  // reg index
	bool spillLock;  // if true, this register cannot be spilled.
	bool isStatic;  // if true, this register will not be written back to ram by the regcache
	// If loc == ML_MEM, it's back in its location in the CPU context struct.
};

namespace MIPSComp {
	struct JitOptions;
	struct JitState;
}

class Arm64RegCache {
public:
	Arm64RegCache(MIPSState *mipsState, MIPSComp::JitState *js, MIPSComp::JitOptions *jo);
	~Arm64RegCache() {}

	void Init(Arm64Gen::ARM64XEmitter *emitter);
	void Start(MIPSAnalyst::AnalysisResults &stats);

	// Protect the arm register containing a MIPS register from spilling, to ensure that
	// it's being kept allocated.
	void SpillLock(MIPSGPReg reg, MIPSGPReg reg2 = MIPS_REG_INVALID, MIPSGPReg reg3 = MIPS_REG_INVALID, MIPSGPReg reg4 = MIPS_REG_INVALID);
	void ReleaseSpillLock(MIPSGPReg reg, MIPSGPReg reg2 = MIPS_REG_INVALID, MIPSGPReg reg3 = MIPS_REG_INVALID, MIPSGPReg reg4 = MIPS_REG_INVALID);
	void ReleaseSpillLocksAndDiscardTemps();

	void SetImm(MIPSGPReg reg, u64 immVal);
	bool IsImm(MIPSGPReg reg) const;
	bool IsPureImm(MIPSGPReg reg) const;
	u64 GetImm(MIPSGPReg reg) const;
	// Optimally set a register to an imm value (possibly using another register.)
	void SetRegImm(Arm64Gen::ARM64Reg reg, u64 imm);

	// May fail and return INVALID_REG if it needs flushing.
	Arm64Gen::ARM64Reg TryMapTempImm(MIPSGPReg);

	// Returns an ARM register containing the requested MIPS register.
	Arm64Gen::ARM64Reg MapReg(MIPSGPReg reg, int mapFlags = 0);
	Arm64Gen::ARM64Reg MapRegAsPointer(MIPSGPReg reg);

	bool IsMapped(MIPSGPReg reg);
	bool IsMappedAsPointer(MIPSGPReg reg);
	bool IsInRAM(MIPSGPReg reg);

	void MarkDirty(Arm64Gen::ARM64Reg reg);
	void MapIn(MIPSGPReg rs);
	void MapInIn(MIPSGPReg rd, MIPSGPReg rs);
	void MapDirtyIn(MIPSGPReg rd, MIPSGPReg rs, bool avoidLoad = true);
	void MapDirtyInIn(MIPSGPReg rd, MIPSGPReg rs, MIPSGPReg rt, bool avoidLoad = true);
	void MapDirtyDirtyIn(MIPSGPReg rd1, MIPSGPReg rd2, MIPSGPReg rs, bool avoidLoad = true);
	void MapDirtyDirtyInIn(MIPSGPReg rd1, MIPSGPReg rd2, MIPSGPReg rs, MIPSGPReg rt, bool avoidLoad = true);
	void FlushArmReg(Arm64Gen::ARM64Reg r);
	void FlushBeforeCall();
	void FlushAll();
	void FlushR(MIPSGPReg r);
	void DiscardR(MIPSGPReg r);

	Arm64Gen::ARM64Reg GetAndLockTempR();

	Arm64Gen::ARM64Reg R(MIPSGPReg preg); // Returns a cached register, while checking that it's NOT mapped as a pointer
	Arm64Gen::ARM64Reg RPtr(MIPSGPReg preg); // Returns a cached register, if it has been mapped as a pointer

	void SetEmitter(Arm64Gen::ARM64XEmitter *emitter) { emit_ = emitter; }

	// For better log output only.
	void SetCompilerPC(u32 compilerPC) { compilerPC_ = compilerPC; }

	int GetMipsRegOffset(MIPSGPReg r);

	// These are called once on startup to generate functions, that you should then call.
	void EmitLoadStaticRegisters();
	void EmitSaveStaticRegisters();

private:
	struct StaticAllocation {
		MIPSGPReg mr;
		Arm64Gen::ARM64Reg ar;
		bool pointerified;
	};
	const StaticAllocation *GetStaticAllocations(int &count);
	const Arm64Gen::ARM64Reg *GetMIPSAllocationOrder(int &count);
	void MapRegTo(Arm64Gen::ARM64Reg reg, MIPSGPReg mipsReg, int mapFlags);
	Arm64Gen::ARM64Reg AllocateReg();
	Arm64Gen::ARM64Reg FindBestToSpill(bool unusedOnly, bool *clobbered);
	Arm64Gen::ARM64Reg ARM64RegForFlush(MIPSGPReg r);

	MIPSState *mips_;
	Arm64Gen::ARM64XEmitter *emit_;
	MIPSComp::JitState *js_;
	MIPSComp::JitOptions *jo_;
	u32 compilerPC_;

	enum {
		NUM_ARMREG = 32,  // 31 actual registers, plus the zero/sp register which is not mappable.
		NUM_MIPSREG = Arm64JitConstants::TOTAL_MAPPABLE_MIPSREGS,
	};

	RegARM64 ar[NUM_ARMREG];
	RegMIPS mr[NUM_MIPSREG];
};
