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
#include "Core/MIPS/RiscV/RiscVRegCache.h"

struct FPURegStatusRiscV {
	int mipsReg;  // if -1, no mipsreg attached.
	bool isDirty;  // Should the register be written back?
};

struct FPURegStatusMIPS {
	// Where is this MIPS register?
	RiscVJitConstants::MIPSLoc loc;
	// Index from F0.
	int reg;

	bool spillLock;  // if true, this register cannot be spilled.
	// If loc == ML_MEM, it's back in its location in the CPU context struct.
};

namespace MIPSComp {
struct JitOptions;
}

class RiscVRegCacheFPU {
public:
	RiscVRegCacheFPU(MIPSState *mipsState, MIPSComp::JitOptions *jo);
	~RiscVRegCacheFPU() {}

	void Init(RiscVGen::RiscVEmitter *emitter);
	void Start(MIPSComp::IRBlock *irBlock);
	void SetIRIndex(int index) {
		irIndex_ = index;
	}

	// Protect the RISC-V register containing a MIPS register from spilling, to ensure that
	// it's being kept allocated.
	void SpillLock(IRRegIndex reg, IRRegIndex reg2 = IRREG_INVALID, IRRegIndex reg3 = IRREG_INVALID, IRRegIndex reg4 = IRREG_INVALID);
	void ReleaseSpillLock(IRRegIndex reg, IRRegIndex reg2 = IRREG_INVALID, IRRegIndex reg3 = IRREG_INVALID, IRRegIndex reg4 = IRREG_INVALID);
	void ReleaseSpillLocksAndDiscardTemps();

	// Returns a RISC-V register containing the requested MIPS register.
	RiscVGen::RiscVReg MapReg(IRRegIndex reg, RiscVJitConstants::MIPSMap mapFlags = RiscVJitConstants::MIPSMap::INIT);

	bool IsMapped(IRRegIndex r);
	bool IsInRAM(IRRegIndex r);

	void MapInIn(IRRegIndex rd, IRRegIndex rs);
	void MapDirtyIn(IRRegIndex rd, IRRegIndex rs, bool avoidLoad = true);
	void MapDirtyInIn(IRRegIndex rd, IRRegIndex rs, IRRegIndex rt, bool avoidLoad = true);
	RiscVGen::RiscVReg MapDirtyInTemp(IRRegIndex rd, IRRegIndex rs, bool avoidLoad = true);
	void Map4DirtyIn(IRRegIndex rdbase, IRRegIndex rsbase, bool avoidLoad = true);
	void Map4DirtyInIn(IRRegIndex rdbase, IRRegIndex rsbase, IRRegIndex rtbase, bool avoidLoad = true);
	RiscVGen::RiscVReg Map4DirtyInTemp(IRRegIndex rdbase, IRRegIndex rsbase, bool avoidLoad = true);
	void FlushBeforeCall();
	void FlushAll();
	void FlushR(IRRegIndex r);
	void FlushRiscVReg(RiscVGen::RiscVReg r);
	void DiscardR(IRRegIndex r);

	RiscVGen::RiscVReg R(int preg); // Returns a cached register

private:
	const RiscVGen::RiscVReg *GetMIPSAllocationOrder(int &count);
	RiscVGen::RiscVReg AllocateReg();
	RiscVGen::RiscVReg FindBestToSpill(bool unusedOnly, bool *clobbered);
	RiscVGen::RiscVReg RiscVRegForFlush(IRRegIndex r);
	int GetMipsRegOffset(IRRegIndex r);

	bool IsValidReg(IRRegIndex r) const;

	void SetupInitialRegs();

	MIPSState *mips_;
	RiscVGen::RiscVEmitter *emit_ = nullptr;
	MIPSComp::JitOptions *jo_;
	MIPSComp::IRBlock *irBlock_ = nullptr;
	int irIndex_ = 0;

	enum {
		// On RiscV, each of the 32 registers are full 128-bit. No sharing of components!
		NUM_RVFPUREG = 32,
		NUM_MIPSFPUREG = RiscVJitConstants::TOTAL_MAPPABLE_MIPSREGS - 32,
	};

	FPURegStatusRiscV ar[NUM_RVFPUREG];
	FPURegStatusMIPS mr[NUM_MIPSFPUREG];

	bool pendingFlush_ = false;
	bool pendingUnlock_ = false;
	bool initialReady_ = false;
	FPURegStatusRiscV arInitial_[NUM_RVFPUREG];
	FPURegStatusMIPS mrInitial_[NUM_MIPSFPUREG];
};
