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
#include "Core/MIPS/IR/IRRegCache.h"

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

class RiscVRegCache : public IRNativeRegCache {
public:
	RiscVRegCache(MIPSComp::JitOptions *jo);

	void Init(RiscVGen::RiscVEmitter *emitter);

	// Protect the arm register containing a MIPS register from spilling, to ensure that
	// it's being kept allocated.
	void SpillLock(IRReg reg, IRReg reg2 = IRREG_INVALID, IRReg reg3 = IRREG_INVALID, IRReg reg4 = IRREG_INVALID);
	void ReleaseSpillLock(IRReg reg, IRReg reg2 = IRREG_INVALID, IRReg reg3 = IRREG_INVALID, IRReg reg4 = IRREG_INVALID);

	void SetImm(IRReg reg, u64 immVal);
	bool IsImm(IRReg reg) const;
	u64 GetImm(IRReg reg) const;

	// May fail and return INVALID_REG if it needs flushing.
	RiscVGen::RiscVReg TryMapTempImm(IRReg);

	// Returns an ARM register containing the requested MIPS register.
	RiscVGen::RiscVReg MapReg(IRReg reg, RiscVJitConstants::MIPSMap mapFlags = RiscVJitConstants::MIPSMap::INIT);
	RiscVGen::RiscVReg MapRegAsPointer(IRReg reg);

	bool IsMapped(IRReg reg);
	bool IsMappedAsPointer(IRReg reg);
	bool IsMappedAsStaticPointer(IRReg reg);
	bool IsInRAM(IRReg reg);
	bool IsNormalized32(IRReg reg);

	void MarkDirty(RiscVGen::RiscVReg reg, bool andNormalized32 = false);
	void MarkPtrDirty(RiscVGen::RiscVReg reg);
	// Copies to another reg if specified, otherwise same reg.
	RiscVGen::RiscVReg Normalize32(IRReg reg, RiscVGen::RiscVReg destReg = RiscVGen::INVALID_REG);
	void MapIn(IRReg rs);
	void MapInIn(IRReg rd, IRReg rs);
	void MapDirtyIn(IRReg rd, IRReg rs, RiscVJitConstants::MapType type = RiscVJitConstants::MapType::AVOID_LOAD);
	void MapDirtyInIn(IRReg rd, IRReg rs, IRReg rt, RiscVJitConstants::MapType type = RiscVJitConstants::MapType::AVOID_LOAD);
	void MapDirtyDirtyIn(IRReg rd1, IRReg rd2, IRReg rs, RiscVJitConstants::MapType type = RiscVJitConstants::MapType::AVOID_LOAD);
	void MapDirtyDirtyInIn(IRReg rd1, IRReg rd2, IRReg rs, IRReg rt, RiscVJitConstants::MapType type = RiscVJitConstants::MapType::AVOID_LOAD);
	void FlushBeforeCall();
	void FlushAll();
	void FlushR(IRReg r);
	void FlushRiscVReg(RiscVGen::RiscVReg r);
	void DiscardR(IRReg r);

	RiscVGen::RiscVReg GetAndLockTempR();

	RiscVGen::RiscVReg R(IRReg preg); // Returns a cached register, while checking that it's NOT mapped as a pointer
	RiscVGen::RiscVReg RPtr(IRReg preg); // Returns a cached register, if it has been mapped as a pointer

	// These are called once on startup to generate functions, that you should then call.
	void EmitLoadStaticRegisters();
	void EmitSaveStaticRegisters();

protected:
	void SetupInitialRegs() override;
	const StaticAllocation *GetStaticAllocations(int &count) override;

private:
	const RiscVGen::RiscVReg *GetMIPSAllocationOrder(int &count);
	void MapRegTo(RiscVGen::RiscVReg reg, IRReg mipsReg, RiscVJitConstants::MIPSMap mapFlags);
	RiscVGen::RiscVReg AllocateReg();
	RiscVGen::RiscVReg FindBestToSpill(bool unusedOnly, bool *clobbered);
	RiscVGen::RiscVReg RiscVRegForFlush(IRReg r);
	void SetRegImm(RiscVGen::RiscVReg reg, u64 imm);
	void AddMemBase(RiscVGen::RiscVReg reg);
	int GetMipsRegOffset(IRReg r);

	bool IsValidReg(IRReg r) const;
	bool IsValidRegNoZero(IRReg r) const;

	RiscVGen::RiscVEmitter *emit_ = nullptr;

	enum {
		NUM_RVREG = 32,
	};
};
