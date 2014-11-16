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
#include <xmmintrin.h>

#include "Common/Log.h"
#include "Common/x64Emitter.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/x86/RegCache.h"
#include "Core/MIPS/x86/RegCacheFPU.h"

u32 FPURegCache::tempValues[NUM_TEMPS];

FPURegCache::FPURegCache() : mips(0), initialReady(false), emit(0) {
	memset(regs, 0, sizeof(regs));
	memset(xregs, 0, sizeof(xregs));
	vregs = regs + 32;
}

void FPURegCache::Start(MIPSState *mips, MIPSAnalyst::AnalysisResults &stats) {
	this->mips = mips;

	if (!initialReady) {
		SetupInitialRegs();
		initialReady = true;
	}

	memcpy(xregs, xregsInitial, sizeof(xregs));
	memcpy(regs, regsInitial, sizeof(regs));
	pendingFlush = false;
}

void FPURegCache::SetupInitialRegs() {
	for (int i = 0; i < NUM_X_FPREGS; i++) {
		memset(xregsInitial[i].mipsRegs, -1, sizeof(xregsInitial[i].mipsRegs));
		xregsInitial[i].dirty = false;
	}
	memset(regsInitial, 0, sizeof(regsInitial));
	OpArg base = GetDefaultLocation(0);
	for (int i = 0; i < 32; i++) {
		regsInitial[i].location = base;
		base.IncreaseOffset(sizeof(float));
	}
	for (int i = 32; i < 32 + 128; i++) {
		regsInitial[i].location = GetDefaultLocation(i);
	}
	base = GetDefaultLocation(32 + 128);
	for (int i = 32 + 128; i < NUM_MIPS_FPRS; i++) {
		regsInitial[i].location = base;
		base.IncreaseOffset(sizeof(float));
	}
}

void FPURegCache::SpillLock(int p1, int p2, int p3, int p4) {
	regs[p1].locked = true;
	if (p2 != 0xFF) regs[p2].locked = true;
	if (p3 != 0xFF) regs[p3].locked = true;
	if (p4 != 0xFF) regs[p4].locked = true;
}

void FPURegCache::SpillLockV(const u8 *vec, VectorSize sz) {
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		vregs[vec[i]].locked = true;
	}
}

void FPURegCache::SpillLockV(int vec, VectorSize sz) {
	u8 r[4];
	GetVectorRegs(r, sz, vec);
	SpillLockV(r, sz);
}

void FPURegCache::ReleaseSpillLockV(const u8 *vec, VectorSize sz) {
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		vregs[vec[i]].locked = false;
	}
}

void FPURegCache::MapRegV(int vreg, int flags) {
	MapReg(vreg + 32, (flags & MAP_NOINIT) == 0, (flags & MAP_DIRTY) != 0);
}

void FPURegCache::MapRegsV(int vec, VectorSize sz, int flags) {
	u8 r[4];
	GetVectorRegs(r, sz, vec);
	SpillLockV(r, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		MapReg(r[i] + 32, (flags & MAP_NOINIT) == 0, (flags & MAP_DIRTY) != 0);
	}
}

void FPURegCache::MapRegsV(const u8 *r, VectorSize sz, int flags) {
	SpillLockV(r, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		MapReg(r[i] + 32, (flags & MAP_NOINIT) == 0, (flags & MAP_DIRTY) != 0);
	}
}

bool FPURegCache::IsMappedVS(const u8 *v, VectorSize vsz) {
	const int n = GetNumVectorElements(vsz);

	// Make sure the first reg is at least mapped in the right place.
	if (!IsMappedVS(v[0]))
		return false;
	if (vregs[v[0]].lane != 1)
		return false;

	// And make sure the rest are mapped to the same reg in the right positions.
	X64Reg xr = VSX(v[0]);
	for (int i = 1; i < n; ++i) {
		if (!IsMappedVS(v[i]) || VSX(v[i]) != xr)
			return false;
		if (vregs[v[i]].lane != i + 1)
			return false;
	}
	// TODO: Optimize this case?  It happens.
	for (int i = n; i < 4; ++i) {
		if (xregs[xr].mipsRegs[i] != -1) {
			return false;
		}
	}
	return true;
}

void FPURegCache::MapRegsVS(const u8 *r, VectorSize vsz, int flags) {
	const int n = GetNumVectorElements(vsz);
	if (!TryMapRegsVS(r, vsz, flags)) {
		// TODO: Could be more optimal.
		for (int i = 0; i < n; ++i) {
			StoreFromRegisterV(r[i]);
		}
		if (!TryMapRegsVS(r, vsz, flags)) {
			_dbg_assert_msg_(JIT, false, "MapRegsVS() failed on second try.");
		}
	}
}

bool FPURegCache::CanMapVS(const u8 *v, VectorSize vsz) {
	const int n = GetNumVectorElements(vsz);

	if (IsMappedVS(v, vsz)) {
		return true;
	} else if (vregs[v[0]].lane != 0) {
		const MIPSCachedFPReg &v0 = vregs[v[0]];
		_dbg_assert_msg_(JIT, v0.away, "Must be away when lane != 0");
		_dbg_assert_msg_(JIT, v0.location.IsSimpleReg(), "Must be is register when lane != 0");

		// Already in a different simd set.
		return false;
	}

	if (vregs[v[0]].locked) {
		// If it's locked, we can't mess with it.
		return false;
	}

	// Next, fail if any of the other regs are in simd currently.
	// TODO: Only if locked?  Not sure if it will be worth breaking them anyway.
	for (int i = 1; i < n; ++i) {
		if (vregs[v[i]].lane != 0) {
			return false;
		}
		// If it's locked, in simd or not, we can't use it.
		if (vregs[v[i]].locked) {
			return false;
		}
		_assert_msg_(JIT, !vregs[v[i]].location.IsImm(), "Cannot handle imms in fp cache.");
	}

	return true;
}

bool FPURegCache::TryMapRegsVS(const u8 *v, VectorSize vsz, int flags) {
	const int n = GetNumVectorElements(vsz);

	if (!CanMapVS(v, vsz)) {
		return false;
	}

	if (IsMappedVS(v, vsz)) {
		// Already mapped then, perfect.  Just mark dirty.
		if ((flags & MAP_DIRTY) != 0)
			xregs[VSX(v[0])].dirty = true;
		return true;
	}

	// At this point, some or all are in single regs or memory, and they're not locked there.

	if (n == 1) {
		// Single is easy, just map normally but track as a SIMD reg.
		// This way V/VS can warn about improper usage properly.
		MapRegV(v[0], flags);
		vregs[v[0]].lane = 1;
		Invariant();
		return true;
	}

	X64Reg xr;
	if ((flags & MAP_NOINIT) == 0) {
		xr = LoadRegsVS(v, n);
	} else {
		xr = GetFreeXReg();
	}

	// Victory, now let's clean up everything.
	OpArg newloc = Gen::R(xr);
	bool dirty = (flags & MAP_DIRTY) != 0;
	for (int i = 0; i < n; ++i) {
		MIPSCachedFPReg &vr = vregs[v[i]];
		if (vr.away) {
			// Clear the xreg it was in before.
			X64Reg oldXReg = vr.location.GetSimpleReg();
			xregs[oldXReg].mipsReg = -1;
			if (xregs[oldXReg].dirty) {
				// Inherit the "dirtiness" (ultimately set below for all regs.)
				dirty = true;
				xregs[oldXReg].dirty = false;
			}
		}
		xregs[xr].mipsRegs[i] = v[i] + 32;
		vr.location = newloc;
		vr.lane = i + 1;
		vr.away = true;
	}
	xregs[xr].dirty = dirty;

	Invariant();
	return true;
}

X64Reg FPURegCache::LoadRegsVS(const u8 *v, int n) {
	int regsAvail = 0;
	int regsLoaded = 0;
	X64Reg xrs[4] = {INVALID_REG, INVALID_REG, INVALID_REG};
	bool xrsLoaded[4] = {false, false, false, false};

	_dbg_assert_msg_(JIT, n >= 2 && n <= 4, "LoadRegsVS is only implemented for simd loads.");

	for (int i = 0; i < n; ++i) {
		const MIPSCachedFPReg &mr = vregs[v[i]];
		if (mr.away && (mr.lane == 0 || xregs[mr.location.GetSimpleReg()].mipsRegs[1] == -1)) {
			// Okay, there's nothing else in this reg, so we can use it.
			xrsLoaded[i] = true;
			xrs[i] = mr.location.GetSimpleReg();
			++regsLoaded;
			++regsAvail;
		} else if (mr.away && mr.lane != 0) {
			_dbg_assert_msg_(JIT, false, "LoadRegsVS is not able to handle simd remapping yet, store first.");
		}
	}

	if (regsAvail < n) {
		// Try to grab some without spilling.
		X64Reg xrFree[4];
		int obtained = GetFreeXRegs(xrFree, n - regsAvail, false);
		int pos = 0;
		for (int i = 0; i < n && pos < obtained; ++i) {
			if (xrs[i] == INVALID_REG) {
				// Okay, it's not loaded but we have a reg for this slot.
				xrs[i] = xrFree[pos++];
				++regsAvail;
			}
		}
	}

	// Did we end up with enough regs?
	// TODO: Not handling the case of some regs avail and some loaded right now.
	if (regsAvail < n) {
		regsAvail = GetFreeXRegs(xrs, 2, true);
		_dbg_assert_msg_(JIT, regsAvail >= 2, "Ran out of fp regs for loading simd regs with.");
		_dbg_assert_msg_(JIT, xrs[0] != xrs[1], "Regs for simd load are the same, bad things await.");
		// We spilled, so we assume that all our regs are screwed up now anyway.
		for (int i = 0; i < 4; ++i) {
			xrsLoaded[i] = false;
		}
		regsLoaded = 0;
	}

	// Let's also check if the memory addresses are sequential.
	int sequential = 1;
	for (int i = 1; i < n; ++i) {
		if (voffset[v[i]] != voffset[v[i - 1]] + 1) {
			break;
		}
		++sequential;
	}

	// If they're sequential, and we wouldn't need to store them all, use a single load.
	// But if they're already loaded, we'd have to store, not worth it.
	if (sequential == n && regsLoaded < n) {
		// TODO: What should we do if some are in regs?  Better to assemble?
		for (int i = 0; i < n; ++i) {
			StoreFromRegisterV(v[i]);
		}
		const float *f = &mips->v[voffset[v[0]]];
		if (((intptr_t)f & 0x7) == 0 && n == 2) {
			emit->MOVQ_xmm(xrs[0], vregs[v[0]].location);
		} else if (((intptr_t)f & 0xf) == 0) {
			// On modern processors, MOVUPS on aligned is fast, but maybe not on older ones.
			emit->MOVAPS(xrs[0], vregs[v[0]].location);
		} else {
			emit->MOVUPS(xrs[0], vregs[v[0]].location);
		}
	} else if (regsAvail >= n) {
		// Have enough regs, potentially all in regs.
		auto loadXR = [&](int l) {
			if (!xrsLoaded[l] && n >= l + 1) {
				emit->MOVSS(xrs[l], vregs[v[l]].location);
			}
		};
		// The order here is intentional.
		loadXR(3);
		loadXR(1);
		loadXR(2);
		loadXR(0);
		if (n == 4) {
			// This gives us [w, y] in the y reg.
			emit->UNPCKLPS(xrs[1], Gen::R(xrs[3]));
		}
		if (n >= 3) {
			// This gives us [z, x].  Then we combine with y.
			emit->UNPCKLPS(xrs[0], Gen::R(xrs[2]));
		}
		if (n >= 2) {
			emit->UNPCKLPS(xrs[0], Gen::R(xrs[1]));
		}
	} else {
		_dbg_assert_msg_(JIT, n > 2, "2 should not be possible here.");
		if (n == 3) {
			emit->MOVSS(xrs[1], vregs[v[2]].location);
			emit->MOVSS(xrs[0], vregs[v[1]].location);
			emit->SHUFPS(xrs[0], Gen::R(xrs[1]), _MM_SHUFFLE(3, 0, 0, 0));
			emit->MOVSS(xrs[1], vregs[v[0]].location);
			emit->MOVSS(xrs[0], Gen::R(xrs[1]));
		} else if (n == 4) {
			emit->MOVSS(xrs[1], vregs[v[2]].location);
			emit->MOVSS(xrs[0], vregs[v[3]].location);
			emit->UNPCKLPS(xrs[1], Gen::R(xrs[0]));
			emit->MOVSS(xrs[0], vregs[v[1]].location);
			emit->SHUFPS(xrs[0], Gen::R(xrs[1]), _MM_SHUFFLE(1, 0, 0, 3));
			emit->MOVSS(xrs[1], vregs[v[0]].location);
			emit->MOVSS(xrs[0], Gen::R(xrs[1]));
		}
	}

	return xrs[0];
}

bool FPURegCache::TryMapDirtyInInVS(const u8 *vd, VectorSize vdsz, const u8 *vs, VectorSize vssz, const u8 *vt, VectorSize vtsz, bool avoidLoad) {
	// Don't waste time mapping if some will for sure fail.
	if (!CanMapVS(vd, vdsz) || !CanMapVS(vs, vssz) || !CanMapVS(vt, vtsz)) {
		return false;
	}
	// But, they could still fail based on overlap.  Hopefully not common...
	bool success = TryMapRegsVS(vs, vssz, 0);
	if (success) {
		SpillLockV(vs, vssz);
		success = TryMapRegsVS(vt, vtsz, 0);
	}
	if (success) {
		SpillLockV(vt, vtsz);
		success = TryMapRegsVS(vd, vdsz, avoidLoad ? (MAP_NOINIT | MAP_DIRTY) : MAP_DIRTY);
	}
	ReleaseSpillLockV(vs, vssz);
	ReleaseSpillLockV(vt, vtsz);

	return success;
}

void FPURegCache::SimpleRegsV(const u8 *v, VectorSize vsz, int flags) {
	const int n = GetNumVectorElements(vsz);
	// TODO: Could be more optimal (in case of Discard or etc.)
	for (int i = 0; i < n; ++i) {
		SimpleRegV(v[i], flags);
	}
}

void FPURegCache::SimpleRegsV(const u8 *v, MatrixSize msz, int flags) {
	const int n = GetMatrixSide(msz);
	// TODO: Could be more optimal (in case of Discard or etc.)
	for (int i = 0; i < n; ++i) {
		for (int j = 0; j < n; ++j) {
			SimpleRegV(v[j * 4 + i], flags);
		}
	}
}

void FPURegCache::SimpleRegV(const u8 v, int flags) {
	const MIPSCachedFPReg &vr = vregs[v];
	if (vr.lane != 0) {
		// This will never end up in a register this way, so ignore dirty.
		if ((flags & MAP_NOINIT)) {
			// This will discard only this reg, and store the others.
			DiscardV(v);
		} else {
			StoreFromRegisterV(v);
		}
	} else if (vr.away) {
		// There are no immediates in the FPR reg file, so we already had this in a register. Make dirty as necessary.
		xregs[VX(v)].dirty |= (flags & MAP_DIRTY) != 0;
		_assert_msg_(JIT, vr.location.IsSimpleReg(), "not loaded and not simple.");
	}
	Invariant();
}

void FPURegCache::ReleaseSpillLock(int mipsreg)
{
	regs[mipsreg].locked = false;
}

void FPURegCache::ReleaseSpillLocks() {
	for (int i = 0; i < NUM_MIPS_FPRS; i++)
		regs[i].locked = false;
	for (int i = TEMP0; i < TEMP0 + NUM_TEMPS; ++i)
		DiscardR(i);
}

void FPURegCache::MapReg(const int i, bool doLoad, bool makeDirty) {
	pendingFlush = true;
	_assert_msg_(JIT, !regs[i].location.IsImm(), "WTF - load - imm");
	if (!regs[i].away) {
		// Reg is at home in the memory register file. Let's pull it out.
		X64Reg xr = GetFreeXReg();
		_assert_msg_(JIT, xr >= 0 && xr < NUM_X_FPREGS, "WTF - load - invalid reg");
		xregs[xr].mipsReg = i;
		xregs[xr].dirty = makeDirty;
		OpArg newloc = ::Gen::R(xr);
		if (doLoad)	{
			if (!regs[i].location.IsImm() && (regs[i].location.offset & 0x3)) {
				PanicAlert("WARNING - misaligned fp register location %i", i);
			}
			emit->MOVSS(xr, regs[i].location);
		}
		regs[i].location = newloc;
		regs[i].lane = 0;
		regs[i].away = true;
	} else if (regs[i].lane != 0) {
		// Well, darn.  This means we need to flush it.
		// TODO: This could be more optimal.  Also check flags.
		StoreFromRegister(i);
		MapReg(i, doLoad, makeDirty);
	} else {
		// There are no immediates in the FPR reg file, so we already had this in a register. Make dirty as necessary.
		xregs[RX(i)].dirty |= makeDirty;
		_assert_msg_(JIT, regs[i].location.IsSimpleReg(), "not loaded and not simple.");
	}
	Invariant();
}

static int MMShuffleSwapTo0(int lane) {
	if (lane == 0) {
		return _MM_SHUFFLE(3, 2, 1, 0);
	} else if (lane == 1) {
		return _MM_SHUFFLE(3, 2, 0, 1);
	} else if (lane == 2) {
		return _MM_SHUFFLE(3, 0, 1, 2);
	} else if (lane == 3) {
		return _MM_SHUFFLE(0, 2, 1, 3);
	} else {
		PanicAlert("MMShuffleSwapTo0: Invalid lane %d", lane);
		return 0;
	}
}

void FPURegCache::StoreFromRegister(int i) {
	_assert_msg_(JIT, !regs[i].location.IsImm(), "WTF - store - imm");
	if (regs[i].away) {
		X64Reg xr = regs[i].location.GetSimpleReg();
		_assert_msg_(JIT, xr >= 0 && xr < NUM_X_FPREGS, "WTF - store - invalid reg");
		if (regs[i].lane != 0) {
			// Store all of them.
			// TODO: This could be more optimal.  Check if we can MOVUPS/MOVAPS, etc.
			for (int j = 0; j < 4; ++j) {
				int mr = xregs[xr].mipsRegs[j];
				if (mr == -1) {
					continue;
				}
				if (j != 0 && xregs[xr].dirty) {
					emit->SHUFPS(xr, Gen::R(xr), MMShuffleSwapTo0(j));
				}

				OpArg newLoc = GetDefaultLocation(mr);
				if (xregs[xr].dirty) {
					emit->MOVSS(newLoc, xr);
				}
				regs[mr].location = newLoc;
				regs[mr].away = false;
				regs[mr].lane = 0;
				xregs[xr].mipsRegs[j] = -1;
			}
		} else {
			xregs[xr].mipsReg = -1;
			OpArg newLoc = GetDefaultLocation(i);
			emit->MOVSS(newLoc, xr);
			regs[i].location = newLoc;
		}
		xregs[xr].dirty = false;
		regs[i].away = false;
	} else {
		//	_assert_msg_(DYNA_REC,0,"already stored");
	}
	Invariant();
}

void FPURegCache::DiscardR(int i) {
	_assert_msg_(JIT, !regs[i].location.IsImm(), "FPU can't handle imm yet.");
	if (regs[i].away) {
		X64Reg xr = regs[i].location.GetSimpleReg();
		_assert_msg_(JIT, xr >= 0 && xr < NUM_X_FPREGS, "DiscardR: MipsReg had bad X64Reg");
		// Note that we DO NOT write it back here. That's the whole point of Discard.
		if (regs[i].lane != 0) {
			// But we can't just discard all of them in SIMD, just the one lane.
			// TODO: Potentially this could be more optimal (MOVQ or etc.)
			xregs[xr].mipsRegs[regs[i].lane - 1] = -1;
			regs[i].lane = 0;
			for (int j = 0; j < 4; ++j) {
				int mr = xregs[xr].mipsRegs[j];
				if (mr == -1) {
					continue;
				}
				if (j != 0 && xregs[xr].dirty) {
					emit->SHUFPS(xr, Gen::R(xr), MMShuffleSwapTo0(j));
				}

				OpArg newLoc = GetDefaultLocation(mr);
				if (xregs[xr].dirty) {
					emit->MOVSS(newLoc, xr);
				}
				regs[mr].location = newLoc;
				regs[mr].away = false;
				regs[mr].lane = 0;
				xregs[xr].mipsRegs[j] = -1;
			}
		} else {
			xregs[xr].mipsReg = -1;
		}
		xregs[xr].dirty = false;
		regs[i].location = GetDefaultLocation(i);
		regs[i].away = false;
		regs[i].tempLocked = false;
	} else {
		//	_assert_msg_(DYNA_REC,0,"already stored");
		regs[i].tempLocked = false;
	}
	Invariant();
}

void FPURegCache::DiscardVS(int vreg) {
	_assert_msg_(JIT, !vregs[vreg].location.IsImm(), "FPU can't handle imm yet.");

	if (vregs[vreg].away) {
		_assert_msg_(JIT, vregs[vreg].lane != 0, "VS expects a SIMD reg.");
		X64Reg xr = vregs[vreg].location.GetSimpleReg();
		_assert_msg_(JIT, xr >= 0 && xr < NUM_X_FPREGS, "DiscardR: MipsReg had bad X64Reg");
		// Note that we DO NOT write it back here. That's the whole point of Discard.
		for (int i = 0; i < 4; ++i) {
			int mr = xregs[xr].mipsRegs[i];
			if (mr != -1) {
				regs[mr].location = GetDefaultLocation(mr);
				regs[mr].away = false;
				regs[mr].tempLocked = false;
			}
			xregs[xr].mipsRegs[i] = -1;
		}
		xregs[xr].dirty = false;
	} else {
		vregs[vreg].tempLocked = false;
	}
	Invariant();
}

bool FPURegCache::IsTempX(X64Reg xr) {
	return xregs[xr].mipsReg >= TEMP0;
}

int FPURegCache::GetTempR() {
	pendingFlush = true;
	for (int r = TEMP0; r < TEMP0 + NUM_TEMPS; ++r) {
		if (!regs[r].away && !regs[r].tempLocked) {
			regs[r].tempLocked = true;
			return r;
		}
	}

	_assert_msg_(JIT, 0, "Regcache ran out of temp regs, might need to DiscardR() some.");
	return -1;
}

void FPURegCache::Flush() {
	if (!pendingFlush) {
		return;
	}
	for (int i = 0; i < NUM_MIPS_FPRS; i++) {
		if (regs[i].locked) {
			PanicAlert("Somebody forgot to unlock MIPS reg %i.", i);
		}
		if (regs[i].away) {
			if (regs[i].location.IsSimpleReg()) {
				X64Reg xr = RX(i);
				StoreFromRegister(i);
				xregs[xr].dirty = false;
			} else if (regs[i].location.IsImm()) {
				StoreFromRegister(i);
			} else {
				_assert_msg_(JIT,0,"Jit64 - Flush unhandled case, reg %i PC: %08x", i, mips->pc);
			}
		}
	}
	pendingFlush = false;
	Invariant();
}

OpArg FPURegCache::GetDefaultLocation(int reg) const {
	if (reg < 32) {
		return MDisp(CTXREG, reg * 4);
	} else if (reg < 32 + 128) {
		return M(&mips->v[voffset[reg - 32]]);
	} else {
		return M(&tempValues[reg - 32 - 128]);
	}
}

void FPURegCache::Invariant() const {
#ifdef _DEBUG
	_dbg_assert_msg_(JIT, SanityCheck() == 0, "Sanity check failed: %d", SanityCheck());
#endif
}

int FPURegCache::SanityCheck() const {
	for (int i = 0; i < NUM_MIPS_FPRS; i++) {
		const MIPSCachedFPReg &mr = regs[i];

		// FPR can never have imms.
		if (mr.location.IsImm())
			return 1;

		bool reallyAway = mr.location.IsSimpleReg();
		if (reallyAway != mr.away)
			return 2;

		if (mr.lane < 0 || mr.lane > 4)
			return 3;
		if (mr.lane != 0 && !reallyAway)
			return 4;

		if (mr.away) {
			Gen::X64Reg simple = mr.location.GetSimpleReg();
			if (mr.lane == 0) {
				if (xregs[simple].mipsReg != i)
					return 5;
				for (int j = 1; j < 4; ++j) {
					if (xregs[simple].mipsRegs[j] != -1)
						return 6;
				}
			} else {
				if (xregs[simple].mipsRegs[mr.lane - 1] != i)
					return 7;
			}
		}
	}

	for (int i = 0; i < NUM_X_FPREGS; ++i) {
		const X64CachedFPReg &xr = xregs[i];
		bool hasReg = xr.mipsReg != -1;
		if (!hasReg && xr.dirty)
			return 8;

		bool hasMoreRegs = hasReg;
		for (int j = 0; j < 4; ++j) {
			if (xr.mipsRegs[j] == -1) {
				hasMoreRegs = false;
				continue;
			}
			// We can't have a hole in the middle / front.
			if (!hasMoreRegs)
				return 9;

			const MIPSCachedFPReg &mr = regs[xr.mipsRegs[j]];
			if (!mr.location.IsSimpleReg(X64Reg(i)))
				return 10;
		}
	}

	return 0;
}

const int *FPURegCache::GetAllocationOrder(int &count) {
	static const int allocationOrder[] = {
#ifdef _M_X64
		XMM6, XMM7, XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15, XMM2, XMM3, XMM4, XMM5
#elif _M_IX86
		XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
#endif
	};
	count = sizeof(allocationOrder) / sizeof(int);
	return allocationOrder;
}

X64Reg FPURegCache::GetFreeXReg() {
	X64Reg res;
	int obtained = GetFreeXRegs(&res, 1);

	_assert_msg_(JIT, obtained == 1, "Regcache ran out of regs");
	return res;
}

int FPURegCache::GetFreeXRegs(X64Reg *res, int n, bool spill) {
	pendingFlush = true;
	int aCount;
	const int *aOrder = GetAllocationOrder(aCount);

	_dbg_assert_msg_(JIT, n <= NUM_X_FPREGS - 2, "Cannot obtain that many regs.");

	int r = 0;

	for (int i = 0; i < aCount; i++) {
		X64Reg xr = (X64Reg)aOrder[i];
		if (xregs[xr].mipsReg == -1) {
			res[r++] = (X64Reg)xr;
			if (r >= n) {
				break;
			}
		}
	}

	if (r < n && spill) {
		// Okay, not found :(... Force grab one.
		// TODO - add a pass to grab xregs whose mipsreg is not used in the next 3 instructions.
		for (int i = 0; i < aCount; i++) {
			X64Reg xr = (X64Reg)aOrder[i];
			int preg = xregs[xr].mipsReg;
			// We're only spilling here, so don't overlap.
			if (preg != -1 && !regs[preg].locked) {
				StoreFromRegister(preg);
				res[r++] = xr;
				if (r >= n) {
					break;
				}
			}
		}
	}

	for (int i = r; i < n; ++i) {
		res[i] = INVALID_REG;
	}
	return r;
}

void FPURegCache::FlushX(X64Reg reg) {
	if (reg >= NUM_X_FPREGS) {
		PanicAlert("Flushing non existent reg");
	} else if (xregs[reg].mipsReg != -1) {
		StoreFromRegister(xregs[reg].mipsReg);
	}
}

void FPURegCache::GetState(FPURegCacheState &state) const {
	memcpy(state.regs, regs, sizeof(regs));
	memcpy(state.xregs, xregs, sizeof(xregs));
}

void FPURegCache::RestoreState(const FPURegCacheState state) {
	memcpy(regs, state.regs, sizeof(regs));
	memcpy(xregs, state.xregs, sizeof(xregs));
	pendingFlush = true;
}
