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

#include "../../MemMap.h"
#include "../MIPSAnalyst.h"
#include "Common/CPUDetect.h"
#include "Core/Config.h"
#include "Core/Reporting.h"

#include "ArmJit.h"
#include "ArmRegCache.h"


#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE { fpr.ReleaseSpillLocks(); Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { fpr.ReleaseSpillLocks(); Comp_Generic(op); return; }

namespace MIPSComp
{
	// Vector regs can overlap in all sorts of swizzled ways.
	// This does allow a single overlap in sregs[i].
	static bool IsOverlapSafeAllowS(int dreg, int di, int sn, u8 sregs[], int tn = 0, u8 tregs[] = NULL)
	{
		for (int i = 0; i < sn; ++i)
		{
			if (sregs[i] == dreg && i != di)
				return false;
		}
		for (int i = 0; i < tn; ++i)
		{
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

	void Jit::Comp_VPFX(u32 op)
	{
		CONDITIONAL_DISABLE;

		int data = op & 0xFFFFF;
		int regnum = (op >> 24) & 3;
		switch (regnum) {
		case 0:  // S
			js.prefixS = data;
			js.prefixSFlag = ArmJitState::PREFIX_KNOWN_DIRTY;
			break;
		case 1:  // T
			js.prefixT = data;
			js.prefixTFlag = ArmJitState::PREFIX_KNOWN_DIRTY;
			break;
		case 2:  // D
			js.prefixD = data;
			js.prefixDFlag = ArmJitState::PREFIX_KNOWN_DIRTY;
			break;
		}
	}

	void Jit::ApplyPrefixST(u8 *vregs, u32 prefix, VectorSize sz) {
		if (prefix == 0xE4) return;

		int n = GetNumVectorElements(sz);
		u8 origV[4];
		static const float constantArray[8] = {0.f, 1.f, 2.f, 0.5f, 3.f, 1.f/3.f, 0.25f, 1.f/6.f};

		for (int i = 0; i < n; i++)
			origV[i] = vregs[i];

		for (int i = 0; i < n; i++)
		{
			int regnum = (prefix >> (i*2)) & 3;
			int abs    = (prefix >> (8+i)) & 1;
			int negate = (prefix >> (16+i)) & 1;
			int constants = (prefix >> (12+i)) & 1;

			// Unchanged, hurray.
			if (!constants && regnum == i && !abs && !negate)
				continue;

			// This puts the value into a temp reg, so we won't write the modified value back.
			vregs[i] = fpr.GetTempV();
			fpr.MapRegV(vregs[i], MAP_NOINIT | MAP_DIRTY);

			if (!constants) {
				// Prefix may say "z, z, z, z" but if this is a pair, we force to x.
				// TODO: But some ops seem to use const 0 instead?
				if (regnum >= n) {
					ERROR_LOG_REPORT(CPU, "Invalid VFPU swizzle: %08x / %d", prefix, sz);
					regnum = 0;
				}
				
				if (abs) {
					VABS(fpr.V(vregs[i]), fpr.V(origV[regnum]));
					if (negate)
						VNEG(fpr.V(vregs[i]), fpr.V(vregs[i]));
				} else {
					if (negate)
						VNEG(fpr.V(vregs[i]), fpr.V(origV[regnum]));
					else
						VMOV(fpr.V(vregs[i]), fpr.V(origV[regnum]));
				}

			} else {
				MOVI2F(fpr.V(vregs[i]), constantArray[regnum + (abs<<2)], R0, negate);
			}

			// TODO: This probably means it will swap out soon, inefficiently...
			fpr.ReleaseSpillLockV(vregs[i]);
		}
	}

	void Jit::GetVectorRegsPrefixD(u8 *regs, VectorSize sz, int vectorReg) {
		_assert_(js.prefixDFlag & ArmJitState::PREFIX_KNOWN);

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

	void Jit::ApplyPrefixD(const u8 *vregs, VectorSize sz) {
		_assert_(js.prefixDFlag & ArmJitState::PREFIX_KNOWN);
		if (!js.prefixD) return;

		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++) 	{
			if (js.VfpuWriteMask(i))
				continue;

			int sat = (js.prefixD >> (i * 2)) & 3;
			if (sat == 1) {
				// clamped = fabs(x) - fabs(x-0.5f) + 0.5f; // [ 0, 1]
				fpr.MapRegV(vregs[i], MAP_DIRTY);
				MOVI2F(S0, 0.5f, R0);
				VABS(S1, fpr.V(vregs[i]));                  // S1 = fabs(x)
				VSUB(fpr.V(vregs[i]), fpr.V(vregs[i]), S0); // S2 = fabs(x-0.5f) {VABD}
				VABS(fpr.V(vregs[i]), fpr.V(vregs[i]));
				VSUB(fpr.V(vregs[i]), S1, fpr.V(vregs[i])); // v[i] = S1 - S2 + 0.5f
				VADD(fpr.V(vregs[i]), fpr.V(vregs[i]), S0);
			} else if (sat == 3) {
				// clamped = fabs(x) - fabs(x-1.0f);        // [-1, 1]
				fpr.MapRegV(vregs[i], MAP_DIRTY);
				MOVI2F(S0, 1.0f, R0);
				VABS(S1, fpr.V(vregs[i]));                  // S1 = fabs(x)
				VSUB(fpr.V(vregs[i]), fpr.V(vregs[i]), S0); // S2 = fabs(x-1.0f) {VABD}
				VABS(fpr.V(vregs[i]), fpr.V(vregs[i]));
				VSUB(fpr.V(vregs[i]), S1, fpr.V(vregs[i])); // v[i] = S1 - S2
			}
		}
	}

	void Jit::Comp_SV(u32 op) {
		CONDITIONAL_DISABLE;

		s32 imm = (signed short)(op&0xFFFC);
		int vt = ((op >> 16) & 0x1f) | ((op & 3) << 5);
		int rs = _RS;

		bool doCheck = false;
		switch (op >> 26)
		{
		case 50: //lv.s  // VI(vt) = Memory::Read_U32(addr);
			{
				// CC might be set by slow path below, so load regs first.
				fpr.MapRegV(vt, MAP_DIRTY | MAP_NOINIT);
				fpr.ReleaseSpillLocks();
				if (gpr.IsImm(rs)) {
					u32 addr = (imm + gpr.GetImm(rs)) & 0x3FFFFFFF;
					MOVI2R(R0, addr + (u32)Memory::base);
				} else {
					gpr.MapReg(rs);
					if (g_Config.bFastMemory) {
						SetR0ToEffectiveAddress(rs, imm);
					} else {
						SetCCAndR0ForSafeAddress(rs, imm, R1);
						doCheck = true;
					}
					ADD(R0, R0, R11);
				}
#ifdef __ARM_ARCH_7S__
				FixupBranch skip;
				if (doCheck) {
					skip = B_CC(CC_EQ);
				}
				VLDR(fpr.V(vt), R0, 0);
				if (doCheck) {
					SetJumpTarget(skip);
					SetCC(CC_AL);
				}
#else
				VLDR(fpr.V(vt), R0, 0);
				if (doCheck) {
					SetCC(CC_EQ);
					MOVI2F(fpr.V(vt), 0.0f, R0);
					SetCC(CC_AL);
				}
#endif
			}
			break;

		case 58: //sv.s   // Memory::Write_U32(VI(vt), addr);
			{
				// CC might be set by slow path below, so load regs first.
				fpr.MapRegV(vt);
				fpr.ReleaseSpillLocks();
				if (gpr.IsImm(rs)) {
					u32 addr = (imm + gpr.GetImm(rs)) & 0x3FFFFFFF;
					MOVI2R(R0, addr + (u32)Memory::base);
				} else {
					gpr.MapReg(rs);
					if (g_Config.bFastMemory) {
						SetR0ToEffectiveAddress(rs, imm);
					} else {
						SetCCAndR0ForSafeAddress(rs, imm, R1);
						doCheck = true;
					}
					ADD(R0, R0, R11);
				}
#ifdef __ARM_ARCH_7S__
				FixupBranch skip;
				if (doCheck) {
					skip = B_CC(CC_EQ);
				}
				VSTR(fpr.V(vt), R0, 0);
				if (doCheck) {
					SetJumpTarget(skip);
					SetCC(CC_AL);
				}
#else
				VSTR(fpr.V(vt), R0, 0);
				if (doCheck) {
					SetCC(CC_AL);
				}
#endif
			}
			break;


		default:
			DISABLE;
		}
	}

	void Jit::Comp_SVQ(u32 op)
	{
		CONDITIONAL_DISABLE;

		int imm = (signed short)(op&0xFFFC);
		int vt = (((op >> 16) & 0x1f)) | ((op&1) << 5);
		int rs = _RS;

		bool doCheck = false;
		switch (op >> 26)
		{
		case 54: //lv.q
			{
				// CC might be set by slow path below, so load regs first.
				u8 vregs[4];
				GetVectorRegs(vregs, V_Quad, vt);
				fpr.MapRegsV(vregs, V_Quad, MAP_DIRTY | MAP_NOINIT);
				fpr.ReleaseSpillLocks();

				if (gpr.IsImm(rs)) {
					u32 addr = (imm + gpr.GetImm(rs)) & 0x3FFFFFFF;
					MOVI2R(R0, addr + (u32)Memory::base);
				} else {
					gpr.MapReg(rs);
					if (g_Config.bFastMemory) {
						SetR0ToEffectiveAddress(rs, imm);
					} else {
						SetCCAndR0ForSafeAddress(rs, imm, R1);
						doCheck = true;
					}
					ADD(R0, R0, R11);
				}

#ifdef __ARM_ARCH_7S__
				FixupBranch skip;
				if (doCheck) {
					skip = B_CC(CC_EQ);
				}

				for (int i = 0; i < 4; i++)
					VLDR(fpr.V(vregs[i]), R0, i * 4);

				if (doCheck) {
					SetJumpTarget(skip);
					SetCC(CC_AL);
				}
#else
				for (int i = 0; i < 4; i++)
					VLDR(fpr.V(vregs[i]), R0, i * 4);

				if (doCheck) {
					SetCC(CC_EQ);
					MOVI2R(R0, 0);
					for (int i = 0; i < 4; i++)
						VMOV(fpr.V(vregs[i]), R0);
					SetCC(CC_AL);
				}
#endif
			}
			break;

		case 62: //sv.q
			{
				// CC might be set by slow path below, so load regs first.
				u8 vregs[4];
				GetVectorRegs(vregs, V_Quad, vt);
				fpr.MapRegsV(vregs, V_Quad, 0);
				fpr.ReleaseSpillLocks();

				if (gpr.IsImm(rs)) {
					u32 addr = (imm + gpr.GetImm(rs)) & 0x3FFFFFFF;
					MOVI2R(R0, addr + (u32)Memory::base);
				} else {
					gpr.MapReg(rs);
					if (g_Config.bFastMemory) {
						SetR0ToEffectiveAddress(rs, imm);
					} else {
						SetCCAndR0ForSafeAddress(rs, imm, R1);
						doCheck = true;
					}
					ADD(R0, R0, R11);
				}

#ifdef __ARM_ARCH_7S__
				FixupBranch skip;
				if (doCheck) {
					skip = B_CC(CC_EQ);
				}

				for (int i = 0; i < 4; i++)
					VSTR(fpr.V(vregs[i]), R0, i * 4);

				if (doCheck) {
					SetJumpTarget(skip);
					SetCC(CC_AL);
				}
#else
				for (int i = 0; i < 4; i++)
					VSTR(fpr.V(vregs[i]), R0, i * 4);

				if (doCheck) {
					SetCC(CC_AL);
				}
#endif
			}
			break;

		default:
			DISABLE;
			break;
		}
	}

	void Jit::Comp_VVectorInit(u32 op)
	{
		CONDITIONAL_DISABLE;

		// WARNING: No prefix support!
		if (js.MayHavePrefix()) {
			Comp_Generic(op);
			js.EatPrefix();
			return;
		}

		switch ((op >> 16) & 0xF)
		{
		case 6: // v=zeros; break;  //vzero
			MOVI2F(S0, 0.0f, R0);
			break;
		case 7: // v=ones; break;   //vone
			MOVI2F(S0, 1.0f, R0);
			break;
		default:
			DISABLE;
			break;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, _VD);
		fpr.MapRegsV(dregs, sz, MAP_NOINIT | MAP_DIRTY);

		for (int i = 0; i < n; ++i)
			VMOV(fpr.V(dregs[i]), S0);

		ApplyPrefixD(dregs, sz);
		fpr.ReleaseSpillLocks();
	}

	void Jit::Comp_VIdt(u32 op) {
		CONDITIONAL_DISABLE

		// WARNING: No prefix support!
		if (js.MayHavePrefix()) {
			DISABLE;
		}

		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);
		MOVI2F(S0, 0.0f, R0);
		MOVI2F(S1, 1.0f, R0);
		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, _VD);
		fpr.MapRegsV(dregs, sz, MAP_NOINIT | MAP_DIRTY);
		switch (sz)
		{
		case V_Pair:
			VMOV(fpr.V(dregs[0]), (vd&1)==0 ? S1 : S0);
			VMOV(fpr.V(dregs[1]), (vd&1)==1 ? S1 : S0);
			break;
		case V_Quad:
			VMOV(fpr.V(dregs[0]), (vd&3)==0 ? S1 : S0);
			VMOV(fpr.V(dregs[1]), (vd&3)==1 ? S1 : S0);
			VMOV(fpr.V(dregs[2]), (vd&3)==2 ? S1 : S0);
			VMOV(fpr.V(dregs[3]), (vd&3)==3 ? S1 : S0);
			break;
		default:
			_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		ApplyPrefixD(dregs, sz);
		fpr.ReleaseSpillLocks();
	}

	void Jit::Comp_VMatrixInit(u32 op)
	{
		CONDITIONAL_DISABLE;

		if (js.HasUnknownPrefix())
			DISABLE;

		MatrixSize sz = GetMtxSize(op);
		int n = GetMatrixSide(sz);

		u8 dregs[16];
		GetMatrixRegs(dregs, sz, _VD);

		switch ((op >> 16) & 0xF) {
		case 3: // vmidt
			MOVI2F(S0, 0.0f, R0);
			MOVI2F(S1, 1.0f, R0);
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					VMOV(fpr.V(dregs[a * 4 + b]), a == b ? S1 : S0);
				}
			}
			break;
		case 6: // vmzero
			MOVI2F(S0, 0.0f, R0);
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					VMOV(fpr.V(dregs[a * 4 + b]), S0);
				}
			}
			break;
		case 7: // vmone
			MOVI2F(S1, 1.0f, R0);
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					VMOV(fpr.V(dregs[a * 4 + b]), S1);
				}
			}
			break;
		}

		fpr.ReleaseSpillLocks();
	}

	void Jit::Comp_VDot(u32 op)
	{
		// DISABLE;
		CONDITIONAL_DISABLE;
		// WARNING: No prefix support!
		if (js.MayHavePrefix()) {
			DISABLE;
		}

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);

		// TODO: Force read one of them into regs? probably not.
		u8 sregs[4], tregs[4];
		GetVectorRegs(sregs, sz, vs);
		GetVectorRegs(tregs, sz, vt);

		// TODO: applyprefixST here somehow (shuffle, etc...)
		fpr.MapRegsV(sregs, sz, 0);
		fpr.MapRegsV(tregs, sz, 0);
		VMUL(S0, fpr.V(sregs[0]), fpr.V(tregs[0]));

		int n = GetNumVectorElements(sz);
		for (int i = 1; i < n; i++) {
			// sum += s[i]*t[i];
			VMLA(S0, fpr.V(sregs[i]), fpr.V(tregs[i]));
		}
		fpr.ReleaseSpillLocks();

		fpr.MapRegV(vd, MAP_NOINIT | MAP_DIRTY);

		// TODO: applyprefixD here somehow (write mask etc..)
		VMOV(fpr.V(vd), S0);

		fpr.ReleaseSpillLocks();

		js.EatPrefix();
	}

	void Jit::Comp_VecDo3(u32 op)
	{
		CONDITIONAL_DISABLE;
		DISABLE;
		// WARNING: No prefix support!
		if (js.MayHavePrefix()) {
			DISABLE;
		}

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;

		void (ARMXEmitter::*triop)(ARMReg, ARMReg, ARMReg) = NULL;
		switch (op >> 26)
		{
		case 24: //VFPU0
			switch ((op >> 23)&7)
			{
			case 0: // d[i] = s[i] + t[i]; break; //vadd
				triop = &ARMXEmitter::VADD;
				break;
			case 1: // d[i] = s[i] - t[i]; break; //vsub
				triop = &ARMXEmitter::VSUB;
				break;
			case 7: // d[i] = s[i] / t[i]; break; //vdiv
				triop = &ARMXEmitter::VDIV;
				break;
			}
			break;
		case 25: //VFPU1
			switch ((op >> 23)&7)
			{
			case 0: // d[i] = s[i] * t[i]; break; //vmul
				triop = &ARMXEmitter::VMUL;
				break;
			}
			break;
			// Unfortunately there is no VMIN/VMAX on ARM without NEON.
		}

		if (!triop) {
			DISABLE;
		}

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
				fpr.MapRegV(tempregs[i], MAP_DIRTY | MAP_NOINIT);
			} else {
				fpr.SpillLockV(dregs[i]);
				fpr.MapRegV(dregs[i], MAP_DIRTY);
				tempregs[i] = dregs[i];
			}
		}

		for (int i = 0; i < n; i++) {
			fpr.MapInInV(sregs[i], tregs[i]);
			(this->*triop)(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
		}

		for (int i = 0; i < n; i++) {
			if (dregs[i] != tempregs[i]) {
				VMOV(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}
		ApplyPrefixD(dregs, sz);
		
		fpr.ReleaseSpillLocks();
		
		js.EatPrefix();
	}

	void Jit::Comp_VV2Op(u32 op) {
		CONDITIONAL_DISABLE;

		DISABLE;

		if (js.HasUnknownPrefix())
			DISABLE;

		// Pre-processing: Eliminate silly no-op VMOVs, common in Wipeout Pure
		if (((op >> 16) & 0x1f) == 0 && _VS == _VD && js.HasNoPrefix()) {
			return;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		ARMReg tempxregs[4];
		for (int i = 0; i < n; ++i)
		{
			if (!IsOverlapSafeAllowS(dregs[i], i, n, sregs))
			{
				int reg = fpr.GetTempV();
				fpr.MapRegV(reg, MAP_NOINIT | MAP_DIRTY);
				fpr.SpillLockV(reg);
				tempxregs[i] = fpr.V(reg);
			}
			else
			{
				fpr.MapRegV(dregs[i], (dregs[i] == sregs[i] ? 0 : MAP_NOINIT) | MAP_DIRTY);
				fpr.SpillLockV(dregs[i]);
				tempxregs[i] = fpr.V(dregs[i]);
			}
		}

		// Warning: sregs[i] and tempxregs[i] may be the same reg.
		// Helps for vmov, hurts for vrcp, etc.
		for (int i = 0; i < n; ++i)
		{
			fpr.MapRegV(sregs[i]);
			switch ((op >> 16) & 0x1f)
			{
			case 0: // d[i] = s[i]; break; //vmov
				// Probably for swizzle.
				VMOV(tempxregs[i], fpr.V(sregs[i]));
				break;
			case 1: // d[i] = fabsf(s[i]); break; //vabs
				//if (!fpr.V(sregs[i]).IsSimpleReg(tempxregs[i]))
				VABS(tempxregs[i], fpr.V(sregs[i]));
				break;
			case 2: // d[i] = -s[i]; break; //vneg
				VNEG(tempxregs[i], fpr.V(sregs[i]));
				break;
			case 4: // if (s[i] < 0) d[i] = 0; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;    // vsat0
				MOVI2F(S0, 0.5f, R0);
				VABS(S1, fpr.V(sregs[i]));                          // S1 = fabs(x)
				VSUB(tempxregs[i], fpr.V(sregs[i]), S0);     // S2 = fabs(x-0.5f) {VABD}
				VABS(tempxregs[i], tempxregs[i]);
				VSUB(tempxregs[i], S1, fpr.V(tempxregs[i])); // v[i] = S1 - S2 + 0.5f
				VADD(tempxregs[i], tempxregs[i], S0);
				break;
			case 5: // if (s[i] < -1.0f) d[i] = -1.0f; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;  // vsat1
				MOVI2F(S0, 1.0f, R0);
				VABS(S1, fpr.V(sregs[i]));                          // S1 = fabs(x)
				VSUB(tempxregs[i], fpr.V(sregs[i]), S0);     // S2 = fabs(x-1.0f) {VABD}
				VABS(tempxregs[i], tempxregs[i]);
				VSUB(tempxregs[i], S1, tempxregs[i]); // v[i] = S1 - S2
				break;
			case 16: // d[i] = 1.0f / s[i]; break; //vrcp
				MOVI2F(S0, 1.0f, R0);
				VDIV(tempxregs[i], S0, fpr.V(sregs[i]));
				break;
			case 17: // d[i] = 1.0f / sqrtf(s[i]); break; //vrsq
				MOVI2F(S0, 1.0f, R0);
				VSQRT(S1, fpr.V(sregs[i]));
				VDIV(tempxregs[i], S0, S1);
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
				VSQRT(tempxregs[i], fpr.V(sregs[i]));
				VABS(tempxregs[i], tempxregs[i]);
				break;
			case 23: // d[i] = asinf(s[i] * (float)M_2_PI); break; //vasin
				DISABLE;
				break;
			case 24: // d[i] = -1.0f / s[i]; break; // vnrcp
				MOVI2F(S0, -1.0f, R0);
				VDIV(tempxregs[i], S0, fpr.V(sregs[i]));
				break;
			case 26: // d[i] = -sinf((float)M_PI_2 * s[i]); break; // vnsin
				DISABLE;
				break;
			case 28: // d[i] = 1.0f / expf(s[i] * (float)M_LOG2E); break; // vrexp2
				DISABLE;
				break;
			}
		}

		fpr.MapRegsV(dregs, sz, MAP_NOINIT | MAP_DIRTY);
		for (int i = 0; i < n; ++i)
		{
			VMOV(fpr.V(dregs[i]), tempxregs[i]);
		}

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocks();
	}
	
	void Jit::Comp_Mftv(u32 op)
	{
		CONDITIONAL_DISABLE;

		int imm = op & 0xFF;
		int rt = _RT;
		switch ((op >> 21) & 0x1f)
		{
		case 3: //mfv / mfvc
			// rt = 0, imm = 255 appears to be used as a CPU interlock by some games.
			if (rt != 0) {
				if (imm < 128) {  //R(rt) = VI(imm);
					fpr.FlushV(imm);
					gpr.MapReg(rt, MAP_NOINIT | MAP_DIRTY);
					LDR(gpr.R(rt), CTXREG, fpr.GetMipsRegOffsetV(imm));
				} else if (imm < 128 + VFPU_CTRL_MAX) { //mtvc
					DISABLE;
					// In case we have a saved prefix.
					//FlushPrefixV();
					//gpr.BindToRegister(rt, false, true);
					//MOV(32, gpr.R(rt), M(&currentMIPS->vfpuCtrl[imm - 128]));
				} else {
					//ERROR - maybe need to make this value too an "interlock" value?
					_dbg_assert_msg_(CPU,0,"mfv - invalid register");
				}
			}
			break;

		case 7: //mtv
			if (imm < 128) {
				gpr.FlushR(rt);
				fpr.MapRegV(imm, MAP_DIRTY | MAP_NOINIT);
				VLDR(fpr.V(imm), CTXREG, gpr.GetMipsRegOffset(rt));
			} else if (imm < 128 + VFPU_CTRL_MAX) { //mtvc //currentMIPS->vfpuCtrl[imm - 128] = R(rt);
				DISABLE; 
				//gpr.BindToRegister(rt, true, false);
				//MOV(32, M(&currentMIPS->vfpuCtrl[imm - 128]), gpr.R(rt));

				// TODO: Optimization if rt is Imm?
				//if (imm - 128 == VFPU_CTRL_SPREFIX) {
				//js.prefixSFlag = JitState::PREFIX_UNKNOWN;
				//} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
				//	js.prefixTFlag = JitState::PREFIX_UNKNOWN;
				//} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
				//	js.prefixDFlag = JitState::PREFIX_UNKNOWN;
				//}
			} else {
				//ERROR
				_dbg_assert_msg_(CPU,0,"mtv - invalid register");
			}
			break;

		default:
			DISABLE;
		}
	}

	void Jit::Comp_Vmtvc(u32 op) {
		CONDITIONAL_DISABLE;
		DISABLE;

		int vs = _VS;
		int imm = op & 0xFF;
		if (imm >= 128 && imm < 128 + VFPU_CTRL_MAX) {
			fpr.MapRegV(vs, 0);
			ADD(R0, CTXREG, offsetof(MIPSState, vfpuCtrl[0]) + (imm - 128) * 4);
			VSTR(fpr.V(vs), R0, 0);
			fpr.ReleaseSpillLocks();

			if (imm - 128 == VFPU_CTRL_SPREFIX) {
				js.prefixSFlag = ArmJitState::PREFIX_UNKNOWN;
			} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
				js.prefixTFlag = ArmJitState::PREFIX_UNKNOWN;
			} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
				js.prefixDFlag = ArmJitState::PREFIX_UNKNOWN;
			}
		}
	}

	void Jit::Comp_Vmmov(u32 op) {
		CONDITIONAL_DISABLE;

		// TODO: This probably ignores prefixes?
		if (js.MayHavePrefix()) {
			DISABLE;
		}

		if (_VS == _VD) {
			// A lot of these in Wipeout...
			return;	
		}

		MatrixSize sz = GetMtxSize(op);
		int n = GetMatrixSide(sz);

		u8 sregs[16], dregs[16];
		GetMatrixRegs(sregs, sz, _VS);
		GetMatrixRegs(dregs, sz, _VD);

		// Rough overlap check.
		bool overlap = false;
		if (GetMtx(_VS) == GetMtx(_VD)) {
			// Potential overlap (guaranteed for 3x3 or more).
			overlap = true;
		}

		if (overlap) {
			// Not so common, fallback.
			DISABLE;
		} else {
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapDirtyInV(dregs[a * 4 + b], sregs[a * 4 + b]);
					VMOV(fpr.V(dregs[a * 4 + b]), fpr.V(sregs[a * 4 + b]));
				}
			}
			fpr.ReleaseSpillLocks();
		}
	}

	void Jit::Comp_VScl(u32 op) {
		CONDITIONAL_DISABLE;
		DISABLE;
		if (js.HasUnknownPrefix())
			DISABLE;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], dregs[4], scale;
		GetVectorRegsPrefixS(sregs, sz, _VS);
		// TODO: Prefixes seem strange...
		GetVectorRegsPrefixT(&scale, V_Single, _VT);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		// Move to S0 early, so we don't have to worry about overlap with scale.
		fpr.LoadToRegV(S0, scale);

		// For prefixes to work, we just have to ensure that none of the output registers spill
		// and that there's no overlap.
		MIPSReg tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (sregs[i] == dregs[i] || IsOverlapSafe(dregs[i], i, n, sregs)) {
				tempregs[i] = dregs[i];
			} else {
				// Need to use temp regs
				tempregs[i] = fpr.GetTempV();
			}
		}

		// The meat of the function!
		for (int i = 0; i < n; i++) {
			fpr.MapDirtyInV(tempregs[i], sregs[i]);
			fpr.SpillLockV(tempregs[i]);
			VMUL(fpr.V(tempregs[i]), fpr.V(sregs[i]), S0);
		}

		for (int i = 0; i < n; i++) {
			// All must be mapped for prefixes to work.
			fpr.MapRegV(dregs[i], MAP_DIRTY | MAP_NOINIT);
			if (tempregs[i] != dregs[i]) {
				VMOV(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocks();
	}

	void Jit::Comp_Vmmul(u32 op) {
		CONDITIONAL_DISABLE;

		// TODO: This probably ignores prefixes?
		if (js.MayHavePrefix()) {
			DISABLE;
		}

		MatrixSize sz = GetMtxSize(op);
		int n = GetMatrixSide(sz);

		u8 sregs[16], tregs[16], dregs[16];
		GetMatrixRegs(sregs, sz, _VS);
		GetMatrixRegs(tregs, sz, _VT);
		GetMatrixRegs(dregs, sz, _VD);

		// Rough overlap check.
		bool overlap = false;
		if (GetMtx(_VS) == GetMtx(_VD) || GetMtx(_VT) == GetMtx(_VD)) {
			// Potential overlap (guaranteed for 3x3 or more).
			overlap = true;
		}

		if (overlap) {
			DISABLE;
		} else {
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapInInV(sregs[b * 4], tregs[a * 4]);
					VMUL(S0, fpr.V(sregs[b * 4]), fpr.V(tregs[a * 4]));
					for (int c = 1; c < n; c++) {
						fpr.MapInInV(sregs[b * 4 + c], tregs[a * 4 + c]);
						VMLA(S0, fpr.V(sregs[b * 4 + c]), fpr.V(tregs[a * 4 + c]));
					}
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					VMOV(fpr.V(dregs[a * 4 + b]), S0);
				}
			}
			fpr.ReleaseSpillLocks();
		}
	}

	void Jit::Comp_Vmscl(u32 op) {
		DISABLE;
	}

	void Jit::Comp_Vtfm(u32 op) {
		CONDITIONAL_DISABLE;

		// TODO: This probably ignores prefixes?  Or maybe uses D?
		if (js.MayHavePrefix())
			DISABLE;

		VectorSize sz = GetVecSize(op);
		MatrixSize msz = GetMtxSize(op);
		int n = GetNumVectorElements(sz);
		int ins = (op >> 23) & 7;

		bool homogenous = false;
		if (n == ins)
		{
			n++;
			sz = (VectorSize)((int)(sz) + 1);
			msz = (MatrixSize)((int)(msz) + 1);
			homogenous = true;
		}
		// Otherwise, n should already be ins + 1.
		else if (n != ins + 1) {
			DISABLE;
		}

		u8 sregs[16], dregs[4], tregs[4];
		GetMatrixRegs(sregs, msz, _VS);
		GetVectorRegs(tregs, sz, _VT);
		GetVectorRegs(dregs, sz, _VD);

		// TODO: test overlap, optimize.
		int tempregs[4];
		for (int i = 0; i < n; i++) {
			fpr.MapInInV(sregs[i * 4], tregs[0]);
			VMUL(S0, fpr.V(sregs[i * 4]), fpr.V(tregs[0]));
			for (int k = 1; k < n; k++) {
				if (!homogenous || k != n - 1) {
					fpr.MapInInV(sregs[i * 4 + k], tregs[k]);
					VMLA(S0, fpr.V(sregs[i * 4 + k]), fpr.V(tregs[k]));
				} else {
					fpr.MapRegV(sregs[i * 4 + k]);
					VADD(S0, S0, fpr.V(sregs[i * 4 + k]));
				}
			}

			int temp = fpr.GetTempV();
			fpr.MapRegV(temp, MAP_NOINIT | MAP_DIRTY);
			fpr.SpillLockV(temp);
			VMOV(fpr.V(temp), S0);
			tempregs[i] = temp;
		}
		for (int i = 0; i < n; i++) {
			u8 temp = tempregs[i];
			fpr.MapRegV(dregs[i], MAP_NOINIT | MAP_DIRTY);
			VMOV(fpr.V(dregs[i]), fpr.V(temp));
		}

		fpr.ReleaseSpillLocks();
	}

	void Jit::Comp_VHdp(u32 op) {
		DISABLE;
	}

	void Jit::Comp_VCrs(u32 op) {
		DISABLE;
	}

	void Jit::Comp_VDet(u32 op) {
		DISABLE;
	}

	void Jit::Comp_Vi2x(u32 op) {
		DISABLE;
	}

	void Jit::Comp_Vx2i(u32 op) {
		DISABLE;
	}

	void Jit::Comp_Vf2i(u32 op) {
		DISABLE;
	}

	void Jit::Comp_Vi2f(u32 op) {
		DISABLE;
	}

	void Jit::Comp_Vcst(u32 op) {
		CONDITIONAL_DISABLE;

		if (js.HasUnknownPrefix())
			DISABLE;

		// TODO: support prefixes properly
		if (js.MayHavePrefix())
			DISABLE;

		int conNum = (op >> 16) & 0x1f;
		int vd = _VD;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, _VD);
		fpr.MapRegsV(dregs, sz, MAP_NOINIT | MAP_DIRTY);

		MOVI2R(R0, (u32)(void *)&cst_constants[conNum]);
		VLDR(S0, R0, 0);
		for (int i = 0; i < n; ++i)
			VMOV(fpr.V(dregs[i]), S0);

		ApplyPrefixD(dregs, sz);
		fpr.ReleaseSpillLocks();
	}

	void Jit::Comp_Vhoriz(u32 op) {
		DISABLE;
	}

	static float sincostemp[2];

	void SinCos(float angle) {
#ifndef M_PI_2
#define M_PI_2     1.57079632679489661923
#endif
		angle *= (float)M_PI_2;
		sincostemp[0] = sinf(angle);
		sincostemp[1] = cosf(angle);
	}

	// sincosf is unavailable in the Android NDK:
	// https://code.google.com/p/android/issues/detail?id=38423
	void SinCosNegSin(float angle) {
#ifndef M_PI_2
#define M_PI_2     1.57079632679489661923
#endif
		angle *= (float)M_PI_2;
		sincostemp[0] = -sinf(angle);
		sincostemp[1] = cosf(angle);
	}

	// Very heavily used by FF:CC
	void Jit::Comp_VRot(u32 op) {
		// Not sure about the ABI so I disable on non-Android.
#if !defined(ARMV7) || !defined(ANDROID)
		DISABLE;
#endif 
		CONDITIONAL_DISABLE;

		int vd = _VD;
		int vs = _VS;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 dregs[4];
		u8 sreg;
		GetVectorRegs(dregs, sz, vd);
		GetVectorRegs(&sreg, V_Single, vs);

		int imm = (op >> 16) & 0x1f;

		gpr.FlushBeforeCall();
		fpr.FlushAll();

		bool negSin = (imm & 0x10) ? true : false;

		fpr.MapRegV(sreg);
		// Silly Android calling conventions, not passing arguments in float regs! (no hardfloat!)
		// We should write a custom pure-asm function instead.
		VMOV(R0, fpr.V(sreg));
		QuickCallFunction(R1, negSin ? (void *)&SinCosNegSin : (void *)&SinCos);
		MOVI2R(R0, (u32)(&sincostemp[0]));
		VLDR(S0, R0, 0);
		VLDR(S1, R0, 4);

		char what[4] = {'0', '0', '0', '0'};
		if (((imm >> 2) & 3) == (imm & 3)) {
			for (int i = 0; i < 4; i++)
				what[i] = 'S';
		}
		what[(imm >> 2) & 3] = 'S';
		what[imm & 3] = 'C';

		for (int i = 0; i < n; i++) {
			fpr.MapRegV(dregs[i], MAP_DIRTY | MAP_NOINIT);
			fpr.SpillLockV(dregs[i]);
			switch (what[i]) {
			case 'C': VMOV(fpr.V(dregs[i]), S1); break;
			case 'S': VMOV(fpr.V(dregs[i]), S0); break;
			case '0':
				{
					MOVI2F(fpr.V(dregs[i]), 0.0f, R0);
					break;
				}
			default:
				ERROR_LOG(HLE, "Bad what in vrot");
				break;
			}
		}

		fpr.ReleaseSpillLocks();
	}
}
