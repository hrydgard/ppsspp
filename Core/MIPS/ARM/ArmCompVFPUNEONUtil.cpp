// Copyright (c) 2013- PPSSPP Project.

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

// NEON VFPU
// This is where we will create an alternate implementation of the VFPU emulation
// that uses NEON Q registers to cache pairs/tris/quads, and so on.
// Will require major extensions to the reg cache and other things.

// ARM NEON can only do pairs and quads, not tris and scalars.
// We can do scalars, though, for many operations if all the operands
// are below Q8 (D16, S32) using regular VFP instructions but really not sure
// if it's worth it.

#include "ppsspp_config.h"
#if PPSSPP_ARCH(ARM)

#include <cmath>

#include "Common/Math/math_util.h"

#include "Common/CPUDetect.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/Config.h"
#include "Core/Reporting.h"

#include "Core/MIPS/ARM/ArmJit.h"
#include "Core/MIPS/ARM/ArmRegCache.h"
#include "Core/MIPS/ARM/ArmCompVFPUNEONUtil.h"

// TODO: Somehow #ifdef away on ARMv5eabi, without breaking the linker.

#define _RS MIPS_GET_RS(op)
#define _RT MIPS_GET_RT(op)
#define _RD MIPS_GET_RD(op)
#define _FS MIPS_GET_FS(op)
#define _FT MIPS_GET_FT(op)
#define _FD MIPS_GET_FD(op)
#define _SA MIPS_GET_SA(op)
#define _POS  ((op>> 6) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)
#define _IMM16 (signed short)(op & 0xFFFF)
#define _IMM26 (op & 0x03FFFFFF)

namespace MIPSComp {

using namespace ArmGen;
using namespace ArmJitConstants;

static const float minus_one = -1.0f;
static const float one = 1.0f;
static const float zero = 0.0f;
	
// On NEON, we map triples to Q registers and singles to D registers.
// Sometimes, as when doing dot products, it matters what's in that unused reg. This zeroes it.
void ArmJit::NEONMaskToSize(ARMReg vs, VectorSize sz) {
	// TODO
}

ARMReg ArmJit::NEONMapPrefixST(int mipsReg, VectorSize sz, u32 prefix, int mapFlags) {
	static const float constantArray[8] = { 0.f, 1.f, 2.f, 0.5f, 3.f, 1.f / 3.f, 0.25f, 1.f / 6.f };
	static const float constantArrayNegated[8] = { -0.f, -1.f, -2.f, -0.5f, -3.f, -1.f / 3.f, -0.25f, -1.f / 6.f };

	// Applying prefixes in SIMD fashion will actually be a lot easier than the old style.
	if (prefix == 0xE4) {
		return fpr.QMapReg(mipsReg, sz, mapFlags);
	}

	int n = GetNumVectorElements(sz);

	int regnum[4] = { -1, -1, -1, -1 };
	int abs[4] = { 0 };
	int negate[4] = { 0 };
	int constants[4] = { 0 };
	int constNum[4] = { 0 };

	int full_mask = (1 << n) - 1;
	
	int abs_mask = (prefix >> 8) & full_mask;
	int negate_mask = (prefix >> 16) & full_mask;
	int constants_mask = (prefix >> 12) & full_mask;

	// Decode prefix to keep the rest readable
	int permuteMask = 0;
	for (int i = 0; i < n; i++) {
		permuteMask |= 3 << (i * 2);
		regnum[i] = (prefix >> (i * 2)) & 3;
		abs[i] = (prefix >> (8 + i)) & 1;
		negate[i] = (prefix >> (16 + i)) & 1;
		constants[i] = (prefix >> (12 + i)) & 1;

		if (constants[i]) {
			constNum[i] = regnum[i] + (abs[i] << 2);
			abs[i] = 0;
		}
	}
	abs_mask &= ~constants_mask;

	bool anyPermute = (prefix & permuteMask) != (0xE4 & permuteMask);

	if (constants_mask == full_mask) {
		// It's all constants! Don't even bother mapping the input register,
		// just allocate a temp one.
		// If a single, this can sometimes be done cheaper. But meh.
		ARMReg ar = fpr.QAllocTemp(sz);
		for (int i = 0; i < n; i++) {
			if ((i & 1) == 0) {
				if (constNum[i] == constNum[i + 1]) {
					// Replace two loads with a single immediate when easily possible.
					ARMReg dest = i & 2 ? D_1(ar) : D_0(ar);
					switch (constNum[i]) {
					case 0:
					case 1:
						{
							float c = constantArray[constNum[i]];
							VMOV_immf(dest, negate[i] ? -c : c);
						}
						break;
						// TODO: There are a few more that are doable.
					default:
						goto skip;
					}

					i++;
					continue;
				skip:
					;
				}
			}
			MOVP2R(R0, (negate[i] ? constantArrayNegated : constantArray) + constNum[i]);
			VLD1_lane(F_32, ar, R0, i, true);
		}
		return ar;
	}

	// 1. Permute.
	// 2. Abs
	// If any constants:
	// 3. Replace values with constants
	// 4. Negate

	ARMReg inputAR = fpr.QMapReg(mipsReg, sz, mapFlags);
	ARMReg ar = fpr.QAllocTemp(sz);

	if (!anyPermute) {
		VMOV(ar, inputAR);
		// No permutations!
	} else {
		bool allSame = false;
		for (int i = 1; i < n; i++) {
			if (regnum[0] == regnum[i])
				allSame = true;
		}

		if (allSame) {
			// Easy, someone is duplicating one value onto all the reg parts.
			// If this is happening and QMapReg must load, we can combine these two actions
			// into a VLD1_lane. TODO
			VDUP(F_32, ar, inputAR, regnum[0]);
		} else {
			// Do some special cases
			if (regnum[0] == 1 && regnum[1] == 0) {
				INFO_LOG(Log::HLE, "PREFIXST: Bottom swap!");
				VREV64(I_32, ar, inputAR);
				regnum[0] = 0;
				regnum[1] = 1;
			}

			// TODO: Make a generic fallback using another temp register

			bool match = true;
			for (int i = 0; i < n; i++) {
				if (regnum[i] != i)
					match = false;
			}

			// TODO: Cannot do this permutation yet!
			if (!match) {
				ERROR_LOG(Log::HLE, "PREFIXST: Unsupported permute! %i %i %i %i / %i", regnum[0], regnum[1], regnum[2], regnum[3], n);
				VMOV(ar, inputAR);
			}
		}
	}

	// ABS
	// Two methods: If all lanes are "absoluted", it's easy.
	if (abs_mask == full_mask) {
		// TODO: elide the above VMOV (in !anyPermute) when possible
		VABS(F_32, ar, ar);
	} else if (abs_mask != 0) {
		// Partial ABS!
		if (abs_mask == 3) {
			VABS(F_32, D_0(ar), D_0(ar));
		} else {
			// Horrifying fallback: Mov to Q0, abs, move back.
			// TODO: Optimize for lower quads where we don't need to move.
			VMOV(MatchSize(Q0, ar), ar);
			for (int i = 0; i < n; i++) {
				if (abs_mask & (1 << i)) {
					VABS((ARMReg)(S0 + i), (ARMReg)(S0 + i));
				}
			}
			VMOV(ar, MatchSize(Q0, ar));
			INFO_LOG(Log::HLE, "PREFIXST: Partial ABS %i/%i! Slow fallback generated.", abs_mask, full_mask);
		}
	}

	if (negate_mask == full_mask) {
		// TODO: elide the above VMOV when possible
		VNEG(F_32, ar, ar);
	} else if (negate_mask != 0) {
		// Partial negate! I guess we build sign bits in another register
		// and simply XOR.
		if (negate_mask == 3) {
			VNEG(F_32, D_0(ar), D_0(ar));
		} else {
			// Horrifying fallback: Mov to Q0, negate, move back.
			// TODO: Optimize for lower quads where we don't need to move.
			VMOV(MatchSize(Q0, ar), ar);
			for (int i = 0; i < n; i++) {
				if (negate_mask & (1 << i)) {
					VNEG((ARMReg)(S0 + i), (ARMReg)(S0 + i));
				}
			}
			VMOV(ar, MatchSize(Q0, ar));
			INFO_LOG(Log::HLE, "PREFIXST: Partial Negate %i/%i! Slow fallback generated.", negate_mask, full_mask);
		}
	}

	// Insert constants where requested, and check negate!
	for (int i = 0; i < n; i++) {
		if (constants[i]) {
			MOVP2R(R0, (negate[i] ? constantArrayNegated : constantArray) + constNum[i]);
			VLD1_lane(F_32, ar, R0, i, true);
		}
	}

	return ar;
}

ArmJit::DestARMReg ArmJit::NEONMapPrefixD(int vreg, VectorSize sz, int mapFlags) {
	// Inverted from the actual bits, easier to reason about 1 == write
	int writeMask = (~(js.prefixD >> 8)) & 0xF;
	int n = GetNumVectorElements(sz);
	int full_mask = (1 << n) - 1;

	DestARMReg dest;
	dest.sz = sz;
	if ((writeMask & full_mask) == full_mask) {
		// No need to apply a write mask.
		// Let's not make things complicated.
		dest.rd = fpr.QMapReg(vreg, sz, mapFlags);
		dest.backingRd = dest.rd;
	} else {
		// Allocate a temporary register.
		ERROR_LOG(Log::JIT, "PREFIXD: Write mask allocated! %i/%i", writeMask, full_mask);
		dest.rd = fpr.QAllocTemp(sz);
		dest.backingRd = fpr.QMapReg(vreg, sz, mapFlags & ~MAP_NOINIT);  // Force initialization of the backing reg.
	}
	return dest;
}

void ArmJit::NEONApplyPrefixD(DestARMReg dest) {
	// Apply clamps to dest.rd
	int n = GetNumVectorElements(dest.sz);

	int sat1_mask = 0;
	int sat3_mask = 0;
	int full_mask = (1 << n) - 1;
	for (int i = 0; i < n; i++) {
		int sat = (js.prefixD >> (i * 2)) & 3;
		if (sat == 1)
			sat1_mask |= 1 << i;
		if (sat == 3)
			sat3_mask |= 1 << i;
	}

	if (sat1_mask && sat3_mask) {
		// Why would anyone do this?
		ERROR_LOG(Log::JIT, "PREFIXD: Can't have both sat[0-1] and sat[-1-1] at the same time yet");
	}

	if (sat1_mask) {
		if (sat1_mask != full_mask) {
			ERROR_LOG(Log::JIT, "PREFIXD: Can't have partial sat1 mask yet (%i vs %i)", sat1_mask, full_mask);
		}
		if (IsD(dest.rd)) {
			VMOV_immf(D0, 0.0);
			VMOV_immf(D1, 1.0);
			VMAX(F_32, dest.rd, dest.rd, D0);
			VMIN(F_32, dest.rd, dest.rd, D1);
		} else {
			VMOV_immf(Q0, 1.0);
			VMIN(F_32, dest.rd, dest.rd, Q0);
			VMOV_immf(Q0, 0.0);
			VMAX(F_32, dest.rd, dest.rd, Q0);
		}
	}

	if (sat3_mask && sat1_mask != full_mask) {
		if (sat3_mask != full_mask) {
			ERROR_LOG(Log::JIT, "PREFIXD: Can't have partial sat3 mask yet (%i vs %i)", sat3_mask, full_mask);
		}
		if (IsD(dest.rd)) {
			VMOV_immf(D0, 0.0);
			VMOV_immf(D1, 1.0);
			VMAX(F_32, dest.rd, dest.rd, D0);
			VMIN(F_32, dest.rd, dest.rd, D1);
		} else {
			VMOV_immf(Q0, 1.0);
			VMIN(F_32, dest.rd, dest.rd, Q0);
			VMOV_immf(Q0, -1.0);
			VMAX(F_32, dest.rd, dest.rd, Q0);
		}
	}

	// Check for actual mask operation (unrelated to the "masks" above).
	if (dest.backingRd != dest.rd) {
		// This means that we need to apply the write mask, from rd to backingRd.
		// What a pain. We can at least shortcut easy cases like half the register.
		// And we can generate the masks easily with some of the crazy vector imm modes. (bits2bytes for example).
		// So no need to load them from RAM.
		int writeMask = (~(js.prefixD >> 8)) & 0xF;

		if (writeMask == 3) {
			INFO_LOG(Log::JIT, "Doing writemask = 3");
			VMOV(D_0(dest.rd), D_0(dest.backingRd));
		} else {
			// TODO
			ERROR_LOG(Log::JIT, "PREFIXD: Arbitrary write masks not supported (%i / %i)", writeMask, full_mask);
			VMOV(dest.backingRd, dest.rd);
		}
	}
}

ArmJit::MappedRegs ArmJit::NEONMapDirtyInIn(MIPSOpcode op, VectorSize dsize, VectorSize ssize, VectorSize tsize, bool applyPrefixes) {
	MappedRegs regs;
	if (applyPrefixes) {
		regs.vs = NEONMapPrefixS(_VS, ssize, 0);
		regs.vt = NEONMapPrefixT(_VT, tsize, 0);
	} else {
		regs.vs = fpr.QMapReg(_VS, ssize, 0);
		regs.vt = fpr.QMapReg(_VT, ssize, 0);
	}

	regs.overlap = GetVectorOverlap(_VD, dsize, _VS, ssize) > 0 || GetVectorOverlap(_VD, dsize, _VT, ssize);
	if (applyPrefixes) {
		regs.vd = NEONMapPrefixD(_VD, dsize, MAP_DIRTY | (regs.overlap ? 0 : MAP_NOINIT));
	} else {
		regs.vd.rd = fpr.QMapReg(_VD, dsize, MAP_DIRTY | (regs.overlap ? 0 : MAP_NOINIT));
		regs.vd.backingRd = regs.vd.rd;
		regs.vd.sz = dsize;
	}
	return regs;
}

ArmJit::MappedRegs ArmJit::NEONMapInIn(MIPSOpcode op, VectorSize ssize, VectorSize tsize, bool applyPrefixes) {
	MappedRegs regs;
	if (applyPrefixes) {
		regs.vs = NEONMapPrefixS(_VS, ssize, 0);
		regs.vt = NEONMapPrefixT(_VT, tsize, 0);
	} else {
		regs.vs = fpr.QMapReg(_VS, ssize, 0);
		regs.vt = fpr.QMapReg(_VT, ssize, 0);
	}
	regs.vd.rd = INVALID_REG;
	regs.vd.sz = V_Invalid;
	return regs;
}

ArmJit::MappedRegs ArmJit::NEONMapDirtyIn(MIPSOpcode op, VectorSize dsize, VectorSize ssize, bool applyPrefixes) {
	MappedRegs regs;
	regs.vs = NEONMapPrefixS(_VS, ssize, 0);
	regs.overlap = GetVectorOverlap(_VD, dsize, _VS, ssize) > 0;
	regs.vd = NEONMapPrefixD(_VD, dsize, MAP_DIRTY | (regs.overlap ? 0 : MAP_NOINIT));
	return regs;
}

// Requires quad registers.
void ArmJit::NEONTranspose4x4(ARMReg cols[4]) {
	// 0123   _\  0426
	// 4567    /  1537
	VTRN(F_32, cols[0], cols[1]);   

	// 89ab   _\  8cae
	// cdef    /  9dbf
	VTRN(F_32, cols[2], cols[3]);

	//  04[26]       048c
	//  15 37   ->    1537
	// [8c]ae       26ae
	//  9d bf         9dbf
	VSWP(D_1(cols[0]), D_0(cols[2]));

	//  04 8c       048c
	//  15[37]   ->  159d
	//  26 ae       26ae
	// [9d]bf       37bf
	VSWP(D_1(cols[1]), D_0(cols[3]));
}

}  // namespace MIPSComp

#endif // PPSSPP_ARCH(ARM)
