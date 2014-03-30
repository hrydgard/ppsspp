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

#include <cstring>
#include "base/logging.h"
#include "Common/CPUDetect.h"
#include "Core/MIPS/ARM/ArmRegCacheFPU.h"
#include "Core/MIPS/MIPSTables.h"

using namespace ArmGen;

ArmRegCacheFPU::ArmRegCacheFPU(MIPSState *mips) : mips_(mips), vr(mr + 32), initialReady(false) {
	if (cpu_info.bNEON) {
		numARMFpuReg_ = 32;
	} else {
		numARMFpuReg_ = 16;
	}
}

void ArmRegCacheFPU::Init(ARMXEmitter *emitter) {
	emit_ = emitter;
}

void ArmRegCacheFPU::Start(MIPSAnalyst::AnalysisResults &stats) {
	if (!initialReady) {
		SetupInitialRegs();
		initialReady = true;
	}

	memcpy(ar, arInitial, sizeof(ar));
	memcpy(mr, mrInitial, sizeof(mr));
	pendingFlush = false;
}

void ArmRegCacheFPU::SetupInitialRegs() {
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

static const ARMReg *GetMIPSAllocationOrder(int &count) {
	// We reserve S0-S1 as scratch. Can afford two registers. Maybe even four, which could simplify some things.
	static const ARMReg allocationOrder[] = {
		          S2,  S3,
		S4,  S5,  S6,  S7,
		S8,  S9,  S10, S11,
		S12, S13, S14, S15
	};

	// With NEON, we have many more.
	// In the future I plan to use S0-S7 (Q0-Q1) for FPU and S8 forwards (Q2-Q15, yes, 15) for VFPU.
	// VFPU will use NEON to do SIMD and it will be awkward to mix with FPU.

	// We should attempt to map scalars to low Q registers and wider things to high registers,
	// as the NEON instructions are all 2-vector or 4-vector, they don't do scalar, we want to be
	// able to use regular VFP instructions too.
	static const ARMReg allocationOrderNEON[] = {
		// Reserve four temp registers. Useful when building quads until we really figure out
		// how to do that best.
		S4,  S5,  S6,  S7,   // Q1
		S8,  S9,  S10, S11,  // Q2
		S12, S13, S14, S15,  // Q3
		S16, S17, S18, S19,  // Q4
		S20, S21, S22, S23,  // Q5
		S24, S25, S26, S27,  // Q6
		S28, S29, S30, S31,  // Q7
		// Q8-Q15 free for NEON tricks
	};

	if (cpu_info.bNEON) {
		count = sizeof(allocationOrderNEON) / sizeof(const int);
		return allocationOrderNEON;
	} else {
		count = sizeof(allocationOrder) / sizeof(const int);
		return allocationOrder;
	}
}

ARMReg ArmRegCacheFPU::MapReg(MIPSReg mipsReg, int mapFlags) {
	pendingFlush = true;
	// Let's see if it's already mapped. If so we just need to update the dirty flag.
	// We don't need to check for ML_NOINIT because we assume that anyone who maps
	// with that flag immediately writes a "known" value to the register.
	if (mr[mipsReg].loc == ML_ARMREG) {
		if (ar[mr[mipsReg].reg].mipsReg != mipsReg) {
			ERROR_LOG(JIT, "Reg mapping out of sync! MR %i", mipsReg);
		}
		if (mapFlags & MAP_DIRTY) {
			ar[mr[mipsReg].reg].isDirty = true;
		}
		//INFO_LOG(JIT, "Already mapped %i to %i", mipsReg, mr[mipsReg].reg);
		return (ARMReg)(mr[mipsReg].reg + S0);
	}

	// Okay, not mapped, so we need to allocate an ARM register.

	int allocCount;
	const ARMReg *allocOrder = GetMIPSAllocationOrder(allocCount);

allocate:
	for (int i = 0; i < allocCount; i++) {
		int reg = allocOrder[i] - S0;

		if (ar[reg].mipsReg == -1) {
			// That means it's free. Grab it, and load the value into it (if requested).
			ar[reg].isDirty = (mapFlags & MAP_DIRTY) ? true : false;
			if (!(mapFlags & MAP_NOINIT)) {
				if (mr[mipsReg].loc == ML_MEM && mipsReg < TEMP0) {
					emit_->VLDR((ARMReg)(reg + S0), CTXREG, GetMipsRegOffset(mipsReg));
				}
			}
			ar[reg].mipsReg = mipsReg;
			mr[mipsReg].loc = ML_ARMREG;
			mr[mipsReg].reg = reg;
			//INFO_LOG(JIT, "Mapped %i to %i", mipsReg, mr[mipsReg].reg);
			return (ARMReg)(reg + S0);
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
		FlushArmReg((ARMReg)(S0 + bestToSpill));
		goto allocate;
	}

	// Uh oh, we have all them spilllocked....
	ERROR_LOG(JIT, "Out of spillable registers at PC %08x!!!", mips_->pc);
	return INVALID_REG;
}

void ArmRegCacheFPU::MapInIn(MIPSReg rd, MIPSReg rs) {
	SpillLock(rd, rs);
	MapReg(rd);
	MapReg(rs);
	ReleaseSpillLock(rd);
	ReleaseSpillLock(rs);
}

void ArmRegCacheFPU::MapDirtyIn(MIPSReg rd, MIPSReg rs, bool avoidLoad) {
	SpillLock(rd, rs);
	bool overlap = avoidLoad && rd == rs;
	MapReg(rd, MAP_DIRTY | (overlap ? 0 : MAP_NOINIT));
	MapReg(rs);
	ReleaseSpillLock(rd);
	ReleaseSpillLock(rs);
}

void ArmRegCacheFPU::MapDirtyInIn(MIPSReg rd, MIPSReg rs, MIPSReg rt, bool avoidLoad) {
	SpillLock(rd, rs, rt);
	bool overlap = avoidLoad && (rd == rs || rd == rt);
	MapReg(rd, MAP_DIRTY | (overlap ? 0 : MAP_NOINIT));
	MapReg(rt);
	MapReg(rs);
	ReleaseSpillLock(rd);
	ReleaseSpillLock(rs);
	ReleaseSpillLock(rt);
}

void ArmRegCacheFPU::SpillLockV(const u8 *v, VectorSize sz) {
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		vr[v[i]].spillLock = true;
	}
}

void ArmRegCacheFPU::SpillLockV(int vec, VectorSize sz) {
	u8 v[4];
	GetVectorRegs(v, sz, vec);
	SpillLockV(v, sz);
}

void ArmRegCacheFPU::MapRegV(int vreg, int flags) {
	MapReg(vreg + 32, flags);
}

void ArmRegCacheFPU::LoadToRegV(ARMReg armReg, int vreg) {
	if (vr[vreg].loc == ML_ARMREG) {
		emit_->VMOV(armReg, (ARMReg)(S0 + vr[vreg].reg));
	} else {
		MapRegV(vreg);
		emit_->VMOV(armReg, V(vreg));
	}
}

void ArmRegCacheFPU::MapRegsAndSpillLockV(int vec, VectorSize sz, int flags) {
	u8 v[4];
	GetVectorRegs(v, sz, vec);
	SpillLockV(v, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		MapRegV(v[i], flags);
	}
}

void ArmRegCacheFPU::MapRegsAndSpillLockV(const u8 *v, VectorSize sz, int flags) {
	SpillLockV(v, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		MapRegV(v[i], flags);
	}
}

void ArmRegCacheFPU::MapInInV(int vs, int vt) {
	SpillLockV(vs);
	SpillLockV(vt);
	MapRegV(vs);
	MapRegV(vt);
	ReleaseSpillLockV(vs);
	ReleaseSpillLockV(vt);
}

void ArmRegCacheFPU::MapDirtyInV(int vd, int vs, bool avoidLoad) {
	bool overlap = avoidLoad && (vd == vs);
	SpillLockV(vd);
	SpillLockV(vs);
	MapRegV(vd, MAP_DIRTY | (overlap ? 0 : MAP_NOINIT));
	MapRegV(vs);
	ReleaseSpillLockV(vd);
	ReleaseSpillLockV(vs);
}

void ArmRegCacheFPU::MapDirtyInInV(int vd, int vs, int vt, bool avoidLoad) {
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

void ArmRegCacheFPU::FlushArmReg(ARMReg r) {
	int reg = r - S0;
	if (ar[reg].mipsReg == -1) {
		// Nothing to do, reg not mapped.
		return;
	}
	if (ar[reg].mipsReg != -1) {
		if (ar[reg].isDirty && mr[ar[reg].mipsReg].loc == ML_ARMREG) {
			//INFO_LOG(JIT, "Flushing ARM reg %i", reg);
			emit_->VSTR(r, CTXREG, GetMipsRegOffset(ar[reg].mipsReg));
		}
		// IMMs won't be in an ARM reg.
		mr[ar[reg].mipsReg].loc = ML_MEM;
		mr[ar[reg].mipsReg].reg = INVALID_REG;
	} else {
		ERROR_LOG(JIT, "Dirty but no mipsreg?");
	}
	ar[reg].isDirty = false;
	ar[reg].mipsReg = -1;
}

void ArmRegCacheFPU::FlushR(MIPSReg r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		// IMM is not allowed for FP (yet).
		ERROR_LOG(JIT, "Imm in FP register?");
		break;

	case ML_ARMREG:
		if (mr[r].reg == (int)INVALID_REG) {
			ERROR_LOG(JIT, "FlushR: MipsReg had bad ArmReg");
		}
		if (ar[mr[r].reg].isDirty) {
			//INFO_LOG(JIT, "Flushing dirty reg %i", mr[r].reg);
			emit_->VSTR((ARMReg)(mr[r].reg + S0), CTXREG, GetMipsRegOffset(r));
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

int ArmRegCacheFPU::GetNumARMFPURegs() {
	if (cpu_info.bNEON)
		return 32;
	else
		return 16;
}

int ArmRegCacheFPU::FlushGetSequential(int a, int maxArmReg) {
	int c = 1;
	int lastMipsOffset = GetMipsRegOffset(ar[a].mipsReg);
	a++;
	while (a < maxArmReg) {
		if (!ar[a].isDirty || ar[a].mipsReg == -1)
			break;
		int mipsOffset = GetMipsRegOffset(ar[a].mipsReg);
		if (mipsOffset != lastMipsOffset + 4) {
			break;
		}

		lastMipsOffset = mipsOffset;
		a++;
		c++;
	}
	return c;
}

void ArmRegCacheFPU::FlushAll() {
	if (!pendingFlush) {
		// Nothing allocated.  FPU regs are not nearly as common as GPR.
		return;
	}

	// Discard temps!
	for (int i = TEMP0; i < TEMP0 + NUM_TEMPS; i++) {
		DiscardR(i);
	}

	// Loop through the ARM registers, then use GetMipsRegOffset to determine if MIPS registers are
	// sequential. This is necessary because we store VFPU registers in a staggered order to get
	// columns sequential (most VFPU math in nearly all games is in columns, not rows).
	
	int numArmRegs;
	// We rely on the allocation order being sequential.
	const ARMReg baseReg = GetMIPSAllocationOrder(numArmRegs)[0];

	for (int i = 0; i < numArmRegs; i++) {
		int a = (baseReg - S0) + i;
		int m = ar[a].mipsReg;

		if (ar[a].isDirty) {
			if (m == -1) {
				ILOG("ARM reg %i is dirty but has no mipsreg", a);
				continue;
			}

			int c = FlushGetSequential(a, GetNumARMFPURegs());
			if (c == 1) {
				// ILOG("Got single register: %i (%i)", a, m);
				emit_->VSTR((ARMReg)(a + S0), CTXREG, GetMipsRegOffset(m));
			} else if (c == 2) {
				// Probably not worth using VSTMIA for two.
				int offset = GetMipsRegOffset(m);
				emit_->VSTR((ARMReg)(a + S0), CTXREG, offset);
				emit_->VSTR((ARMReg)(a + 1 + S0), CTXREG, offset + 4);
			} else {
				// ILOG("Got sequence: %i at %i (%i)", c, a, m);
				emit_->ADDI2R(SCRATCHREG1, CTXREG, GetMipsRegOffset(m), SCRATCHREG2);
				// ILOG("VSTMIA R0, %i, %i", a, c);
				emit_->VSTMIA(SCRATCHREG1, false, (ARMReg)(S0 + a), c);
			}

			// Skip past, and mark as non-dirty.
			for (int j = 0; j < c; j++) {
				int b = a + j;
				mr[ar[b].mipsReg].loc = ML_MEM;
				mr[ar[b].mipsReg].reg = (int)INVALID_REG;
				ar[a + j].mipsReg = -1;
				ar[a + j].isDirty = false;
			}
			i += c - 1;
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
			ERROR_LOG(JIT, "Flush fail: ar[%i].mipsReg=%i", i, ar[i].mipsReg);
		}
	}
	pendingFlush = false;
}

void ArmRegCacheFPU::DiscardR(MIPSReg r) {
	switch (mr[r].loc) {
	case ML_IMM:
		// IMM is always "dirty".
		// IMM is not allowed for FP (yet).
		ERROR_LOG(JIT, "Imm in FP register?");
		break;
		 
	case ML_ARMREG:
		if (mr[r].reg == (int)INVALID_REG) {
			ERROR_LOG(JIT, "DiscardR: MipsReg had bad ArmReg");
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

bool ArmRegCacheFPU::IsTempX(ARMReg r) const {
	return ar[r - S0].mipsReg >= TEMP0;
}

int ArmRegCacheFPU::GetTempR() {
	pendingFlush = true;
	for (int r = TEMP0; r < TEMP0 + NUM_TEMPS; ++r) {
		if (mr[r].loc == ML_MEM && !mr[r].tempLock) {
			mr[r].tempLock = true;
			return r;
		}
	}

	ERROR_LOG(CPU, "Out of temp regs! Might need to DiscardR() some");
	_assert_msg_(JIT, 0, "Regcache ran out of temp regs, might need to DiscardR() some.");
	return -1;
}

int ArmRegCacheFPU::GetMipsRegOffset(MIPSReg r) {
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

void ArmRegCacheFPU::SpillLock(MIPSReg r1, MIPSReg r2, MIPSReg r3, MIPSReg r4) {
	mr[r1].spillLock = true;
	if (r2 != -1) mr[r2].spillLock = true;
	if (r3 != -1) mr[r3].spillLock = true;
	if (r4 != -1) mr[r4].spillLock = true;
}

// This is actually pretty slow with all the 160 regs...
void ArmRegCacheFPU::ReleaseSpillLocksAndDiscardTemps() {
	for (int i = 0; i < NUM_MIPSFPUREG; i++)
		mr[i].spillLock = false;
	for (int i = TEMP0; i < TEMP0 + NUM_TEMPS; ++i)
		DiscardR(i);
}

ARMReg ArmRegCacheFPU::R(int mipsReg) {
	if (mr[mipsReg].loc == ML_ARMREG) {
		return (ARMReg)(mr[mipsReg].reg + S0);
	} else {
		if (mipsReg < 32) {
			ERROR_LOG(JIT, "FReg %i not in ARM reg. compilerPC = %08x : %s", mipsReg, compilerPC_, MIPSDisasmAt(compilerPC_));
		} else if (mipsReg < 32 + 128) {
			ERROR_LOG(JIT, "VReg %i not in ARM reg. compilerPC = %08x : %s", mipsReg - 32, compilerPC_, MIPSDisasmAt(compilerPC_));
		} else {
			ERROR_LOG(JIT, "Tempreg %i not in ARM reg. compilerPC = %08x : %s", mipsReg - 128 - 32, compilerPC_, MIPSDisasmAt(compilerPC_));
		}
		return INVALID_REG;  // BAAAD
	}
}
