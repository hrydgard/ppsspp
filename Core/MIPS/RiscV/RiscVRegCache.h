// Copyright (c) 2023- PPSSPP Project.

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

#include "Common/RiscVEmitter.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/IR/IRJit.h"

namespace RiscVJitConstants {

// Note: we don't support 32-bit or 128-bit CPUs currently.
constexpr int XLEN = 64;

const RiscVGen::RiscVReg DOWNCOUNTREG = RiscVGen::X24;
const RiscVGen::RiscVReg JITBASEREG = RiscVGen::X25;
const RiscVGen::RiscVReg CTXREG = RiscVGen::X26;
const RiscVGen::RiscVReg MEMBASEREG = RiscVGen::X27;
// TODO: Experiment.  X7-X13 are compressed regs.  X8/X9 are saved so nice for static alloc, though.
const RiscVGen::RiscVReg SCRATCH1 = RiscVGen::X10;
const RiscVGen::RiscVReg SCRATCH2 = RiscVGen::X11;

// Have to account for all of them due to temps, etc.
constexpr int TOTAL_MAPPABLE_MIPSREGS = 256;

enum class MIPSLoc {
	IMM,
	RVREG,
	// In a native reg, but an adjusted pointer (not pointerified - unaligned.)
	RVREG_AS_PTR,
	// In a native reg, but also has a known immediate value.
	RVREG_IMM,
	MEM,
};

// Initing is the default so the flag is reversed.
enum class MIPSMap {
	INIT = 0,
	DIRTY = 1,
	NOINIT = 2 | DIRTY,
	MARK_NORM32 = 4,
};
static inline MIPSMap operator |(const MIPSMap &lhs, const MIPSMap &rhs) {
	return MIPSMap((int)lhs | (int)rhs);
}
static inline MIPSMap operator &(const MIPSMap &lhs, const MIPSMap &rhs) {
	return MIPSMap((int)lhs & (int)rhs);
}

enum class MapType {
	AVOID_LOAD,
	AVOID_LOAD_MARK_NORM32,
	ALWAYS_LOAD,
};

} // namespace RiscVJitConstants

namespace MIPSComp {
struct JitOptions;
}

// Not using IRReg since this can be -1.
typedef int IRRegIndex;
constexpr IRRegIndex IRREG_INVALID = -1;

struct RegStatusRiscV {
	IRRegIndex mipsReg;  // if -1, no mipsreg attached.
	bool isDirty;  // Should the register be written back?
	bool pointerified;  // Has added the memory base into the top part of the reg. Note - still usable as 32-bit reg (in only some cases.)
	bool tempLocked; // Reserved for a temp register.
	bool normalized32;  // 32 bits sign extended to XLEN.  RISC-V can't always address just the low 32-bits, so this matters.
};

struct RegStatusMIPS {
	// Where is this MIPS register?
	RiscVJitConstants::MIPSLoc loc;
	// Data (both or only one may be used, depending on loc.)
	u64 imm;
	RiscVGen::RiscVReg reg;  // reg index
	bool spillLock;  // if true, this register cannot be spilled.
	bool isStatic;  // if true, this register will not be written back to ram by the regcache
	// If loc == ML_MEM, it's back in its location in the CPU context struct.
};

class RiscVRegCache {
public:
	RiscVRegCache(MIPSState *mipsState, MIPSComp::JitOptions *jo);
	~RiscVRegCache() {}

	void Init(RiscVGen::RiscVEmitter *emitter);
	void Start(MIPSComp::IRBlock *irBlock);
	void SetIRIndex(int index) {
		irIndex_ = index;
	}

	// Protect the arm register containing a MIPS register from spilling, to ensure that
	// it's being kept allocated.
	void SpillLock(IRRegIndex reg, IRRegIndex reg2 = IRREG_INVALID, IRRegIndex reg3 = IRREG_INVALID, IRRegIndex reg4 = IRREG_INVALID);
	void ReleaseSpillLock(IRRegIndex reg, IRRegIndex reg2 = IRREG_INVALID, IRRegIndex reg3 = IRREG_INVALID, IRRegIndex reg4 = IRREG_INVALID);
	void ReleaseSpillLocksAndDiscardTemps();

	void SetImm(IRRegIndex reg, u64 immVal);
	bool IsImm(IRRegIndex reg) const;
	u64 GetImm(IRRegIndex reg) const;

	// May fail and return INVALID_REG if it needs flushing.
	RiscVGen::RiscVReg TryMapTempImm(IRRegIndex);

	// Returns an ARM register containing the requested MIPS register.
	RiscVGen::RiscVReg MapReg(IRRegIndex reg, RiscVJitConstants::MIPSMap mapFlags = RiscVJitConstants::MIPSMap::INIT);
	RiscVGen::RiscVReg MapRegAsPointer(IRRegIndex reg);

	bool IsMapped(IRRegIndex reg);
	bool IsMappedAsPointer(IRRegIndex reg);
	bool IsMappedAsStaticPointer(IRRegIndex reg);
	bool IsInRAM(IRRegIndex reg);
	bool IsNormalized32(IRRegIndex reg);

	void MarkDirty(RiscVGen::RiscVReg reg, bool andNormalized32 = false);
	void MarkPtrDirty(RiscVGen::RiscVReg reg);
	// Copies to another reg if specified, otherwise same reg.
	RiscVGen::RiscVReg Normalize32(IRRegIndex reg, RiscVGen::RiscVReg destReg = RiscVGen::INVALID_REG);
	void MapIn(IRRegIndex rs);
	void MapInIn(IRRegIndex rd, IRRegIndex rs);
	void MapDirtyIn(IRRegIndex rd, IRRegIndex rs, RiscVJitConstants::MapType type = RiscVJitConstants::MapType::AVOID_LOAD);
	void MapDirtyInIn(IRRegIndex rd, IRRegIndex rs, IRRegIndex rt, RiscVJitConstants::MapType type = RiscVJitConstants::MapType::AVOID_LOAD);
	void MapDirtyDirtyIn(IRRegIndex rd1, IRRegIndex rd2, IRRegIndex rs, RiscVJitConstants::MapType type = RiscVJitConstants::MapType::AVOID_LOAD);
	void MapDirtyDirtyInIn(IRRegIndex rd1, IRRegIndex rd2, IRRegIndex rs, IRRegIndex rt, RiscVJitConstants::MapType type = RiscVJitConstants::MapType::AVOID_LOAD);
	void FlushBeforeCall();
	void FlushAll();
	void FlushR(IRRegIndex r);
	void FlushRiscVReg(RiscVGen::RiscVReg r);
	void DiscardR(IRRegIndex r);

	RiscVGen::RiscVReg GetAndLockTempR();

	RiscVGen::RiscVReg R(IRRegIndex preg); // Returns a cached register, while checking that it's NOT mapped as a pointer
	RiscVGen::RiscVReg RPtr(IRRegIndex preg); // Returns a cached register, if it has been mapped as a pointer

	// These are called once on startup to generate functions, that you should then call.
	void EmitLoadStaticRegisters();
	void EmitSaveStaticRegisters();

private:
	struct StaticAllocation {
		IRRegIndex mr;
		RiscVGen::RiscVReg ar;
		bool pointerified;
	};
	const StaticAllocation *GetStaticAllocations(int &count);
	const RiscVGen::RiscVReg *GetMIPSAllocationOrder(int &count);
	void MapRegTo(RiscVGen::RiscVReg reg, IRRegIndex mipsReg, RiscVJitConstants::MIPSMap mapFlags);
	RiscVGen::RiscVReg AllocateReg();
	RiscVGen::RiscVReg FindBestToSpill(bool unusedOnly, bool *clobbered);
	RiscVGen::RiscVReg RiscVRegForFlush(IRRegIndex r);
	void SetRegImm(RiscVGen::RiscVReg reg, u64 imm);
	void AddMemBase(RiscVGen::RiscVReg reg);
	int GetMipsRegOffset(IRRegIndex r);

	bool IsValidReg(IRRegIndex r) const;
	bool IsValidRegNoZero(IRRegIndex r) const;

	void SetupInitialRegs();

	MIPSState *mips_;
	RiscVGen::RiscVEmitter *emit_ = nullptr;
	MIPSComp::JitOptions *jo_;
	MIPSComp::IRBlock *irBlock_ = nullptr;
	int irIndex_ = 0;

	enum {
		NUM_RVREG = 32,  // 31 actual registers, plus the zero/sp register which is not mappable.
		NUM_MIPSREG = RiscVJitConstants::TOTAL_MAPPABLE_MIPSREGS,
	};

	RegStatusRiscV ar[NUM_RVREG]{};
	RegStatusMIPS mr[NUM_MIPSREG]{};

	bool initialReady_ = false;
	bool pendingUnlock_ = false;
	RegStatusRiscV arInitial_[NUM_RVREG];
	RegStatusMIPS mrInitial_[NUM_MIPSREG];
};
