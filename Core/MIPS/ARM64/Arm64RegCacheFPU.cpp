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

#include "ppsspp_config.h"
#if PPSSPP_ARCH(ARM64)

#include <cstring>

#include "Common/CPUDetect.h"
#include "Common/Log.h"
#include "Core/Reporting.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/ARM64/Arm64RegCacheFPU.h"
#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/MIPSTables.h"

using namespace Arm64Gen;
using namespace Arm64JitConstants;

Arm64RegCacheFPU::Arm64RegCacheFPU(MIPSState *mipsState, MIPSComp::JitState *js, MIPSComp::JitOptions *jo) : mips_(mipsState), vr(mr + 32), js_(js), jo_(jo) {
	numARMFpuReg_ = 32;
}

void Arm64RegCacheFPU::Init(Arm64Gen::ARM64XEmitter *emit, Arm64Gen::ARM64FloatEmitter *fp) {
	emit_ = emit;
	fp_ = fp;
}

void Arm64RegCacheFPU::Start(MIPSAnalyst::AnalysisResults &stats) {
	if (!initialReady) {
		SetupInitialRegs();
		initialReady = true;
	}

	memcpy(ar, arInitial, sizeof(ar));
	memcpy(mr, mrInitial, sizeof(mr));
	pendingFlush = false;
}

void Arm64RegCacheFPU::SetupInitialRegs() {
	for (int i = 0; i < numARMFpuReg_; i++) {
		arInitial[i].mipsReg = -1;
		arInitial[i].isDirty = false;
	}
	for (int i = 0; i < NUM_MIPSFPUREG; i++) {
		mrInitial[i].loc = ML_MEM;
		mrInitial[i].reg = INVALID_REG;
		mrInitial[i].spillLock = false;
		mrInitial[i].tempLock = false;
	}
}

const ARM64Reg *Arm64RegCacheFPU::GetMIPSAllocationOrder(int &count) {
	// On ARM64, all 32 registers are fully 128-bit and fully interchangable so we don't
	// have to care about upper or lower registers. However, only S8-S15 are callee-save, and
	// only the bottom 64 bits of those. So we should allocate into these when we call
	// C functions, although we don't currently do so...

	static const ARM64Reg allocationOrder[] = {
		// Reserve four full 128-bit temp registers, should be plenty.
		S4,  S5,  S6,  S7,
		S8,  S9,  S10, S11, // Partially callee-save (bottom 64 bits)
		S12, S13, S14, S15, // Partially callee-save (bottom 64 bits)
		S16, S17, S18, S19,
		S20, S21, S22, S23,
		S24, S25, S26, S27,
		S28, S29, S30, S31,
	};

	static const ARM64Reg allocationOrderNEONVFPU[] = {
		// Reserve four full 128-bit temp registers, should be plenty.

		// Then let's use 12 register as singles
		S4, S5, S6, S7,
		S8,  S9,  S10, S11,
		S12, S13, S14, S15,

		// And do quads in the rest? Or use a strategy more similar to what we do on x86?
	};

	if (jo_->useASIMDVFPU) {
		count = sizeof(allocationOrderNEONVFPU) / sizeof(const ARM64Reg);
		return allocationOrderNEONVFPU;
	} else {
		count = sizeof(allocationOrder) / sizeof(const ARM64Reg);
		return allocationOrder;
	}
}

bool Arm64RegCacheFPU::IsMapped(MIPSReg r) {
	return mr[r].loc == ML_ARMREG;
}

bool Arm64RegCacheFPU::IsInRAM(MIPSReg r) {
	return mr[r].loc == ML_MEM;
}

ARM64Reg Arm64RegCacheFPU::MapReg(MIPSReg mipsReg, int mapFlags) {
	// INFO_LOG(Log::JIT, "FPR MapReg: %i flags=%i", mipsReg, mapFlags);
	if (jo_->useASIMDVFPU && mipsReg >= 32) {
		ERROR_LOG(Log::JIT, "Cannot map VFPU registers to ARM VFP registers in NEON mode. PC=%08x", js_->compilerPC);
		return S0;
	}

	pendingFlush = true;
	// Let's see if it's already mapped. If so we just need to update the dirty flag.
	// We don't need to check for ML_NOINIT because we assume that anyone who maps
	// with that flag immediately writes a "known" value to the register.
	if (mr[mipsReg].loc == ML_ARMREG) {
		if (ar[mr[mipsReg].reg].mipsReg != mipsReg) {
			ERROR_LOG(Log::JIT, "Reg mapping out of sync! MR %i", mipsReg);
		}
		if (mapFlags & MAP_DIRTY) {
			ar[mr[mipsReg].reg].isDirty = true;
		}
		//INFO_LOG(Log::JIT, "Already mapped %i to %i", mipsReg, mr[mipsReg].reg);
		return (ARM64Reg)(mr[mipsReg].reg + S0);
	}

	// Okay, not mapped, so we need to allocate an ARM register.

	int allocCount;
	const ARM64Reg *allocOrder = GetMIPSAllocationOrder(allocCount);

allocate:
	for (int i = 0; i < allocCount; i++) {
		int reg = DecodeReg(allocOrder[i]);

		if (ar[reg].mipsReg == -1) {
			// That means it's free. Grab it, and load the value into it (if requested).
			ar[reg].isDirty = (mapFlags & MAP_DIRTY) ? true : false;
			if ((mapFlags & MAP_NOINIT) != MAP_NOINIT) {
				if (mr[mipsReg].loc == ML_MEM && mipsReg < TEMP0) {
					fp_->LDR(32, INDEX_UNSIGNED, (ARM64Reg)(reg + S0), CTXREG, GetMipsRegOffset(mipsReg));
				}
			}
			ar[reg].mipsReg = mipsReg;
			mr[mipsReg].loc = ML_ARMREG;
			mr[mipsReg].reg = reg;
			//INFO_LOG(Log::JIT, "Mapped %i to %i", mipsReg, mr[mipsReg].reg);
			return (ARM64Reg)(reg + S0);
		}
	}


	// Still nothing. Let's spill a reg and goto 10.
	// TODO: Use age or something to choose which register to spill?
	// TODO: Spill dirty regs first? or opposite?
	int bestToSpill = -1;
	for (int i = 0; i < allocCount; i++) {
		int reg = allocOrder[i] - S0;
		if (ar[reg].mipsReg != -1 && (mr[ar[reg].mipsReg].spillLock || mr[ar[reg].mipsReg].tempLock))
			continue;
		bestToSpill = reg;
		break;
	}

	if (bestToSpill != -1) {
		FlushArmReg((ARM64Reg)(S0 + bestToSpill));
		goto allocate;
	}

	// Uh oh, we have all them spilllocked....
	ERROR_LOG(Log::JIT, "Out of spillable registers at PC %08x!!!", js_->compilerPC);
	return INVALID_REG;
}

void Arm64RegCacheFPU::MapInIn(MIPSReg rd, MIPSReg rs) {
	SpillLock(rd, rs);
	MapReg(rd);
	MapReg(rs);
	ReleaseSpillLock(rd);
	ReleaseSpillLock(rs);
}

void Arm64RegCacheFPU::MapDirtyIn(MIPSReg rd, MIPSReg rs, bool avoidLoad) {
	SpillLock(rd, rs);
	bool load = !avoidLoad || rd == rs;
	MapReg(rd, load ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rs);
	ReleaseSpillLock(rd);
	ReleaseSpillLock(rs);
}

void Arm64RegCacheFPU::MapDirtyInIn(MIPSReg rd, MIPSReg rs, MIPSReg rt, bool avoidLoad) {
	SpillLock(rd, rs, rt);
	bool load = !avoidLoad || (rd == rs || rd == rt);
	MapReg(rd, load ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLock(rd);
	ReleaseSpillLock(rs);
	ReleaseSpillLock(rt);
}

void Arm64RegCacheFPU::SpillLockV(const u8 *v, VectorSize sz) {
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		vr[v[i]].spillLock = true;
	}
}

void Arm64RegCacheFPU::SpillLockV(int vec, VectorSize sz) {
	u8 v[4];
	GetVectorRegs(v, sz, vec);
	SpillLockV(v, sz);
}

void Arm64RegCacheFPU::MapRegV(int vreg, int flags) {
	MapReg(vreg + 32, flags);
}

void Arm64RegCacheFPU::LoadToRegV(ARM64Reg armReg, int vreg) {
	if (vr[vreg].loc == ML_ARMREG) {
		fp_->FMOV(armReg, (ARM64Reg)(S0 + vr[vreg].reg));
	} else {
		MapRegV(vreg);
		fp_->FMOV(armReg, V(vreg));
	}
}

void Arm64RegCacheFPU::MapRegsAndSpillLockV(int vec, VectorSize sz, int flags) {
	u8 v[4];
	GetVectorRegs(v, sz, vec);
	SpillLockV(v, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		MapRegV(v[i], flags);
	}
}

void Arm64RegCacheFPU::MapRegsAndSpillLockV(const u8 *v, VectorSize sz, int flags) {
	SpillLockV(v, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		MapRegV(v[i], flags);
	}
}

void Arm64RegCacheFPU::MapInInV(int vs, int vt) {
	SpillLockV(vs);
	SpillLockV(vt);
	MapRegV(vs);
	MapRegV(vt);
	ReleaseSpillLockV(vs);
	ReleaseSpillLockV(vt);
}

void Arm64RegCacheFPU::MapDirtyInV(int vd, int vs, bool avoidLoad) {
	bool load = !avoidLoad || (vd == vs);
	SpillLockV(vd);
	SpillLockV(vs);
	MapRegV(vd, load ? MAP_DIRTY : MAP_NOINIT);
	MapRegV(vs);
	ReleaseSpillLockV(vd);
	ReleaseSpillLockV(vs);
}

void Arm64RegCacheFPU::MapDirtyInInV(int vd, int vs, int vt, bool avoidLoad) {
	bool load = !avoidLoad || (vd == vs || vd == vt);
	SpillLockV(vd);
	SpillLockV(vs);
	SpillLockV(vt);
	MapRegV(vd, load ? MAP_DIRTY : MAP_NOINIT);
	MapRegV(vs);
	MapRegV(vt);
	ReleaseSpillLockV(vd);
	ReleaseSpillLockV(vs);
	ReleaseSpillLockV(vt);
}

void Arm64RegCacheFPU::FlushArmReg(ARM64Reg r) {
	if (r >= S0 && r <= S31) {
		int reg = r - S0;
		if (ar[reg].mipsReg == -1) {
			// Nothing to do, reg not mapped.
			return;
		}
		if (ar[reg].mipsReg != -1) {
			if (ar[reg].isDirty && mr[ar[reg].mipsReg].loc == ML_ARMREG){
				//INFO_LOG(Log::JIT, "Flushing ARM reg %i", reg);
				fp_->STR(32, INDEX_UNSIGNED, r, CTXREG, GetMipsRegOffset(ar[reg].mipsReg));
			}
			// IMMs won't be in an ARM reg.
			mr[ar[reg].mipsReg].loc = ML_MEM;
			mr[ar[reg].mipsReg].reg = INVALID_REG;
		} else {
			ERROR_LOG(Log::JIT, "Dirty but no mipsreg?");
		}
		ar[reg].mipsReg = -1;
		ar[reg].isDirty = false;
	}
}

void Arm64RegCacheFPU::FlushV(MIPSReg r) {
	FlushR(r + 32);
}

void Arm64RegCacheFPU::FlushR(MIPSReg r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		// IMM is not allowed for FP (yet).
		ERROR_LOG(Log::JIT, "Imm in FP register?");
		break;

	case ML_ARMREG:
		if (mr[r].reg == INVALID_REG) {
			ERROR_LOG(Log::JIT, "FlushR: MipsReg had bad ArmReg");
		}
		FlushArmReg((ARM64Reg)(S0 + mr[r].reg));
		break;

	case ML_MEM:
		// Already there, nothing to do.
		break;

	default:
		//BAD
		break;
	}
}

Arm64Gen::ARM64Reg Arm64RegCacheFPU::ARM64RegForFlush(int r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		// IMM is not allowed for FP (yet).
		ERROR_LOG(Log::JIT, "Imm in FP register?");
		return INVALID_REG;

	case ML_ARMREG:
		if (mr[r].reg == INVALID_REG) {
			ERROR_LOG_REPORT(Log::JIT, "ARM64RegForFlush: MipsReg %d had bad ArmReg", r);
			return INVALID_REG;
		}
		// No need to flush if it's not dirty.
		if (!ar[mr[r].reg].isDirty) {
			return INVALID_REG;
		}
		return (ARM64Reg)(S0 + mr[r].reg);

	case ML_MEM:
		return INVALID_REG;

	default:
		ERROR_LOG_REPORT(Log::JIT, "ARM64RegForFlush: MipsReg %d with invalid location %d", r, mr[r].loc);
		return INVALID_REG;
	}
}

void Arm64RegCacheFPU::FlushAll() {
	if (!pendingFlush) {
		// Nothing allocated.  FPU regs are not nearly as common as GPR.
		return;
	}

	// Discard temps!
	for (int i = TEMP0; i < TEMP0 + NUM_TEMPS; i++) {
		DiscardR(i);
	}

	int numArmRegs = 0;

	const ARM64Reg *order = GetMIPSAllocationOrder(numArmRegs);

	// Flush pairs first when possible. Note that STP's offset can't reach more than 256 bytes so
	// most VFPU registers cannot be flushed this way, unless we are willing to generate another offset pointer
	// (which we could actually do right here, point right in the middle of the VFPU stuff and would reach it all)... TODO
	for (int i = 0; i < 31; i++) {
		int mr1 = i;
		int mr2 = i + 1;
		ARM64Reg ar1 = ARM64RegForFlush(mr1);
		ARM64Reg ar2 = ARM64RegForFlush(mr2);

		if (ar1 != INVALID_REG && ar2 != INVALID_REG) {
			fp_->STP(32, INDEX_SIGNED, ar1, ar2, CTXREG, GetMipsRegOffset(mr1));
			DiscardR(mr1);
			DiscardR(mr2);
		}
	}

	// Then flush one by one.

	for (int i = 0; i < numArmRegs; i++) {
		int a = DecodeReg(order[i]);
		int m = ar[a].mipsReg;

		if (ar[a].isDirty) {
			if (m == -1) {
				INFO_LOG(Log::JIT, "ARM reg %d is dirty but has no mipsreg", a);
				continue;
			}

			fp_->STR(32, INDEX_UNSIGNED, (ARM64Reg)(a + S0), CTXREG, GetMipsRegOffset(m));

			mr[m].loc = ML_MEM;
			mr[m].reg = (int)INVALID_REG;
			ar[a].mipsReg = -1;
			ar[a].isDirty = false;
		} else {
			if (m != -1) {
				mr[m].loc = ML_MEM;
				mr[m].reg = (int)INVALID_REG;
			}
			ar[a].mipsReg = -1;
			// already not dirty
		}
	}

	// Sanity check
	for (int i = 0; i < numARMFpuReg_; i++) {
		if (ar[i].mipsReg != -1) {
			ERROR_LOG(Log::JIT, "Flush fail: ar[%d].mipsReg=%d", i, ar[i].mipsReg);
		}
	}
	pendingFlush = false;
}

void Arm64RegCacheFPU::DiscardR(MIPSReg r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		// IMM is not allowed for FP (yet).
		ERROR_LOG(Log::JIT, "Imm in FP register?");
		break;
		 
	case ML_ARMREG:
		if (mr[r].reg == INVALID_REG) {
			ERROR_LOG(Log::JIT, "DiscardR: MipsReg had bad ArmReg");
		} else {
			// Note that we DO NOT write it back here. That's the whole point of Discard.
			ar[mr[r].reg].isDirty = false;
			ar[mr[r].reg].mipsReg = -1;
		}
		break;

	case ML_MEM:
		// Already there, nothing to do.
		break;

	default:
		//BAD
		break;
	}
	mr[r].loc = ML_MEM;
	mr[r].reg = (int)INVALID_REG;
	mr[r].tempLock = false;
	mr[r].spillLock = false;
}

bool Arm64RegCacheFPU::IsTempX(ARM64Reg r) const {
	return ar[r - S0].mipsReg >= TEMP0;
}

int Arm64RegCacheFPU::GetTempR() {
	if (jo_->useASIMDVFPU) {
		ERROR_LOG(Log::JIT, "VFP temps not allowed in NEON mode");
		return 0;
	}
	pendingFlush = true;
	for (int r = TEMP0; r < TEMP0 + NUM_TEMPS; ++r) {
		if (mr[r].loc == ML_MEM && !mr[r].tempLock) {
			mr[r].tempLock = true;
			return r;
		}
	}

	ERROR_LOG(Log::CPU, "Out of temp regs! Might need to DiscardR() some");
	_assert_msg_(false, "Regcache ran out of temp regs, might need to DiscardR() some.");
	return -1;
}

int Arm64RegCacheFPU::GetMipsRegOffset(MIPSReg r) {
	// These are offsets within the MIPSState structure. First there are the GPRS, then FPRS, then the "VFPURs", then the VFPU ctrls.
	if (r < 0 || r > 32 + 128 + NUM_TEMPS) {
		ERROR_LOG(Log::JIT, "bad mips register %i, out of range", r);
		return 0;  // or what?
	}

	if (r < 32 || r >= 32 + 128) {
		return (32 + r) << 2;
	} else {
		// r is between 32 and 128 + 32
		return (32 + 32 + voffset[r - 32]) << 2;
	}
}

void Arm64RegCacheFPU::SpillLock(MIPSReg r1, MIPSReg r2, MIPSReg r3, MIPSReg r4) {
	mr[r1].spillLock = true;
	if (r2 != -1) mr[r2].spillLock = true;
	if (r3 != -1) mr[r3].spillLock = true;
	if (r4 != -1) mr[r4].spillLock = true;
}

// This is actually pretty slow with all the 160 regs...
void Arm64RegCacheFPU::ReleaseSpillLocksAndDiscardTemps() {
	for (int i = 0; i < NUM_MIPSFPUREG; i++) {
		mr[i].spillLock = false;
	}
	for (int i = TEMP0; i < TEMP0 + NUM_TEMPS; ++i) {
		DiscardR(i);
	}
}

ARM64Reg Arm64RegCacheFPU::R(int mipsReg) {
	if (mr[mipsReg].loc == ML_ARMREG) {
		return (ARM64Reg)(mr[mipsReg].reg + S0);
	} else {
		if (mipsReg < 32) {
			ERROR_LOG(Log::JIT, "FReg %i not in ARM reg. compilerPC = %08x : %s", mipsReg, js_->compilerPC, MIPSDisasmAt(js_->compilerPC).c_str());
		} else if (mipsReg < 32 + 128) {
			ERROR_LOG(Log::JIT, "VReg %i not in ARM reg. compilerPC = %08x : %s", mipsReg - 32, js_->compilerPC, MIPSDisasmAt(js_->compilerPC).c_str());
		} else {
			ERROR_LOG(Log::JIT, "Tempreg %i not in ARM reg. compilerPC = %08x : %s", mipsReg - 128 - 32, js_->compilerPC, MIPSDisasmAt(js_->compilerPC).c_str());
		}
		return INVALID_REG;  // BAAAD
	}
}

#endif // PPSSPP_ARCH(ARM64)
