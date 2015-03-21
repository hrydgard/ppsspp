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

#include <cmath>
#include "math/math_util.h"

#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Common/CPUDetect.h"
#include "Core/Config.h"
#include "Core/Reporting.h"

#include "Common/Arm64Emitter.h"
#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/ARM64/Arm64RegCache.h"

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

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

namespace MIPSComp
{
	using namespace Arm64Gen;
	using namespace Arm64JitConstants;

	// Vector regs can overlap in all sorts of swizzled ways.
	// This does allow a single overlap in sregs[i].
	static bool IsOverlapSafeAllowS(int dreg, int di, int sn, u8 sregs[], int tn = 0, u8 tregs[] = NULL)
	{
		for (int i = 0; i < sn; ++i) {
			if (sregs[i] == dreg && i != di)
				return false;
		}
		for (int i = 0; i < tn; ++i) {
			if (tregs[i] == dreg)
				return false;
		}

		// Hurray, no overlap, we can write directly.
		return true;
	}

	static bool IsOverlapSafe(int dreg, int di, int sn, u8 sregs[], int tn = 0, u8 tregs[] = NULL)
	{
		return IsOverlapSafeAllowS(dreg, di, sn, sregs, tn, tregs) && sregs[di] != dreg;
	}

	void Arm64Jit::Comp_VPFX(MIPSOpcode op)	{
		CONDITIONAL_DISABLE;
		int data = op & 0xFFFFF;
		int regnum = (op >> 24) & 3;
		switch (regnum) {
		case 0:  // S
			js.prefixS = data;
			js.prefixSFlag = JitState::PREFIX_KNOWN_DIRTY;
			break;
		case 1:  // T
			js.prefixT = data;
			js.prefixTFlag = JitState::PREFIX_KNOWN_DIRTY;
			break;
		case 2:  // D
			js.prefixD = data;
			js.prefixDFlag = JitState::PREFIX_KNOWN_DIRTY;
			break;
		default:
			ERROR_LOG(CPU, "VPFX - bad regnum %i : data=%08x", regnum, data);
			break;
		}
	}

	void Arm64Jit::ApplyPrefixST(u8 *vregs, u32 prefix, VectorSize sz) {
		if (prefix == 0xE4)
			return;

		int n = GetNumVectorElements(sz);
		u8 origV[4];
		static const float constantArray[8] = { 0.f, 1.f, 2.f, 0.5f, 3.f, 1.f / 3.f, 0.25f, 1.f / 6.f };

		for (int i = 0; i < n; i++)
			origV[i] = vregs[i];

		for (int i = 0; i < n; i++) {
			int regnum = (prefix >> (i * 2)) & 3;
			int abs = (prefix >> (8 + i)) & 1;
			int negate = (prefix >> (16 + i)) & 1;
			int constants = (prefix >> (12 + i)) & 1;

			// Unchanged, hurray.
			if (!constants && regnum == i && !abs && !negate)
				continue;

			// This puts the value into a temp reg, so we won't write the modified value back.
			vregs[i] = fpr.GetTempV();
			if (!constants) {
				fpr.MapDirtyInV(vregs[i], origV[regnum]);
				fpr.SpillLockV(vregs[i]);

				// Prefix may say "z, z, z, z" but if this is a pair, we force to x.
				// TODO: But some ops seem to use const 0 instead?
				if (regnum >= n) {
					WARN_LOG(CPU, "JIT: Invalid VFPU swizzle: %08x : %d / %d at PC = %08x (%s)", prefix, regnum, n, js.compilerPC, MIPSDisasmAt(js.compilerPC));
					regnum = 0;
				}

				if (abs) {
					fp.FABS(fpr.V(vregs[i]), fpr.V(origV[regnum]));
					if (negate)
						fp.FNEG(fpr.V(vregs[i]), fpr.V(vregs[i]));
				} else {
					if (negate)
						fp.FNEG(fpr.V(vregs[i]), fpr.V(origV[regnum]));
					else
						fp.FMOV(fpr.V(vregs[i]), fpr.V(origV[regnum]));
				}
			} else {
				fpr.MapRegV(vregs[i], MAP_DIRTY | MAP_NOINIT);
				fpr.SpillLockV(vregs[i]);
				fp.MOVI2F(fpr.V(vregs[i]), constantArray[regnum + (abs << 2)], SCRATCH1, (bool)negate);
			}
		}
	}

	void Arm64Jit::GetVectorRegsPrefixD(u8 *regs, VectorSize sz, int vectorReg) {
		_assert_(js.prefixDFlag & JitState::PREFIX_KNOWN);

		GetVectorRegs(regs, sz, vectorReg);
		if (js.prefixD == 0)
			return;

		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++) {
			// Hopefully this is rare, we'll just write it into a reg we drop.
			if (js.VfpuWriteMask(i))
				regs[i] = fpr.GetTempV();
		}
	}

	void Arm64Jit::ApplyPrefixD(const u8 *vregs, VectorSize sz) {
		_assert_(js.prefixDFlag & JitState::PREFIX_KNOWN);
		if (!js.prefixD)
			return;

		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++) {
			if (js.VfpuWriteMask(i))
				continue;

			int sat = (js.prefixD >> (i * 2)) & 3;
			if (sat == 1) {
				// clamped = x < 0 ? (x > 1 ? 1 : x) : x [0, 1]
				fpr.MapRegV(vregs[i], MAP_DIRTY);

				fp.MOVI2F(S0, 0.0f, SCRATCH1);
				fp.MOVI2F(S1, 1.0f, SCRATCH1);
				fp.FMIN(fpr.V(vregs[i]), fpr.V(vregs[i]), S1);
				fp.FMAX(fpr.V(vregs[i]), fpr.V(vregs[i]), S0);
			} else if (sat == 3) {
				// clamped = x < -1 ? (x > 1 ? 1 : x) : x [-1, 1]
				fpr.MapRegV(vregs[i], MAP_DIRTY);

				fp.MOVI2F(S0, -1.0f, SCRATCH1);
				fp.MOVI2F(S1, 1.0f, SCRATCH1);
				fp.FMIN(fpr.V(vregs[i]), fpr.V(vregs[i]), S1);
				fp.FMAX(fpr.V(vregs[i]), fpr.V(vregs[i]), S0);
			}
		}
	}

	void Arm64Jit::Comp_SV(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_SVQ(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_VVectorInit(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_VIdt(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}
		DISABLE;

		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);
		fp.MOVI2F(S0, 0.0f, SCRATCH1);
		fp.MOVI2F(S1, 1.0f, SCRATCH1);
		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, _VD);
		fpr.MapRegsAndSpillLockV(dregs, sz, MAP_NOINIT | MAP_DIRTY);
		switch (sz) {
		case V_Pair:
			fp.FMOV(fpr.V(dregs[0]), (vd & 1) == 0 ? S1 : S0);
			fp.FMOV(fpr.V(dregs[1]), (vd & 1) == 1 ? S1 : S0);
			break;
		case V_Quad:
			fp.FMOV(fpr.V(dregs[0]), (vd & 3) == 0 ? S1 : S0);
			fp.FMOV(fpr.V(dregs[1]), (vd & 3) == 1 ? S1 : S0);
			fp.FMOV(fpr.V(dregs[2]), (vd & 3) == 2 ? S1 : S0);
			fp.FMOV(fpr.V(dregs[3]), (vd & 3) == 3 ? S1 : S0);
			break;
		default:
			_dbg_assert_msg_(CPU, 0, "Trying to interpret instruction that can't be interpreted");
			break;
		}

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_VMatrixInit(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			// Don't think matrix init ops care about prefixes.
			// DISABLE;
		}
		DISABLE;

		MatrixSize sz = GetMtxSize(op);
		int n = GetMatrixSide(sz);

		u8 dregs[16];
		GetMatrixRegs(dregs, sz, _VD);

		switch ((op >> 16) & 0xF) {
		case 3: // vmidt
			fp.MOVI2F(S0, 0.0f, SCRATCH1);
			fp.MOVI2F(S1, 1.0f, SCRATCH1);
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					fp.FMOV(fpr.V(dregs[a * 4 + b]), a == b ? S1 : S0);
				}
			}
			break;
		case 6: // vmzero
			fp.MOVI2F(S0, 0.0f, SCRATCH1);
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					fp.FMOV(fpr.V(dregs[a * 4 + b]), S0);
				}
			}
			break;
		case 7: // vmone
			fp.MOVI2F(S1, 1.0f, SCRATCH1);
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					fp.FMOV(fpr.V(dregs[a * 4 + b]), S1);
				}
			}
			break;
		}

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_VHdp(MIPSOpcode op) {
		DISABLE;
	}

	static const float MEMORY_ALIGNED16(vavg_table[4]) = { 1.0f, 1.0f / 2.0f, 1.0f / 3.0f, 1.0f / 4.0f };

	void Arm64Jit::Comp_Vhoriz(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_VDot(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);

		// TODO: Force read one of them into regs? probably not.
		u8 sregs[4], tregs[4], dregs[1];
		GetVectorRegsPrefixS(sregs, sz, vs);
		GetVectorRegsPrefixT(tregs, sz, vt);
		GetVectorRegsPrefixD(dregs, V_Single, vd);

		// TODO: applyprefixST here somehow (shuffle, etc...)
		fpr.MapRegsAndSpillLockV(sregs, sz, 0);
		fpr.MapRegsAndSpillLockV(tregs, sz, 0);
		fp.FMUL(S0, fpr.V(sregs[0]), fpr.V(tregs[0]));

		int n = GetNumVectorElements(sz);
		for (int i = 1; i < n; i++) {
			// sum += s[i]*t[i];
			fp.FMADD(S0, fpr.V(sregs[i]), fpr.V(tregs[i]), S0);
		}
		fpr.ReleaseSpillLocksAndDiscardTemps();

		fpr.MapRegV(dregs[0], MAP_NOINIT | MAP_DIRTY);

		fp.FMOV(fpr.V(dregs[0]), S0);
		ApplyPrefixD(dregs, V_Single);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_VecDo3(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], tregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixT(tregs, sz, _VT);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		MIPSReg tempregs[4];
		for (int i = 0; i < n; i++) {
			if (!IsOverlapSafe(dregs[i], i, n, sregs, n, tregs)) {
				tempregs[i] = fpr.GetTempV();
			} else {
				tempregs[i] = dregs[i];
			}
		}

		// Map first, then work. This will allow us to use VLDMIA more often
		// (when we add the appropriate map function) and the instruction ordering
		// will improve.
		// Note that mapping like this (instead of first all sregs, first all tregs etc)
		// reduces the amount of continuous registers a lot :(
		for (int i = 0; i < n; i++) {
			fpr.MapDirtyInInV(tempregs[i], sregs[i], tregs[i]);
			fpr.SpillLockV(tempregs[i]);
			fpr.SpillLockV(sregs[i]);
			fpr.SpillLockV(tregs[i]);
		}

		for (int i = 0; i < n; i++) {
			switch (op >> 26) {
			case 24: //VFPU0
				switch ((op >> 23) & 7) {
				case 0: // d[i] = s[i] + t[i]; break; //vadd
					fp.FADD(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				case 1: // d[i] = s[i] - t[i]; break; //vsub
					fp.FSUB(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				case 7: // d[i] = s[i] / t[i]; break; //vdiv
					fp.FDIV(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				default:
					DISABLE;
				}
				break;
			case 25: //VFPU1
				switch ((op >> 23) & 7) {
				case 0: // d[i] = s[i] * t[i]; break; //vmul
					fp.FMUL(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				default:
					DISABLE;
				}
				break;
				// Fortunately there is FMIN/FMAX on ARM64!
			case 27: //VFPU3
				switch ((op >> 23) & 7) {
				case 2:  // vmin
				{
					fp.FMIN(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				}
				case 3:  // vmax
				{
					fp.FMAX(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				}
				case 6:  // vsge
					DISABLE;  // pending testing
					break;
				case 7:  // vslt
					DISABLE;  // pending testing
					break;
				}
				break;

			default:
				DISABLE;
			}
		}

		for (int i = 0; i < n; i++) {
			if (dregs[i] != tempregs[i]) {
				fpr.MapDirtyInV(dregs[i], tempregs[i]);
				fp.FMOV(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}
		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_VV2Op(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		// Pre-processing: Eliminate silly no-op VMOVs, common in Wipeout Pure
		if (((op >> 16) & 0x1f) == 0 && _VS == _VD && js.HasNoPrefix()) {
			return;
		}

		// Catch the disabled operations immediately so we don't map registers unnecessarily later.
		// Move these down to the big switch below as they are implemented.
		switch ((op >> 16) & 0x1f) {
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
		case 26: // d[i] = -sinf((float)M_PI_2 * s[i]); break; // vnsin
			DISABLE;
			break;
		case 28: // d[i] = 1.0f / expf(s[i] * (float)M_LOG2E); break; // vrexp2
			DISABLE;
			break;
		default:
			;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		MIPSReg tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (!IsOverlapSafe(dregs[i], i, n, sregs)) {
				tempregs[i] = fpr.GetTempV();
			} else {
				tempregs[i] = dregs[i];
			}
		}

		// Pre map the registers to get better instruction ordering.
		// Note that mapping like this (instead of first all sregs, first all tempregs etc)
		// reduces the amount of continuous registers a lot :(
		for (int i = 0; i < n; i++) {
			fpr.MapDirtyInV(tempregs[i], sregs[i]);
			fpr.SpillLockV(tempregs[i]);
			fpr.SpillLockV(sregs[i]);
		}

		// Warning: sregs[i] and tempxregs[i] may be the same reg.
		// Helps for vmov, hurts for vrcp, etc.
		for (int i = 0; i < n; i++) {
			switch ((op >> 16) & 0x1f) {
			case 0: // d[i] = s[i]; break; //vmov
				// Probably for swizzle.
				fp.FMOV(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				break;
			case 1: // d[i] = fabsf(s[i]); break; //vabs
				fp.FABS(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				break;
			case 2: // d[i] = -s[i]; break; //vneg
				fp.FNEG(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				break;
			case 4: // if (s[i] < 0) d[i] = 0; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;    // vsat0
				if (i == 0) {
					fp.MOVI2F(S0, 0.0f, SCRATCH1);
					fp.MOVI2F(S1, 1.0f, SCRATCH1);
				}
				fp.FCMP(fpr.V(sregs[i]), S0);
				fp.FMOV(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				fp.FMAX(fpr.V(tempregs[i]), fpr.V(tempregs[i]), S0);
				fp.FMIN(fpr.V(tempregs[i]), fpr.V(tempregs[i]), S1);
				break;
			case 5: // if (s[i] < -1.0f) d[i] = -1.0f; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;  // vsat1
				if (i == 0) {
					fp.MOVI2F(S0, -1.0f, SCRATCH1);
					fp.MOVI2F(S1, 1.0f, SCRATCH1);
				}
				fp.FCMP(fpr.V(sregs[i]), S0);
				fp.FMOV(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				fp.FMAX(fpr.V(tempregs[i]), fpr.V(tempregs[i]), S0);
				fp.FMIN(fpr.V(tempregs[i]), fpr.V(tempregs[i]), S1);
				break;
			case 16: // d[i] = 1.0f / s[i]; break; //vrcp
				if (i == 0) {
					fp.MOVI2F(S0, 1.0f, SCRATCH1);
				}
				fp.FDIV(fpr.V(tempregs[i]), S0, fpr.V(sregs[i]));
				break;
			case 17: // d[i] = 1.0f / sqrtf(s[i]); break; //vrsq
				if (i == 0) {
					fp.MOVI2F(S0, 1.0f, SCRATCH1);
				}
				fp.FSQRT(S1, fpr.V(sregs[i]));
				fp.FDIV(fpr.V(tempregs[i]), S0, S1);
				break;
			case 22: // d[i] = sqrtf(s[i]); break; //vsqrt
				fp.FSQRT(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				fp.FABS(fpr.V(tempregs[i]), fpr.V(tempregs[i]));
				break;
			case 23: // d[i] = asinf(s[i] * (float)M_2_PI); break; //vasin
				DISABLE;
				break;
			case 24: // d[i] = -1.0f / s[i]; break; // vnrcp
				if (i == 0) {
					fp.MOVI2F(S0, -1.0f, SCRATCH1);
				}
				fp.FDIV(fpr.V(tempregs[i]), S0, fpr.V(sregs[i]));
				break;
			default:
				ERROR_LOG(JIT, "case missing in vfpu vv2op");
				DISABLE;
				break;
			}
		}

		for (int i = 0; i < n; ++i) {
			if (dregs[i] != tempregs[i]) {
				fpr.MapDirtyInV(dregs[i], tempregs[i]);
				fp.FMOV(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_Vi2f(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vh2f(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vf2i(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Mftv(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vmfvc(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vmtvc(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vmmov(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_VScl(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], dregs[4], treg;
		GetVectorRegsPrefixS(sregs, sz, _VS);
		// TODO: Prefixes seem strange...
		GetVectorRegsPrefixT(&treg, V_Single, _VT);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		// Move to S0 early, so we don't have to worry about overlap with scale.
		fpr.LoadToRegV(S0, treg);

		// For prefixes to work, we just have to ensure that none of the output registers spill
		// and that there's no overlap.
		MIPSReg tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (!IsOverlapSafe(dregs[i], i, n, sregs)) {
				// Need to use temp regs
				tempregs[i] = fpr.GetTempV();
			} else {
				tempregs[i] = dregs[i];
			}
		}

		// The meat of the function!
		for (int i = 0; i < n; i++) {
			fpr.MapDirtyInV(tempregs[i], sregs[i]);
			fp.FMUL(fpr.V(tempregs[i]), fpr.V(sregs[i]), S0);
		}

		for (int i = 0; i < n; i++) {
			// All must be mapped for prefixes to work.
			if (dregs[i] != tempregs[i]) {
				fpr.MapDirtyInV(dregs[i], tempregs[i]);
				fp.FMOV(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_Vmmul(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vmscl(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vtfm(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_VCrs(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_VDet(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vi2x(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vx2i(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_VCrossQuat(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vcmp(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vcmov(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Viim(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vfim(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vcst(MIPSOpcode op) {
		CONDITIONAL_DISABLE;
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		int conNum = (op >> 16) & 0x1f;
		int vd = _VD;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, _VD);
		fpr.MapRegsAndSpillLockV(dregs, sz, MAP_NOINIT | MAP_DIRTY);

		MOVP2R(SCRATCH1_64, (void *)&cst_constants[conNum]);
		fp.LDR(32, INDEX_UNSIGNED, S0, SCRATCH1_64, 0);
		for (int i = 0; i < n; ++i)
			fp.FMOV(fpr.V(dregs[i]), S0);

		ApplyPrefixD(dregs, sz);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::CompVrotShuffle(u8 *dregs, int imm, VectorSize sz, bool negSin) {
		int n = GetNumVectorElements(sz);
		char what[4] = { '0', '0', '0', '0' };
		if (((imm >> 2) & 3) == (imm & 3)) {
			for (int i = 0; i < 4; i++)
				what[i] = 'S';
		}
		what[(imm >> 2) & 3] = 'S';
		what[imm & 3] = 'C';

		fpr.MapRegsAndSpillLockV(dregs, sz, MAP_DIRTY | MAP_NOINIT);
		for (int i = 0; i < n; i++) {
			switch (what[i]) {
			case 'C': fp.FMOV(fpr.V(dregs[i]), S1); break;
			case 'S': if (negSin) fp.FNEG(fpr.V(dregs[i]), S0); else fp.FMOV(fpr.V(dregs[i]), S0); break;
			case '0':
			{
				fp.MOVI2F(fpr.V(dregs[i]), 0.0f);
				break;
			}
			default:
				ERROR_LOG(JIT, "Bad what in vrot");
				break;
			}
		}
	}

	// Very heavily used by FF:CC. Should be replaced by a fast approximation instead of
	// calling the math library.
	void Arm64Jit::Comp_VRot(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vsgn(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vocp(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_ColorConv(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vbfy(MIPSOpcode op) {
		DISABLE;
	}
}
