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
#if PPSSPP_ARCH(ARM)

#include <cmath>
#include "Common/CPUDetect.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Math/math_util.h"

#include "Core/Compatibility.h"
#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"

#include "Core/MIPS/ARM/ArmJit.h"
#include "Core/MIPS/ARM/ArmRegCache.h"

// Cool NEON references:
// http://www.delmarnorth.com/microwave/requirements/neon-test-tutorial.pdf

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE { fpr.ReleaseSpillLocksAndDiscardTemps(); Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE(flag) if (jo.Disabled(JitDisable::flag)) { Comp_Generic(op); return; }
#define DISABLE { fpr.ReleaseSpillLocksAndDiscardTemps(); Comp_Generic(op); return; }

#define NEON_IF_AVAILABLE(func) { if (jo.useNEONVFPU) { func(op); return; } }
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
	using namespace ArmGen;
	using namespace ArmJitConstants;

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

	void ArmJit::Comp_VPFX(MIPSOpcode op)
	{
		CONDITIONAL_DISABLE(VFPU_XFER);
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
			js.prefixD = data & 0x00000FFF;
			js.prefixDFlag = JitState::PREFIX_KNOWN_DIRTY;
			break;
		default:
			ERROR_LOG(Log::CPU, "VPFX - bad regnum %i : data=%08x", regnum, data);
			break;
		}
	}

	void ArmJit::ApplyPrefixST(u8 *vregs, u32 prefix, VectorSize sz) {
		if (prefix == 0xE4)
			return;

		int n = GetNumVectorElements(sz);
		u8 origV[4];
		static const float constantArray[8] = {0.f, 1.f, 2.f, 0.5f, 3.f, 1.f/3.f, 0.25f, 1.f/6.f};

		for (int i = 0; i < n; i++)
			origV[i] = vregs[i];

		for (int i = 0; i < n; i++) {
			int regnum = (prefix >> (i*2)) & 3;
			int abs    = (prefix >> (8+i)) & 1;
			int negate = (prefix >> (16+i)) & 1;
			int constants = (prefix >> (12+i)) & 1;

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
					WARN_LOG(Log::CPU, "JIT: Invalid VFPU swizzle: %08x : %d / %d at PC = %08x (%s)", prefix, regnum, n, GetCompilerPC(), MIPSDisasmAt(GetCompilerPC()).c_str());
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
				fpr.MapRegV(vregs[i], MAP_DIRTY | MAP_NOINIT);
				fpr.SpillLockV(vregs[i]);
				MOVI2F(fpr.V(vregs[i]), constantArray[regnum + (abs<<2)], SCRATCHREG1, negate != 0);
			}
		}
	}

	void ArmJit::GetVectorRegsPrefixD(u8 *regs, VectorSize sz, int vectorReg) {
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

	void ArmJit::ApplyPrefixD(const u8 *vregs, VectorSize sz) {
		_assert_(js.prefixDFlag & JitState::PREFIX_KNOWN);
		if (!js.prefixD)
			return;

		int n = GetNumVectorElements(sz);
		for (int i = 0; i < n; i++) 	{
			if (js.VfpuWriteMask(i))
				continue;

			int sat = (js.prefixD >> (i * 2)) & 3;
			if (sat == 1) {
				// clamped = x < 0 ? (x > 1 ? 1 : x) : x [0, 1]
				fpr.MapRegV(vregs[i], MAP_DIRTY);
				
				MOVI2F(S0, 0.0f, SCRATCHREG1);
				MOVI2F(S1, 1.0f, SCRATCHREG1);
				VCMP(fpr.V(vregs[i]), S0);
				VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
				SetCC(CC_LS);
				VMOV(fpr.V(vregs[i]), S0);
				SetCC(CC_AL);
				VCMP(fpr.V(vregs[i]), S1);
				VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
				SetCC(CC_GT);
				VMOV(fpr.V(vregs[i]), S1);
				SetCC(CC_AL);
			} else if (sat == 3) {
				// clamped = x < -1 ? (x > 1 ? 1 : x) : x [-1, 1]
				fpr.MapRegV(vregs[i], MAP_DIRTY);

				MOVI2F(S0, -1.0f, SCRATCHREG1);
				MOVI2F(S1, 1.0f, SCRATCHREG1);
				VCMP(fpr.V(vregs[i]), S0);
				VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
				SetCC(CC_LO);
				VMOV(fpr.V(vregs[i]), S0);
				SetCC(CC_AL);
				VCMP(fpr.V(vregs[i]), S1);
				VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
				SetCC(CC_GT);
				VMOV(fpr.V(vregs[i]), S1);
				SetCC(CC_AL);
			}
		}
	}

	void ArmJit::Comp_SV(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_SV);
		CONDITIONAL_DISABLE(LSU_VFPU);
		CheckMemoryBreakpoint();

		s32 offset = (signed short)(op & 0xFFFC);
		int vt = ((op >> 16) & 0x1f) | ((op & 3) << 5);
		MIPSGPReg rs = _RS;

		bool doCheck = false;
		switch (op >> 26)
		{
		case 50: //lv.s  // VI(vt) = Memory::Read_U32(addr);
			{
				if (!gpr.IsImm(rs) && jo.cachePointers && g_Config.bFastMemory && (offset & 3) == 0 && offset < 0x400 && offset > -0x400) {
					gpr.MapRegAsPointer(rs);
					fpr.MapRegV(vt, MAP_NOINIT | MAP_DIRTY);
					VLDR(fpr.V(vt), gpr.RPtr(rs), offset);
					break;
				}

				// CC might be set by slow path below, so load regs first.
				fpr.MapRegV(vt, MAP_DIRTY | MAP_NOINIT);
				if (gpr.IsImm(rs)) {
					u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
					gpr.SetRegImm(R0, addr + (u32)Memory::base);
				} else {
					gpr.MapReg(rs);
					if (g_Config.bFastMemory) {
						SetR0ToEffectiveAddress(rs, offset);
					} else {
						SetCCAndR0ForSafeAddress(rs, offset, SCRATCHREG2);
						doCheck = true;
					}
					ADD(R0, R0, MEMBASEREG);
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
					MOVI2F(fpr.V(vt), 0.0f, SCRATCHREG1);
					SetCC(CC_AL);
				}
#endif
			}
			break;

		case 58: //sv.s   // Memory::Write_U32(VI(vt), addr);
			{
				if (!gpr.IsImm(rs) && jo.cachePointers && g_Config.bFastMemory && (offset & 3) == 0 && offset < 0x400 && offset > -0x400) {
					gpr.MapRegAsPointer(rs);
					fpr.MapRegV(vt, 0);
					VSTR(fpr.V(vt), gpr.RPtr(rs), offset);
					break;
				}

				// CC might be set by slow path below, so load regs first.
				fpr.MapRegV(vt);
				if (gpr.IsImm(rs)) {
					u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
					gpr.SetRegImm(R0, addr + (u32)Memory::base);
				} else {
					gpr.MapReg(rs);
					if (g_Config.bFastMemory) {
						SetR0ToEffectiveAddress(rs, offset);
					} else {
						SetCCAndR0ForSafeAddress(rs, offset, SCRATCHREG2);
						doCheck = true;
					}
					ADD(R0, R0, MEMBASEREG);
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

	void ArmJit::Comp_SVQ(MIPSOpcode op)
	{
		NEON_IF_AVAILABLE(CompNEON_SVQ);
		CONDITIONAL_DISABLE(LSU_VFPU);
		CheckMemoryBreakpoint();

		int imm = (signed short)(op&0xFFFC);
		int vt = (((op >> 16) & 0x1f)) | ((op&1) << 5);
		MIPSGPReg rs = _RS;

		bool doCheck = false;
		switch (op >> 26)
		{
		case 54: //lv.q
			{
				// CC might be set by slow path below, so load regs first.
				u8 vregs[4];
				GetVectorRegs(vregs, V_Quad, vt);
				fpr.MapRegsAndSpillLockV(vregs, V_Quad, MAP_DIRTY | MAP_NOINIT);

				if (gpr.IsImm(rs)) {
					u32 addr = (imm + gpr.GetImm(rs)) & 0x3FFFFFFF;
					gpr.SetRegImm(R0, addr + (u32)Memory::base);
				} else {
					gpr.MapReg(rs);
					if (g_Config.bFastMemory) {
						SetR0ToEffectiveAddress(rs, imm);
					} else {
						SetCCAndR0ForSafeAddress(rs, imm, SCRATCHREG2);
						doCheck = true;
					}
					ADD(R0, R0, MEMBASEREG);
				}

#ifdef __ARM_ARCH_7S__
				FixupBranch skip;
				if (doCheck) {
					skip = B_CC(CC_EQ);
				}

				bool consecutive = true;
				for (int i = 0; i < 3 && consecutive; i++)
					if ((fpr.V(vregs[i]) + 1) != fpr.V(vregs[i+1]))
						consecutive = false;
				if (consecutive) {
					VLDMIA(R0, false, fpr.V(vregs[0]), 4);
				} else {
					for (int i = 0; i < 4; i++)
						VLDR(fpr.V(vregs[i]), R0, i * 4);
				}

				if (doCheck) {
					SetJumpTarget(skip);
					SetCC(CC_AL);
				}
#else
				bool consecutive = true;
				for (int i = 0; i < 3 && consecutive; i++)
					if ((fpr.V(vregs[i]) + 1) != fpr.V(vregs[i+1]))
						consecutive = false;
				if (consecutive) {
					VLDMIA(R0, false, fpr.V(vregs[0]), 4);
				} else {
					for (int i = 0; i < 4; i++)
						VLDR(fpr.V(vregs[i]), R0, i * 4);
				}

				if (doCheck) {
					SetCC(CC_EQ);
					MOVI2R(SCRATCHREG1, 0);
					for (int i = 0; i < 4; i++)
						VMOV(fpr.V(vregs[i]), SCRATCHREG1);
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
				fpr.MapRegsAndSpillLockV(vregs, V_Quad, 0);

				if (gpr.IsImm(rs)) {
					u32 addr = (imm + gpr.GetImm(rs)) & 0x3FFFFFFF;
					gpr.SetRegImm(R0, addr + (u32)Memory::base);
				} else {
					gpr.MapReg(rs);
					if (g_Config.bFastMemory) {
						SetR0ToEffectiveAddress(rs, imm);
					} else {
						SetCCAndR0ForSafeAddress(rs, imm, SCRATCHREG2);
						doCheck = true;
					}
					ADD(R0, R0, MEMBASEREG);
				}

#ifdef __ARM_ARCH_7S__
				FixupBranch skip;
				if (doCheck) {
					skip = B_CC(CC_EQ);
				}

				bool consecutive = true;
				for (int i = 0; i < 3 && consecutive; i++)
					if ((fpr.V(vregs[i]) + 1) != fpr.V(vregs[i+1]))
						consecutive = false;
				if (consecutive) {
					VSTMIA(R0, false, fpr.V(vregs[0]), 4);
				} else {
					for (int i = 0; i < 4; i++)
						VSTR(fpr.V(vregs[i]), R0, i * 4);
				}

				if (doCheck) {
					SetJumpTarget(skip);
					SetCC(CC_AL);
				}
#else
				bool consecutive = true;
				for (int i = 0; i < 3 && consecutive; i++)
					if ((fpr.V(vregs[i]) + 1) != fpr.V(vregs[i+1]))
						consecutive = false;
				if (consecutive) {
					VSTMIA(R0, false, fpr.V(vregs[0]), 4);
				} else {
					for (int i = 0; i < 4; i++)
						VSTR(fpr.V(vregs[i]), R0, i * 4);
				}

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
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_VVectorInit(MIPSOpcode op)
	{
		NEON_IF_AVAILABLE(CompNEON_VVectorInit);
		CONDITIONAL_DISABLE(VFPU_XFER);
		// WARNING: No prefix support!
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		switch ((op >> 16) & 0xF)
		{
		case 6: // v=zeros; break;  //vzero
			MOVI2F(S0, 0.0f, SCRATCHREG1);
			break;
		case 7: // v=ones; break;   //vone
			MOVI2F(S0, 1.0f, SCRATCHREG1);
			break;
		default:
			DISABLE;
			break;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, _VD);
		fpr.MapRegsAndSpillLockV(dregs, sz, MAP_NOINIT | MAP_DIRTY);

		for (int i = 0; i < n; ++i)
			VMOV(fpr.V(dregs[i]), S0);

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_VIdt(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_VIdt);

		CONDITIONAL_DISABLE(VFPU_XFER);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		int vd = _VD;
		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);
		MOVI2F(S0, 0.0f, SCRATCHREG1);
		MOVI2F(S1, 1.0f, SCRATCHREG1);
		u8 dregs[4];
		GetVectorRegsPrefixD(dregs, sz, _VD);
		fpr.MapRegsAndSpillLockV(dregs, sz, MAP_NOINIT | MAP_DIRTY);
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
			_dbg_assert_msg_(false,"Trying to interpret instruction that can't be interpreted");
			break;
		}
		
		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_VMatrixInit(MIPSOpcode op)
	{
		NEON_IF_AVAILABLE(CompNEON_VMatrixInit);
		CONDITIONAL_DISABLE(VFPU_XFER);
		if (js.HasUnknownPrefix()) {
			// Don't think matrix init ops care about prefixes.
			// DISABLE;
		}

		MatrixSize sz = GetMtxSize(op);
		int n = GetMatrixSide(sz);

		u8 dregs[16];
		GetMatrixRegs(dregs, sz, _VD);

		switch ((op >> 16) & 0xF) {
		case 3: // vmidt
			MOVI2F(S0, 0.0f, SCRATCHREG1);
			MOVI2F(S1, 1.0f, SCRATCHREG1);
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					VMOV(fpr.V(dregs[a * 4 + b]), a == b ? S1 : S0);
				}
			}
			break;
		case 6: // vmzero
			MOVI2F(S0, 0.0f, SCRATCHREG1);
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					VMOV(fpr.V(dregs[a * 4 + b]), S0);
				}
			}
			break;
		case 7: // vmone
			MOVI2F(S1, 1.0f, SCRATCHREG1);
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapRegV(dregs[a * 4 + b], MAP_DIRTY | MAP_NOINIT);
					VMOV(fpr.V(dregs[a * 4 + b]), S1);
				}
			}
			break;
		}

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_VHdp(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_VHdp);
		CONDITIONAL_DISABLE(VFPU_VEC);
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
		VMUL(S0, fpr.V(sregs[0]), fpr.V(tregs[0]));

		int n = GetNumVectorElements(sz);
		for (int i = 1; i < n; i++) {
			// sum += s[i]*t[i];
			if (i == n - 1) {
				VADD(S0, S0, fpr.V(tregs[i]));
			} else {
				VMLA(S0, fpr.V(sregs[i]), fpr.V(tregs[i]));
			}
		}
		fpr.ReleaseSpillLocksAndDiscardTemps();

		fpr.MapRegV(dregs[0], MAP_NOINIT | MAP_DIRTY);

		VMOV(fpr.V(dregs[0]), S0);
		ApplyPrefixD(dregs, V_Single);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	alignas(16) static const float vavg_table[4] = { 1.0f, 1.0f / 2.0f, 1.0f / 3.0f, 1.0f / 4.0f };

	void ArmJit::Comp_Vhoriz(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vhoriz);
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		int vd = _VD;
		int vs = _VS;
		int vt = _VT;
		VectorSize sz = GetVecSize(op);

		// TODO: Force read one of them into regs? probably not.
		u8 sregs[4], dregs[1];
		GetVectorRegsPrefixS(sregs, sz, vs);
		GetVectorRegsPrefixD(dregs, V_Single, vd);

		// TODO: applyprefixST here somehow (shuffle, etc...)
		fpr.MapRegsAndSpillLockV(sregs, sz, 0);

		int n = GetNumVectorElements(sz);

		bool is_vavg = ((op >> 16) & 0x1f) == 7;
		if (is_vavg) {
			MOVI2F(S1, vavg_table[n - 1], R0);
		}
		// Have to start at +0.000 for the correct sign.
		MOVI2F(S0, 0.0f, SCRATCHREG1);
		for (int i = 0; i < n; i++) {
			// sum += s[i];
			VADD(S0, S0, fpr.V(sregs[i]));
		}

		fpr.MapRegV(dregs[0], MAP_NOINIT | MAP_DIRTY);
		if (is_vavg) {
			VMUL(fpr.V(dregs[0]), S0, S1);
		} else {
			VMOV(fpr.V(dregs[0]), S0);
		}
		ApplyPrefixD(dregs, V_Single);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_VDot(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_VDot);
		CONDITIONAL_DISABLE(VFPU_VEC);
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
		VMUL(S0, fpr.V(sregs[0]), fpr.V(tregs[0]));

		int n = GetNumVectorElements(sz);
		for (int i = 1; i < n; i++) {
			// sum += s[i]*t[i];
			VMLA(S0, fpr.V(sregs[i]), fpr.V(tregs[i]));
		}
		fpr.ReleaseSpillLocksAndDiscardTemps();

		fpr.MapRegV(dregs[0], MAP_NOINIT | MAP_DIRTY);

		VMOV(fpr.V(dregs[0]), S0);
		ApplyPrefixD(dregs, V_Single);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_VecDo3(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_VecDo3);
		CONDITIONAL_DISABLE(VFPU_VEC);
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
				switch ((op >> 23)&7) {
				case 0: // d[i] = s[i] + t[i]; break; //vadd
					VADD(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				case 1: // d[i] = s[i] - t[i]; break; //vsub
					VSUB(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				case 7: // d[i] = s[i] / t[i]; break; //vdiv
					VDIV(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				default:
					DISABLE;
				}
				break;
			case 25: //VFPU1
				switch ((op >> 23) & 7) {
				case 0: // d[i] = s[i] * t[i]; break; //vmul
					VMUL(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					break;
				default:
					DISABLE;
				}
				break;
				// Unfortunately there is no VMIN/VMAX on ARM without NEON.
			case 27: //VFPU3
				switch ((op >> 23) & 7)	{
				case 2:  // vmin
				{
					VCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
					VMRS_APSR();
					FixupBranch skipNAN = B_CC(CC_VC);
					VMOV(SCRATCHREG1, fpr.V(sregs[i]));
					VMOV(SCRATCHREG2, fpr.V(tregs[i]));
					// If both are negative, we reverse the comparison.  We want the highest mantissa then.
					// Also, between -NAN and -5.0, we want -NAN to be less.
					TST(SCRATCHREG1, SCRATCHREG2);
					FixupBranch cmpPositive = B_CC(CC_PL);
					CMP(SCRATCHREG2, SCRATCHREG1);
					FixupBranch skipPositive = B();
					SetJumpTarget(cmpPositive);
					CMP(SCRATCHREG1, SCRATCHREG2);
					SetJumpTarget(skipPositive);
					SetCC(CC_AL);
					SetJumpTarget(skipNAN);
					SetCC(CC_LT);
					VMOV(fpr.V(tempregs[i]), fpr.V(sregs[i]));
					SetCC(CC_GE);
					VMOV(fpr.V(tempregs[i]), fpr.V(tregs[i]));
					SetCC(CC_AL);
					break;
				}
				case 3:  // vmax
				{
					VCMP(fpr.V(tregs[i]), fpr.V(sregs[i]));
					VMRS_APSR();
					FixupBranch skipNAN = B_CC(CC_VC);
					VMOV(SCRATCHREG1, fpr.V(sregs[i]));
					VMOV(SCRATCHREG2, fpr.V(tregs[i]));
					// If both are negative, we reverse the comparison.  We want the lowest mantissa then.
					// Also, between -NAN and -5.0, we want -5.0 to be greater.
					TST(SCRATCHREG2, SCRATCHREG1);
					FixupBranch cmpPositive = B_CC(CC_PL);
					CMP(SCRATCHREG1, SCRATCHREG2);
					FixupBranch skipPositive = B();
					SetJumpTarget(cmpPositive);
					CMP(SCRATCHREG2, SCRATCHREG1);
					SetJumpTarget(skipPositive);
					SetCC(CC_AL);
					SetJumpTarget(skipNAN);
					SetCC(CC_LT);
					VMOV(fpr.V(tempregs[i]), fpr.V(sregs[i]));
					SetCC(CC_GE);
					VMOV(fpr.V(tempregs[i]), fpr.V(tregs[i]));
					SetCC(CC_AL);
					break;
				}
				case 6:  // vsge
					DISABLE;  // pending testing
					VCMP(fpr.V(tregs[i]), fpr.V(sregs[i]));
					VMRS_APSR();
					// Unordered is always 0.
					SetCC(CC_GE);
					MOVI2F(fpr.V(tempregs[i]), 1.0f, SCRATCHREG1);
					SetCC(CC_LT);
					MOVI2F(fpr.V(tempregs[i]), 0.0f, SCRATCHREG1);
					SetCC(CC_AL);
					break;
				case 7:  // vslt
					DISABLE;  // pending testing
					VCMP(fpr.V(tregs[i]), fpr.V(sregs[i]));
					VMRS_APSR();
					// Unordered is always 0.
					SetCC(CC_LO);
					MOVI2F(fpr.V(tempregs[i]), 1.0f, SCRATCHREG1);
					SetCC(CC_HS);
					MOVI2F(fpr.V(tempregs[i]), 0.0f, SCRATCHREG1);
					SetCC(CC_AL);
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
				VMOV(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}
		ApplyPrefixD(dregs, sz);
		
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_VV2Op(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_VV2Op);
		CONDITIONAL_DISABLE(VFPU_VEC);
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

		// Get some extra temps, used by vasin only. 
		ARMReg t2 = INVALID_REG, t3 = INVALID_REG, t4 = INVALID_REG;
		if (((op >> 16) & 0x1f) == 23) {
			// Only get here on vasin.
			int t[3] = { fpr.GetTempV(), fpr.GetTempV(), fpr.GetTempV() };
			fpr.MapRegV(t[0], MAP_NOINIT);
			fpr.MapRegV(t[1], MAP_NOINIT);
			fpr.MapRegV(t[2], MAP_NOINIT);
			t2 = fpr.V(t[0]);
			t3 = fpr.V(t[1]);
			t4 = fpr.V(t[2]);
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
				VMOV(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				break;
			case 1: // d[i] = fabsf(s[i]); break; //vabs
				VABS(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				break;
			case 2: // d[i] = -s[i]; break; //vneg
				VNEG(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				break;
			case 4: // if (s[i] < 0) d[i] = 0; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;    // vsat0
				if (i == 0) {
					MOVI2F(S0, 0.0f, SCRATCHREG1);
					MOVI2F(S1, 1.0f, SCRATCHREG1);
				}
				VCMP(fpr.V(sregs[i]), S0);
				VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
				VMOV(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				SetCC(CC_LS);
				VMOV(fpr.V(tempregs[i]), S0);
				SetCC(CC_AL);
				VCMP(fpr.V(sregs[i]), S1);
				VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
				SetCC(CC_GT);
				VMOV(fpr.V(tempregs[i]), S1);
				SetCC(CC_AL);
				break;
			case 5: // if (s[i] < -1.0f) d[i] = -1.0f; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;  // vsat1
				if (i == 0) {
					MOVI2F(S0, -1.0f, SCRATCHREG1);
					MOVI2F(S1, 1.0f, SCRATCHREG1);
				}
				VCMP(fpr.V(sregs[i]), S0);
				VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
				VMOV(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				SetCC(CC_LO);
				VMOV(fpr.V(tempregs[i]), S0);
				SetCC(CC_AL);
				VCMP(fpr.V(sregs[i]), S1);
				VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
				SetCC(CC_GT);
				VMOV(fpr.V(tempregs[i]), S1);
				SetCC(CC_AL);
				break;
			case 16: // d[i] = 1.0f / s[i]; break; //vrcp
				if (i == 0) {
					MOVI2F(S0, 1.0f, SCRATCHREG1);
				}
				VDIV(fpr.V(tempregs[i]), S0, fpr.V(sregs[i]));
				break;
			case 17: // d[i] = 1.0f / sqrtf(s[i]); break; //vrsq
				if (i == 0) {
					MOVI2F(S0, 1.0f, SCRATCHREG1);
				}
				VSQRT(S1, fpr.V(sregs[i]));
				VDIV(fpr.V(tempregs[i]), S0, S1);
				break;
			case 22: // d[i] = sqrtf(s[i]); break; //vsqrt
				VSQRT(fpr.V(tempregs[i]), fpr.V(sregs[i]));
				VABS(fpr.V(tempregs[i]), fpr.V(tempregs[i]));
				break;
			case 23: // d[i] = asinf(s[i] * (float)M_2_PI); break; //vasin
				// Seems to work well enough but can disable if it becomes a problem.
				// Should be easy enough to translate to NEON. There we can load all the constants
				// in one go of course.
				VCMP(fpr.V(sregs[i]));       // flags = sign(sregs[i])
				VMRS_APSR();
				MOVI2F(S0, 1.0f, SCRATCHREG1);
				VABS(t4, fpr.V(sregs[i]));   // t4 = |sregs[i]|
				VSUB(t3, S0, t4);
				VSQRT(t3, t3);               // t3 = sqrt(1 - |sregs[i]|)
				MOVI2F(S1, -0.0187293f, SCRATCHREG1);
				MOVI2F(t2, 0.0742610f, SCRATCHREG1);
				VMLA(t2, t4, S1);
				MOVI2F(S1, -0.2121144f, SCRATCHREG1);
				VMLA(S1, t4, t2);
				MOVI2F(t2, 1.5707288f, SCRATCHREG1);
				VMLA(t2, t4, S1);
				MOVI2F(fpr.V(tempregs[i]), M_PI / 2, SCRATCHREG1);
				VMLS(fpr.V(tempregs[i]), t2, t3);    // tr[i] = M_PI / 2 - t2 * t3
				{
					FixupBranch br = B_CC(CC_GE);
					VNEG(fpr.V(tempregs[i]), fpr.V(tempregs[i]));
					SetJumpTarget(br);
				}
				// Correction factor for PSP range. Could be baked into the calculation above?
				MOVI2F(S1, 1.0f / (M_PI / 2), SCRATCHREG1);
				VMUL(fpr.V(tempregs[i]), fpr.V(tempregs[i]), S1);
				break;
			case 24: // d[i] = -1.0f / s[i]; break; // vnrcp
				if (i == 0) {
					MOVI2F(S0, -1.0f, SCRATCHREG1);
				}
				VDIV(fpr.V(tempregs[i]), S0, fpr.V(sregs[i]));
				break;
			default:
				ERROR_LOG(Log::JIT, "case missing in vfpu vv2op");
				DISABLE;
				break;
			}
		}

		for (int i = 0; i < n; ++i) {
			if (dregs[i] != tempregs[i]) {
				fpr.MapDirtyInV(dregs[i], tempregs[i]);
				VMOV(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_Vi2f(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vi2f);
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		int imm = (op >> 16) & 0x1f;
		const float mult = 1.0f / (float)(1UL << imm);

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

		if (mult != 1.0f)
			MOVI2F(S0, mult, SCRATCHREG1);

		for (int i = 0; i < n; i++) {
			fpr.MapDirtyInV(tempregs[i], sregs[i]);
			VCVT(fpr.V(tempregs[i]), fpr.V(sregs[i]), TO_FLOAT | IS_SIGNED);
			if (mult != 1.0f) 
				VMUL(fpr.V(tempregs[i]), fpr.V(tempregs[i]), S0);
		}

		for (int i = 0; i < n; ++i) {
			if (dregs[i] != tempregs[i]) {
				fpr.MapDirtyInV(dregs[i], tempregs[i]);
				VMOV(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}

		ApplyPrefixD(dregs, sz);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_Vh2f(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vh2f);
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		// This multi-VCVT.F32.F16 is only available in the VFPv4 extension.
		// The VFPv3 one is VCVTB, VCVTT which we don't yet have support for.
		if (!(cpu_info.bHalf && cpu_info.bVFPv4)) {
			// No hardware support for half-to-float, fallback to interpreter
			// TODO: Translate the fast SSE solution to standard integer/VFP stuff
			// for the weaker CPUs.
			DISABLE;
		}

		u8 sregs[4], dregs[4];
		VectorSize sz = GetVecSize(op);
		VectorSize outSz;

		switch (sz) {
		case V_Single:
			outSz = V_Pair;
			break;
		case V_Pair:
			outSz = V_Quad;
			break;
		default:
			DISABLE;
		}

		int n = GetNumVectorElements(sz);
		int nOut = n * 2;
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, outSz, _VD);

		static const ARMReg tmp[4] = { S0, S1, S2, S3 };

		for (int i = 0; i < n; i++) {
			fpr.MapRegV(sregs[i], sz);
			VMOV(tmp[i], fpr.V(sregs[i]));
		}

		// This always converts four 16-bit floats in D0 to four 32-bit floats
		// in Q0. If we are dealing with a pair here, we just ignore the upper two outputs.
		// There are also a couple of other instructions that do it one at a time but doesn't
		// seem worth the trouble.
		VCVTF32F16(Q0, D0);

		for (int i = 0; i < nOut; i++) {
			fpr.MapRegV(dregs[i], MAP_DIRTY | MAP_NOINIT);
			VMOV(fpr.V(dregs[i]), tmp[i]);
		}

		ApplyPrefixD(dregs, sz);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_Vf2i(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vf2i);
		CONDITIONAL_DISABLE(VFPU_VEC);

		if (js.HasUnknownPrefix()) {
			DISABLE;
		}
		DISABLE;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		int imm = (op >> 16) & 0x1f;
		float mult = (float)(1ULL << imm);

		switch ((op >> 21) & 0x1f)
		{
		case 17:
			break; //z - truncate. Easy to support.
		case 16:
		case 18:
		case 19:
			DISABLE;
			break;
		}

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

		if (mult != 1.0f)
			MOVI2F(S1, mult, SCRATCHREG1);

		for (int i = 0; i < n; i++) {
			fpr.MapDirtyInV(tempregs[i], sregs[i]);
			switch ((op >> 21) & 0x1f) {
			case 16: /* TODO */ break; //n
			case 17:
				if (mult != 1.0f) {
					VMUL(S0, fpr.V(sregs[i]), S1);
					VCVT(fpr.V(tempregs[i]), S0, TO_INT | ROUND_TO_ZERO);
				} else {
					VCVT(fpr.V(tempregs[i]), fpr.V(sregs[i]), TO_INT | ROUND_TO_ZERO);
				}
				break;
			case 18: /* TODO */ break; //u
			case 19: /* TODO */ break; //d
			}
		}

		for (int i = 0; i < n; ++i) {
			if (dregs[i] != tempregs[i]) {
				fpr.MapDirtyInV(dregs[i], tempregs[i]);
				VMOV(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}

		ApplyPrefixD(dregs, sz);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_Mftv(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Mftv);
		CONDITIONAL_DISABLE(VFPU_XFER);

		int imm = op & 0xFF;
		MIPSGPReg rt = _RT;
		switch ((op >> 21) & 0x1f) {
		case 3: //mfv / mfvc
			// rt = 0, imm = 255 appears to be used as a CPU interlock by some games.
			if (rt != 0) {
				if (imm < 128) {  //R(rt) = VI(imm);
					fpr.MapRegV(imm, 0);
					gpr.MapReg(rt, MAP_NOINIT | MAP_DIRTY);
					VMOV(gpr.R(rt), fpr.V(imm));
				} else if (imm < 128 + VFPU_CTRL_MAX) { //mtvc
					if (imm - 128 == VFPU_CTRL_CC) {
						if (gpr.IsImm(MIPS_REG_VFPUCC)) {
							gpr.SetImm(rt, gpr.GetImm(MIPS_REG_VFPUCC));
						} else {
							gpr.MapDirtyIn(rt, MIPS_REG_VFPUCC);
							MOV(gpr.R(rt), gpr.R(MIPS_REG_VFPUCC));
						}
					} else {
						// In case we have a saved prefix.
						FlushPrefixV();
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
				gpr.MapReg(rt);
				fpr.MapRegV(imm, MAP_DIRTY | MAP_NOINIT);
				VMOV(fpr.V(imm), gpr.R(rt));
			} else if (imm < 128 + VFPU_CTRL_MAX) { //mtvc //currentMIPS->vfpuCtrl[imm - 128] = R(rt);
				if (imm - 128 == VFPU_CTRL_CC) {
					if (gpr.IsImm(rt)) {
						gpr.SetImm(MIPS_REG_VFPUCC, gpr.GetImm(rt));
					} else {
						gpr.MapDirtyIn(MIPS_REG_VFPUCC, rt);
						MOV(gpr.R(MIPS_REG_VFPUCC), gpr.R(rt));
					}
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

	void ArmJit::Comp_Vmfvc(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vmtvc);
		CONDITIONAL_DISABLE(VFPU_XFER);

		int vd = _VD;
		int imm = (op >> 8) & 0x7F;
		if (imm < VFPU_CTRL_MAX) {
			fpr.MapRegV(vd);
			if (imm == VFPU_CTRL_CC) {
				gpr.MapReg(MIPS_REG_VFPUCC, 0);
				VMOV(fpr.V(vd), gpr.R(MIPS_REG_VFPUCC));
			} else {
				ADDI2R(SCRATCHREG1, CTXREG, offsetof(MIPSState, vfpuCtrl[0]) + imm * 4, SCRATCHREG2);
				VLDR(fpr.V(vd), SCRATCHREG1, 0);
			}
			fpr.ReleaseSpillLocksAndDiscardTemps();
		} else {
			fpr.MapRegV(vd);
			MOVI2F(fpr.V(vd), 0.0f, SCRATCHREG1);
		}
	}

	void ArmJit::Comp_Vmtvc(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vmtvc);
		CONDITIONAL_DISABLE(VFPU_XFER);

		int vs = _VS;
		int imm = op & 0x7F;
		if (imm < VFPU_CTRL_MAX) {
			fpr.MapRegV(vs);
			if (imm == VFPU_CTRL_CC) {
				gpr.MapReg(MIPS_REG_VFPUCC, MAP_DIRTY | MAP_NOINIT);
				VMOV(gpr.R(MIPS_REG_VFPUCC), fpr.V(vs));
			} else {
				ADDI2R(SCRATCHREG1, CTXREG, offsetof(MIPSState, vfpuCtrl[0]) + imm * 4, SCRATCHREG2);
				VSTR(fpr.V(vs), SCRATCHREG1, 0);
			}
			fpr.ReleaseSpillLocksAndDiscardTemps();

			if (imm == VFPU_CTRL_SPREFIX) {
				js.prefixSFlag = JitState::PREFIX_UNKNOWN;
				js.blockWrotePrefixes = true;
			} else if (imm == VFPU_CTRL_TPREFIX) {
				js.prefixTFlag = JitState::PREFIX_UNKNOWN;
				js.blockWrotePrefixes = true;
			} else if (imm == VFPU_CTRL_DPREFIX) {
				js.prefixDFlag = JitState::PREFIX_UNKNOWN;
				js.blockWrotePrefixes = true;
			}
		}
	}

	void ArmJit::Comp_Vmmov(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vmmov);
		CONDITIONAL_DISABLE(VFPU_MTX_VMMOV);

		// This probably ignores prefixes for all sane intents and purposes.
		if (_VS == _VD) {
			// A lot of these no-op matrix moves in Wipeout... Just drop the instruction entirely.
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
			fpr.ReleaseSpillLocksAndDiscardTemps();
		}
	}

	void ArmJit::Comp_VScl(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_VScl);
		CONDITIONAL_DISABLE(VFPU_VEC);
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
			VMUL(fpr.V(tempregs[i]), fpr.V(sregs[i]), S0);
		}

		for (int i = 0; i < n; i++) {
			// All must be mapped for prefixes to work.
			if (dregs[i] != tempregs[i]) {
				fpr.MapDirtyInV(dregs[i], tempregs[i]);
				VMOV(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_Vmmul(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_MTX_VMMUL);
		if (!js.HasNoPrefix()) {
			DISABLE;
		}
		NEON_IF_AVAILABLE(CompNEON_Vmmul);

		if (PSP_CoreParameter().compat.flags().MoreAccurateVMMUL) {
			// Fall back to interpreter, which has the accurate implementation.
			// Later we might do something more optimized here.
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
			fpr.ReleaseSpillLocksAndDiscardTemps();
		}
	}

	void ArmJit::Comp_Vmscl(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vmscl);
		DISABLE;
	}

	void ArmJit::Comp_Vtfm(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vtfm);
		CONDITIONAL_DISABLE(VFPU_MTX_VTFM);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		// TODO: This probably ignores prefixes?  Or maybe uses D?
		
		VectorSize sz = GetVecSize(op);
		MatrixSize msz = GetMtxSize(op);
		int n = GetNumVectorElements(sz);
		int ins = (op >> 23) & 7;

		bool homogenous = false;
		if (n == ins) {
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

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_VCrs(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_VCrs);
		DISABLE;
	}

	void ArmJit::Comp_VDet(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_VDet);
		DISABLE;
	}

	void ArmJit::Comp_Vi2x(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vi2x);
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		int bits = ((op >> 16) & 2) == 0 ? 8 : 16; // vi2uc/vi2c (0/1), vi2us/vi2s (2/3)
		bool unsignedOp = ((op >> 16) & 1) == 0; // vi2uc (0), vi2us (2)

		if (unsignedOp) {
			// Requires a tricky clamp operation that we can't do without more temps, see below
			DISABLE;
		}

		// These instructions pack pairs or quads of integers into 32 bits.
		// The unsigned (u) versions skip the sign bit when packing.
		VectorSize sz = GetVecSize(op);
		VectorSize outsize;
		if (bits == 8) {
			outsize = V_Single;
			if (sz != V_Quad) {
				DISABLE;
			}
		} else {
			switch (sz) {
			case V_Pair:
				outsize = V_Single;
				break;
			case V_Quad:
				outsize = V_Pair;
				break;
			default:
				DISABLE;
			}
		}

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, outsize, _VD);

		// First, let's assemble the sregs into lanes of either D0 (pair) or Q0 (quad).
		bool quad = sz == V_Quad;
		fpr.MapRegsAndSpillLockV(sregs, sz, 0);
		VMOV(S0, fpr.V(sregs[0]));
		VMOV(S1, fpr.V(sregs[1]));
		if (quad) {
			VMOV(S2, fpr.V(sregs[2]));
			VMOV(S3, fpr.V(sregs[3]));
		}

		// TODO: For "u" type ops, we clamp to zero and shift off the sign bit first.
		// Need some temp regs to do that efficiently, right?

		// At this point, we simply need to collect the high bits of each 32-bit lane into one register.
		if (bits == 8) {
			// Really want to do a VSHRN(..., 24) but that can't be encoded. So we synthesize it.
			VSHR(I_32, Q0, Q0, 16);
			VSHRN(I_32, D0, Q0, 8);
			VMOVN(I_16, D0, Q0);
		} else {
			VSHRN(I_32, D0, Q0, 16);
		}

		fpr.MapRegsAndSpillLockV(dregs, outsize, MAP_DIRTY|MAP_NOINIT);
		VMOV(fpr.V(dregs[0]), S0);
		if (outsize == V_Pair) {
			VMOV(fpr.V(dregs[1]), S1);
		}

		ApplyPrefixD(dregs, outsize);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_Vx2i(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vx2i);
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		int bits = ((op >> 16) & 2) == 0 ? 8 : 16; // vuc2i/vc2i (0/1), vus2i/vs2i (2/3)
		bool unsignedOp = ((op >> 16) & 1) == 0; // vuc2i (0), vus2i (2)

		if (bits == 8 && unsignedOp) {
			// vuc2i is odd and needs temp registers for implementation.
			DISABLE;
		}
		// vs2i or vus2i unpack pairs of 16-bit integers into 32-bit integers, with the values
		// at the top.  vus2i shifts it an extra bit right afterward.
		// vc2i and vuc2i unpack quads of 8-bit integers into 32-bit integers, with the values
		// at the top too.  vuc2i is a bit special (see below.)
		// Let's do this similarly as h2f - we do a solution that works for both singles and pairs
		// then use it for both.

		VectorSize sz = GetVecSize(op);
		VectorSize outsize;
		if (bits == 8) {
			outsize = V_Quad;
		} else {
			switch (sz) {
			case V_Single:
				outsize = V_Pair;
				break;
			case V_Pair:
				outsize = V_Quad;
				break;
			default:
				DISABLE;
			}
		}

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, outsize, _VD);

		fpr.MapRegsAndSpillLockV(sregs, sz, 0);
		if (sz == V_Single) {
			VMOV(S0, fpr.V(sregs[0]));
		} else if (sz == V_Pair) {
			VMOV(S0, fpr.V(sregs[0]));
			VMOV(S1, fpr.V(sregs[1]));
		} else if (bits == 8) {
			// For some reason, sz is quad on vc2i.
			VMOV(S0, fpr.V(sregs[0]));
		}


		if (bits == 16) {
			// Simply expand, to upper bits.
			VSHLL(I_16, Q0, D0, 16);
		} else if (bits == 8) {
			if (unsignedOp) {
				// vuc2i is a bit special.  It spreads out the bits like this:
				// s[0] = 0xDDCCBBAA -> d[0] = (0xAAAAAAAA >> 1), d[1] = (0xBBBBBBBB >> 1), etc.
				// TODO
			} else {
				VSHLL(I_8, Q0, D0, 8);
				VSHLL(I_16, Q0, D0, 16);
			}
		}

		// At this point we have the regs in the 4 lanes.
		// In the "u" mode, we need to shift it out of the sign bit.
		if (unsignedOp) {
			ArmGen::ARMReg reg = (outsize == V_Quad) ? Q0 : D0;
			VSHR(I_32 | I_UNSIGNED, reg, reg, 1);
		}

		fpr.MapRegsAndSpillLockV(dregs, outsize, MAP_NOINIT);

		VMOV(fpr.V(dregs[0]), S0);
		VMOV(fpr.V(dregs[1]), S1);
		if (outsize == V_Quad) {
			VMOV(fpr.V(dregs[2]), S2);
			VMOV(fpr.V(dregs[3]), S3);
		}

		ApplyPrefixD(dregs, outsize);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_VCrossQuat(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_VCrossQuat);
		// This op does not support prefixes anyway.
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix())
			DISABLE;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], tregs[4], dregs[4];
		GetVectorRegs(sregs, sz, _VS);
		GetVectorRegs(tregs, sz, _VT);
		GetVectorRegs(dregs, sz, _VD);

		// Map everything into registers.
		fpr.MapRegsAndSpillLockV(sregs, sz, 0);
		fpr.MapRegsAndSpillLockV(tregs, sz, 0);

		if (sz == V_Triple) {
			MIPSReg temp3 = fpr.GetTempV();
			fpr.MapRegV(temp3, MAP_DIRTY | MAP_NOINIT);
			// Cross product vcrsp.t

			// Compute X
			VMUL(S0, fpr.V(sregs[1]), fpr.V(tregs[2]));
			VMLS(S0, fpr.V(sregs[2]), fpr.V(tregs[1]));

			// Compute Y
			VMUL(S1, fpr.V(sregs[2]), fpr.V(tregs[0]));
			VMLS(S1, fpr.V(sregs[0]), fpr.V(tregs[2]));

			// Compute Z
			VMUL(fpr.V(temp3), fpr.V(sregs[0]), fpr.V(tregs[1]));
			VMLS(fpr.V(temp3), fpr.V(sregs[1]), fpr.V(tregs[0]));

			fpr.MapRegsAndSpillLockV(dregs, sz, MAP_NOINIT);
			VMOV(fpr.V(dregs[0]), S0);
			VMOV(fpr.V(dregs[1]), S1);
			VMOV(fpr.V(dregs[2]), fpr.V(temp3));
		} else if (sz == V_Quad) {
			MIPSReg temp3 = fpr.GetTempV();
			MIPSReg temp4 = fpr.GetTempV();
			fpr.MapRegV(temp3, MAP_DIRTY | MAP_NOINIT);
			fpr.MapRegV(temp4, MAP_DIRTY | MAP_NOINIT);

			// Quaternion product  vqmul.q  untested
			// d[0] = s[0] * t[3] + s[1] * t[2] - s[2] * t[1] + s[3] * t[0];
			VMUL(S0, fpr.V(sregs[0]), fpr.V(tregs[3]));
			VMLA(S0, fpr.V(sregs[1]), fpr.V(tregs[2]));
			VMLS(S0, fpr.V(sregs[2]), fpr.V(tregs[1]));
			VMLA(S0, fpr.V(sregs[3]), fpr.V(tregs[0]));

			//d[1] = -s[0] * t[2] + s[1] * t[3] + s[2] * t[0] + s[3] * t[1];
			VNMUL(S1, fpr.V(sregs[0]), fpr.V(tregs[2]));
			VMLA(S1, fpr.V(sregs[1]), fpr.V(tregs[3]));
			VMLA(S1, fpr.V(sregs[2]), fpr.V(tregs[0]));
			VMLA(S1, fpr.V(sregs[3]), fpr.V(tregs[1]));

			//d[2] = s[0] * t[1] - s[1] * t[0] + s[2] * t[3] + s[3] * t[2];
			VMUL(fpr.V(temp3), fpr.V(sregs[0]), fpr.V(tregs[1]));
			VMLS(fpr.V(temp3), fpr.V(sregs[1]), fpr.V(tregs[0]));
			VMLA(fpr.V(temp3), fpr.V(sregs[2]), fpr.V(tregs[3]));
			VMLA(fpr.V(temp3), fpr.V(sregs[3]), fpr.V(tregs[2]));

			//d[3] = -s[0] * t[0] - s[1] * t[1] - s[2] * t[2] + s[3] * t[3];
			VNMUL(fpr.V(temp4), fpr.V(sregs[0]), fpr.V(tregs[0]));
			VMLS(fpr.V(temp4), fpr.V(sregs[1]), fpr.V(tregs[1]));
			VMLS(fpr.V(temp4), fpr.V(sregs[2]), fpr.V(tregs[2]));
			VMLA(fpr.V(temp4), fpr.V(sregs[3]), fpr.V(tregs[3]));

			fpr.MapRegsAndSpillLockV(dregs, sz, MAP_NOINIT);
			VMOV(fpr.V(dregs[0]), S0);
			VMOV(fpr.V(dregs[1]), S1);
			VMOV(fpr.V(dregs[2]), fpr.V(temp3));
			VMOV(fpr.V(dregs[3]), fpr.V(temp4));
		}

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_Vcmp(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vcmp);
		CONDITIONAL_DISABLE(VFPU_COMP);
		if (js.HasUnknownPrefix())
			DISABLE;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		VCondition cond = (VCondition)(op & 0xF);

		u8 sregs[4], tregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixT(tregs, sz, _VT);

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
			if (_VS != _VT)
				DISABLE;
			break;

		case VC_EZ:
		case VC_NZ:
			break;
		default:
			;
		}

		// First, let's get the trivial ones.
		int affected_bits = (1 << 4) | (1 << 5);  // 4 and 5

		MOVI2R(SCRATCHREG1, 0);
		for (int i = 0; i < n; ++i) {
			// Let's only handle the easy ones, and fall back on the interpreter for the rest.
			CCFlags flag = CC_AL;
			switch (cond) {
			case VC_FL: // c = 0;
				break;

			case VC_TR: // c = 1
				if (i == 0) {
					if (n == 1) {
						MOVI2R(SCRATCHREG1, 0x31);
					} else {
						MOVI2R(SCRATCHREG1, 1 << i);
					}
				} else {
					ORR(SCRATCHREG1, SCRATCHREG1, 1 << i);
				}
				break;

			case VC_ES: // c = my_isnan(s[i]) || my_isinf(s[i]); break;   // Tekken Dark Resurrection
			case VC_NS: // c = !(my_isnan(s[i]) || my_isinf(s[i])); break;
				// For these, we use the integer ALU as there is no support on ARM for testing for INF.
				// Testing for nan or inf is the same as testing for &= 0x7F800000 == 0x7F800000.
				// We need an extra temporary register so we store away SCRATCHREG1.
				STR(SCRATCHREG1, CTXREG, offsetof(MIPSState, temp));
				fpr.MapRegV(sregs[i], 0);
				MOVI2R(SCRATCHREG1, 0x7F800000);
				VMOV(SCRATCHREG2, fpr.V(sregs[i]));
				AND(SCRATCHREG2, SCRATCHREG2, SCRATCHREG1);
				CMP(SCRATCHREG2, SCRATCHREG1);   // (SCRATCHREG2 & 0x7F800000) == 0x7F800000
				flag = cond == VC_ES ? CC_EQ : CC_NEQ;
				LDR(SCRATCHREG1, CTXREG, offsetof(MIPSState, temp));
				break;

			case VC_EN: // c = my_isnan(s[i]); break;  // Tekken 6
				// Should we involve T? Where I found this used, it compared a register with itself so should be fine.
				fpr.MapInInV(sregs[i], tregs[i]);
				VCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				VMRS_APSR();
				flag = CC_VS;  // overflow = unordered : http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0204j/Chdhcfbc.html
				break;

			case VC_NN: // c = !my_isnan(s[i]); break;
				// Should we involve T? Where I found this used, it compared a register with itself so should be fine.
				fpr.MapInInV(sregs[i], tregs[i]);
				VCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				VMRS_APSR();
				flag = CC_VC;  // !overflow = !unordered : http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0204j/Chdhcfbc.html
				break;

			case VC_EQ: // c = s[i] == t[i]
				fpr.MapInInV(sregs[i], tregs[i]);
				VCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				VMRS_APSR();
				flag = CC_EQ;
				break;

			case VC_LT: // c = s[i] < t[i]
				fpr.MapInInV(sregs[i], tregs[i]);
				VCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				VMRS_APSR();
				flag = CC_LO;
				break;

			case VC_LE: // c = s[i] <= t[i]; 
				fpr.MapInInV(sregs[i], tregs[i]);
				VCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				VMRS_APSR();
				flag = CC_LS;
				break;

			case VC_NE: // c = s[i] != t[i]
				fpr.MapInInV(sregs[i], tregs[i]);
				VCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				VMRS_APSR();
				flag = CC_NEQ;
				break;

			case VC_GE: // c = s[i] >= t[i]
				fpr.MapInInV(sregs[i], tregs[i]);
				VCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				VMRS_APSR();
				flag = CC_GE;
				break;

			case VC_GT: // c = s[i] > t[i]
				fpr.MapInInV(sregs[i], tregs[i]);
				VCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				VMRS_APSR();
				flag = CC_GT;
				break;

			case VC_EZ: // c = s[i] == 0.0f || s[i] == -0.0f
				fpr.MapRegV(sregs[i]);
				VCMP(fpr.V(sregs[i])); // vcmp(sregs[i], #0.0)
				VMRS_APSR();
				flag = CC_EQ;
				break;

			case VC_NZ: // c = s[i] != 0
				fpr.MapRegV(sregs[i]);
				VCMP(fpr.V(sregs[i])); // vcmp(sregs[i], #0.0)
				VMRS_APSR();
				flag = CC_NEQ;
				break;

			default:
				DISABLE;
			}
			if (flag != CC_AL) {
				SetCC(flag);
				if (i == 0) {
					if (n == 1) {
						MOVI2R(SCRATCHREG1, 0x31);
					} else {
						MOVI2R(SCRATCHREG1, 1);  // 1 << i, but i == 0
					}
				} else {
					ORR(SCRATCHREG1, SCRATCHREG1, 1 << i);
				}
				SetCC(CC_AL);
			}

			affected_bits |= 1 << i;
		}

		// Aggregate the bits. Urgh, expensive. Can optimize for the case of one comparison, which is the most common
		// after all.
		if (n > 1) {
			CMP(SCRATCHREG1, affected_bits & 0xF);
			SetCC(CC_EQ);
			ORR(SCRATCHREG1, SCRATCHREG1, 1 << 5);
			SetCC(CC_AL);

			CMP(SCRATCHREG1, 0);
			SetCC(CC_NEQ);
			ORR(SCRATCHREG1, SCRATCHREG1, 1 << 4);
			SetCC(CC_AL);
		}

		gpr.MapReg(MIPS_REG_VFPUCC, MAP_DIRTY);
		BIC(gpr.R(MIPS_REG_VFPUCC), gpr.R(MIPS_REG_VFPUCC), affected_bits);
		ORR(gpr.R(MIPS_REG_VFPUCC), gpr.R(MIPS_REG_VFPUCC), SCRATCHREG1);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_Vcmov(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vcmov);
		CONDITIONAL_DISABLE(VFPU_COMP);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 sregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);
		int tf = (op >> 19) & 1;
		int imm3 = (op >> 16) & 7;

		for (int i = 0; i < n; ++i) {
			// Simplification: Disable if overlap unsafe
			if (!IsOverlapSafeAllowS(dregs[i], i, n, sregs)) {
				DISABLE;
			}
		}

		if (imm3 < 6) {
			// Test one bit of CC. This bit decides whether none or all subregisters are copied.
			fpr.MapRegsAndSpillLockV(dregs, sz, MAP_DIRTY);
			fpr.MapRegsAndSpillLockV(sregs, sz, 0);
			gpr.MapReg(MIPS_REG_VFPUCC);
			TST(gpr.R(MIPS_REG_VFPUCC), 1 << imm3);
			SetCC(tf ? CC_EQ : CC_NEQ);
			for (int i = 0; i < n; i++) {
				VMOV(fpr.V(dregs[i]), fpr.V(sregs[i]));
			}
			SetCC(CC_AL);
		} else {
			// Look at the bottom four bits of CC to individually decide if the subregisters should be copied.
			fpr.MapRegsAndSpillLockV(dregs, sz, MAP_DIRTY);
			fpr.MapRegsAndSpillLockV(sregs, sz, 0);
			gpr.MapReg(MIPS_REG_VFPUCC);
			for (int i = 0; i < n; i++) {
				TST(gpr.R(MIPS_REG_VFPUCC), 1 << i);
				SetCC(tf ? CC_EQ : CC_NEQ);
				VMOV(fpr.V(dregs[i]), fpr.V(sregs[i]));
				SetCC(CC_AL);
			}
		}

		ApplyPrefixD(dregs, sz);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_Viim(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Viim);
		CONDITIONAL_DISABLE(VFPU_XFER);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		u8 dreg;
		GetVectorRegs(&dreg, V_Single, _VT);

		s32 imm = SignExtend16ToS32(op);
		fpr.MapRegV(dreg, MAP_DIRTY | MAP_NOINIT);
		MOVI2F(fpr.V(dreg), (float)imm, SCRATCHREG1);

		ApplyPrefixD(&dreg, V_Single);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_Vfim(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vfim);
		CONDITIONAL_DISABLE(VFPU_XFER);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		u8 dreg;
		GetVectorRegs(&dreg, V_Single, _VT);

		FP16 half;
		half.u = op & 0xFFFF;
		FP32 fval = half_to_float_fast5(half);
		fpr.MapRegV(dreg, MAP_DIRTY | MAP_NOINIT);
		MOVI2F(fpr.V(dreg), fval.f, SCRATCHREG1);

		ApplyPrefixD(&dreg, V_Single);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_Vcst(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vcst);
		CONDITIONAL_DISABLE(VFPU_XFER);
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

		gpr.SetRegImm(SCRATCHREG1, (u32)(void *)&cst_constants[conNum]);
		VLDR(S0, SCRATCHREG1, 0);
		for (int i = 0; i < n; ++i)
			VMOV(fpr.V(dregs[i]), S0);

		ApplyPrefixD(dregs, sz);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	static double SinCos(float angle) {
		union { struct { float sin; float cos; }; double out; } sincos;
		vfpu_sincos(angle, sincos.sin, sincos.cos);
		return sincos.out;
	}

	static double SinCosNegSin(float angle) {
		union { struct { float sin; float cos; }; double out; } sincos;
		vfpu_sincos(angle, sincos.sin, sincos.cos);
		sincos.sin = -sincos.sin;
		return sincos.out;
	}

	void ArmJit::CompVrotShuffle(u8 *dregs, int imm, VectorSize sz, bool negSin) {
		int n = GetNumVectorElements(sz);
		char what[4] = {'0', '0', '0', '0'};
		if (((imm >> 2) & 3) == (imm & 3)) {
			for (int i = 0; i < 4; i++)
				what[i] = 'S';
		}
		what[(imm >> 2) & 3] = 'S';
		what[imm & 3] = 'C';

		fpr.MapRegsAndSpillLockV(dregs, sz, MAP_DIRTY | MAP_NOINIT);
		for (int i = 0; i < n; i++) {
			switch (what[i]) {
			case 'C': VMOV(fpr.V(dregs[i]), S1); break;
			case 'S': if (negSin) VNEG(fpr.V(dregs[i]), S0); else VMOV(fpr.V(dregs[i]), S0); break;
			case '0':
				{
					MOVI2F(fpr.V(dregs[i]), 0.0f, SCRATCHREG1);
					break;
				}
			default:
				ERROR_LOG(Log::JIT, "Bad what in vrot");
				break;
			}
		}
	}

	// Very heavily used by FF:CC. Should be replaced by a fast approximation instead of
	// calling the math library.
	// Apparently this may not work on hardfp. I don't think we have any platforms using this though.
	void ArmJit::Comp_VRot(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_VRot);
		// VRot probably doesn't accept prefixes anyway.
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

#if PPSSPP_ARCH(ARM_HARDFP)
		DISABLE;
#endif

		int vd = _VD;
		int vs = _VS;

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		u8 dregs[4];
		u8 dregs2[4];

		MIPSOpcode nextOp = GetOffsetInstruction(1);
		int vd2 = -1;
		int imm2 = -1;
		if ((nextOp >> 26) == 60 && ((nextOp >> 21) & 0x1F) == 29 && _VS == MIPS_GET_VS(nextOp)) {
			// Pair of vrot. Let's join them.
			vd2 = MIPS_GET_VD(nextOp);
			imm2 = (nextOp >> 16) & 0x1f;
			// NOTICE_LOG(Log::JIT, "Joint VFPU at %08x", js.blockStart);
		}
		u8 sreg;
		GetVectorRegs(dregs, sz, vd);
		if (vd2 >= 0)
			GetVectorRegs(dregs2, sz, vd2);
		GetVectorRegs(&sreg, V_Single, vs);

		int imm = (op >> 16) & 0x1f;

		gpr.FlushBeforeCall();
		fpr.FlushAll();

		bool negSin1 = (imm & 0x10) ? true : false;

		fpr.MapRegV(sreg);
		// We should write a custom pure-asm function instead.
#if defined(__ARM_PCS_VFP) // Hardfp
		VMOV(S0, fpr.V(sreg));
#else                      // Softfp
		VMOV(R0, fpr.V(sreg));
#endif
		// FlushBeforeCall saves R1.
		QuickCallFunction(R1, negSin1 ? (void *)&SinCosNegSin : (void *)&SinCos);
#if !defined(__ARM_PCS_VFP)
		// Returns D0 on hardfp and R0,R1 on softfp due to union joining the two floats
		VMOV(D0, R0, R1);
#endif
		CompVrotShuffle(dregs, imm, sz, false);
		if (vd2 != -1) {
			// If the negsin setting differs between the two joint invocations, we need to flip the second one.
			bool negSin2 = (imm2 & 0x10) ? true : false;
			CompVrotShuffle(dregs2, imm2, sz, negSin1 != negSin2);
			EatInstruction(nextOp);
		}

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_Vsgn(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vsgn);
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix()) {
			DISABLE;
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

		for (int i = 0; i < n; ++i) {
			fpr.MapDirtyInV(tempregs[i], sregs[i]);
			VCMP(fpr.V(sregs[i])); // vcmp(sregs[i], #0.0)
			VMOV(SCRATCHREG1, fpr.V(sregs[i]));
			VMRS_APSR(); // Move FP flags from FPSCR to APSR (regular flags).
			SetCC(CC_NEQ);
			AND(SCRATCHREG1, SCRATCHREG1, AssumeMakeOperand2(0x80000000));
			ORR(SCRATCHREG1, SCRATCHREG1, AssumeMakeOperand2(0x3F800000));
			SetCC(CC_EQ);
			MOV(SCRATCHREG1, AssumeMakeOperand2(0x0));
			SetCC(CC_AL);
			VMOV(fpr.V(tempregs[i]), SCRATCHREG1);
		}

		for (int i = 0; i < n; ++i) {
			if (dregs[i] != tempregs[i]) {
				fpr.MapDirtyInV(dregs[i], tempregs[i]);
				VMOV(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_Vocp(MIPSOpcode op) {
		NEON_IF_AVAILABLE(CompNEON_Vocp);
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		VectorSize sz = GetVecSize(op);
		int n = GetNumVectorElements(sz);

		// This is a hack that modifies prefixes.  We eat them later, so just overwrite.
		// S prefix forces the negate flags.
		js.prefixS |= 0x000F0000;
		// T prefix forces constants on and regnum to 1.
		// That means negate still works, and abs activates a different constant.
		js.prefixT = (js.prefixT & ~0x000000FF) | 0x00000055 | 0x0000F000;

		u8 sregs[4], tregs[4], dregs[4];
		GetVectorRegsPrefixS(sregs, sz, _VS);
		GetVectorRegsPrefixT(tregs, sz, _VS);
		GetVectorRegsPrefixD(dregs, sz, _VD);

		MIPSReg tempregs[4];
		for (int i = 0; i < n; ++i) {
			if (!IsOverlapSafe(dregs[i], i, n, sregs)) {
				tempregs[i] = fpr.GetTempV();
			} else {
				tempregs[i] = dregs[i];
			}
		}

		for (int i = 0; i < n; ++i) {
			fpr.MapDirtyInInV(tempregs[i], sregs[i], tregs[i]);
			VADD(fpr.V(tempregs[i]), fpr.V(tregs[i]), fpr.V(sregs[i]));
		}

		for (int i = 0; i < n; ++i) {
			if (dregs[i] != tempregs[i]) {
				fpr.MapDirtyInV(dregs[i], tempregs[i]);
				VMOV(fpr.V(dregs[i]), fpr.V(tempregs[i]));
			}
		}

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void ArmJit::Comp_ColorConv(MIPSOpcode op) {
		DISABLE;
	}

	void ArmJit::Comp_Vbfy(MIPSOpcode op) {
		DISABLE;
	}
}

#endif // PPSSPP_ARCH(ARM)
