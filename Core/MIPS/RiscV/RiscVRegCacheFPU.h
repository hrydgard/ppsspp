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

namespace MIPSComp {
struct JitOptions;
}

class RiscVRegCacheFPU : public IRNativeRegCacheBase {
public:
	RiscVRegCacheFPU(MIPSComp::JitOptions *jo);

	void Init(RiscVGen::RiscVEmitter *emitter);

	// Returns a RISC-V register containing the requested MIPS register.
	RiscVGen::RiscVReg MapReg(IRReg reg, MIPSMap mapFlags = MIPSMap::INIT);

	void MapInIn(IRReg rd, IRReg rs);
	void MapDirtyIn(IRReg rd, IRReg rs, bool avoidLoad = true);
	void MapDirtyInIn(IRReg rd, IRReg rs, IRReg rt, bool avoidLoad = true);
	RiscVGen::RiscVReg MapDirtyInTemp(IRReg rd, IRReg rs, bool avoidLoad = true);
	void Map4DirtyIn(IRReg rdbase, IRReg rsbase, bool avoidLoad = true);
	void Map4DirtyInIn(IRReg rdbase, IRReg rsbase, IRReg rtbase, bool avoidLoad = true);
	RiscVGen::RiscVReg Map4DirtyInTemp(IRReg rdbase, IRReg rsbase, bool avoidLoad = true);
	void FlushBeforeCall();
	void FlushR(IRReg r);
	void DiscardR(IRReg r);

	RiscVGen::RiscVReg R(IRReg preg); // Returns a cached register

protected:
	void SetupInitialRegs() override;
	const int *GetAllocationOrder(MIPSLoc type, int &count, int &base) const override;

	void LoadNativeReg(IRNativeReg nreg, IRReg first, int lanes) override;
	void StoreNativeReg(IRNativeReg nreg, IRReg first, int lanes) override;
	void SetNativeRegValue(IRNativeReg nreg, uint32_t imm) override;
	void StoreRegValue(IRReg mreg, uint32_t imm) override;

private:
	RiscVGen::RiscVEmitter *emit_ = nullptr;

	enum {
		NUM_RVFPUREG = 32,
	};
};
