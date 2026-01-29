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

#include "Common/CPUDetect.h"
#include "Common/Log.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/ARM/ArmRegCacheFPU.h"
#include "Core/MIPS/ARM/ArmJit.h"
#include "Core/MIPS/MIPSTables.h"

using namespace ArmGen;
using namespace ArmJitConstants;

ArmRegCacheFPU::ArmRegCacheFPU(MIPSState *mipsState, MIPSComp::JitState *js, MIPSComp::JitOptions *jo) : mips_(mipsState), js_(js), jo_(jo), vr(mr + 32) {}

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
	for (int i = 0; i < NUM_ARMFPUREG; i++) {
		arInitial[i].mipsReg = -1;
		arInitial[i].isDirty = false;
	}
	for (int i = 0; i < NUM_MIPSFPUREG; i++) {
		mrInitial[i].loc = ML_MEM;
		mrInitial[i].reg = INVALID_REG;
		mrInitial[i].spillLock = false;
		mrInitial[i].tempLock = false;
	}
	for (int i = 0; i < NUM_ARMQUADS; i++) {
		qr[i].isDirty = false;
		qr[i].mipsVec = -1;
		qr[i].sz = V_Invalid;
		qr[i].spillLock = false;
		qr[i].isTemp = false;
		memset(qr[i].vregs, 0xff, 4);
	}
}

const ARMReg *ArmRegCacheFPU::GetMIPSAllocationOrder(int &count) {
	// VFP mapping
	// VFPU registers and regular FP registers are mapped interchangably on top of the standard
	// 16 FPU registers.

	// NEON mapping
	// We map FPU and VFPU registers entirely separately. FPU is mapped to 12 of the bottom 16 S registers.
	// VFPU is mapped to the upper 48 regs, 32 of which can only be reached through NEON
	// (or D16-D31 as doubles, but not relevant).
	// Might consider shifting the split in the future, giving more regs to NEON allowing it to map more quads.
	
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

	count = sizeof(allocationOrderNEON) / sizeof(const ARMReg);
	return allocationOrderNEON;
}

bool ArmRegCacheFPU::IsMapped(MIPSReg r) {
	return mr[r].loc == ML_ARMREG;
}

ARMReg ArmRegCacheFPU::MapReg(MIPSReg mipsReg, int mapFlags) {
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
			if ((mapFlags & MAP_NOINIT) != MAP_NOINIT) {
				if (mr[mipsReg].loc == ML_MEM && mipsReg < TEMP0) {
					emit_->VLDR((ARMReg)(reg + S0), CTXREG, GetMipsRegOffset(mipsReg));
				}
			}
			ar[reg].mipsReg = mipsReg;
			mr[mipsReg].loc = ML_ARMREG;
			mr[mipsReg].reg = reg;
			//INFO_LOG(Log::JIT, "Mapped %i to %i", mipsReg, mr[mipsReg].reg);
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
	ERROR_LOG(Log::JIT, "Out of spillable registers at PC %08x!!!", js_->compilerPC);
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
	bool load = !avoidLoad || rd == rs;
	MapReg(rd, load ? MAP_DIRTY : MAP_NOINIT);
	MapReg(rs);
	ReleaseSpillLock(rd);
	ReleaseSpillLock(rs);
}

void ArmRegCacheFPU::MapDirtyInIn(MIPSReg rd, MIPSReg rs, MIPSReg rt, bool avoidLoad) {
	SpillLock(rd, rs, rt);
	bool load = !avoidLoad || (rd == rs || rd == rt);
	MapReg(rd, load ? MAP_DIRTY : MAP_NOINIT);
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
	bool load = !avoidLoad || (vd == vs);
	SpillLockV(vd);
	SpillLockV(vs);
	MapRegV(vd, load ? MAP_DIRTY : MAP_NOINIT);
	MapRegV(vs);
	ReleaseSpillLockV(vd);
	ReleaseSpillLockV(vs);
}

void ArmRegCacheFPU::MapDirtyInInV(int vd, int vs, int vt, bool avoidLoad) {
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

void ArmRegCacheFPU::FlushArmReg(ARMReg r) {
	if (r >= S0 && r <= S31) {
		int reg = r - S0;
		if (ar[reg].mipsReg == -1) {
			// Nothing to do, reg not mapped.
			return;
		}
		if (ar[reg].mipsReg != -1) {
			if (ar[reg].isDirty && mr[ar[reg].mipsReg].loc == ML_ARMREG)
			{
				//INFO_LOG(Log::JIT, "Flushing ARM reg %i", reg);
				emit_->VSTR(r, CTXREG, GetMipsRegOffset(ar[reg].mipsReg));
			}
			// IMMs won't be in an ARM reg.
			mr[ar[reg].mipsReg].loc = ML_MEM;
			mr[ar[reg].mipsReg].reg = INVALID_REG;
		} else {
			ERROR_LOG(Log::JIT, "Dirty but no mipsreg?");
		}
		ar[reg].isDirty = false;
		ar[reg].mipsReg = -1;
	} else if (r >= D0 && r <= D31) {
		// TODO: Convert to S regs and flush them individually.
	} else if (r >= Q0 && r <= Q15) {
		QFlush(r);
	}
}

void ArmRegCacheFPU::FlushV(MIPSReg r) {
	FlushR(r + 32);
}

/*
void ArmRegCacheFPU::FlushQWithV(MIPSReg r) {
	// Look for it in all the quads. If it's in any, flush that quad clean.
	int flushCount = 0;
	for (int i = 0; i < MAX_ARMQUADS; i++) {
		if (qr[i].sz == V_Invalid)
			continue;

		int n = qr[i].sz;
		bool flushThis = false;
		for (int j = 0; j < n; j++) {
			if (qr[i].vregs[j] == r) {
				flushThis = true;
			}
		}

		if (flushThis) {
			QFlush(i);
			flushCount++;
		}
	}

	if (flushCount > 1) {
		WARN_LOG(Log::JIT, "ERROR: More than one quad was flushed to flush reg %i", r);
	}
}
*/

void ArmRegCacheFPU::FlushR(MIPSReg r) {
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

		if (mr[r].reg >= Q0 && mr[r].reg <= Q15) {
			// This should happen rarely, but occasionally we need to flush a single stray
			// mipsreg that's been part of a quad.
			int quad = mr[r].reg - Q0;
			if (qr[quad].isDirty) {
				WARN_LOG(Log::JIT, "FlushR found quad register %i - PC=%08x", quad, js_->compilerPC);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffset(r), R1);
				emit_->VST1_lane(F_32, (ARMReg)mr[r].reg, R0, mr[r].lane, true);
			}
		} else {
			if (ar[mr[r].reg].isDirty) {
				//INFO_LOG(Log::JIT, "Flushing dirty reg %i", mr[r].reg);
				emit_->VSTR((ARMReg)(mr[r].reg + S0), CTXREG, GetMipsRegOffset(r));
				ar[mr[r].reg].isDirty = false;
			}
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
}

// Scalar only. Need a similar one for sequential Q vectors.
int ArmRegCacheFPU::FlushGetSequential(int a) {
	int c = 1;
	int lastMipsOffset = GetMipsRegOffset(ar[a].mipsReg);
	a++;
	while (a < 32) {
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

	// Flush quads!
	// These could also use sequential detection.
	for (int i = 4; i < NUM_ARMQUADS; i++) {
		QFlush(i);
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
				INFO_LOG(Log::JIT, "ARM reg %i is dirty but has no mipsreg", a);
				continue;
			}

			int c = FlushGetSequential(a);
			if (c == 1) {
				// INFO_LOG(Log::JIT, "Got single register: %i (%i)", a, m);
				emit_->VSTR((ARMReg)(a + S0), CTXREG, GetMipsRegOffset(m));
			} else if (c == 2) {
				// Probably not worth using VSTMIA for two.
				int offset = GetMipsRegOffset(m);
				emit_->VSTR((ARMReg)(a + S0), CTXREG, offset);
				emit_->VSTR((ARMReg)(a + 1 + S0), CTXREG, offset + 4);
			} else {
				// INFO_LOG(Log::JIT, "Got sequence: %i at %i (%i)", c, a, m);
				emit_->ADDI2R(SCRATCHREG1, CTXREG, GetMipsRegOffset(m), SCRATCHREG2);
				// INFO_LOG(Log::JIT, "VSTMIA R0, %i, %i", a, c);
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
	for (int i = 0; i < NUM_ARMFPUREG; i++) {
		if (ar[i].mipsReg != -1) {
			ERROR_LOG(Log::JIT, "Flush fail: ar[%i].mipsReg=%i", i, ar[i].mipsReg);
		}
	}
	pendingFlush = false;
}

void ArmRegCacheFPU::DiscardR(MIPSReg r) {
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

	ERROR_LOG(Log::CPU, "Out of temp regs! Might need to DiscardR() some");
	_assert_msg_(false, "Regcache ran out of temp regs, might need to DiscardR() some.");
	return -1;
}

int ArmRegCacheFPU::GetMipsRegOffset(MIPSReg r) {
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

void ArmRegCacheFPU::SpillLock(MIPSReg r1, MIPSReg r2, MIPSReg r3, MIPSReg r4) {
	mr[r1].spillLock = true;
	if (r2 != -1) mr[r2].spillLock = true;
	if (r3 != -1) mr[r3].spillLock = true;
	if (r4 != -1) mr[r4].spillLock = true;
}

// This is actually pretty slow with all the 160 regs...
void ArmRegCacheFPU::ReleaseSpillLocksAndDiscardTemps() {
	for (int i = 0; i < NUM_MIPSFPUREG; i++) {
		mr[i].spillLock = false;
	}
	for (int i = TEMP0; i < TEMP0 + NUM_TEMPS; ++i) {
		DiscardR(i);
	}
	for (int i = 0; i < NUM_ARMQUADS; i++) {
		qr[i].spillLock = false;
		if (qr[i].isTemp) {
			qr[i].isTemp = false;
			qr[i].sz = V_Invalid;
		}
	}
}

ARMReg ArmRegCacheFPU::R(int mipsReg) {
	if (mr[mipsReg].loc == ML_ARMREG) {
		return (ARMReg)(mr[mipsReg].reg + S0);
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

inline ARMReg QuadAsD(int quad) {
	return (ARMReg)(D0 + quad * 2);
}

inline ARMReg QuadAsQ(int quad) {
	return (ARMReg)(Q0 + quad);
}

bool MappableQ(int quad) {
	return quad >= 4;
}

void ArmRegCacheFPU::QLoad4x4(MIPSGPReg regPtr, int vquads[4]) {
	ERROR_LOG(Log::JIT, "QLoad4x4 not implemented");
	// TODO	
}

void ArmRegCacheFPU::QFlush(int quad) {
	if (!MappableQ(quad)) {
		ERROR_LOG(Log::JIT, "Cannot flush non-mappable quad %i", quad);
		return;
	}

	if (qr[quad].isDirty && !qr[quad].isTemp) {
		INFO_LOG(Log::JIT, "Flushing Q%i (%s)", quad, GetVectorNotation(qr[quad].mipsVec, qr[quad].sz).c_str());

		ARMReg q = QuadAsQ(quad);
		// Unlike reads, when writing to the register file we need to be careful to write the correct
		// number of floats.

		switch (qr[quad].sz) {
		case V_Single:
			emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(qr[quad].vregs[0]), R1);
			emit_->VST1_lane(F_32, q, R0, 0, true);
			// WARN_LOG(Log::JIT, "S: Falling back to individual flush: pc=%08x", js_->compilerPC);
			break;
		case V_Pair:
			if (Consecutive(qr[quad].vregs[0], qr[quad].vregs[1])) {
				// Can combine, it's a column!
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(qr[quad].vregs[0]), R1);
				emit_->VST1(F_32, q, R0, 1, ALIGN_NONE);  // TODO: Allow ALIGN_64 when applicable
			} else {
				// WARN_LOG(Log::JIT, "P: Falling back to individual flush: pc=%08x", js_->compilerPC);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(qr[quad].vregs[0]), R1);
				emit_->VST1_lane(F_32, q, R0, 0, true);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(qr[quad].vregs[1]), R1);
				emit_->VST1_lane(F_32, q, R0, 1, true);
			}
			break;
		case V_Triple:
			if (Consecutive(qr[quad].vregs[0], qr[quad].vregs[1], qr[quad].vregs[2])) {
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(qr[quad].vregs[0]), R1);
				emit_->VST1(F_32, QuadAsD(quad), R0, 1, ALIGN_NONE, REG_UPDATE);  // TODO: Allow ALIGN_64 when applicable
				emit_->VST1_lane(F_32, q, R0, 2, true);
			} else {
				// WARN_LOG(Log::JIT, "T: Falling back to individual flush: pc=%08x", js_->compilerPC);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(qr[quad].vregs[0]), R1);
				emit_->VST1_lane(F_32, q, R0, 0, true);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(qr[quad].vregs[1]), R1);
				emit_->VST1_lane(F_32, q, R0, 1, true);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(qr[quad].vregs[2]), R1);
				emit_->VST1_lane(F_32, q, R0, 2, true);
			}
			break;
		case V_Quad:
			if (Consecutive(qr[quad].vregs[0], qr[quad].vregs[1], qr[quad].vregs[2], qr[quad].vregs[3])) {
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(qr[quad].vregs[0]), R1);
				emit_->VST1(F_32, QuadAsD(quad), R0, 2, ALIGN_NONE);  // TODO: Allow ALIGN_64 when applicable
			} else {
				// WARN_LOG(Log::JIT, "Q: Falling back to individual flush: pc=%08x", js_->compilerPC);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(qr[quad].vregs[0]), R1);
				emit_->VST1_lane(F_32, q, R0, 0, true);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(qr[quad].vregs[1]), R1);
				emit_->VST1_lane(F_32, q, R0, 1, true);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(qr[quad].vregs[2]), R1);
				emit_->VST1_lane(F_32, q, R0, 2, true);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(qr[quad].vregs[3]), R1);
				emit_->VST1_lane(F_32, q, R0, 3, true);
			}
			break;
		default:
			ERROR_LOG(Log::JIT, "Unknown quad size %i", qr[quad].sz);
			break;
		}

		qr[quad].isDirty = false;

		int n = GetNumVectorElements(qr[quad].sz);
		for (int i = 0; i < n; i++) {
			int vr = qr[quad].vregs[i];
			if (vr < 0 || vr > 128) {
				ERROR_LOG(Log::JIT, "Bad vr %i", vr);
			}
			FPURegMIPS &m = mr[32 + vr];
			m.loc = ML_MEM;
			m.lane = -1;
			m.reg = -1;
		}

	} else {
		if (qr[quad].isTemp) {
			WARN_LOG(Log::JIT, "Not flushing quad %i; dirty = %i, isTemp = %i", quad, qr[quad].isDirty, qr[quad].isTemp);
		}
	}

	qr[quad].isTemp = false;
	qr[quad].mipsVec = -1;
	qr[quad].sz = V_Invalid;
	memset(qr[quad].vregs, 0xFF, 4);
}

int ArmRegCacheFPU::QGetFreeQuad(int start, int count, const char *reason) {
	// Search for a free quad. A quad is free if the first register in it is free.
	for (int i = 0; i < count; i++) {
		int q = (i + start) & 15;

		if (!MappableQ(q))
			continue;

		// Don't steal temp quads!
		if (qr[q].mipsVec == (int)INVALID_REG && !qr[q].isTemp) {
			// INFO_LOG(Log::JIT, "Free quad: %i", q);
			// Oh yeah! Free quad!
			return q;
		}
	}

	// Okay, find the "best scoring" reg to replace. Scoring algorithm TBD but may include some
	// sort of age.
	int bestQuad = -1;
	int bestScore = -1;
	for (int i = 0; i < count; i++) {
		int q = (i + start) & 15;

		if (!MappableQ(q))
			continue;
		if (qr[q].spillLock)
			continue;
		if (qr[q].isTemp)
			continue;

		int score = 0;
		if (!qr[q].isDirty) {
			score += 5;
		}

		if (score > bestScore) {
			bestQuad = q;
			bestScore = score;
		}
	}

	if (bestQuad == -1) {
		ERROR_LOG(Log::JIT, "Failed finding a free quad. Things will now go haywire!");
		return -1;
	} else {
		INFO_LOG(Log::JIT, "No register found in %i and the next %i, kicked out #%i (%s)", start, count, bestQuad, reason ? reason : "no reason");
		QFlush(bestQuad);
		return bestQuad;
	}
}

ARMReg ArmRegCacheFPU::QAllocTemp(VectorSize sz) {
	int q = QGetFreeQuad(8, 16, "allocating temporary");  // Prefer high quads as temps
	if (q < 0) {
		ERROR_LOG(Log::JIT, "Failed to allocate temp quad");
		q = 0;
	}
	qr[q].spillLock = true;
	qr[q].isTemp = true;
	qr[q].sz = sz;
	qr[q].isDirty = false;  // doesn't matter

	INFO_LOG(Log::JIT, "Allocated temp quad %i", q);

	if (sz == V_Single || sz == V_Pair) {
		return D_0(ARMReg(Q0 + q));
	} else {
		return ARMReg(Q0 + q);
	}
}

bool ArmRegCacheFPU::Consecutive(int v1, int v2) const {
	return (voffset[v1] + 1) == voffset[v2];
}

bool ArmRegCacheFPU::Consecutive(int v1, int v2, int v3) const {
	return Consecutive(v1, v2) && Consecutive(v2, v3);
}

bool ArmRegCacheFPU::Consecutive(int v1, int v2, int v3, int v4) const {
	return Consecutive(v1, v2) && Consecutive(v2, v3) && Consecutive(v3, v4);
}

void ArmRegCacheFPU::QMapMatrix(ARMReg *regs, int matrix, MatrixSize mz, int flags) {
	u8 vregs[4];
	if (flags & MAP_MTX_TRANSPOSED) {
		GetMatrixRows(matrix, mz, vregs);
	} else {
		GetMatrixColumns(matrix, mz, vregs);
	}

	// TODO: Zap existing mappings, reserve 4 consecutive regs, then do a fast load.
	int n = GetMatrixSide(mz);
	VectorSize vsz = GetVectorSize(mz);
	for (int i = 0; i < n; i++) {
		regs[i] = QMapReg(vregs[i], vsz, flags);
	}
}

ARMReg ArmRegCacheFPU::QMapReg(int vreg, VectorSize sz, int flags) {
	qTime_++;

	int n = GetNumVectorElements(sz);
	u8 vregs[4];
	GetVectorRegs(vregs, sz, vreg);

	// Range of registers to consider
	int start = 0;
	int count = 16;

	if (flags & MAP_PREFER_HIGH) {
		start = 8;
	} else if (flags & MAP_PREFER_LOW) {
		start = 4;
	} else if (flags & MAP_FORCE_LOW) {
		start = 4;
		count = 4;
	} else if (flags & MAP_FORCE_HIGH) {
		start = 8;
		count = 8;
	}

	// Let's check if they are all mapped in a quad somewhere.
	// At the same time, check for the quad already being mapped.
	// Later we can check for possible transposes as well.

	// First just loop over all registers. If it's here and not in range, or overlapped, kick.
	std::vector<int> quadsToFlush;
	for (int i = 0; i < 16; i++) {
		int q = (i + start) & 15;
		if (!MappableQ(q))
			continue;

		// Skip unmapped quads.
		if (qr[q].sz == V_Invalid)
			continue;

		// Check if completely there already. If so, set spill-lock, transfer dirty flag and exit.
		if (vreg == qr[q].mipsVec && sz == qr[q].sz) {
			if (i < count) {
				INFO_LOG(Log::JIT, "Quad already mapped: %i : %i (size %i)", q, vreg, sz);
				qr[q].isDirty = qr[q].isDirty || (flags & MAP_DIRTY);
				qr[q].spillLock = true;

				// Sanity check vregs
				for (int i = 0; i < n; i++) {
					if (vregs[i] != qr[q].vregs[i]) {
						ERROR_LOG(Log::JIT, "Sanity check failed: %i vs %i", vregs[i], qr[q].vregs[i]);
					}
				}

				return (ARMReg)(Q0 + q);
			} else {
				INFO_LOG(Log::JIT, "Quad already mapped at %i which is out of requested range [%i-%i) (count = %i), needs moving. For now we flush.", q, start, start+count, count);
				quadsToFlush.push_back(q);
				continue;
			}
		}

		// Check for any overlap. Overlap == flush.
		int origN = GetNumVectorElements(qr[q].sz);
		for (int a = 0; a < n; a++) {
			for (int b = 0; b < origN; b++) {
				if (vregs[a] == qr[q].vregs[b]) {
					quadsToFlush.push_back(q);
					goto doubleBreak;
				}
			}
		}
	doubleBreak:
		;
	}

	// We didn't find the extra register, but we got a list of regs to flush. Flush 'em.
	// Here we can check for opportunities to do a "transpose-flush" of row vectors, etc.
	if (!quadsToFlush.empty()) {
		INFO_LOG(Log::JIT, "New mapping %s collided with %d quads, flushing them.", GetVectorNotation(vreg, sz).c_str(), (int)quadsToFlush.size());
	}
	for (size_t i = 0; i < quadsToFlush.size(); i++) {
		QFlush(quadsToFlush[i]);
	}

	// Find where we want to map it, obeying the constraints we gave.
	int quad = QGetFreeQuad(start, count, "mapping");
	if (quad < 0)
		return INVALID_REG;

	// If parts of our register are elsewhere, and we are dirty, we need to flush them
	// before we reload in a new location.
	// This may be problematic if inputs overlap irregularly with output, say:
	// vdot S700, R000, C000
	// It might still work by accident...
	if (flags & MAP_DIRTY) {
		for (int i = 0; i < n; i++) {
			FlushV(vregs[i]);
		}
	}

	qr[quad].sz = sz;
	qr[quad].mipsVec = vreg;

	if ((flags & MAP_NOINIT) != MAP_NOINIT) {
		// Okay, now we will try to load the whole thing in one go. This is possible
		// if it's a row and easy if it's a single.
		// Rows are rare, columns are common - but thanks to our register reordering,
		// columns are actually in-order in memory.
		switch (sz) {
		case V_Single:
			emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(vregs[0]), R1);
			emit_->VLD1_lane(F_32, QuadAsQ(quad), R0, 0, true);
			break;
		case V_Pair:
			if (Consecutive(vregs[0], vregs[1])) {
				// Can combine, it's a column!
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(vregs[0]), R1);
				emit_->VLD1(F_32, QuadAsD(quad), R0, 1, ALIGN_NONE);  // TODO: Allow ALIGN_64 when applicable
			} else {
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(vregs[0]), R1);
				emit_->VLD1_lane(F_32, QuadAsQ(quad), R0, 0, true);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(vregs[1]), R1);
				emit_->VLD1_lane(F_32, QuadAsQ(quad), R0, 1, true);
			}
			break;
		case V_Triple:
			if (Consecutive(vregs[0], vregs[1], vregs[2])) {
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(vregs[0]), R1);
				emit_->VLD1(F_32, QuadAsD(quad), R0, 1, ALIGN_NONE, REG_UPDATE);  // TODO: Allow ALIGN_64 when applicable
				emit_->VLD1_lane(F_32, QuadAsQ(quad), R0, 2, true);
			} else {
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(vregs[0]), R1);
				emit_->VLD1_lane(F_32, QuadAsQ(quad), R0, 0, true);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(vregs[1]), R1);
				emit_->VLD1_lane(F_32, QuadAsQ(quad), R0, 1, true);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(vregs[2]), R1);
				emit_->VLD1_lane(F_32, QuadAsQ(quad), R0, 2, true);
			}
			break;
		case V_Quad:
			if (Consecutive(vregs[0], vregs[1], vregs[2], vregs[3])) {
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(vregs[0]), R1);
				emit_->VLD1(F_32, QuadAsD(quad), R0, 2, ALIGN_NONE);  // TODO: Allow ALIGN_64 when applicable
			} else {
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(vregs[0]), R1);
				emit_->VLD1_lane(F_32, QuadAsQ(quad), R0, 0, true);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(vregs[1]), R1);
				emit_->VLD1_lane(F_32, QuadAsQ(quad), R0, 1, true);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(vregs[2]), R1);
				emit_->VLD1_lane(F_32, QuadAsQ(quad), R0, 2, true);
				emit_->ADDI2R(R0, CTXREG, GetMipsRegOffsetV(vregs[3]), R1);
				emit_->VLD1_lane(F_32, QuadAsQ(quad), R0, 3, true);
			}
			break;
		default:
			;
		}
	}

	// OK, let's fill out the arrays to confirm that we have grabbed these registers.
	for (int i = 0; i < n; i++) {
		int mipsReg = 32 + vregs[i];
		mr[mipsReg].loc = ML_ARMREG;
		mr[mipsReg].reg = QuadAsQ(quad);
		mr[mipsReg].lane = i;
		qr[quad].vregs[i] = vregs[i];
	}
	qr[quad].isDirty = (flags & MAP_DIRTY) != 0;
	qr[quad].spillLock = true;

	INFO_LOG(Log::JIT, "Mapped Q%i to vfpu %i (%s), sz=%i, dirty=%i", quad, vreg, GetVectorNotation(vreg, sz).c_str(), (int)sz, qr[quad].isDirty);
	if (sz == V_Single || sz == V_Pair) {
		return D_0(QuadAsQ(quad));
	} else {
		return QuadAsQ(quad);
	}
}

