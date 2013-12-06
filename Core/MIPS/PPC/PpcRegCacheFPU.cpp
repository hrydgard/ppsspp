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

#include "base/logging.h"
#include "Common/PpcEmitter.h"
#include "Common/CPUDetect.h"
#include "Core/MIPS/PPC/PpcRegCacheFPU.h"


using namespace PpcGen;


PpcRegCacheFPU::PpcRegCacheFPU(MIPSState *mips) : mips_(mips), vr(mr + 32) {
}

void PpcRegCacheFPU::Init(PPCXEmitter *emitter) {
	emit_ = emitter;
}

void PpcRegCacheFPU::Start(MIPSAnalyst::AnalysisResults &stats) {
	for (int i = 0; i < NUM_PPCFPUREG; i++) {
		ar[i].mipsReg = -1;
		ar[i].isDirty = false;
	}
	for (int i = 0; i < NUM_MIPSFPUREG; i++) {
		mr[i].loc = ML_MEM;
		mr[i].reg = INVALID_REG;
		mr[i].spillLock = false;
		mr[i].tempLock = false;
	}
}

static const PPCReg *GetMIPSAllocationOrder(int &count) {
	// We reserve S0-S1 as scratch. Can afford two registers. Maybe even four, which could simplify some things.
	static const PPCReg allocationOrder[] = {
		FPR14,	FPR15,	FPR16,	FPR17,	
		FPR18,	FPR19,	FPR20,	FPR21,
		FPR22,	FPR23,	FPR24,	FPR25,
		FPR26,	FPR27,	FPR28,	FPR29,	
		FPR30,	FPR31
	};

	count = sizeof(allocationOrder) / sizeof(const int);
	return allocationOrder;
}

PPCReg PpcRegCacheFPU::MapReg(MIPSReg mipsReg, int mapFlags) {
	// Let's see if it's already mapped. If so we just need to update the dirty flag.
	// We don't need to check for ML_NOINIT because we assume that anyone who maps
	// with that flag immediately writes a "known" value to the register.
	if (mr[mipsReg].loc == ML_PPCREG) {
		if (ar[mr[mipsReg].reg].mipsReg != mipsReg) {
			ERROR_LOG(HLE, "Register mapping out of sync! %i", mipsReg);
		}
		if (mapFlags & MAP_DIRTY) {
			ar[mr[mipsReg].reg].isDirty = true;
		}
		//INFO_LOG(HLE, "Already mapped %i to %i", mipsReg, mr[mipsReg].reg);
		return (PPCReg)(mr[mipsReg].reg + FPR0);
	}

	// Okay, not mapped, so we need to allocate an PPC register.

	int allocCount;
	const PPCReg *allocOrder = GetMIPSAllocationOrder(allocCount);

allocate:
	for (int i = 0; i < allocCount; i++) {
		int reg = allocOrder[i] - FPR0;

		if (ar[reg].mipsReg == -1) {
			// That means it's free. Grab it, and load the value into it (if requested).
			ar[reg].isDirty = (mapFlags & MAP_DIRTY) ? true : false;
			if (!(mapFlags & MAP_NOINIT)) {
				if (mr[mipsReg].loc == ML_MEM && mipsReg < TEMP0) {
					emit_->LFS((PPCReg)(reg + FPR0), CTXREG, GetMipsRegOffset(mipsReg));
				}
			}
			ar[reg].mipsReg = mipsReg;
			mr[mipsReg].loc = ML_PPCREG;
			mr[mipsReg].reg = reg;
			//INFO_LOG(HLE, "Mapped %i to %i", mipsReg, mr[mipsReg].reg);
			return (PPCReg)(reg + FPR0);
		}
	}


	// Still nothing. Let's spill a reg and goto 10.
	// TODO: Use age or something to choose which register to spill?
	// TODO: Spill dirty regs first? or opposite?
	int bestToSpill = -1;
	for (int i = 0; i < allocCount; i++) {
		int reg = allocOrder[i] - FPR0;
		if (ar[reg].mipsReg != -1 && (mr[ar[reg].mipsReg].spillLock || mr[ar[reg].mipsReg].tempLock))
			continue;
		bestToSpill = reg;
		break;
	}

	if (bestToSpill != -1) {
		FlushPpcReg((PPCReg)(FPR0 + bestToSpill));
		goto allocate;
	}

	// Uh oh, we have all them spilllocked....
	ERROR_LOG(JIT, "Out of spillable registers at PC %08x!!!", mips_->pc);
	return INVALID_REG;
}

void PpcRegCacheFPU::MapInIn(MIPSReg rd, MIPSReg rs) {
	SpillLock(rd, rs);
	MapReg(rd);
	MapReg(rs);
	ReleaseSpillLock(rd);
	ReleaseSpillLock(rs);
}

void PpcRegCacheFPU::MapDirtyIn(MIPSReg rd, MIPSReg rs, bool avoidLoad) {
	SpillLock(rd, rs);
	bool overlap = avoidLoad && rd == rs;
	MapReg(rd, MAP_DIRTY | (overlap ? 0 : MAP_NOINIT));
	MapReg(rs);
	ReleaseSpillLock(rd);
	ReleaseSpillLock(rs);
}

void PpcRegCacheFPU::MapDirtyInIn(MIPSReg rd, MIPSReg rs, MIPSReg rt, bool avoidLoad) {
	SpillLock(rd, rs, rt);
	bool overlap = avoidLoad && (rd == rs || rd == rt);
	MapReg(rd, MAP_DIRTY | (overlap ? 0 : MAP_NOINIT));
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLock(rd);
	ReleaseSpillLock(rs);
	ReleaseSpillLock(rt);
}

void PpcRegCacheFPU::SpillLockV(const u8 *v, VectorSize sz) {
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		vr[v[i]].spillLock = true;
	}
}

void PpcRegCacheFPU::SpillLockV(int vec, VectorSize sz) {
	u8 v[4];
	GetVectorRegs(v, sz, vec);
	SpillLockV(v, sz);
}

void PpcRegCacheFPU::MapRegV(int vreg, int flags) {
	MapReg(vreg + 32, flags);
}

void PpcRegCacheFPU::LoadToRegV(PPCReg ppcReg, int vreg) {
	if (vr[vreg].loc == ML_PPCREG) {
		emit_->FMR(ppcReg, (PPCReg)(FPR0 + vr[vreg].reg));
	} else {
		MapRegV(vreg);
		emit_->FMR(ppcReg, V(vreg));
	}
}

void PpcRegCacheFPU::MapRegsAndSpillLockV(int vec, VectorSize sz, int flags) {
	u8 v[4];
	GetVectorRegs(v, sz, vec);
	SpillLockV(v, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		MapRegV(v[i], flags);
	}
}

void PpcRegCacheFPU::MapRegsAndSpillLockV(const u8 *v, VectorSize sz, int flags) {
	SpillLockV(v, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		MapRegV(v[i], flags);
	}
}

void PpcRegCacheFPU::MapInInV(int vs, int vt) {
	SpillLockV(vs);
	SpillLockV(vt);
	MapRegV(vs);
	MapRegV(vt);
	ReleaseSpillLockV(vs);
	ReleaseSpillLockV(vt);
}

void PpcRegCacheFPU::MapDirtyInV(int vd, int vs, bool avoidLoad) {
	bool overlap = avoidLoad && (vd == vs);
	SpillLockV(vd);
	SpillLockV(vs);
	MapRegV(vd, MAP_DIRTY | (overlap ? 0 : MAP_NOINIT));
	MapRegV(vs);
	ReleaseSpillLockV(vd);
	ReleaseSpillLockV(vs);
}

void PpcRegCacheFPU::MapDirtyInInV(int vd, int vs, int vt, bool avoidLoad) {
	bool overlap = avoidLoad && ((vd == vs) || (vd == vt));
	SpillLockV(vd);
	SpillLockV(vs);
	SpillLockV(vt);
	MapRegV(vd, MAP_DIRTY | (overlap ? 0 : MAP_NOINIT));
	MapRegV(vs);
	MapRegV(vt);
	ReleaseSpillLockV(vd);
	ReleaseSpillLockV(vs);
	ReleaseSpillLockV(vt);
}

void PpcRegCacheFPU::FlushPpcReg(PPCReg r) {
	int reg = r - FPR0;
	if (ar[reg].mipsReg == -1) {
		// Nothing to do, reg not mapped.
		return;
	}
	if (ar[reg].mipsReg != -1) {
		if (ar[reg].isDirty && mr[ar[reg].mipsReg].loc == ML_PPCREG)
		{
			//INFO_LOG(HLE, "Flushing PPC reg %i", reg);
			emit_->SFS(r, CTXREG, GetMipsRegOffset(ar[reg].mipsReg));
		}
		// IMMs won't be in an PPC reg.
		mr[ar[reg].mipsReg].loc = ML_MEM;
		mr[ar[reg].mipsReg].reg = INVALID_REG;
	} else {
		ERROR_LOG(HLE, "Dirty but no mipsreg?");
	}
	ar[reg].isDirty = false;
	ar[reg].mipsReg = -1;
}

void PpcRegCacheFPU::FlushR(MIPSReg r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		// IMM is not allowed for FP (yet).
		ERROR_LOG(HLE, "Imm in FP register?");
		break;

	case ML_PPCREG:
		if (mr[r].reg == (int)INVALID_REG) {
			ERROR_LOG(HLE, "FlushR: MipsReg had bad PpcReg");
		}
		if (ar[mr[r].reg].isDirty) {
			//INFO_LOG(HLE, "Flushing dirty reg %i", mr[r].reg);
			emit_->SFS((PPCReg)(mr[r].reg + FPR0), CTXREG, GetMipsRegOffset(r));
			ar[mr[r].reg].isDirty = false;
		}
		ar[mr[r].reg].mipsReg = -1;
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
}

void PpcRegCacheFPU::DiscardR(MIPSReg r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		// IMM is not allowed for FP (yet).
		ERROR_LOG(HLE, "Imm in FP register?");
		break;
		 
	case ML_PPCREG:
		if (mr[r].reg == (int)INVALID_REG) {
			ERROR_LOG(HLE, "DiscardR: MipsReg had bad PpcReg");
		}
		// Note that we DO NOT write it back here. That's the whole point of Discard.
		ar[mr[r].reg].isDirty = false;
		ar[mr[r].reg].mipsReg = -1;
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


bool PpcRegCacheFPU::IsTempX(PPCReg r) const {
	return ar[r - FPR0].mipsReg >= TEMP0;
}

int PpcRegCacheFPU::GetTempR() {
	for (int r = TEMP0; r < TEMP0 + NUM_TEMPS; ++r) {
		if (mr[r].loc == ML_MEM && !mr[r].tempLock) {
			mr[r].tempLock = true;
			return r;
		}
	}

	ERROR_LOG(CPU, "Out of temp regs! Might need to DiscardR() some");
	_assert_msg_(DYNA_REC, 0, "Regcache ran out of temp regs, might need to DiscardR() some.");
	return -1;
}


void PpcRegCacheFPU::FlushAll() {
	// Discard temps!
	for (int i = TEMP0; i < TEMP0 + NUM_TEMPS; i++) {
		DiscardR(i);
	}
	for (int i = 0; i < NUM_MIPSFPUREG; i++) {
		FlushR(i);
	} 
	// Sanity check
	for (int i = 0; i < NUM_PPCFPUREG; i++) {
		if (ar[i].mipsReg != -1) {
			ERROR_LOG(JIT, "Flush fail: ar[%i].mipsReg=%i", i, ar[i].mipsReg);
		}
	}
}

int PpcRegCacheFPU::GetMipsRegOffset(MIPSReg r) {
	// These are offsets within the MIPSState structure. First there are the GPRS, then FPRS, then the "VFPURs", then the VFPU ctrls.
	if (r < 0 || r > 32 + 128 + NUM_TEMPS) {
		ERROR_LOG(JIT, "bad mips register %i, out of range", r);
		return 0;  // or what?
	}

	if (r < 32 || r > 32 + 128) {
		return (32 + r) << 2;
	} else {
		// r is between 32 and 128 + 32
		return (32 + 32 + voffset[r - 32]) << 2;
	}
}

void PpcRegCacheFPU::SpillLock(MIPSReg r1, MIPSReg r2, MIPSReg r3, MIPSReg r4) {
	mr[r1].spillLock = true;
	if (r2 != -1) mr[r2].spillLock = true;
	if (r3 != -1) mr[r3].spillLock = true;
	if (r4 != -1) mr[r4].spillLock = true;
}

// This is actually pretty slow with all the 160 regs...
void PpcRegCacheFPU::ReleaseSpillLocksAndDiscardTemps() {
	for (int i = 0; i < NUM_MIPSFPUREG; i++)
		mr[i].spillLock = false;
	for (int i = TEMP0; i < TEMP0 + NUM_TEMPS; ++i)
		DiscardR(i);
}

PPCReg PpcRegCacheFPU::R(int mipsReg) {
	if (mr[mipsReg].loc == ML_PPCREG) {
		return (PPCReg)(mr[mipsReg].reg + FPR0);
	} else {
		if (mipsReg < 32) {
			ERROR_LOG(JIT, "FReg %i not in PPC reg. compilerPC = %08x : %s", mipsReg, compilerPC_, currentMIPS->DisasmAt(compilerPC_));
		} else if (mipsReg < 32 + 128) {
			ERROR_LOG(JIT, "VReg %i not in PPC reg. compilerPC = %08x : %s", mipsReg - 32, compilerPC_, currentMIPS->DisasmAt(compilerPC_));
		} else {
			ERROR_LOG(JIT, "Tempreg %i not in PPC reg. compilerPC = %08x : %s", mipsReg - 128 - 32, compilerPC_, currentMIPS->DisasmAt(compilerPC_));
		}
		return INVALID_REG;  // BAAAD
	}
}
