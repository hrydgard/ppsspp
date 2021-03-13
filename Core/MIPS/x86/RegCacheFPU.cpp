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
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)

#include <cstring>
#include <emmintrin.h>

#include "Common/Log.h"
#include "Common/x64Emitter.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/x86/Jit.h"
#include "Core/MIPS/x86/RegCache.h"
#include "Core/MIPS/x86/RegCacheFPU.h"

using namespace Gen;
using namespace X64JitConstants;

FPURegCache::FPURegCache() {
	vregs = regs + 32;
}

void FPURegCache::Start(MIPSState *mipsState, MIPSComp::JitState *js, MIPSComp::JitOptions *jo, MIPSAnalyst::AnalysisResults &stats, bool useRip) {
	mips_ = mipsState;
	useRip_ = useRip;
	if (!initialReady) {
		SetupInitialRegs();
		initialReady = true;
	}

	memcpy(xregs, xregsInitial, sizeof(xregs));
	memcpy(regs, regsInitial, sizeof(regs));
	pendingFlush = false;

	js_ = js;
	jo_ = jo;
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
	regs[p1].locked++;
	if (p2 != 0xFF) regs[p2].locked++;
	if (p3 != 0xFF) regs[p3].locked++;
	if (p4 != 0xFF) regs[p4].locked++;
}

void FPURegCache::SpillLockV(const u8 *vec, VectorSize sz) {
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		vregs[vec[i]].locked++;
	}
}

void FPURegCache::SpillLockV(int vec, VectorSize sz) {
	u8 r[4];
	GetVectorRegs(r, sz, vec);
	SpillLockV(r, sz);
}

void FPURegCache::ReleaseSpillLockV(const u8 *vec, VectorSize sz) {
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		vregs[vec[i]].locked = 0;
	}
}

void FPURegCache::ReduceSpillLock(int mipsreg) {
	regs[mipsreg].locked--;
}

void FPURegCache::ReduceSpillLockV(const u8 *vec, VectorSize sz) {
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		vregs[vec[i]].locked--;
	}
}

void FPURegCache::FlushRemap(int oldreg, int newreg) {
	OpArg oldLocation = regs[oldreg].location;
	_assert_msg_(oldLocation.IsSimpleReg(), "FlushRemap: Must already be in an x86 SSE register");
	_assert_msg_(regs[oldreg].lane == 0, "FlushRemap only supports FPR registers");

	X64Reg xr = oldLocation.GetSimpleReg();
	if (oldreg == newreg) {
		xregs[xr].dirty = true;
		return;
	}

	StoreFromRegister(oldreg);

	// Now, if newreg already was mapped somewhere, get rid of that.
	DiscardR(newreg);

	// Now, take over the old register.
	regs[newreg].location = oldLocation;
	regs[newreg].away = true;
	regs[newreg].locked = true;
	regs[newreg].lane = 0;
	xregs[xr].mipsReg = newreg;
	xregs[xr].dirty = true;
}

void FPURegCache::MapRegV(int vreg, int flags) {
	MapReg(vreg + 32, (flags & MAP_NOINIT) != MAP_NOINIT, (flags & MAP_DIRTY) != 0);
}

void FPURegCache::MapRegsV(int vec, VectorSize sz, int flags) {
	u8 r[4];
	GetVectorRegs(r, sz, vec);
	SpillLockV(r, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		MapReg(r[i] + 32, (flags & MAP_NOINIT) != MAP_NOINIT, (flags & MAP_DIRTY) != 0);
	}
	if ((flags & MAP_NOLOCK) != 0) {
		// We have to lock so the sz won't spill, so we unlock after.
		// If they were already locked, we only reduce the lock we added above.
		ReduceSpillLockV(r, sz);
	}
}

void FPURegCache::MapRegsV(const u8 *r, VectorSize sz, int flags) {
	SpillLockV(r, sz);
	for (int i = 0; i < GetNumVectorElements(sz); i++) {
		MapReg(r[i] + 32, (flags & MAP_NOINIT) != MAP_NOINIT, (flags & MAP_DIRTY) != 0);
	}
	if ((flags & MAP_NOLOCK) != 0) {
		// We have to lock so the sz won't spill, so we unlock after.
		// If they were already locked, we only reduce the lock we added above.
		ReduceSpillLockV(r, sz);
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
	X64Reg xr = VSX(v);
	for (int i = 1; i < n; ++i) {
		u8 vi = v[i];
		if (!IsMappedVS(vi) || VSX(&vi) != xr)
			return false;
		if (vregs[vi].lane != i + 1)
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

	_dbg_assert_msg_(jo_->enableVFPUSIMD, "Should not map simd regs when option is off.");

	if (!TryMapRegsVS(r, vsz, flags)) {
		// TODO: Could be more optimal.
		for (int i = 0; i < n; ++i) {
			StoreFromRegisterV(r[i]);
		}
		if (!TryMapRegsVS(r, vsz, flags)) {
			_dbg_assert_msg_(false, "MapRegsVS() failed on second try.");
		}
	}
}

bool FPURegCache::CanMapVS(const u8 *v, VectorSize vsz) {
	const int n = GetNumVectorElements(vsz);

	if (!jo_->enableVFPUSIMD) {
		return false;
	}

	if (IsMappedVS(v, vsz)) {
		return true;
	} else if (vregs[v[0]].lane != 0) {
		const MIPSCachedFPReg &v0 = vregs[v[0]];
		_dbg_assert_msg_(v0.away, "Must be away when lane != 0");
		_dbg_assert_msg_(v0.location.IsSimpleReg(), "Must be is register when lane != 0");

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
		_assert_msg_(!vregs[v[i]].location.IsImm(), "Cannot handle imms in fp cache.");
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
			xregs[VSX(v)].dirty = true;
		if ((flags & MAP_NOLOCK) == 0)
			SpillLockV(v, vsz);
		return true;
	}

	// At this point, some or all are in single regs or memory, and they're not locked there.

	if (n == 1) {
		// Single is easy, just map normally but track as a SIMD reg.
		// This way V/VS can warn about improper usage properly.
		MapRegV(v[0], flags);
		X64Reg vx = VX(v[0]);
		if (vx == INVALID_REG)
			return false;

		vregs[v[0]].lane = 1;
		if ((flags & MAP_DIRTY) != 0)
			xregs[vx].dirty = true;
		if ((flags & MAP_NOLOCK) == 0)
			SpillLockV(v, vsz);
		Invariant();
		return true;
	}

	X64Reg xr;
	if ((flags & MAP_NOINIT) != MAP_NOINIT) {
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
			if (oldXReg != xr) {
				xregs[oldXReg].mipsReg = -1;
			}
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

	if ((flags & MAP_NOLOCK) == 0) {
		SpillLockV(v, vsz);
	}

	Invariant();
	return true;
}

X64Reg FPURegCache::LoadRegsVS(const u8 *v, int n) {
	int regsAvail = 0;
	int regsLoaded = 0;
	X64Reg xrs[4] = {INVALID_REG, INVALID_REG, INVALID_REG, INVALID_REG};
	bool xrsLoaded[4] = {false, false, false, false};

	_dbg_assert_msg_(n >= 2 && n <= 4, "LoadRegsVS is only implemented for simd loads.");

	for (int i = 0; i < n; ++i) {
		const MIPSCachedFPReg &mr = vregs[v[i]];
		if (mr.away) {
			X64Reg mrx = mr.location.GetSimpleReg();
			// If it's not simd, or lanes 1+ are clear, we can use it.
			if (mr.lane == 0 || xregs[mrx].mipsRegs[1] == -1) {
				// Okay, there's nothing else in this reg, so we can use it.
				xrsLoaded[i] = true;
				xrs[i] = mrx;
				++regsLoaded;
				++regsAvail;
			} else if (mr.lane != 0) {
				_dbg_assert_msg_(false, "LoadRegsVS is not able to handle simd remapping yet, store first.");
			}
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

	// Let's also check if the memory addresses are sequential.
	int sequential = 1;
	for (int i = 1; i < n; ++i) {
		if (v[i] < 128 && v[i - 1] < 128) {
			if (voffset[v[i]] != voffset[v[i - 1]] + 1) {
				break;
			}
		} else if (v[i] >= 128 && v[i - 1] >= 128) {
			if (v[i] != v[i - 1] + 1) {
				break;
			}
		} else {
			// Temps can't be sequential with non-temps.
			break;
		}
		++sequential;
	}

	// Did we end up with enough regs?
	// TODO: Not handling the case of some regs avail and some loaded right now.
	if (regsAvail < n && (sequential != n || regsLoaded == n || regsAvail == 0)) {
		regsAvail = GetFreeXRegs(xrs, 2, true);
		_dbg_assert_msg_(regsAvail >= 2, "Ran out of fp regs for loading simd regs with.");
		_dbg_assert_msg_(xrs[0] != xrs[1], "Regs for simd load are the same, bad things await.");
		// We spilled, so we assume that all our regs are screwed up now anyway.
		for (int i = 0; i < 4; ++i) {
			xrsLoaded[i] = false;
		}
		for (int i = 2; i < n; ++i){
			xrs[i] = INVALID_REG;
		}
		regsLoaded = 0;
	}

	// If they're sequential, and we wouldn't need to store them all, use a single load.
	// But if they're already loaded, we'd have to store, not worth it.
	X64Reg res = INVALID_REG;
	if (sequential == n && regsLoaded < n) {
		// TODO: What should we do if some are in regs?  Better to assemble?
		for (int i = 0; i < n; ++i) {
			StoreFromRegisterV(v[i]);
		}

		// Grab any available reg.
		for (int i = 0; i < n; ++i) {
			if (xrs[i] != INVALID_REG) {
				res = xrs[i];
				break;
			}
		}
		const float *f = v[0] < 128 ? &mips_->v[voffset[v[0]]] : &mips_->tempValues[v[0] - 128];
		if (((intptr_t)f & 0x7) == 0 && n == 2) {
			emit->MOVQ_xmm(res, vregs[v[0]].location);
		} else if (((intptr_t)f & 0xf) == 0) {
			// On modern processors, MOVUPS on aligned is fast, but maybe not on older ones.
			emit->MOVAPS(res, vregs[v[0]].location);
		} else {
			emit->MOVUPS(res, vregs[v[0]].location);
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
		res = xrs[0];
	} else {
		_dbg_assert_msg_(n > 2, "2 should not be possible here.");

		// Available regs are less than n, and some may be loaded.
		// Let's grab the most optimal unloaded ones.
		X64Reg xr1 = n == 3 ? xrs[1] : xrs[3];
		X64Reg xr2 = xrs[2];
		if (xr1 == INVALID_REG) {
			// Not one of the available ones.  Grab another.
			for (int i = n - 1; i >= 0; --i) {
				if (xrs[i] != INVALID_REG && xrs[i] != xr2) {
					StoreFromRegisterV(v[i]);
					xr1 = xrs[i];
					break;
				}
			}
		}
		if (xr2 == INVALID_REG) {
			// Not one of the available ones.  Grab another.
			for (int i = n - 1; i >= 0; --i) {
				if (xrs[i] != INVALID_REG && xrs[i] != xr1) {
					StoreFromRegisterV(v[i]);
					xr2 = xrs[i];
					break;
				}
			}
		}

		if (n == 3) {
			if (!vregs[v[2]].location.IsSimpleReg(xr2))
				emit->MOVSS(xr2, vregs[v[2]].location);
			if (!vregs[v[1]].location.IsSimpleReg(xr1))
				emit->MOVSS(xr1, vregs[v[1]].location);
			emit->SHUFPS(xr1, Gen::R(xr2), _MM_SHUFFLE(3, 0, 0, 0));
			emit->MOVSS(xr2, vregs[v[0]].location);
			emit->MOVSS(xr1, Gen::R(xr2));
		} else if (n == 4) {
			if (!vregs[v[2]].location.IsSimpleReg(xr2))
				emit->MOVSS(xr2, vregs[v[2]].location);
			if (!vregs[v[3]].location.IsSimpleReg(xr1))
				emit->MOVSS(xr1, vregs[v[3]].location);
			emit->UNPCKLPS(xr2, Gen::R(xr1));
			emit->MOVSS(xr1, vregs[v[1]].location);
			emit->SHUFPS(xr1, Gen::R(xr2), _MM_SHUFFLE(1, 0, 0, 3));
			emit->MOVSS(xr2, vregs[v[0]].location);
			emit->MOVSS(xr1, Gen::R(xr2));
		}
		res = xr1;
	}

	return res;
}

bool FPURegCache::TryMapDirtyInVS(const u8 *vd, VectorSize vdsz, const u8 *vs, VectorSize vssz, bool avoidLoad) {
	// Don't waste time mapping if some will for sure fail.
	if (!CanMapVS(vd, vdsz) || !CanMapVS(vs, vssz)) {
		return false;
	}
	// But, they could still fail based on overlap.  Hopefully not common...
	bool success = TryMapRegsVS(vs, vssz, 0);
	if (success) {
		success = TryMapRegsVS(vd, vdsz, avoidLoad ? MAP_NOINIT : MAP_DIRTY);
	}
	ReleaseSpillLockV(vs, vssz);
	ReleaseSpillLockV(vd, vdsz);

	_dbg_assert_msg_(!success || IsMappedVS(vd, vdsz), "vd should be mapped now");
	_dbg_assert_msg_(!success || IsMappedVS(vs, vssz), "vs should be mapped now");

	return success;
}

bool FPURegCache::TryMapDirtyInInVS(const u8 *vd, VectorSize vdsz, const u8 *vs, VectorSize vssz, const u8 *vt, VectorSize vtsz, bool avoidLoad) {
	// Don't waste time mapping if some will for sure fail.
	if (!CanMapVS(vd, vdsz) || !CanMapVS(vs, vssz) || !CanMapVS(vt, vtsz)) {
		return false;
	}


	// But, they could still fail based on overlap.  Hopefully not common...
	bool success = TryMapRegsVS(vs, vssz, 0);
	if (success) {
		success = TryMapRegsVS(vt, vtsz, 0);
	}
	if (success) {
		success = TryMapRegsVS(vd, vdsz, avoidLoad ? MAP_NOINIT : MAP_DIRTY);
	}
	ReleaseSpillLockV(vd, vdsz);
	ReleaseSpillLockV(vs, vssz);
	ReleaseSpillLockV(vt, vtsz);

	_dbg_assert_msg_(!success || IsMappedVS(vd, vdsz), "vd should be mapped now");
	_dbg_assert_msg_(!success || IsMappedVS(vs, vssz), "vs should be mapped now");
	_dbg_assert_msg_(!success || IsMappedVS(vt, vtsz), "vt should be mapped now");

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
	MIPSCachedFPReg &vr = vregs[v];
	// Special optimization: if it's in a single simd, we can keep it there.
	if (vr.lane == 1 && xregs[VSX(&v)].mipsRegs[1] == -1) {
		if (flags & MAP_DIRTY) {
			xregs[VSX(&v)].dirty = true;
		}
		// Just change the lane to 0.
		vr.lane = 0;
	} else if (vr.lane != 0) {
		// This will never end up in a register this way, so ignore dirty.
		if ((flags & MAP_NOINIT) == MAP_NOINIT) {
			// This will discard only this reg, and store the others.
			DiscardV(v);
		} else {
			StoreFromRegisterV(v);
		}
	} else if (vr.away) {
		// There are no immediates in the FPR reg file, so we already had this in a register. Make dirty as necessary.
		if (flags & MAP_DIRTY) {
			xregs[VX(v)].dirty = true;
		}
		_assert_msg_(vr.location.IsSimpleReg(), "not loaded and not simple.");
	}
	Invariant();
}

void FPURegCache::ReleaseSpillLock(int mipsreg) {
	regs[mipsreg].locked = 0;
}

void FPURegCache::ReleaseSpillLocks() {
	for (int i = 0; i < NUM_MIPS_FPRS; i++)
		regs[i].locked = 0;
	for (int i = TEMP0; i < TEMP0 + NUM_X86_FPU_TEMPS; ++i)
		DiscardR(i);
}

void FPURegCache::MapReg(const int i, bool doLoad, bool makeDirty) {
	pendingFlush = true;
	_assert_msg_(!regs[i].location.IsImm(), "WTF - FPURegCache::MapReg - imm");
	_assert_msg_(i >= 0 && i < NUM_MIPS_FPRS, "WTF - FPURegCache::MapReg - invalid mips reg %d", i);

	if (!regs[i].away) {
		// Reg is at home in the memory register file. Let's pull it out.
		X64Reg xr = GetFreeXReg();
		_assert_msg_(xr < NUM_X_FPREGS, "WTF - FPURegCache::MapReg - invalid reg %d", (int)xr);
		xregs[xr].mipsReg = i;
		xregs[xr].dirty = makeDirty;
		OpArg newloc = ::Gen::R(xr);
		if (doLoad)	{
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
		_assert_msg_(regs[i].location.IsSimpleReg(), "not loaded and not simple.");
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
		_assert_msg_(false, "MMShuffleSwapTo0: Invalid lane %d", lane);
		return 0;
	}
}

void FPURegCache::StoreFromRegister(int i) {
	_assert_msg_(!regs[i].location.IsImm(), "WTF - FPURegCache::StoreFromRegister - it's an imm");
	_assert_msg_(i >= 0 && i < NUM_MIPS_FPRS, "WTF - FPURegCache::StoreFromRegister - invalid mipsreg %i PC=%08x", i, js_->compilerPC);

	if (regs[i].away) {
		X64Reg xr = regs[i].location.GetSimpleReg();
		_assert_msg_(xr < NUM_X_FPREGS, "WTF - FPURegCache::StoreFromRegister - invalid reg: x %i (mr: %i). PC=%08x", (int)xr, i, js_->compilerPC);
		if (regs[i].lane != 0) {
			const int *mri = xregs[xr].mipsRegs;
			int seq = 1;
			for (int j = 1; j < 4; ++j) {
				if (mri[j] == -1) {
					break;
				}
				if (mri[j] - 32 >= 128 && mri[j - 1] - 32 >= 128 && mri[j] == mri[j - 1] + 1) {
					seq++;
				} else if (mri[j] - 32 < 128 && mri[j - 1] - 32 < 128 && voffset[mri[j] - 32] == voffset[mri[j - 1] - 32] + 1) {
					seq++;
				} else {
					break;
				}
			}

			const float *f = mri[0] - 32 < 128 ? &mips_->v[voffset[mri[0] - 32]] : &mips_->tempValues[mri[0] - 32 - 128];
			int align = (intptr_t)f & 0xf;

			// If we can do a multistore...
			if ((seq == 2 && (align & 0x7) == 0) || seq == 4) {
				OpArg newLoc = GetDefaultLocation(mri[0]);
				if (xregs[xr].dirty) {
					if (seq == 4 && align == 0)
						emit->MOVAPS(newLoc, xr);
					else if (seq == 4)
						emit->MOVUPS(newLoc, xr);
					else
						emit->MOVQ_xmm(newLoc, xr);
				}
				for (int j = 0; j < seq; ++j) {
					int mr = xregs[xr].mipsRegs[j];
					if (mr == -1) {
						continue;
					}
					OpArg newLoc = GetDefaultLocation(mr);
					regs[mr].location = newLoc;
					regs[mr].away = false;
					regs[mr].lane = 0;
					xregs[xr].mipsRegs[j] = -1;
				}
			} else {
				seq = 0;
			}
			// Store the rest.
			for (int j = seq; j < 4; ++j) {
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
			OpArg newLoc = GetDefaultLocation(i);
			xregs[xr].mipsReg = -1;
			if (xregs[xr].dirty) {
				emit->MOVSS(newLoc, xr);
			}
			regs[i].location = newLoc;
		}
		xregs[xr].dirty = false;
		regs[i].away = false;
	} else {
		//	_assert_msg_(false,"already stored");
	}
	Invariant();
}

void FPURegCache::DiscardR(int i) {
	_assert_msg_(!regs[i].location.IsImm(), "FPU can't handle imm yet.");
	if (regs[i].away) {
		X64Reg xr = regs[i].location.GetSimpleReg();
		_assert_msg_(xr < NUM_X_FPREGS, "DiscardR: MipsReg had bad X64Reg");
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
		//	_assert_msg_(false,"already stored");
		regs[i].tempLocked = false;
	}
	Invariant();
}

void FPURegCache::DiscardVS(int vreg) {
	_assert_msg_(!vregs[vreg].location.IsImm(), "FPU can't handle imm yet.");

	if (vregs[vreg].away) {
		_assert_msg_(vregs[vreg].lane != 0, "VS expects a SIMD reg.");
		X64Reg xr = vregs[vreg].location.GetSimpleReg();
		_assert_msg_(xr < NUM_X_FPREGS, "DiscardR: MipsReg had bad X64Reg");
		// Note that we DO NOT write it back here. That's the whole point of Discard.
		for (int i = 0; i < 4; ++i) {
			int mr = xregs[xr].mipsRegs[i];
			if (mr != -1) {
				regs[mr].location = GetDefaultLocation(mr);
				regs[mr].away = false;
				regs[mr].tempLocked = false;
				regs[mr].lane = 0;
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
	for (int r = TEMP0; r < TEMP0 + NUM_X86_FPU_TEMPS; ++r) {
		if (!regs[r].away && !regs[r].tempLocked) {
			regs[r].tempLocked = true;
			return r;
		}
	}

	_assert_msg_(false, "Regcache ran out of temp regs, might need to DiscardR() some.");
	return -1;
}

int FPURegCache::GetTempVS(u8 *v, VectorSize vsz) {
	pendingFlush = true;
	const int n = GetNumVectorElements(vsz);

	// Let's collect regs as we go, but try for n free in a row.
	int found = 0;
	for (int r = TEMP0; r <= TEMP0 + NUM_X86_FPU_TEMPS - n; ++r) {
		if (regs[r].away || regs[r].tempLocked) {
			continue;
		}

		// How many free siblings does this have?
		int seq = 1;
		for (int i = 1; i < n; ++i) {
			if (regs[r + i].away || regs[r + i].tempLocked) {
				break;
			}
			++seq;
		}

		if (seq == n) {
			// Got 'em.  Exacty as many as we need.
			for (int i = 0; i < n; ++i) {
				v[i] = r + i - 32;
			}
			found = n;
			break;
		}

		if (found < n) {
			v[found++] = r - 32;
		}
	}

	if (found != n) {
		_assert_msg_(false, "Regcache ran out of temp regs, might need to DiscardR() some.");
		return -1;
	}

	for (int i = 0; i < n; ++i) {
		regs[v[i] + 32].tempLocked = true;
	}

	return 0;  // ??
}

void FPURegCache::Flush() {
	if (!pendingFlush) {
		return;
	}
	for (int i = 0; i < NUM_MIPS_FPRS; i++) {
		_assert_msg_(!regs[i].locked, "Somebody forgot to unlock MIPS reg %d.", i);
		if (regs[i].away) {
			if (regs[i].location.IsSimpleReg()) {
				X64Reg xr = RX(i);
				StoreFromRegister(i);
				xregs[xr].dirty = false;
			} else if (regs[i].location.IsImm()) {
				StoreFromRegister(i);
			} else {
				_assert_msg_(false, "Jit64 - Flush unhandled case, reg %i PC: %08x", i, mips_->pc);
			}
		}
	}
	pendingFlush = false;
	Invariant();
}

OpArg FPURegCache::GetDefaultLocation(int reg) const {
	if (reg < 32) {
		// Smaller than RIP addressing since we can use a byte offset.
		return MDisp(CTXREG, reg * 4);
	} else if (reg < 32 + 128) {
		// Here, RIP has the advantage so let's use it when possible
		if (useRip_) {
			return M(&mips_->v[voffset[reg - 32]]);  // rip accessible
		} else {
			return MIPSSTATE_VAR_ELEM32(v[0], voffset[reg - 32]);
		}
	} else {
		if (useRip_) {
			return M(&mips_->tempValues[reg - 32 - 128]);  // rip accessible
		} else {
			return MIPSSTATE_VAR_ELEM32(tempValues[0], reg - 32 - 128);
		}
	}
}

void FPURegCache::Invariant() const {
#if 0
	_assert_msg_(SanityCheck() == 0, "Sanity check failed: %d", SanityCheck());
#endif
}

static int GetMRMtx(int mr) {
	if (mr < 32)
		return -1;
	if (mr >= 128 + 32)
		return -1;
	return ((mr - 32) >> 2) & 7;
}

static int GetMRRow(int mr) {
	if (mr < 32)
		return -1;
	if (mr >= 128 + 32)
		return -1;
	return ((mr - 32) >> 0) & 3;
}

static int GetMRCol(int mr) {
	if (mr < 32)
		return -1;
	if (mr >= 128 + 32)
		return -1;
	return ((mr - 32) >> 5) & 3;
}

static bool IsMRTemp(int mr) {
	return mr >= 128 + 32;
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
		int mtx = -2;
		int row = -2;
		int col = -2;
		bool rowMatched = true;
		bool colMatched = true;
		for (int j = 0; j < 4; ++j) {
			if (xr.mipsRegs[j] == -1) {
				hasMoreRegs = false;
				continue;
			}
			if (xr.mipsRegs[j] >= NUM_MIPS_FPRS) {
				return 13;
			}
			// We can't have a hole in the middle / front.
			if (!hasMoreRegs)
				return 9;

			const MIPSCachedFPReg &mr = regs[xr.mipsRegs[j]];
			if (!mr.location.IsSimpleReg(X64Reg(i)))
				return 10;

			if (!IsMRTemp(xr.mipsRegs[j])) {
				if (mtx == -2)
					mtx = GetMRMtx(xr.mipsRegs[j]);
				else if (mtx != GetMRMtx(xr.mipsRegs[j]))
					return 11;

				if (row == -2)
					row = GetMRRow(xr.mipsRegs[j]);
				else if (row != GetMRRow(xr.mipsRegs[j]))
					rowMatched = false;

				if (col == -2)
					col = GetMRCol(xr.mipsRegs[j]);
				else if (col != GetMRCol(xr.mipsRegs[j]))
					colMatched = false;
			}
		}
		if (!rowMatched && !colMatched) {
			return 12;
		}
	}

	return 0;
}

const int *FPURegCache::GetAllocationOrder(int &count) {
	static const int allocationOrder[] = {
#if PPSSPP_ARCH(AMD64)
		XMM6, XMM7, XMM8, XMM9, XMM10, XMM11, XMM12, XMM13, XMM14, XMM15, XMM2, XMM3, XMM4, XMM5
#elif PPSSPP_ARCH(X86)
		XMM2, XMM3, XMM4, XMM5, XMM6, XMM7,
#endif
	};
	count = sizeof(allocationOrder) / sizeof(int);
	return allocationOrder;
}

X64Reg FPURegCache::GetFreeXReg() {
	X64Reg res;
	int obtained = GetFreeXRegs(&res, 1);

	_assert_msg_(obtained == 1, "Regcache ran out of regs");
	return res;
}

int FPURegCache::GetFreeXRegs(X64Reg *res, int n, bool spill) {
	pendingFlush = true;
	int aCount;
	const int *aOrder = GetAllocationOrder(aCount);

	_dbg_assert_msg_(n <= NUM_X_FPREGS - 2, "Cannot obtain that many regs.");

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
			_assert_msg_(preg >= -1 && preg < NUM_MIPS_FPRS, "WTF - FPURegCache::GetFreeXRegs - invalid mips reg %d in xr %d", preg, (int)xr);

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
		_assert_msg_(false, "Flushing non existent reg");
	} else if (xregs[reg].mipsReg != -1) {
		StoreFromRegister(xregs[reg].mipsReg);
	}
}

void FPURegCache::GetState(FPURegCacheState &state) const {
	memcpy(state.regs, regs, sizeof(regs));
	memcpy(state.xregs, xregs, sizeof(xregs));
}

void FPURegCache::RestoreState(const FPURegCacheState& state) {
	memcpy(regs, state.regs, sizeof(regs));
	memcpy(xregs, state.xregs, sizeof(xregs));
	pendingFlush = true;
}

#endif // PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
