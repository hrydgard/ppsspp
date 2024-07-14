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

#include "Common/Data/Convert/SmallDataConvert.h"
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
#include "Core/MIPS/ARM/ArmRegCacheFPU.h"
#include "Core/MIPS/ARM/ArmCompVFPUNEONUtil.h"

// TODO: Somehow #ifdef away on ARMv5eabi, without breaking the linker.

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE { fpr.ReleaseSpillLocksAndDiscardTemps(); Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE(flag) if (jo.Disabled(JitDisable::flag)) { Comp_Generic(op); return; }
#define DISABLE { fpr.ReleaseSpillLocksAndDiscardTemps(); Comp_Generic(op); return; }
#define DISABLE_UNKNOWN_PREFIX { WARN_LOG(Log::JIT, "DISABLE: Unknown Prefix in %s", __FUNCTION__); fpr.ReleaseSpillLocksAndDiscardTemps(); Comp_Generic(op); return; }

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


void ArmJit::CompNEON_VecDo3(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_VEC);
	if (js.HasUnknownPrefix()) {
		DISABLE_UNKNOWN_PREFIX;
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
		case 7: // vdiv  // vdiv  THERE IS NO NEON SIMD VDIV :(  There's a fast reciprocal iterator thing though.
			{
				// Implement by falling back to VFP
				VMOV(D0, D_0(r.vs));
				VMOV(D1, D_0(r.vt));
				VDIV(S0, S0, S2);
				if (sz >= V_Pair)
					VDIV(S1, S1, S3);
				VMOV(D_0(r.vd), D0);
				if (sz >= V_Triple) {
					VMOV(D0, D_1(r.vs));
					VMOV(D1, D_1(r.vt));
					VDIV(S0, S0, S2);
					if (sz == V_Quad)
						VDIV(S1, S1, S3);
					VMOV(D_1(r.vd), D0);
				}
			}
			break; 
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


// #define CONDITIONAL_DISABLE { fpr.ReleaseSpillLocksAndDiscardTemps(); Comp_Generic(op); return; }

void ArmJit::CompNEON_SV(MIPSOpcode op) {
	CONDITIONAL_DISABLE(LSU_VFPU);
	CheckMemoryBreakpoint();
	
	// Remember to use single lane stores here and not VLDR/VSTR - switching usage
	// between NEON and VFPU can be expensive on some chips.

	// Here's a common idiom we should optimize:
	// lv.s S200, 0(s4)
	// lv.s S201, 4(s4)
	// lv.s S202, 8(s4)
	// vone.s S203
	// vtfm4.q C000, E600, C200
	// Would be great if we could somehow combine the lv.s into one vector instead of mapping three
	// separate quads.

	s32 offset = (signed short)(op & 0xFFFC);
	int vt = ((op >> 16) & 0x1f) | ((op & 3) << 5);
	MIPSGPReg rs = _RS;

	bool doCheck = false;
	switch (op >> 26)
	{
	case 50: //lv.s  // VI(vt) = Memory::Read_U32(addr);
		{
			if (!gpr.IsImm(rs) && jo.cachePointers && g_Config.bFastMemory && (offset & 3) == 0 && offset < 0x400 && offset > -0x400) {
				INFO_LOG(Log::HLE, "LV.S fastmode!");
				// TODO: Also look forward and combine multiple loads.
				gpr.MapRegAsPointer(rs);
				ARMReg ar = fpr.QMapReg(vt, V_Single, MAP_NOINIT | MAP_DIRTY);
				if (offset) {
					ADDI2R(R0, gpr.RPtr(rs), offset, R1);
					VLD1_lane(F_32, ar, R0, 0, true);
				} else {
					VLD1_lane(F_32, ar, gpr.RPtr(rs), 0, true);
				}
				break;
			}
			INFO_LOG(Log::HLE, "LV.S slowmode!");

			// CC might be set by slow path below, so load regs first.
			ARMReg ar = fpr.QMapReg(vt, V_Single, MAP_DIRTY | MAP_NOINIT);
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
			VLD1_lane(F_32, ar, R0, 0, true);
			if (doCheck) {
				SetJumpTarget(skip);
				SetCC(CC_AL);
			}
		}
		break;

	case 58: //sv.s   // Memory::Write_U32(VI(vt), addr);
		{
			if (!gpr.IsImm(rs) && jo.cachePointers && g_Config.bFastMemory && (offset & 3) == 0 && offset < 0x400 && offset > -0x400) {
				INFO_LOG(Log::HLE, "SV.S fastmode!");
				// TODO: Also look forward and combine multiple stores.
				gpr.MapRegAsPointer(rs);
				ARMReg ar = fpr.QMapReg(vt, V_Single, 0);
				if (offset) {
					ADDI2R(R0, gpr.RPtr(rs), offset, R1);
					VST1_lane(F_32, ar, R0, 0, true);
				} else {
					VST1_lane(F_32, ar, gpr.RPtr(rs), 0, true);
				}
				break;
			}

			INFO_LOG(Log::HLE, "SV.S slowmode!");
			// CC might be set by slow path below, so load regs first.
			ARMReg ar = fpr.QMapReg(vt, V_Single, 0);
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
			VST1_lane(F_32, ar, R0, 0, true);
			if (doCheck) {
				SetJumpTarget(skip);
				SetCC(CC_AL);
			}
		}
		break;
	}
	fpr.ReleaseSpillLocksAndDiscardTemps();
}

inline int MIPS_GET_VQVT(u32 op) {
	return (((op >> 16) & 0x1f)) | ((op & 1) << 5);
}

void ArmJit::CompNEON_SVQ(MIPSOpcode op) {
	CONDITIONAL_DISABLE(LSU_VFPU);
	CheckMemoryBreakpoint();

	int offset = (signed short)(op & 0xFFFC);
	int vt = MIPS_GET_VQVT(op.encoding);
	MIPSGPReg rs = _RS;
	bool doCheck = false;
	switch (op >> 26)
	{
	case 54: //lv.q
		{
			// Check for four-in-a-row
			const u32 ops[4] = {
				op.encoding, 
				GetOffsetInstruction(1).encoding,
				GetOffsetInstruction(2).encoding,
				GetOffsetInstruction(3).encoding,
			};
			if (g_Config.bFastMemory && (ops[1] >> 26) == 54 && (ops[2] >> 26) == 54 && (ops[3] >> 26) == 54) {
				int offsets[4] = {offset, (s16)(ops[1] & 0xFFFC), (s16)(ops[2] & 0xFFFC), (s16)(ops[3] & 0xFFFC)};
				int rss[4] = {MIPS_GET_RS(op), MIPS_GET_RS(ops[1]), MIPS_GET_RS(ops[2]), MIPS_GET_RS(ops[3])};
				if (offsets[1] == offset + 16 && offsets[2] == offsets[1] + 16 && offsets[3] == offsets[2] + 16 &&
					  rss[0] == rss[1] && rss[1] == rss[2] && rss[2] == rss[3]) {
					int vts[4] = {MIPS_GET_VQVT(op.encoding), MIPS_GET_VQVT(ops[1]), MIPS_GET_VQVT(ops[2]), MIPS_GET_VQVT(ops[3])};
					// TODO: Also check the destination registers!
					// Detected four consecutive ones!
					// gpr.MapRegAsPointer(rs);
					// fpr.QLoad4x4(vts[4], rs, offset);
					INFO_LOG(Log::JIT, "Matrix load detected! TODO: optimize");
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
			const u32 ops[4] = {
				op.encoding,
				GetOffsetInstruction(1).encoding,
				GetOffsetInstruction(2).encoding,
				GetOffsetInstruction(3).encoding,
			};
			if (g_Config.bFastMemory && (ops[1] >> 26) == 54 && (ops[2] >> 26) == 54 && (ops[3] >> 26) == 54) {
				int offsets[4] = { offset, (s16)(ops[1] & 0xFFFC), (s16)(ops[2] & 0xFFFC), (s16)(ops[3] & 0xFFFC) };
				int rss[4] = { MIPS_GET_RS(op), MIPS_GET_RS(ops[1]), MIPS_GET_RS(ops[2]), MIPS_GET_RS(ops[3]) };
				if (offsets[1] == offset + 16 && offsets[2] == offsets[1] + 16 && offsets[3] == offsets[2] + 16 &&
					rss[0] == rss[1] && rss[1] == rss[2] && rss[2] == rss[3]) {
					int vts[4] = { MIPS_GET_VQVT(op.encoding), MIPS_GET_VQVT(ops[1]), MIPS_GET_VQVT(ops[2]), MIPS_GET_VQVT(ops[3]) };
					// TODO: Also check the destination registers!
					// Detected four consecutive ones!
					// gpr.MapRegAsPointer(rs);
					// fpr.QLoad4x4(vts[4], rs, offset);
					INFO_LOG(Log::JIT, "Matrix store detected! TODO: optimize");
					// break;
				}
			}
						 
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

void ArmJit::CompNEON_VVectorInit(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_XFER);
	// WARNING: No prefix support!
	if (js.HasUnknownPrefix()) {
		DISABLE_UNKNOWN_PREFIX;
	}
	VectorSize sz = GetVecSize(op);
	DestARMReg vd = NEONMapPrefixD(_VD, sz, MAP_NOINIT | MAP_DIRTY);

	switch ((op >> 16) & 0xF) {
	case 6:  // vzero
		VEOR(vd.rd, vd.rd, vd.rd);
		break;
	case 7:  // vone
		VMOV_immf(vd.rd, 1.0f);
		break;
	default:
		DISABLE;
		break;
	}
	NEONApplyPrefixD(vd);

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_VDot(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_VEC);
	if (js.HasUnknownPrefix()) {
		DISABLE_UNKNOWN_PREFIX;
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
		VADD(F_32, r.vd, D0, D1);
		break;
	case V_Quad:
		VMUL(F_32, D0, D_0(r.vs), D_0(r.vt));
		VMLA(F_32, D0, D_1(r.vs), D_1(r.vt));
		VPADD(F_32, r.vd, D0, D0);
		break;
	case V_Single:
	case V_Invalid:
		;
	}

	NEONApplyPrefixD(r.vd);
	fpr.ReleaseSpillLocksAndDiscardTemps();
}


void ArmJit::CompNEON_VHdp(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_VEC);
	if (js.HasUnknownPrefix()) {
		DISABLE_UNKNOWN_PREFIX;
	}
	
	DISABLE;

	// Similar to VDot but the last component is only s instead of s * t.
	// A bit tricky on NEON...
}

void ArmJit::CompNEON_VScl(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_VEC);
	if (js.HasUnknownPrefix()) {
		DISABLE_UNKNOWN_PREFIX;
	}

	VectorSize sz = GetVecSize(op);
	MappedRegs r = NEONMapDirtyInIn(op, sz, sz, V_Single);

	ARMReg temp = MatchSize(Q0, r.vt);

	// TODO: VMUL_scalar directly when possible
	VMOV_neon(temp, r.vt);
	VMUL_scalar(F_32, r.vd, r.vs, DScalar(Q0, 0));

	NEONApplyPrefixD(r.vd);
	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_VV2Op(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_VEC);
	if (js.HasUnknownPrefix()) {
		DISABLE_UNKNOWN_PREFIX;
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
		VMOV_neon(r.vd, r.vs);
		break;
	case 1: // d[i] = fabsf(s[i]); break; //vabs
		VABS(F_32, r.vd, r.vs);
		break;
	case 2: // d[i] = -s[i]; break; //vneg
		VNEG(F_32, r.vd, r.vs);
		break;

	case 4: // if (s[i] < 0) d[i] = 0; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;    // vsat0
		if (IsD(r.vd)) {
			VMOV_immf(D0, 0.0f);
			VMOV_immf(D1, 1.0f);
			VMAX(F_32, r.vd, r.vs, D0);
			VMIN(F_32, r.vd, r.vd, D1);
		} else {
			VMOV_immf(Q0, 1.0f);
			VMIN(F_32, r.vd, r.vs, Q0);
			VMOV_immf(Q0, 0.0f);
			VMAX(F_32, r.vd, r.vd, Q0);
		}
		break;
	case 5: // if (s[i] < -1.0f) d[i] = -1.0f; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;  // vsat1
		if (IsD(r.vd)) {
			VMOV_immf(D0, -1.0f);
			VMOV_immf(D1, 1.0f);
			VMAX(F_32, r.vd, r.vs, D0);
			VMIN(F_32, r.vd, r.vd, D1);
		} else {
			VMOV_immf(Q0, 1.0f);
			VMIN(F_32, r.vd, r.vs, Q0);
			VMOV_immf(Q0, -1.0f);
			VMAX(F_32, r.vd, r.vd, Q0);
		}
		break;

	case 16: // d[i] = 1.0f / s[i]; break; //vrcp
		// Can just fallback to VFP and use VDIV.
		DISABLE;
		{
			ARMReg temp2 = fpr.QAllocTemp(sz);
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
		DISABLE;
		// Needs iterations on NEON
		{
			if (true) {
				// Not-very-accurate estimate
				VRSQRTE(F_32, r.vd, r.vs);
			} else {
				ARMReg temp2 = fpr.QAllocTemp(sz);
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
		// Let's just defer to VFP for now. Better than calling the interpreter for sure.
		VMOV_neon(MatchSize(Q0, r.vs), r.vs);
		for (int i = 0; i < n; i++) {
			VSQRT((ARMReg)(S0 + i), (ARMReg)(S0 + i));
		}
		VMOV_neon(MatchSize(Q0, r.vd), r.vd);
		break;
	case 23: // d[i] = asinf(s[i] * (float)M_2_PI); break; //vasin
		DISABLE;
		break;
	case 24: // d[i] = -1.0f / s[i]; break; // vnrcp
		// Needs iterations on NEON. Just do the same as vrcp and negate.
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

void ArmJit::CompNEON_Mftv(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_XFER);
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
				VMOV_neon(MatchSize(Q0, r), r);
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
				ERROR_LOG(Log::CPU, "mfv - invalid register %i", imm);
			}
		}
		break;

	case 7: // mtv
		if (imm < 128) {
			// TODO: It's pretty common that this is preceded by mfc1, that is, a value is being
			// moved from the regular floating point registers. It would probably be faster to do
			// the copy directly in the FPRs instead of going through the GPRs.

			ARMReg r = fpr.QMapReg(imm, V_Single, MAP_DIRTY | MAP_NOINIT);
			if (gpr.IsMapped(rt)) {
				VMOV(S0, gpr.R(rt));
				VMOV_neon(r, MatchSize(Q0, r));
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

			// TODO: Optimization if rt is Imm?
			// Set these BEFORE disable!
			if (imm - 128 == VFPU_CTRL_SPREFIX) {
				js.prefixSFlag = JitState::PREFIX_UNKNOWN;
				js.blockWrotePrefixes = true;
			} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
				js.prefixTFlag = JitState::PREFIX_UNKNOWN;
				js.blockWrotePrefixes = true;
			} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
				js.prefixDFlag = JitState::PREFIX_UNKNOWN;
				js.blockWrotePrefixes = true;
			}
		} else {
			//ERROR
			_dbg_assert_msg_(false,"mtv - invalid register");
		}
		break;

	default:
		DISABLE;
	}

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_Vmfvc(MIPSOpcode op) {
	DISABLE;
}

void ArmJit::CompNEON_Vmtvc(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_XFER);

	int vs = _VS;
	int imm = op & 0xFF;
	if (imm >= 128 && imm < 128 + VFPU_CTRL_MAX) {
		ARMReg r = fpr.QMapReg(vs, V_Single, 0);
		ADDI2R(R0, CTXREG, offsetof(MIPSState, vfpuCtrl[0]) + (imm - 128) * 4, R1);
		VST1_lane(F_32, r, R0, 0, true);
		fpr.ReleaseSpillLocksAndDiscardTemps();

		if (imm - 128 == VFPU_CTRL_SPREFIX) {
			js.prefixSFlag = JitState::PREFIX_UNKNOWN;
			js.blockWrotePrefixes = true;
		} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
			js.prefixTFlag = JitState::PREFIX_UNKNOWN;
			js.blockWrotePrefixes = true;
		} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
			js.prefixDFlag = JitState::PREFIX_UNKNOWN;
			js.blockWrotePrefixes = true;
		}
	}
}

void ArmJit::CompNEON_VMatrixInit(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_XFER);

	MatrixSize msz = GetMtxSize(op);
	int n = GetMatrixSide(msz);

	ARMReg cols[4];
	fpr.QMapMatrix(cols, _VD, msz, MAP_NOINIT | MAP_DIRTY);

	switch ((op >> 16) & 0xF) {
	case 3:  // vmidt
		// There has to be a better way to synthesize: 1.0, 0.0, 0.0, 1.0 in a quad
		VEOR(D0, D0, D0);
		VMOV_immf(D1, 1.0f);
		VTRN(F_32, D0, D1);
		VREV64(I_32, D0, D0);
		switch (msz) {
		case M_2x2:
			VMOV_neon(cols[0], D0);
			VMOV_neon(cols[1], D1);
			break;
		case M_3x3:
			VMOV_neon(D_0(cols[0]), D0);
			VMOV_imm(I_8, D_1(cols[0]), VIMMxxxxxxxx, 0);
			VMOV_neon(D_0(cols[1]), D1);
			VMOV_imm(I_8, D_1(cols[1]), VIMMxxxxxxxx, 0);
			VMOV_imm(I_8, D_0(cols[2]), VIMMxxxxxxxx, 0);
			VMOV_neon(D_1(cols[2]), D0);
			break;
		case M_4x4:
			VMOV_neon(D_0(cols[0]), D0);
			VMOV_imm(I_8, D_1(cols[0]), VIMMxxxxxxxx, 0);
			VMOV_neon(D_0(cols[1]), D1);
			VMOV_imm(I_8, D_1(cols[1]), VIMMxxxxxxxx, 0);
			VMOV_imm(I_8, D_0(cols[2]), VIMMxxxxxxxx, 0);
			VMOV_neon(D_1(cols[2]), D0);
			VMOV_imm(I_8, D_0(cols[3]), VIMMxxxxxxxx, 0);
			VMOV_neon(D_1(cols[3]), D1);

			// NEONTranspose4x4(cols);
			break;
		default:
			_assert_msg_(false, "Bad matrix size");
			break;
		}
		break;
	case 6: // vmzero
		for (int i = 0; i < n; i++) {
			VEOR(cols[i], cols[i], cols[i]);
		}
		break;
	case 7: // vmone
		for (int i = 0; i < n; i++) {
			VMOV_immf(cols[i], 1.0f);
		}
		break;
	}

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_Vmmov(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_MTX_VMMOV);
	if (_VS == _VD) {
		// A lot of these no-op matrix moves in Wipeout... Just drop the instruction entirely.
		return;
	}

	MatrixSize msz = GetMtxSize(op);

	MatrixOverlapType overlap = GetMatrixOverlap(_VD, _VS, msz);
	if (overlap != OVERLAP_NONE) {
		// Too complicated to bother handling in the JIT.
		// TODO: Special case for in-place (and other) transpose, etc.
		DISABLE;
	}
	
	ARMReg s_cols[4], d_cols[4];
	fpr.QMapMatrix(s_cols, _VS, msz, 0);
	fpr.QMapMatrix(d_cols, _VD, msz, MAP_DIRTY | MAP_NOINIT);

	int n = GetMatrixSide(msz);
	for (int i = 0; i < n; i++) {
		VMOV_neon(d_cols[i], s_cols[i]);
	}

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_Vmmul(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_MTX_VMMUL);

	MatrixSize msz = GetMtxSize(op);
	int n = GetMatrixSide(msz);

	bool overlap = GetMatrixOverlap(_VD, _VS, msz) || GetMatrixOverlap(_VD, _VT, msz);
	if (overlap) {
		// Later. Fortunately, the VFPU also seems to prohibit overlap for matrix mul.
		INFO_LOG(Log::JIT, "Matrix overlap, ignoring.");
		DISABLE;
	}

	// Having problems with 2x2s for some reason.
	if (msz == M_2x2) {
		DISABLE;
	}

	ARMReg s_cols[4], t_cols[4], d_cols[4];

	// For some reason, vmmul is encoded with the first matrix (S) transposed from the real meaning.
	fpr.QMapMatrix(t_cols, _VT, msz, MAP_FORCE_LOW);  // Need to see if we can avoid having to force it low in some sane way. Will need crazy prediction logic for loads otherwise.
	fpr.QMapMatrix(s_cols, Xpose(_VS), msz, MAP_PREFER_HIGH);
	fpr.QMapMatrix(d_cols, _VD, msz, MAP_PREFER_HIGH | MAP_NOINIT | MAP_DIRTY);
	
	// TODO: Getting there but still getting wrong results.
	for (int i = 0; i < n; i++) {
		for (int j = 0; j < n; j++) {
			if (i == 0) {
				VMUL_scalar(F_32, d_cols[j], s_cols[i], XScalar(t_cols[j], i));
			} else {
				VMLA_scalar(F_32, d_cols[j], s_cols[i], XScalar(t_cols[j], i));
			}
		}
	}

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_Vmscl(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_MTX_VMSCL);

	MatrixSize msz = GetMtxSize(op);

	bool overlap = GetMatrixOverlap(_VD, _VS, msz) != OVERLAP_NONE;
	if (overlap) {
		DISABLE;
	}

	int n = GetMatrixSide(msz);

	ARMReg s_cols[4], t, d_cols[4];
	fpr.QMapMatrix(s_cols, _VS, msz, 0);
	fpr.QMapMatrix(d_cols, _VD, msz, MAP_NOINIT | MAP_DIRTY);

	t = fpr.QMapReg(_VT, V_Single, 0);
	VMOV_neon(D0, t);
	for (int i = 0; i < n; i++) {
		VMUL_scalar(F_32, d_cols[i], s_cols[i], DScalar(D0, 0));
	}

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_Vtfm(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_MTX_VTFM);
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}

	if (_VT == _VD) {
		DISABLE;
	}

	VectorSize sz = GetVecSize(op);
	MatrixSize msz = GetMtxSize(op);
	int n = GetNumVectorElements(sz);
	int ins = (op >> 23) & 7;

	bool homogenous = false;
	if (n == ins) {
		n++;
		sz = (VectorSize)((int)(sz)+1);
		msz = (MatrixSize)((int)(msz)+1);
		homogenous = true;
	}
	// Otherwise, n should already be ins + 1.
	else if (n != ins + 1) {
		DISABLE;
	}

	ARMReg s_cols[4], t, d;
	t = fpr.QMapReg(_VT, sz, MAP_FORCE_LOW);
	fpr.QMapMatrix(s_cols, Xpose(_VS), msz, MAP_PREFER_HIGH);
	d = fpr.QMapReg(_VD, sz, MAP_DIRTY | MAP_NOINIT | MAP_PREFER_HIGH);
	
	VMUL_scalar(F_32, d, s_cols[0], XScalar(t, 0));
	for (int i = 1; i < n; i++) {
		if (homogenous && i == n - 1) {
			VADD(F_32, d, d, s_cols[i]);
		} else {
			VMLA_scalar(F_32, d, s_cols[i], XScalar(t, i));
		}
	}

	// VTFM does not have prefix support.

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_VCrs(MIPSOpcode op) {
	DISABLE;
}

void ArmJit::CompNEON_VDet(MIPSOpcode op) {
	DISABLE;
}

void ArmJit::CompNEON_Vi2x(MIPSOpcode op) {
	DISABLE;
}

void ArmJit::CompNEON_Vx2i(MIPSOpcode op) {
	DISABLE;
}

void ArmJit::CompNEON_Vf2i(MIPSOpcode op) {
	DISABLE;
}

void ArmJit::CompNEON_Vi2f(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_VEC);
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}

	DISABLE;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	int imm = (op >> 16) & 0x1f;
	const float mult = 1.0f / (float)(1UL << imm);

	MappedRegs regs = NEONMapDirtyIn(op, sz, sz);

	MOVI2F_neon(MatchSize(Q0, regs.vd), mult, R0);
	
	VCVT(F_32, regs.vd, regs.vs);
	VMUL(F_32, regs.vd, regs.vd, Q0);

	NEONApplyPrefixD(regs.vd);

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_Vh2f(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_VEC);
	if (!cpu_info.bHalf) {
		// No hardware support for half-to-float, fallback to interpreter
		// TODO: Translate the fast SSE solution to standard integer/VFP stuff
		// for the weaker CPUs.
		DISABLE;
	}

	VectorSize sz = GetVecSize(op);

	VectorSize outsize = V_Pair;
	switch (sz) {
	case V_Single:
		outsize = V_Pair;
		break;
	case V_Pair:
		outsize = V_Quad;
		break;
	default:
		ERROR_LOG(Log::JIT, "Vh2f: Must be pair or quad");
		break;
	}

	ARMReg vs = NEONMapPrefixS(_VS, sz, 0);
	// TODO: MAP_NOINIT if they're definitely not overlapping.
	DestARMReg vd = NEONMapPrefixD(_VD, outsize, MAP_DIRTY);

	VCVTF32F16(vd.rd, vs);

	NEONApplyPrefixD(vd);
	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_Vcst(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_XFER);
	if (js.HasUnknownPrefix()) {
		DISABLE_UNKNOWN_PREFIX;
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

void ArmJit::CompNEON_Vhoriz(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_VEC);
	if (js.HasUnknownPrefix()) {
		DISABLE_UNKNOWN_PREFIX;
	}
	VectorSize sz = GetVecSize(op);
	// Do any games use these a noticeable amount?
	switch ((op >> 16) & 31) {
	case 6:  // vfad
		{
			VMOV_neon(F_32, D1, 0.0f);
			MappedRegs r = NEONMapDirtyIn(op, V_Single, sz);
			switch (sz) {
			case V_Pair:
				VPADD(F_32, r.vd, r.vs, r.vs);
				break;
			case V_Triple:
				VPADD(F_32, D0, D_0(r.vs), D_0(r.vs));
				VADD(F_32, r.vd, D0, D_1(r.vs));
				break;
			case V_Quad:
				VADD(F_32, D0, D_0(r.vs), D_1(r.vs));
				VPADD(F_32, r.vd, D0, D0);
				break;
			default:
				;
			}
			// This forces the sign of -0.000 to +0.000.
			VADD(F_32, r.vd, r.vd, D1);
			break;
		}

	case 7:  // vavg
		DISABLE;
		break;
	}
	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_VRot(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_VEC);

	if (js.HasUnknownPrefix()) {
		DISABLE_UNKNOWN_PREFIX;
	}

	DISABLE;

	int vd = _VD;
	int vs = _VS;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	// ...
	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_VIdt(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_XFER);
	if (js.HasUnknownPrefix()) {
		DISABLE_UNKNOWN_PREFIX;
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
	case V_Triple:
	case V_Quad:
		{
			// TODO: This can be optimized.
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
		_dbg_assert_msg_(false,"Bad vidt instruction");
		break;
	}

	NEONApplyPrefixD(vd);
	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_Vcmp(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_COMP);
	if (js.HasUnknownPrefix())
		DISABLE;

	// Not a chance that this works on the first try :P
	DISABLE;
	
	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	VCondition cond = (VCondition)(op & 0xF);

	MappedRegs regs = NEONMapInIn(op, sz, sz);

	ARMReg vs = regs.vs, vt = regs.vt;
	ARMReg res = fpr.QAllocTemp(sz);

	// Some, we just fall back to the interpreter.
	// ES is just really equivalent to (value & 0x7F800000) == 0x7F800000.
	switch (cond) {
	case VC_EI: // c = my_isinf(s[i]); break;
	case VC_NI: // c = !my_isinf(s[i]); break;
		DISABLE;
	case VC_ES: // c = my_isnan(s[i]) || my_isinf(s[i]); break;   // Tekken Dark Resurrection
	case VC_NS: // c = !my_isnan(s[i]) && !my_isinf(s[i]); break;
	case VC_EN: // c = my_isnan(s[i]); break;
	case VC_NN: // c = !my_isnan(s[i]); break;
		// if (_VS != _VT)
			DISABLE;
		break;

	case VC_EZ:
	case VC_NZ:
		VMOV_immf(Q0, 0.0f);
		break;
	default:
		;
	}
	
	int affected_bits = (1 << 4) | (1 << 5);  // 4 and 5
	for (int i = 0; i < n; i++) {
		affected_bits |= 1 << i;
	}

	// Preload the pointer to our magic mask
	static const u32 collectorBits[4] = { 1, 2, 4, 8 };
	MOVP2R(R1, &collectorBits);

	// Do the compare
	MOVI2R(R0, 0);
	CCFlags flag = CC_AL;

	bool oneIsFalse = false;
	switch (cond) {
	case VC_FL: // c = 0;
		break;

	case VC_TR: // c = 1
		MOVI2R(R0, affected_bits);
		break;

	case VC_ES: // c = my_isnan(s[i]) || my_isinf(s[i]); break;   // Tekken Dark Resurrection
	case VC_NS: // c = !(my_isnan(s[i]) || my_isinf(s[i])); break;
		DISABLE;  // TODO: these shouldn't be that hard
		break;

	case VC_EN: // c = my_isnan(s[i]); break;  // Tekken 6
	case VC_NN: // c = !my_isnan(s[i]); break;
		DISABLE;  // TODO: these shouldn't be that hard
		break;

	case VC_EQ: // c = s[i] == t[i]
		VCEQ(F_32, res, vs, vt);
		break;

	case VC_LT: // c = s[i] < t[i]
		VCLT(F_32, res, vs, vt);
		break;

	case VC_LE: // c = s[i] <= t[i]; 
		VCLE(F_32, res, vs, vt);
		break;

	case VC_NE: // c = s[i] != t[i]
		VCEQ(F_32, res, vs, vt);
		oneIsFalse = true;
		break;

	case VC_GE: // c = s[i] >= t[i]
		VCGE(F_32, res, vs, vt);
		break;

	case VC_GT: // c = s[i] > t[i]
		VCGT(F_32, res, vs, vt);
		break;

	case VC_EZ: // c = s[i] == 0.0f || s[i] == -0.0f
		VCEQ(F_32, res, vs);
		break;

	case VC_NZ: // c = s[i] != 0
		VCEQ(F_32, res, vs);
		oneIsFalse = true;
		break;

	default:
		DISABLE;
	}
	if (oneIsFalse) {
		VMVN(res, res);
	}
	// Somehow collect the bits into a mask.
	
	// Collect the bits. Where's my PMOVMSKB? :(
	VLD1(I_32, Q0, R1, n < 2 ? 1 : 2);
	VAND(Q0, Q0, res);
	VPADD(I_32, Q0, Q0, Q0);
	VPADD(I_32, D0, D0, D0);
	// OK, bits now in S0.
	VMOV(R0, S0);
	// Zap irrelevant bits (V_Single, V_Triple)
	AND(R0, R0, affected_bits);

	// TODO: Now, how in the world do we generate the component OR and AND bits without burning tens of ALU instructions?? Lookup-table?

	gpr.MapReg(MIPS_REG_VFPUCC, MAP_DIRTY);
	BIC(gpr.R(MIPS_REG_VFPUCC), gpr.R(MIPS_REG_VFPUCC), affected_bits);
	ORR(gpr.R(MIPS_REG_VFPUCC), gpr.R(MIPS_REG_VFPUCC), R0);
}

void ArmJit::CompNEON_Vcmov(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_COMP);
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}

	DISABLE;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	ARMReg vs = NEONMapPrefixS(_VS, sz, 0);
	DestARMReg vd = NEONMapPrefixD(_VD, sz, MAP_DIRTY);
	int tf = (op >> 19) & 1;
	int imm3 = (op >> 16) & 7;

	if (imm3 < 6) {
		// Test one bit of CC. This bit decides whether none or all subregisters are copied.
		gpr.MapReg(MIPS_REG_VFPUCC);
		TST(gpr.R(MIPS_REG_VFPUCC), 1 << imm3);
		FixupBranch skip = B_CC(CC_NEQ);
		VMOV_neon(vd, vs);
		SetJumpTarget(skip);
	} else {
		// Look at the bottom four bits of CC to individually decide if the subregisters should be copied.
		// This is the nasty one! Need to expand those bits into a full NEON register somehow.
		DISABLE;
		/*
		gpr.MapReg(MIPS_REG_VFPUCC);
		for (int i = 0; i < n; i++) {
			TST(gpr.R(MIPS_REG_VFPUCC), 1 << i);
			SetCC(tf ? CC_EQ : CC_NEQ);
			VMOV(fpr.V(dregs[i]), fpr.V(sregs[i]));
			SetCC(CC_AL);
		}
		*/
	}

	NEONApplyPrefixD(vd);

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_Viim(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_XFER);
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}

	DestARMReg vt = NEONMapPrefixD(_VT, V_Single, MAP_NOINIT | MAP_DIRTY);

	s32 imm = SignExtend16ToS32(op);
	// TODO: Optimize for low registers.
	MOVI2F(S0, (float)imm, R0);
	VMOV_neon(vt.rd, D0);

	NEONApplyPrefixD(vt);
	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_Vfim(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_XFER);
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}

	DestARMReg vt = NEONMapPrefixD(_VT, V_Single, MAP_NOINIT | MAP_DIRTY);

	FP16 half;
	half.u = op & 0xFFFF;
	FP32 fval = half_to_float_fast5(half);
	// TODO: Optimize for low registers.
	MOVI2F(S0, (float)fval.f, R0);
	VMOV_neon(vt.rd, D0);

	NEONApplyPrefixD(vt);
	fpr.ReleaseSpillLocksAndDiscardTemps();
}

// https://code.google.com/p/bullet/source/browse/branches/PhysicsEffects/include/vecmath/neon/vectormath_neon_assembly_implementations.S?r=2488
void ArmJit::CompNEON_VCrossQuat(MIPSOpcode op) {
	// This op does not support prefixes anyway.
	CONDITIONAL_DISABLE(VFPU_VEC);
	if (js.HasUnknownPrefix()) {
		DISABLE_UNKNOWN_PREFIX;
	}
	
	VectorSize sz = GetVecSize(op);
	if (sz != V_Triple) {
		// Quaternion product. Bleh.
		DISABLE;
	}

	MappedRegs r = NEONMapDirtyInIn(op, sz, sz, sz, false);

	ARMReg t1 = Q0;
	ARMReg t2 = fpr.QAllocTemp(V_Triple);
	
	// There has to be a faster way to do this. This is not really any better than
	// scalar.

	// d18, d19 (q9) = t1 = r.vt
	// d16, d17 (q8) = t2 = r.vs
	// d20, d21 (q10) = t
	VMOV_neon(t1, r.vs);
	VMOV_neon(t2, r.vt);
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

void ArmJit::CompNEON_Vsgn(MIPSOpcode op) {
	DISABLE;

	// This will be a bunch of bit magic.
}

void ArmJit::CompNEON_Vocp(MIPSOpcode op) {
	CONDITIONAL_DISABLE(VFPU_VEC);
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}

	// TODO: Handle T prefix.  Right now it uses 1.0f always.

	// This is a hack that modifies prefixes.  We eat them later, so just overwrite.
	// S prefix forces the negate flags.
	js.prefixS |= 0x000F0000;
	// T prefix forces constants on and regnum to 1.
	// That means negate still works, and abs activates a different constant.
	js.prefixT = (js.prefixT & ~0x000000FF) | 0x00000055 | 0x0000F000;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	MappedRegs regs = NEONMapDirtyIn(op, sz, sz);
	MOVI2F_neon(Q0, 1.0f, R0);
	VADD(F_32, regs.vd, Q0, regs.vs);
	NEONApplyPrefixD(regs.vd);

	fpr.ReleaseSpillLocksAndDiscardTemps();
}

void ArmJit::CompNEON_ColorConv(MIPSOpcode op) {
	DISABLE;
}

void ArmJit::CompNEON_Vbfy(MIPSOpcode op) {
	DISABLE;
}

}
// namespace MIPSComp

#endif // PPSSPP_ARCH(ARM)
