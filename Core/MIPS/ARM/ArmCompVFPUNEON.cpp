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



#include <cmath>

#include "base/logging.h"
#include "math/math_util.h"

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

// TODO: Somehow #ifdef away on ARMv5eabi, without breaking the linker.

#define CONDITIONAL_DISABLE ;
#define DISABLE { fpr.ReleaseSpillLocksAndDiscardTemps(); Comp_Generic(op); return; }

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

static const float minus_one = -1.0f;
static const float one = 1.0f;
static const float zero = 0.0f;


// On NEON, we map triples to Q registers and singles to D registers.
// Sometimes, as when doing dot products, it matters what's in that unused reg. This zeroes it.
void Jit::NEONMaskToSize(ARMReg vs, VectorSize sz) {
	// TODO
}


ARMReg Jit::NEONMapPrefixST(int mipsReg, VectorSize sz, u32 prefix, int mapFlags) {
	static const float constantArray[8] = {0.f, 1.f, 2.f, 0.5f, 3.f, 1.f/3.f, 0.25f, 1.f/6.f};
	static const float constantArrayNegated[8] = {-0.f, -1.f, -2.f, -0.5f, -3.f, -1.f/3.f, -0.25f, -1.f/6.f};

	// Applying prefixes in SIMD fashion will actually be a lot easier than the old style.
	if (prefix == 0xE4) {
		return fpr.QMapReg(mipsReg, sz, mapFlags);
	}
	int n = GetNumVectorElements(sz);

	int regnum[4];
	int abs[4];
	int negate[4];
	int constants[4];

	int abs_mask = (prefix >> 8) & 0xF;
	int negate_mask = (prefix >> 16) & 0xF;
	int constants_mask = (prefix >> 12) & 0xF;

	int full_mask = (1 << n) - 1;

	// Decode prefix to keep the rest readable
	int permuteMask = 0;
	for (int i = 0; i < n; i++) {
		permuteMask |= 3 << (i * 2);
		regnum[i] = (prefix >> (i*2)) & 3;
		abs[i]    = (prefix >> (8+i)) & 1;
		negate[i] = (prefix >> (16+i)) & 1;
		constants[i] = (prefix >> (12+i)) & 1;
	}

	bool anyPermute = (prefix & permuteMask) == (0xE4 & permuteMask);
	
	if (constants_mask == full_mask) {
		// It's all constants! Don't even bother mapping the input register,
		// just allocate a temp one.
		// If a single, this can sometimes be done cheaper. But meh.
		ARMReg ar = fpr.QAllocTemp();
		for (int i = 0; i < n; i++) {
			int constNum = regnum[i] + (abs[i] << 2);
			MOVP2R(R0, (negate[i] ? constantArrayNegated : constantArray) + constNum);
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
	ARMReg ar = fpr.QAllocTemp();
	if (!anyPermute) {
		VMOV(ar, inputAR);
		// No permutations!
	} else {
		bool allSame = true;
		for (int i = 1; i < n; i++) {
			if (regnum[0] == regnum[i])
				allSame = false;
		}
		if (allSame) {
			// Easy, someone is duplicating one value onto all the reg parts.
			// If this is happening and QMapReg must load, we can combine these two actions
			// into a VLD1_lane. TODO
			VDUP(F_32, ar, inputAR, regnum[0]);
		} else {
			// Can check for VSWP match?

			// TODO: Cannot do this permutation yet!
		}
	}

	// ABS
	// Two methods: If all lanes are "absoluted", it's easy.
	if (abs_mask == full_mask) {
		// TODO: elide the above VMOV when possible
		VABS(F_32, ar, ar);
	} else {
		// Partial ABS! TODO
	}
	
	if (negate_mask == full_mask) {
		// TODO: elide the above VMOV when possible
		VNEG(F_32, ar, ar);
	} else {
		// Partial negate! I guess we build sign bits in another register
		// and simply XOR.
	}

	// Insert constants where requested, and check negate!
	for (int i = 0; i < n; i++) {
		if (constants[i]) {
			int constNum = regnum[i] + (abs[i] << 2);
			MOVP2R(R0, (negate[i] ? constantArrayNegated : constantArray) + constNum);
			VLD1_lane(F_32, ar, R0, i, true);
		}
	}

	return ar;
}

inline ARMReg MatchSize(ARMReg x, ARMReg target) {
	// TODO
	return x;
}

Jit::DestARMReg Jit::NEONMapPrefixD(int vreg, VectorSize sz, int mapFlags) {
	// Inverted from the actual bits, easier to reason about 1 == write
	int writeMask = (~(js.prefixD >> 8)) & 0xF;

	DestARMReg dest;
	dest.sz = sz;
	if (writeMask == 0xF) {
		// No need to apply a write mask.
		// Let's not make things complicated.
		dest.rd = fpr.QMapReg(vreg, sz, mapFlags);
		dest.backingRd = dest.rd;
	} else {
		// Allocate a temporary register.
		dest.rd = fpr.QAllocTemp();
		dest.backingRd = fpr.QMapReg(vreg, sz, mapFlags & ~MAP_NOINIT);  // Force initialization of the backing reg.
	}
	return dest;
}

void Jit::NEONApplyPrefixD(DestARMReg dest) {
	// Apply clamps to dest.rd
	int n = GetNumVectorElements(dest.sz);

	int sat1_mask = 0;
	int sat3_mask = 0;
	int full_mask = 0;
	for (int i = 0; i < n; i++) {
		int sat = (js.prefixD >> (i * 2)) & 3;
		if (sat == 1)
			sat1_mask |= i << 1;
		if (sat == 3)
			sat3_mask |= i << 1;
		full_mask |= i << 1;
	}

	if (sat1_mask && sat3_mask) {
		// Why would anyone do this?
		ELOG("Can't have both sat[0-1] and sat[-1-1] at the same time yet");
	}

	if (sat1_mask) {
		if (sat1_mask != full_mask) {
			ELOG("Can't have partial sat1 mask yet (%i vs %i)", sat1_mask, full_mask);
		}
		ARMReg temp = MatchSize(Q0, dest.rd);
		VMOV_immf(temp, 1.0);
		VMIN(F_32, dest.rd, dest.rd, temp);
		VMOV_immf(temp, 0.0);
		VMAX(F_32, dest.rd, dest.rd, temp);
	}

	if (sat3_mask && sat1_mask != full_mask) {
		if (sat3_mask != full_mask) {
			ELOG("Can't have partial sat3 mask yet (%i vs %i)", sat3_mask, full_mask);
		}
		ARMReg temp = MatchSize(Q0, dest.rd);
		VMOV_immf(temp, 1.0f);
		VMIN(F_32, dest.rd, dest.rd, temp);
		VMOV_immf(temp, -1.0f);
		VMAX(F_32, dest.rd, dest.rd, temp);
	}

	// Check for actual mask operation (unrelated to the "masks" above).
	if (dest.backingRd != dest.rd) {
		// This means that we need to apply the write mask, from rd to backingRd.
		// What a pain. We can at least shortcut easy cases like half the register.
		// And we can generate the masks easily with some of the crazy vector imm modes. (bits2bytes for example).
		// So no need to load them from RAM.

		// TODO
		VMOV(dest.backingRd, dest.rd);
	}
}

Jit::MappedRegs Jit::NEONMapDirtyInIn(MIPSOpcode op, VectorSize dsize, VectorSize ssize, VectorSize tsize, bool applyPrefixes) {
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

Jit::MappedRegs Jit::NEONMapDirtyIn(MIPSOpcode op, VectorSize dsize, VectorSize ssize) {
	MappedRegs regs;
	regs.vs = NEONMapPrefixS(_VS, ssize, 0);

	regs.overlap = GetVectorOverlap(_VD, dsize, _VS, ssize) > 0;
	regs.vd = NEONMapPrefixD(_VD, dsize, MAP_DIRTY | (regs.overlap ? 0 : MAP_NOINIT));
	return regs;
}

void Jit::CompNEON_VecDo3(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	MappedRegs r = NEONMapDirtyInIn(op, sz, sz, sz);
	ARMReg temp = MatchSize(Q0, r.vs);
	// TODO: Special case for scalar
	switch (op >> 26) {
	case 24: //VFPU0
		switch ((op >> 23) & 7) {
		case 0: VADD(F_32, r.vd, r.vs, r.vt); break; // vadd
		case 1: VSUB(F_32, r.vd, r.vs, r.vt); break; // vsub
		case 7: DISABLE; /* VDIV(F_32, vd, vs, vt); */  break; // vdiv  THERE IS NO NEON SIMD VDIV :(  There's a fast reciprocal iterator thing though.
		default:
			DISABLE;
		}
		break;
	case 25: //VFPU1
		switch ((op >> 23) & 7) {
		case 0: VMUL(F_32, r.vd, r.vs, r.vt); break;  // vmul
		default:
			DISABLE;
		}
		break;
	case 27: //VFPU3
		switch ((op >> 23) & 7)	{
		case 2: VMIN(F_32, r.vd, r.vs, r.vt); break;   // vmin
		case 3: VMAX(F_32, r.vd, r.vs, r.vt); break;   // vmax
		case 6:  // vsge
			VMOV_immf(temp, 1.0f);
			VCGE(F_32, r.vd, r.vs, r.vt);
			VAND(r.vd, r.vd, temp);
			break;
		case 7:  // vslt
			VMOV_immf(temp, 1.0f);
			VCLT(F_32, r.vd, r.vs, r.vt);
			VAND(r.vd, r.vd, temp);
			break;
		}
		break;

	default:
		DISABLE;
	}

	NEONApplyPrefixD(r.vd);

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void Jit::CompNEON_SV(MIPSOpcode op) {
	DISABLE;

	// Remember to use single lane stores here and not VLDR/VSTR - switching usage
	// between NEON and VFPU can be expensive on some chips.
}

#define MIPS_GET_VQVT(op) (((op >> 16) & 0x1f)) | ((op&1) << 5)

void Jit::CompNEON_SVQ(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	int offset = (signed short)(op&0xFFFC);
	int vt = MIPS_GET_VQVT(op);
	MIPSGPReg rs = _RS;
	bool doCheck = false;
	switch (op >> 26)
	{
	case 54: //lv.q
		{
			// Check for four-in-a-row
			u32 ops[4] = {op.encoding, 
				Memory::Read_Instruction(js.compilerPC + 4).encoding,
				Memory::Read_Instruction(js.compilerPC + 8).encoding,
				Memory::Read_Instruction(js.compilerPC + 12).encoding
			};
			if (g_Config.bFastMemory && (ops[1] >> 26) == 54 && (ops[2] >> 26) == 54 && (ops[3] >> 26) == 54) {
				int offsets[4] = {offset, (s16)(ops[1] & 0xFFFC), (s16)(ops[2] & 0xFFFC), (s16)(ops[3] & 0xFFFC)};
				int rss[4] = {MIPS_GET_RS(op), MIPS_GET_RS(ops[1]), MIPS_GET_RS(ops[2]), MIPS_GET_RS(ops[3])};
				if (offsets[1] == offset + 16 && offsets[2] == offsets[1] + 16 && offsets[3] == offsets[2] + 16 &&
					  rss[0] == rss[1] && rss[1] == rss[2] && rss[2] == rss[3]) {
					int vts[4] = {MIPS_GET_VQVT(op), MIPS_GET_VQVT(ops[1]), MIPS_GET_VQVT(ops[2]), MIPS_GET_VQVT(ops[3])};
					// Detected four consecutive ones!
					// gpr.MapRegAsPointer(rs);
					// fpr.QLoad4x4(vts[4], rs, offset);
					INFO_LOG(JIT, "Matrix load detected! TODO: optimize");
					// break;
				}
			}

			if (!gpr.IsImm(rs) && jo.cachePointers && g_Config.bFastMemory && offset < 0x400-16 && offset > -0x400-16) {
				gpr.MapRegAsPointer(rs);
				ARMReg ar = fpr.QMapReg(vt, V_Quad, MAP_DIRTY | MAP_NOINIT);
				if (offset) {
					ADDI2R(R0, gpr.RPtr(rs), offset, R1);
					VLD1(F_32, ar, R0, 2, ALIGN_128);
				} else {
					VLD1(F_32, ar, gpr.RPtr(rs), 2, ALIGN_128);
				}
				break;
			}

			// CC might be set by slow path below, so load regs first.
			ARMReg ar = fpr.QMapReg(vt, V_Quad, MAP_DIRTY | MAP_NOINIT);
			if (gpr.IsImm(rs)) {
				u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
				gpr.SetRegImm(R0, addr + (u32)Memory::base);
			} else {
				gpr.MapReg(rs);
				if (g_Config.bFastMemory) {
					SetR0ToEffectiveAddress(rs, offset);
				} else {
					SetCCAndR0ForSafeAddress(rs, offset, R1);
					doCheck = true;
				}
				ADD(R0, R0, MEMBASEREG);
			}

			FixupBranch skip;
			if (doCheck) {
				skip = B_CC(CC_EQ);
			}

			VLD1(F_32, ar, R0, 2, ALIGN_128);

			if (doCheck) {
				SetJumpTarget(skip);
				SetCC(CC_AL);
			}
		}
		break;

	case 62: //sv.q
		{
			// TODO: Check next few instructions for more than one sv.q. We can optimize
			// full matrix stores quite a bit, I think.
			if (!gpr.IsImm(rs) && jo.cachePointers && g_Config.bFastMemory && offset < 0x400-16 && offset > -0x400-16) {
				gpr.MapRegAsPointer(rs);
				ARMReg ar = fpr.QMapReg(vt, V_Quad, 0);
				if (offset) {
					ADDI2R(R0, gpr.RPtr(rs), offset, R1);
					VST1(F_32, ar, R0, 2, ALIGN_128);
				} else {
					VST1(F_32, ar, gpr.RPtr(rs), 2, ALIGN_128);
				}
				break;
			}

			// CC might be set by slow path below, so load regs first.
			u8 vregs[4];
			ARMReg ar = fpr.QMapReg(vt, V_Quad, 0);

			if (gpr.IsImm(rs)) {
				u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
				gpr.SetRegImm(R0, addr + (u32)Memory::base);
			} else {
				gpr.MapReg(rs);
				if (g_Config.bFastMemory) {
					SetR0ToEffectiveAddress(rs, offset);
				} else {
					SetCCAndR0ForSafeAddress(rs, offset, R1);
					doCheck = true;
				}
				ADD(R0, R0, MEMBASEREG);
			}

			FixupBranch skip;
			if (doCheck) {
				skip = B_CC(CC_EQ);
			}

			VST1(F_32, ar, R0, 2, ALIGN_128);

			if (doCheck) {
				SetJumpTarget(skip);
				SetCC(CC_AL);
			}
		}
		break;

	default:
		DISABLE;
		break;
	}
	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void Jit::CompNEON_VVectorInit(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	// WARNING: No prefix support!
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}
	VectorSize sz = GetVecSize(op);
	DestARMReg vd = NEONMapPrefixD(_VD, sz, MAP_NOINIT | MAP_DIRTY);

	switch ((op >> 16) & 0xF) {
	case 6:  // vzero
		VEOR(vd, vd, vd);
		break;
	case 7:  // vone
		VMOV_immf(vd, 1.0f);
		break;
	default:
		DISABLE;
		break;
	}
	NEONApplyPrefixD(vd);
	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void Jit::CompNEON_VMatrixInit(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VDot(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}

	VectorSize sz = GetVecSize(op);
	MappedRegs r = NEONMapDirtyInIn(op, V_Single, sz, sz);

	switch (sz) {
	case V_Pair:
		VMUL(F_32, r.vd, r.vs, r.vt);
		VPADD(F_32, r.vd, r.vd, r.vd);
		break;
	case V_Triple:
		VMUL(F_32, Q0, r.vs, r.vt);
		VPADD(F_32, D0, D0, D0);
		VADD(F_32, D0, D0, D1);
		VMOV(r.vd, Q0);  // this will copy junk too, we really only care about the bottom reg.
		break;
	case V_Quad:
		// TODO: Alternate solution: VMUL d, VMLA, PADD
		VMUL(F_32, Q0, r.vs, r.vt);
		VPADD(F_32, D0, D0, D0);
		VPADD(F_32, D1, D1, D1);
		VADD(F_32, D0, D0, D1);
		VMOV(r.vd, MatchSize(Q0, r.vd));  // this will copy junk too, we really only care about the bottom reg.
		break;
	}

	NEONApplyPrefixD(r.vd);
}

void Jit::CompNEON_VScl(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}

	VectorSize sz = GetVecSize(op);
	MappedRegs r = NEONMapDirtyInIn(op, sz, sz, V_Single);

	ARMReg temp = MatchSize(Q0, r.vt);

	// TODO: VMUL_scalar directly when possible
	VMOV(temp, r.vt);
	VMUL_scalar(F_32, r.vd, r.vs, DScalar(Q0, 0));

	NEONApplyPrefixD(r.vd);
}

void Jit::CompNEON_VV2Op(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}

	// Pre-processing: Eliminate silly no-op VMOVs, common in Wipeout Pure
	if (((op >> 16) & 0x1f) == 0 && _VS == _VD && js.HasNoPrefix()) {
		return;
	}
	
	// Must bail before we start mapping registers.
	switch ((op >> 16) & 0x1f) {
	case 0: // d[i] = s[i]; break; //vmov
	case 1: // d[i] = fabsf(s[i]); break; //vabs
	case 2: // d[i] = -s[i]; break; //vneg
	case 17: // d[i] = 1.0f / sqrtf(s[i]); break; //vrsq
		break;

	default:
		DISABLE;
		break;
	}


	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	MappedRegs r = NEONMapDirtyIn(op, sz, sz);

	ARMReg temp = MatchSize(Q0, r.vs);

	switch ((op >> 16) & 0x1f) {
	case 0: // d[i] = s[i]; break; //vmov
		// Probably for swizzle.
		VMOV(r.vd, r.vs);
		break;
	case 1: // d[i] = fabsf(s[i]); break; //vabs
		VABS(F_32, r.vd, r.vs);
		break;
	case 2: // d[i] = -s[i]; break; //vneg
		VNEG(F_32, r.vd, r.vs);
		break;

	case 4: // if (s[i] < 0) d[i] = 0; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;    // vsat0
		DISABLE;
		break;
	case 5: // if (s[i] < -1.0f) d[i] = -1.0f; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;  // vsat1
		DISABLE;
		break;

	case 16: // d[i] = 1.0f / s[i]; break; //vrcp
		DISABLE;
		{
			ARMReg temp2 = MatchSize(fpr.QAllocTemp(), r.vs);
			// Needs iterations on NEON. And two temps - which is a problem if vs == vd! Argh!
			VRECPE(F_32, temp, r.vs);
			VRECPS(temp2, r.vs, temp);
			VMUL(F_32, temp2, temp2, temp);
			VRECPS(temp2, r.vs, temp);
			VMUL(F_32, temp2, temp2, temp);
		}
		// http://stackoverflow.com/questions/6759897/how-to-divide-in-neon-intrinsics-by-a-float-number
		// reciprocal = vrecpeq_f32(b);
		// reciprocal = vmulq_f32(vrecpsq_f32(b, reciprocal), reciprocal);
		// reciprocal = vmulq_f32(vrecpsq_f32(b, reciprocal), reciprocal);
		DISABLE;
		break;

	case 17: // d[i] = 1.0f / sqrtf(s[i]); break; //vrsq
		// Needs iterations on NEON
		{
			if (true) {
				// Not-very-accurate estimate
				VRSQRTE(F_32, r.vd, r.vs);
			} else {
				ARMReg temp2 = MatchSize(fpr.QAllocTemp(), r.vs);
				// TODO: It's likely that some games will require one or two Newton-Raphson
				// iterations to refine the estimate.
				VRSQRTE(F_32, temp, r.vs);
				VRSQRTS(temp2, r.vs, temp);
				VMUL(F_32, r.vd, temp2, temp);
				//VRSQRTS(temp2, r.vs, temp);
				// VMUL(F_32, r.vd, temp2, temp);
			}
		}
		break;
	case 18: // d[i] = sinf((float)M_PI_2 * s[i]); break; //vsin
		DISABLE;
		break;
	case 19: // d[i] = cosf((float)M_PI_2 * s[i]); break; //vcos
		DISABLE;
		break;
	case 20: // d[i] = powf(2.0f, s[i]); break; //vexp2
		DISABLE;
		break;
	case 21: // d[i] = logf(s[i])/log(2.0f); break; //vlog2
		DISABLE;
		break;
	case 22: // d[i] = sqrtf(s[i]); break; //vsqrt
		// Needs iterations on NEON
		DISABLE;
		break;
	case 23: // d[i] = asinf(s[i] * (float)M_2_PI); break; //vasin
		DISABLE;
		break;
	case 24: // d[i] = -1.0f / s[i]; break; // vnrcp
		// Needs iterations on NEON
		DISABLE;
		break;
	case 26: // d[i] = -sinf((float)M_PI_2 * s[i]); break; // vnsin
		DISABLE;
		break;
	case 28: // d[i] = 1.0f / expf(s[i] * (float)M_LOG2E); break; // vrexp2
		DISABLE;
		break;
	default:
		DISABLE;
		break;
	}

	NEONApplyPrefixD(r.vd);

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void Jit::CompNEON_Mftv(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	int imm = op & 0xFF;
	MIPSGPReg rt = _RT;
	switch ((op >> 21) & 0x1f) {
	case 3: //mfv / mfvc
		// rt = 0, imm = 255 appears to be used as a CPU interlock by some games.
		if (rt != 0) {
			if (imm < 128) {  //R(rt) = VI(imm);
				ARMReg r = fpr.QMapReg(imm, V_Single, MAP_READ);
				gpr.MapReg(rt, MAP_NOINIT | MAP_DIRTY);
				// TODO: Gotta be a faster way
				VMOV(MatchSize(Q0, r), r);
				VMOV(gpr.R(rt), S0);
			} else if (imm < 128 + VFPU_CTRL_MAX) { //mtvc
				// In case we have a saved prefix.
				FlushPrefixV();
				if (imm - 128 == VFPU_CTRL_CC) {
					gpr.MapDirtyIn(rt, MIPS_REG_VFPUCC);
					MOV(gpr.R(rt), gpr.R(MIPS_REG_VFPUCC));
				} else {
					gpr.MapReg(rt, MAP_NOINIT | MAP_DIRTY);
					LDR(gpr.R(rt), CTXREG, offsetof(MIPSState, vfpuCtrl) + 4 * (imm - 128));
				}
			} else {
				//ERROR - maybe need to make this value too an "interlock" value?
				ERROR_LOG(CPU, "mfv - invalid register %i", imm);
			}
		}
		break;

	case 7: // mtv
		if (imm < 128) {
			ARMReg r = fpr.QMapReg(imm, V_Single, MAP_DIRTY | MAP_NOINIT);
			if (gpr.IsMapped(rt)) {
				VMOV(S0, gpr.R(rt));
				VMOV(r, MatchSize(Q0, r));
			} else {
				ADDI2R(R0, CTXREG, gpr.GetMipsRegOffset(rt), R1);
				VLD1_lane(F_32, r, R0, 0, true);
			}
		} else if (imm < 128 + VFPU_CTRL_MAX) { //mtvc //currentMIPS->vfpuCtrl[imm - 128] = R(rt);
			if (imm - 128 == VFPU_CTRL_CC) {
				gpr.MapDirtyIn(MIPS_REG_VFPUCC, rt);
				MOV(gpr.R(MIPS_REG_VFPUCC), rt);
			} else {
				gpr.MapReg(rt);
				STR(gpr.R(rt), CTXREG, offsetof(MIPSState, vfpuCtrl) + 4 * (imm - 128));
			}
			//gpr.BindToRegister(rt, true, false);
			//MOV(32, M(&currentMIPS->vfpuCtrl[imm - 128]), gpr.R(rt));

			// TODO: Optimization if rt is Imm?
			// Set these BEFORE disable!
			if (imm - 128 == VFPU_CTRL_SPREFIX) {
				js.prefixSFlag = JitState::PREFIX_UNKNOWN;
			} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
				js.prefixTFlag = JitState::PREFIX_UNKNOWN;
			} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
				js.prefixDFlag = JitState::PREFIX_UNKNOWN;
			}
		} else {
			//ERROR
			_dbg_assert_msg_(CPU,0,"mtv - invalid register");
		}
		break;

	default:
		DISABLE;
	}

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void Jit::CompNEON_Vmtvc(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	int vs = _VS;
	int imm = op & 0xFF;
	if (imm >= 128 && imm < 128 + VFPU_CTRL_MAX) {
		ARMReg r = fpr.QMapReg(vs, V_Single, 0);
		ADDI2R(R0, CTXREG, offsetof(MIPSState, vfpuCtrl[0]) + (imm - 128) * 4, R1);
		VST1_lane(F_32, r, R0, 0, true);
		fpr.ReleaseSpillLocksAndDiscardTemps();

		if (imm - 128 == VFPU_CTRL_SPREFIX) {
			js.prefixSFlag = JitState::PREFIX_UNKNOWN;
		} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
			js.prefixTFlag = JitState::PREFIX_UNKNOWN;
		} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
			js.prefixDFlag = JitState::PREFIX_UNKNOWN;
		}
	}
}

void Jit::CompNEON_Vmmov(MIPSOpcode op) {
	DISABLE;

	if (_VS == _VD) {
		// A lot of these no-op matrix moves in Wipeout... Just drop the instruction entirely.
		return;
	}

	// Do we really need to map it all and do VMOVs or can we do more clever things in the regalloc?
}

void Jit::CompNEON_Vmmul(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vmscl(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vtfm(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VHdp(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VCrs(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VDet(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vi2x(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vx2i(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vf2i(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vi2f(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vh2f(MIPSOpcode op) {
	DISABLE;

	if (!cpu_info.bHalf) {
		// No hardware support for half-to-float, fallback to interpreter
		// TODO: Translate the fast SSE solution to standard integer/VFP stuff
		// for the weaker CPUs.
		DISABLE;
	}

	VectorSize sz = GetVecSize(op);

	ARMReg vs = NEONMapPrefixS(_VS, sz, 0);
	DestARMReg vd = NEONMapPrefixD(_VD, sz, MAP_DIRTY | (vd == vs ? 0 : MAP_NOINIT));

}

void Jit::CompNEON_Vcst(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}

	int conNum = (op >> 16) & 0x1f;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);
	DestARMReg vd = NEONMapPrefixD(_VD, sz, MAP_DIRTY | MAP_NOINIT);
	gpr.SetRegImm(R0, (u32)(void *)&cst_constants[conNum]);
	VLD1_all_lanes(F_32, vd, R0, true);
	NEONApplyPrefixD(vd);  // TODO: Could bake this into the constant we load.

	fpr.ReleaseSpillLocksAndDiscardTemps();
}


void Jit::CompNEON_Vhoriz(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VRot(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VIdt(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}

	VectorSize sz = GetVecSize(op);
	DestARMReg vd = NEONMapPrefixD(_VD, sz, MAP_NOINIT | MAP_DIRTY);
	switch (sz) {
	case V_Pair:
		VMOV_immf(vd, 1.0f);
		if ((_VD & 1) == 0) {
			// Load with 1.0, 0.0
			VMOV_imm(I_64, D0, VIMMbits2bytes, 0x0F);
			VAND(vd, vd, D0);
		} else {
			VMOV_imm(I_64, D0, VIMMbits2bytes, 0xF0);
			VAND(vd, vd, D0);
		}
		break;
	case V_Quad:
		{
			VEOR(vd, vd, vd);
			ARMReg dest = (_VD & 2) ? D_1(vd) : D_0(vd);
			VMOV_immf(dest, 1.0f);
			if ((_VD & 1) == 0) {
				// Load with 1.0, 0.0
				VMOV_imm(I_64, D0, VIMMbits2bytes, 0x0F);
				VAND(dest, dest, D0);
			} else {
				VMOV_imm(I_64, D0, VIMMbits2bytes, 0xF0);
				VAND(dest, dest, D0);
			}
		}
		break;
	default:
		_dbg_assert_msg_(CPU,0,"Bad vidt instruction");
		break;
	}

	NEONApplyPrefixD(vd);
	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void Jit::CompNEON_Vcmp(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vcmov(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Viim(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vfim(MIPSOpcode op) {
	DISABLE;
}

// https://code.google.com/p/bullet/source/browse/branches/PhysicsEffects/include/vecmath/neon/vectormath_neon_assembly_implementations.S?r=2488
void Jit::CompNEON_VCrossQuat(MIPSOpcode op) {
	// This op does not support prefixes anyway.
	CONDITIONAL_DISABLE;
	if (js.HasUnknownPrefix())
		DISABLE;

	VectorSize sz = GetVecSize(op);
	if (sz != V_Triple) {
		// Quaternion product. Bleh.
		DISABLE;
	}

	MappedRegs r = NEONMapDirtyInIn(op, sz, sz, sz, false);

	ARMReg t1 = Q0;
	ARMReg t2 = fpr.QAllocTemp();
	
	// There has to be a faster way to do this. This is not really any better than
	// scalar.

	// d18, d19 (q9) = t1 = r.vt
	// d16, d17 (q8) = t2 = r.vs
	// d20, d21 (q10) = t
	VMOV(t1, r.vs);
	VMOV(t2, r.vt);
	VTRN(F_32, D_0(t2), D_1(t2));    //	vtrn.32 d18,d19			@  q9 = <x2,z2,y2,w2> = d18,d19
	VREV64(F_32, D_0(t1), D_0(t1));  // vrev64.32 d16,d16		@  q8 = <y1,x1,z1,w1> = d16,d17
	VREV64(F_32, D_0(t2), D_0(t2));   // vrev64.32 d18,d18		@  q9 = <z2,x2,y2,w2> = d18,d19
	VTRN(F_32, D_0(t1), D_1(t1));    // vtrn.32 d16,d17			@  q8 = <y1,z1,x1,w1> = d16,d17
	// perform first half of cross product using rearranged inputs
	VMUL(F_32, r.vd, t1, t2);           // vmul.f32 q10, q8, q9	@ q10 = <y1*z2,z1*x2,x1*y2,w1*w2>
	// @ rearrange inputs again
	VTRN(F_32, D_0(t2), D_1(t2));   // vtrn.32 d18,d19			@  q9 = <z2,y2,x2,w2> = d18,d19
	VREV64(F_32, D_0(t1), D_0(t1));  // vrev64.32 d16,d16		@  q8 = <z1,y1,x1,w1> = d16,d17
	VREV64(F_32, D_0(t2), D_0(t2));  // vrev64.32 d18,d18		@  q9 = <y2,z2,x2,w2> = d18,d19
	VTRN(F_32, D_0(t1), D_1(t1));   // vtrn.32 d16,d17			@  q8 = <z1,x1,y1,w1> = d16,d17
	// @ perform last half of cross product using rearranged inputs
	VMLS(F_32, r.vd, t1, t2);   // vmls.f32 q10, q8, q9	@ q10 = <y1*z2-y2*z1,z1*x2-z2*x1,x1*y2-x2*y1,w1*w2-w2*w1>

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void Jit::CompNEON_Vsgn(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vocp(MIPSOpcode op) {
	DISABLE;
}

}
// namespace MIPSComp