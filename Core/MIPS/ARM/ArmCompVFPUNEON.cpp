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
#include "Core/MIPS/ARM/ArmCompVFPUNEONUtil.h"

// TODO: Somehow #ifdef away on ARMv5eabi, without breaking the linker.
// #define CONDITIONAL_DISABLE { fpr.ReleaseSpillLocksAndDiscardTemps(); Comp_Generic(op); return; }

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

void Jit::CompNEON_SV(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	
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
				INFO_LOG(HLE, "LV.S fastmode!");
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
			INFO_LOG(HLE, "LV.S slowmode!");

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
				INFO_LOG(HLE, "SV.S fastmode!");
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

			INFO_LOG(HLE, "SV.S slowmode!");
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
}

inline int MIPS_GET_VQVT(u32 op) {
	return (((op >> 16) & 0x1f)) | ((op & 1) << 5);
}

void Jit::CompNEON_SVQ(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	int offset = (signed short)(op & 0xFFFC);
	int vt = MIPS_GET_VQVT(op.encoding);
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
					int vts[4] = {MIPS_GET_VQVT(op.encoding), MIPS_GET_VQVT(ops[1]), MIPS_GET_VQVT(ops[2]), MIPS_GET_VQVT(ops[3])};
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
		// Let's just defer to VFP for now. Better than calling the interpreter for sure.
		VMOV(MatchSize(Q0, r.vs), r.vs);
		for (int i = 0; i < n; i++) {
			VSQRT((ARMReg)(S0 + i), (ARMReg)(S0 + i));
		}
		VMOV(MatchSize(Q0, r.vd), r.vd);
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
			// TODO: It's pretty common that this is preceded by mfc1, that is, a value is being
			// moved from the regular floating point registers. It would probably be faster to do
			// the copy directly in the FPRs instead of going through the GPRs.

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
	CONDITIONAL_DISABLE;
	if (js.HasUnknownPrefix()) {
		DISABLE;
	}
	VectorSize sz = GetVecSize(op);
	// Do any games use these a noticable amount?
	switch ((op >> 16) & 31) {
	case 6:  // vfad
		{
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
				VADD(F_32, R0, D_0(r.vs), D_1(r.vs));
				VPADD(F_32, r.vd, R0, R0);
				break;
			default:
				;
			}
			break;
		}

	case 7:  // vavg
		DISABLE;
		break;
	}
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