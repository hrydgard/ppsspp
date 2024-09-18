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
#if PPSSPP_ARCH(ARM64)

#include <cmath>
#include "Common/Arm64Emitter.h"
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
#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/ARM64/Arm64RegCache.h"

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE(flag) { fpr.ReleaseSpillLocksAndDiscardTemps(); Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE(flag) if (jo.Disabled(JitDisable::flag)) { Comp_Generic(op); return; }
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
					WARN_LOG(Log::CPU, "JIT: Invalid VFPU swizzle: %08x : %d / %d at PC = %08x (%s)", prefix, regnum, n, GetCompilerPC(), MIPSDisasmAt(GetCompilerPC()).c_str());
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
		_assert_msg_(js.prefixDFlag & JitState::PREFIX_KNOWN, "Unexpected unknown prefix!");
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
		CONDITIONAL_DISABLE(LSU_VFPU);
		CheckMemoryBreakpoint();

		s32 offset = (signed short)(op & 0xFFFC);
		int vt = ((op >> 16) & 0x1f) | ((op & 3) << 5);
		MIPSGPReg rs = _RS;

		std::vector<FixupBranch> skips;
		switch (op >> 26) {
		case 50: //lv.s  // VI(vt) = Memory::Read_U32(addr);
		{
			if (!gpr.IsImm(rs) && jo.cachePointers && g_Config.bFastMemory && (offset & 3) == 0 && offset >= 0 && offset < 16384) {
				gpr.MapRegAsPointer(rs);
				fpr.MapRegV(vt, MAP_NOINIT | MAP_DIRTY);
				fp.LDR(32, INDEX_UNSIGNED, fpr.V(vt), gpr.RPtr(rs), offset);
				break;
			}

			// CC might be set by slow path below, so load regs first.
			fpr.MapRegV(vt, MAP_DIRTY | MAP_NOINIT);
			if (gpr.IsImm(rs)) {
#ifdef MASKED_PSP_MEMORY
				u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
#else
				u32 addr = offset + gpr.GetImm(rs);
#endif
				gpr.SetRegImm(SCRATCH1, addr);
			} else {
				gpr.MapReg(rs);
				if (g_Config.bFastMemory) {
					SetScratch1ToEffectiveAddress(rs, offset);
				} else {
					skips = SetScratch1ForSafeAddress(rs, offset, SCRATCH2);
				}
			}
			fp.LDR(32, fpr.V(vt), SCRATCH1_64, ArithOption(MEMBASEREG));
			for (auto skip : skips) {
				SetJumpTarget(skip);
			}
		}
			break;

		case 58: //sv.s   // Memory::Write_U32(VI(vt), addr);
		{
			if (!gpr.IsImm(rs) && jo.cachePointers && g_Config.bFastMemory && (offset & 3) == 0 && offset >= 0 && offset < 16384) {
				gpr.MapRegAsPointer(rs);
				fpr.MapRegV(vt, 0);
				fp.STR(32, INDEX_UNSIGNED, fpr.V(vt), gpr.RPtr(rs), offset);
				break;
			}

			// CC might be set by slow path below, so load regs first.
			fpr.MapRegV(vt);
			if (gpr.IsImm(rs)) {
#ifdef MASKED_PSP_MEMORY
				u32 addr = (offset + gpr.GetImm(rs)) & 0x3FFFFFFF;
#else
				u32 addr = offset + gpr.GetImm(rs);
#endif
				gpr.SetRegImm(SCRATCH1, addr);
			} else {
				gpr.MapReg(rs);
				if (g_Config.bFastMemory) {
					SetScratch1ToEffectiveAddress(rs, offset);
				} else {
					skips = SetScratch1ForSafeAddress(rs, offset, SCRATCH2);
				}
			}
			fp.STR(32, fpr.V(vt), SCRATCH1_64, ArithOption(MEMBASEREG));
			for (auto skip : skips) {
				SetJumpTarget(skip);
			}
		}
			break;


		default:
			DISABLE;
		}
	}

	void Arm64Jit::Comp_SVQ(MIPSOpcode op) {
		CONDITIONAL_DISABLE(LSU_VFPU);
		CheckMemoryBreakpoint();

		int imm = (signed short)(op&0xFFFC);
		int vt = (((op >> 16) & 0x1f)) | ((op&1) << 5);
		MIPSGPReg rs = _RS;

		std::vector<FixupBranch> skips;
		switch (op >> 26)
		{
		case 54: //lv.q
			{
				// CC might be set by slow path below, so load regs first.
				u8 vregs[4];
				GetVectorRegs(vregs, V_Quad, vt);
				fpr.MapRegsAndSpillLockV(vregs, V_Quad, MAP_DIRTY | MAP_NOINIT);

				if (gpr.IsImm(rs)) {
#ifdef MASKED_PSP_MEMORY
					u32 addr = (imm + gpr.GetImm(rs)) & 0x3FFFFFFF;
#else
					u32 addr = imm + gpr.GetImm(rs);
#endif
					gpr.SetRegImm(SCRATCH1_64, addr + (uintptr_t)Memory::base);
				} else {
					gpr.MapReg(rs);
					if (g_Config.bFastMemory) {
						SetScratch1ToEffectiveAddress(rs, imm);
					} else {
						skips = SetScratch1ForSafeAddress(rs, imm, SCRATCH2);
					}
					if (jo.enablePointerify) {
						MOVK(SCRATCH1_64, ((uint64_t)Memory::base) >> 32, SHIFT_32);
					} else {
						ADD(SCRATCH1_64, SCRATCH1_64, MEMBASEREG);
					}
				}

				fp.LDP(32, INDEX_SIGNED, fpr.V(vregs[0]), fpr.V(vregs[1]), SCRATCH1_64, 0);
				fp.LDP(32, INDEX_SIGNED, fpr.V(vregs[2]), fpr.V(vregs[3]), SCRATCH1_64, 8);

				for (auto skip : skips) {
					SetJumpTarget(skip);
				}
			}
			break;

		case 62: //sv.q
			{
				// CC might be set by slow path below, so load regs first.
				u8 vregs[4];
				GetVectorRegs(vregs, V_Quad, vt);
				fpr.MapRegsAndSpillLockV(vregs, V_Quad, 0);

				if (gpr.IsImm(rs)) {
#ifdef MASKED_PSP_MEMORY
					u32 addr = (imm + gpr.GetImm(rs)) & 0x3FFFFFFF;
#else
					u32 addr = imm + gpr.GetImm(rs);
#endif
					gpr.SetRegImm(SCRATCH1_64, addr + (uintptr_t)Memory::base);
				} else {
					gpr.MapReg(rs);
					if (g_Config.bFastMemory) {
						SetScratch1ToEffectiveAddress(rs, imm);
					} else {
						skips = SetScratch1ForSafeAddress(rs, imm, SCRATCH2);
					}
					if (jo.enablePointerify) {
						MOVK(SCRATCH1_64, ((uint64_t)Memory::base) >> 32, SHIFT_32);
					} else {
						ADD(SCRATCH1_64, SCRATCH1_64, MEMBASEREG);
					}
				}
				fp.STP(32, INDEX_SIGNED, fpr.V(vregs[0]), fpr.V(vregs[1]), SCRATCH1_64, 0);
				fp.STP(32, INDEX_SIGNED, fpr.V(vregs[2]), fpr.V(vregs[3]), SCRATCH1_64, 8);

				for (auto skip : skips) {
					SetJumpTarget(skip);
				}
			}
			break;

		default:
			DISABLE;
			break;
		}
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_VVectorInit(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);
		// WARNING: No prefix support!
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		switch ((op >> 16) & 0xF) {
		case 6: // v=zeros; break;  //vzero
			fp.MOVI2F(S0, 0.0f, SCRATCH1);
			break;
		case 7: // v=ones; break;   //vone
			fp.MOVI2F(S0, 1.0f, SCRATCH1);
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
			fp.FMOV(fpr.V(dregs[i]), S0);

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_VIdt(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

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
			_dbg_assert_msg_( 0, "Trying to interpret instruction that can't be interpreted");
			break;
		}

		ApplyPrefixD(dregs, sz);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_VMatrixInit(MIPSOpcode op) {
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
		fp.FMUL(S0, fpr.V(sregs[0]), fpr.V(tregs[0]));

		int n = GetNumVectorElements(sz);
		for (int i = 1; i < n; i++) {
			// sum += s[i]*t[i];
			if (i == n - 1) {
				fp.FADD(S0, S0, fpr.V(tregs[i]));
			} else {
				fp.FMADD(S0, fpr.V(sregs[i]), fpr.V(tregs[i]), S0);
			}
		}
		fpr.ReleaseSpillLocksAndDiscardTemps();

		fpr.MapRegV(dregs[0], MAP_NOINIT | MAP_DIRTY);

		fp.FMOV(fpr.V(dregs[0]), S0);
		ApplyPrefixD(dregs, V_Single);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	alignas(16) static const float vavg_table[4] = { 1.0f, 1.0f / 2.0f, 1.0f / 3.0f, 1.0f / 4.0f };

	void Arm64Jit::Comp_Vhoriz(MIPSOpcode op) {
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
			fp.MOVI2F(S1, vavg_table[n - 1], SCRATCH1);
		}
		// Have to start at +0.000 for the correct sign.
		fp.MOVI2F(S0, 0.0f, SCRATCH1);
		for (int i = 0; i < n; i++) {
			// sum += s[i];
			fp.FADD(S0, S0, fpr.V(sregs[i]));
		}

		fpr.MapRegV(dregs[0], MAP_NOINIT | MAP_DIRTY);
		if (is_vavg) {
			fp.FMUL(fpr.V(dregs[0]), S0, S1);
		} else {
			fp.FMOV(fpr.V(dregs[0]), S0);
		}
		ApplyPrefixD(dregs, V_Single);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_VDot(MIPSOpcode op) {
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
					fp.FCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
					FixupBranch unordered = B(CC_VS);
					fp.FMIN(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					FixupBranch skip = B();

					SetJumpTarget(unordered);
					// Move to integer registers, it'll be easier.  Or maybe there's a simd way?
					fp.FMOV(SCRATCH1, fpr.V(sregs[i]));
					fp.FMOV(SCRATCH2, fpr.V(tregs[i]));
					// And together to find if both have negative set.
					TST(SCRATCH1, SCRATCH2);
					FixupBranch cmpPositive = B(CC_PL);
					// If both are negative, "min" is the greater of the two, since it has the largest mantissa.
					CMP(SCRATCH1, SCRATCH2);
					CSEL(SCRATCH1, SCRATCH1, SCRATCH2, CC_GE);
					FixupBranch skipPositive = B();
					// If either one is positive, we just want the lowest one.
					SetJumpTarget(cmpPositive);
					CMP(SCRATCH1, SCRATCH2);
					CSEL(SCRATCH1, SCRATCH1, SCRATCH2, CC_LE);
					SetJumpTarget(skipPositive);
					// Now, whether negative or positive, move to the result.
					fp.FMOV(fpr.V(tempregs[i]), SCRATCH1);
					SetJumpTarget(skip);
					break;
				}
				case 3:  // vmax
				{
					fp.FCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
					FixupBranch unordered = B(CC_VS);
					fp.FMAX(fpr.V(tempregs[i]), fpr.V(sregs[i]), fpr.V(tregs[i]));
					FixupBranch skip = B();

					SetJumpTarget(unordered);
					// Move to integer registers, it'll be easier.  Or maybe there's a simd way?
					fp.FMOV(SCRATCH1, fpr.V(sregs[i]));
					fp.FMOV(SCRATCH2, fpr.V(tregs[i]));
					// And together to find if both have negative set.
					TST(SCRATCH1, SCRATCH2);
					FixupBranch cmpPositive = B(CC_PL);
					// If both are negative, "max" is the least of the two, since it has the lowest mantissa.
					CMP(SCRATCH1, SCRATCH2);
					CSEL(SCRATCH1, SCRATCH1, SCRATCH2, CC_LE);
					FixupBranch skipPositive = B();
					// If either one is positive, we just want the highest one.
					SetJumpTarget(cmpPositive);
					CMP(SCRATCH1, SCRATCH2);
					CSEL(SCRATCH1, SCRATCH1, SCRATCH2, CC_GE);
					SetJumpTarget(skipPositive);
					// Now, whether negative or positive, move to the result.
					fp.FMOV(fpr.V(tempregs[i]), SCRATCH1);
					SetJumpTarget(skip);
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
				ERROR_LOG(Log::JIT, "case missing in vfpu vv2op");
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
			fp.MOVI2F(S0, mult, SCRATCH1);

		// TODO: Use the SCVTF with builtin scaling where possible.
		for (int i = 0; i < n; i++) {
			fpr.MapDirtyInV(tempregs[i], sregs[i]);
			fp.SCVTF(fpr.V(tempregs[i]), fpr.V(sregs[i]));
			if (mult != 1.0f)
				fp.FMUL(fpr.V(tempregs[i]), fpr.V(tempregs[i]), S0);
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

	void Arm64Jit::Comp_Vh2f(MIPSOpcode op) {
		// TODO: Fix by porting the general SSE solution to NEON
		// FCVTL doesn't provide identical results to the PSP hardware, according to the unit test:
		// O vh2f: 00000000,400c0000,00000000,7ff00000
		// E vh2f: 00000000,400c0000,00000000,7f800380
		DISABLE;

		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix()) {
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

		// Take the single registers and combine them to a D register.
		for (int i = 0; i < n; i++) {
			fpr.MapRegV(sregs[i], sz);
			fp.INS(32, Q0, i, fpr.V(sregs[i]), 0);
		}
		// Convert four 16-bit floats in D0 to four 32-bit floats in Q0 (even if we only have two...)
		fp.FCVTL(32, Q0, D0);
		// Split apart again.
		for (int i = 0; i < nOut; i++) {
			fpr.MapRegV(dregs[i], MAP_DIRTY | MAP_NOINIT);
			fp.INS(32, fpr.V(dregs[i]), 0, Q0, i);
		}

		ApplyPrefixD(dregs, sz);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_Vf2i(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Mftv(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);
		int imm = op & 0xFF;
		MIPSGPReg rt = _RT;
		switch ((op >> 21) & 0x1f) {
		case 3: //mfv / mfvc
			// rt = 0, imm = 255 appears to be used as a CPU interlock by some games.
			if (rt != 0) {
				if (imm < 128) {  //R(rt) = VI(imm);
					if (!fpr.IsInRAMV(imm)) {
						fpr.MapRegV(imm, 0);
						gpr.MapReg(rt, MAP_NOINIT | MAP_DIRTY);
						fp.FMOV(gpr.R(rt), fpr.V(imm));
					} else {
						gpr.MapReg(rt, MAP_NOINIT | MAP_DIRTY);
						LDR(INDEX_UNSIGNED, gpr.R(rt), CTXREG, fpr.GetMipsRegOffsetV(imm));
					}
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
						LDR(INDEX_UNSIGNED, gpr.R(rt), CTXREG, offsetof(MIPSState, vfpuCtrl) + 4 * (imm - 128));
					}
				} else {
					//ERROR - maybe need to make this value too an "interlock" value?
					ERROR_LOG(Log::CPU, "mfv - invalid register %i", imm);
				}
			}
			break;

		case 7: // mtv
			if (imm < 128) {
				if (rt == MIPS_REG_ZERO) {
					fpr.MapRegV(imm, MAP_DIRTY | MAP_NOINIT);
					fp.MOVI2F(fpr.V(imm), 0.0f, SCRATCH1);
				} else if (!gpr.IsInRAM(rt)) {
					gpr.MapReg(rt);
					fpr.MapRegV(imm, MAP_DIRTY | MAP_NOINIT);
					fp.FMOV(fpr.V(imm), gpr.R(rt));
				} else {
					fpr.MapRegV(imm, MAP_DIRTY | MAP_NOINIT);
					fp.LDR(32, INDEX_UNSIGNED, fpr.V(imm), CTXREG, gpr.GetMipsRegOffset(rt));
				}
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
					STR(INDEX_UNSIGNED, gpr.R(rt), CTXREG, offsetof(MIPSState, vfpuCtrl) + 4 * (imm - 128));
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
				_dbg_assert_msg_( 0, "mtv - invalid register");
			}
			break;

		default:
			DISABLE;
		}

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_Vmfvc(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);

		int vd = _VD;
		int imm = (op >> 8) & 0x7F;
		if (imm < VFPU_CTRL_MAX) {
			fpr.MapRegV(vd);
			if (imm == VFPU_CTRL_CC) {
				gpr.MapReg(MIPS_REG_VFPUCC, 0);
				fp.FMOV(fpr.V(vd), gpr.R(MIPS_REG_VFPUCC));
			} else {
				ADDI2R(SCRATCH1_64, CTXREG, offsetof(MIPSState, vfpuCtrl[0]) + imm * 4, SCRATCH2);
				fp.LDR(32, INDEX_UNSIGNED, fpr.V(vd), SCRATCH1_64, 0);
			}
			fpr.ReleaseSpillLocksAndDiscardTemps();
		} else {
			fpr.MapRegV(vd);
			fp.MOVI2F(fpr.V(vd), 0.0f, SCRATCH1);
		}
	}

	void Arm64Jit::Comp_Vmtvc(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);

		int vs = _VS;
		int imm = op & 0x7F;
		if (imm < VFPU_CTRL_MAX) {
			fpr.MapRegV(vs);
			if (imm == VFPU_CTRL_CC) {
				gpr.MapReg(MIPS_REG_VFPUCC, MAP_DIRTY | MAP_NOINIT);
				fp.FMOV(gpr.R(MIPS_REG_VFPUCC), fpr.V(vs));
			} else {
				ADDI2R(SCRATCH1_64, CTXREG, offsetof(MIPSState, vfpuCtrl[0]) + imm * 4, SCRATCH2);
				fp.STR(32, INDEX_UNSIGNED, fpr.V(vs), SCRATCH1_64, 0);
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

	void Arm64Jit::Comp_Vmmov(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_MTX_VMMOV);
		if (!js.HasNoPrefix()) {
			DISABLE;
		}

		if (_VS == _VD) {
			// A lot of these no-op matrix moves in Wipeout... Just drop the instruction entirely.
			return;
		}

		MatrixSize sz = GetMtxSize(op);
		int n = GetMatrixSide(sz);

		u8 sregs[16], dregs[16];
		GetMatrixRegs(sregs, sz, _VS);
		GetMatrixRegs(dregs, sz, _VD);

		switch (GetMatrixOverlap(_VS, _VD, sz)) {
		case OVERLAP_EQUAL:
			// In-place transpose
			DISABLE;
		case OVERLAP_PARTIAL:
			DISABLE;
		case OVERLAP_NONE:
		default:
			break;
		}

		for (int a = 0; a < n; a++) {
			for (int b = 0; b < n; b++) {
				fpr.MapDirtyInV(dregs[a * 4 + b], sregs[a * 4 + b]);
				fp.FMOV(fpr.V(dregs[a * 4 + b]), fpr.V(sregs[a * 4 + b]));
			}
		}
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_VScl(MIPSOpcode op) {
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
		CONDITIONAL_DISABLE(VFPU_MTX_VMMUL);
		if (!js.HasNoPrefix()) {
			DISABLE;
		}

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

		MatrixOverlapType soverlap = GetMatrixOverlap(_VS, _VD, sz);
		MatrixOverlapType toverlap = GetMatrixOverlap(_VT, _VD, sz);

		if (soverlap || toverlap) {
			DISABLE;
		} else {
			for (int a = 0; a < n; a++) {
				for (int b = 0; b < n; b++) {
					fpr.MapDirtyInInV(dregs[a * 4 + b], sregs[b * 4], tregs[a * 4], true);
					fp.FMUL(fpr.V(dregs[a * 4 + b]), fpr.V(sregs[b * 4]), fpr.V(tregs[a * 4]));
					for (int c = 1; c < n; c++) {
						fpr.MapDirtyInInV(dregs[a * 4 + b], sregs[b * 4 + c], tregs[a * 4 + c], false);
						fp.FMUL(S0, fpr.V(sregs[b * 4 + c]), fpr.V(tregs[a * 4 + c]));
						fp.FADD(fpr.V(dregs[a * 4 + b]), fpr.V(dregs[a * 4 + b]), S0);
					}
				}
			}
			fpr.ReleaseSpillLocksAndDiscardTemps();
		}
	}

	void Arm64Jit::Comp_Vmscl(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vtfm(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_MTX_VTFM);
		if (!js.HasNoPrefix()) {
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

		u8 sregs[16], dregs[4], tregs[4];
		GetMatrixRegs(sregs, msz, _VS);
		GetVectorRegs(tregs, sz, _VT);
		GetVectorRegs(dregs, sz, _VD);

		MatrixOverlapType soverlap = GetMatrixOverlap(_VS, _VD, msz);
		MatrixOverlapType toverlap = GetMatrixOverlap(_VT, _VD, msz);

		int tempregs[4];
		for (int i = 0; i < n; i++) {
			if (soverlap || toverlap) {
				tempregs[i] = fpr.GetTempV();
			} else {
				tempregs[i] = dregs[i];
			}
			fpr.SpillLockV(tempregs[i]);
		}
		for (int i = 0; i < n; i++) {
			fpr.MapRegV(tempregs[i], MAP_NOINIT);
			fpr.MapInInV(sregs[i * 4], tregs[0]);
			fp.FMUL(fpr.V(tempregs[i]), fpr.V(sregs[i * 4]), fpr.V(tregs[0]));
			for (int k = 1; k < n; k++) {
				if (!homogenous || k != n - 1) {
					fpr.MapInInV(sregs[i * 4 + k], tregs[k]);
					fp.FMADD(fpr.V(tempregs[i]), fpr.V(sregs[i * 4 + k]), fpr.V(tregs[k]), fpr.V(tempregs[i]));
				} else {
					fpr.MapRegV(sregs[i * 4 + k]);
					fp.FADD(fpr.V(tempregs[i]), fpr.V(tempregs[i]), fpr.V(sregs[i * 4 + k]));
				}
			}
		}
		for (int i = 0; i < n; i++) {
			u8 temp = tempregs[i];
			if (temp != dregs[i]) {
				fpr.MapDirtyInV(dregs[i], temp, true);
				fp.FMOV(fpr.V(dregs[i]), fpr.V(temp));
			}
		}

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_VCrs(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_VDet(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vi2x(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix())
			DISABLE;

		int bits = ((op >> 16) & 2) == 0 ? 8 : 16; // vi2uc/vi2c (0/1), vi2us/vi2s (2/3)
		bool unsignedOp = ((op >> 16) & 1) == 0; // vi2uc (0), vi2us (2)

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

		int n = GetNumVectorElements(sz);
		int nOut = GetNumVectorElements(outsize);

		// Take the single registers and combine them to a D or Q register.
		for (int i = 0; i < n; i++) {
			fpr.MapRegV(sregs[i], sz);
			fp.INS(32, Q0, i, fpr.V(sregs[i]), 0);
		}

		if (unsignedOp) {
			// What's the best way to zero a Q reg?
			fp.EOR(Q1, Q1, Q1);
			fp.SMAX(32, Q0, Q0, Q1);
		}

		// At this point, we simply need to collect the high bits of each 32-bit lane into one register.
		if (bits == 8) {
			// Really want to do a SHRN(..., 23/24) but that can't be encoded. So we synthesize it.
			fp.USHR(32, Q0, Q0, 16);
			fp.SHRN(16, D0, Q0, unsignedOp ? 7 : 8);
			fp.XTN(8, D0, Q0);
		} else {
			fp.SHRN(16, D0, Q0, unsignedOp ? 15 : 16);
		}

		// Split apart again.
		for (int i = 0; i < nOut; i++) {
			fpr.MapRegV(dregs[i], MAP_DIRTY | MAP_NOINIT);
			fp.INS(32, fpr.V(dregs[i]), 0, Q0, i);
		}

		ApplyPrefixD(dregs, outsize);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_Vx2i(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix())
			DISABLE;

		int bits = ((op >> 16) & 2) == 0 ? 8 : 16; // vuc2i/vc2i (0/1), vus2i/vs2i (2/3)
		bool unsignedOp = ((op >> 16) & 1) == 0; // vuc2i (0), vus2i (2)

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
		int n = 1;
		if (sz == V_Single) {
			n = 1;
		} else if (sz == V_Pair) {
			n = 2;
		} else if (bits == 8) {
			n = 1;
		}

		// Take the single registers and combine them to a D or Q register.
		for (int i = 0; i < n; i++) {
			fpr.MapRegV(sregs[i], sz);
			fp.INS(32, Q0, i, fpr.V(sregs[i]), 0);
		}

		if (bits == 16) {
			// Simply expand, to upper bits.
			// Hm, can't find a USHLL equivalent that works with shift == size?
			fp.UXTL(16, Q0, D0);
			fp.SHL(32, Q0, Q0, 16);
		} else if (bits == 8) {
			fp.UXTL(8, Q0, D0);
			fp.UXTL(16, Q0, D0);
			fp.SHL(32, Q0, D0, 24);
			if (unsignedOp) {
				// vuc2i is a bit special.  It spreads out the bits like this:
				// s[0] = 0xDDCCBBAA -> d[0] = (0xAAAAAAAA >> 1), d[1] = (0xBBBBBBBB >> 1), etc.
				fp.USHR(32, Q1, Q0, 8);
				fp.ORR(Q0, Q0, Q1);
				fp.USHR(32, Q1, Q0, 16);
				fp.ORR(Q0, Q0, Q1);
			}
		}

		// At this point we have the regs in the 4 lanes.
		// In the "u" mode, we need to shift it out of the sign bit.
		if (unsignedOp) {
			Arm64Gen::ARM64Reg reg = (outsize == V_Quad) ? Q0 : D0;
			fp.USHR(32, reg, reg, 1);
		}

		fpr.MapRegsAndSpillLockV(dregs, outsize, MAP_NOINIT);

		int nOut = 2;
		if (outsize == V_Quad)
			nOut = 4;

		// Split apart again.
		for (int i = 0; i < nOut; i++) {
			fpr.MapRegV(dregs[i], MAP_DIRTY | MAP_NOINIT);
			fp.INS(32, fpr.V(dregs[i]), 0, Q0, i);
		}

		ApplyPrefixD(dregs, outsize);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_VCrossQuat(MIPSOpcode op) {
		// This op does not support prefixes anyway.
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (!js.HasNoPrefix())
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
			MIPSReg temp4 = fpr.GetTempV();
			fpr.MapRegV(temp3, MAP_DIRTY | MAP_NOINIT);
			fpr.MapRegV(temp4, MAP_DIRTY | MAP_NOINIT);
			// Cross product vcrsp.t

			// Note: using FMSUB here causes accuracy issues, see #18203.
			// Compute X: s[1] * t[2] - s[2] * t[1]
			fp.FMUL(fpr.V(temp3), fpr.V(sregs[1]), fpr.V(tregs[2]));
			fp.FMUL(fpr.V(temp4), fpr.V(sregs[2]), fpr.V(tregs[1]));
			fp.FSUB(S0, fpr.V(temp3), fpr.V(temp4));

			// Compute Y: s[2] * t[0] - s[0] * t[2]
			fp.FMUL(fpr.V(temp3), fpr.V(sregs[2]), fpr.V(tregs[0]));
			fp.FMUL(fpr.V(temp4), fpr.V(sregs[0]), fpr.V(tregs[2]));
			fp.FSUB(S1, fpr.V(temp3), fpr.V(temp4));

			// Compute Z: s[0] * t[1] - s[1] * t[0]
			fp.FMUL(fpr.V(temp3), fpr.V(sregs[0]), fpr.V(tregs[1]));
			fp.FMUL(fpr.V(temp4), fpr.V(sregs[1]), fpr.V(tregs[0]));
			fp.FSUB(fpr.V(temp3), fpr.V(temp3), fpr.V(temp4));

			fpr.MapRegsAndSpillLockV(dregs, sz, MAP_NOINIT);
			fp.FMOV(fpr.V(dregs[0]), S0);
			fp.FMOV(fpr.V(dregs[1]), S1);
			fp.FMOV(fpr.V(dregs[2]), fpr.V(temp3));
		} else if (sz == V_Quad) {
			MIPSReg temp3 = fpr.GetTempV();
			MIPSReg temp4 = fpr.GetTempV();
			fpr.MapRegV(temp3, MAP_DIRTY | MAP_NOINIT);
			fpr.MapRegV(temp4, MAP_DIRTY | MAP_NOINIT);

			// Quaternion product  vqmul.q  untested
			// d[0] = s[0] * t[3] + s[1] * t[2] - s[2] * t[1] + s[3] * t[0];
			fp.FMUL(S0, fpr.V(sregs[0]), fpr.V(tregs[3]));
			fp.FMADD(S0, fpr.V(sregs[1]), fpr.V(tregs[2]), S0);
			fp.FMSUB(S0, fpr.V(sregs[2]), fpr.V(tregs[1]), S0);
			fp.FMADD(S0, fpr.V(sregs[3]), fpr.V(tregs[0]), S0);

			//d[1] = -s[0] * t[2] + s[1] * t[3] + s[2] * t[0] + s[3] * t[1];
			fp.FNMUL(S1, fpr.V(sregs[0]), fpr.V(tregs[2]));
			fp.FMADD(S1, fpr.V(sregs[1]), fpr.V(tregs[3]), S1);
			fp.FMADD(S1, fpr.V(sregs[2]), fpr.V(tregs[0]), S1);
			fp.FMADD(S1, fpr.V(sregs[3]), fpr.V(tregs[1]), S1);

			//d[2] = s[0] * t[1] - s[1] * t[0] + s[2] * t[3] + s[3] * t[2];
			fp.FMUL(fpr.V(temp3), fpr.V(sregs[0]), fpr.V(tregs[1]));
			fp.FMSUB(fpr.V(temp3), fpr.V(sregs[1]), fpr.V(tregs[0]), fpr.V(temp3));
			fp.FMADD(fpr.V(temp3), fpr.V(sregs[2]), fpr.V(tregs[3]), fpr.V(temp3));
			fp.FMADD(fpr.V(temp3), fpr.V(sregs[3]), fpr.V(tregs[2]), fpr.V(temp3));

			//d[3] = -s[0] * t[0] - s[1] * t[1] - s[2] * t[2] + s[3] * t[3];
			fp.FNMUL(fpr.V(temp4), fpr.V(sregs[0]), fpr.V(tregs[0]));
			fp.FMSUB(fpr.V(temp4), fpr.V(sregs[1]), fpr.V(tregs[1]), fpr.V(temp4));
			fp.FMSUB(fpr.V(temp4), fpr.V(sregs[2]), fpr.V(tregs[2]), fpr.V(temp4));
			fp.FMADD(fpr.V(temp4), fpr.V(sregs[3]), fpr.V(tregs[3]), fpr.V(temp4));

			fpr.MapRegsAndSpillLockV(dregs, sz, MAP_NOINIT);
			fp.FMOV(fpr.V(dregs[0]), S0);
			fp.FMOV(fpr.V(dregs[1]), S1);
			fp.FMOV(fpr.V(dregs[2]), fpr.V(temp3));
			fp.FMOV(fpr.V(dregs[3]), fpr.V(temp4));
		}

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_Vcmp(MIPSOpcode op) {
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

		MOVI2R(SCRATCH1, 0);
		for (int i = 0; i < n; ++i) {
			// Let's only handle the easy ones, and fall back on the interpreter for the rest.
			CCFlags flag = CC_AL;
			switch (cond) {
			case VC_FL: // c = 0;
				break;

			case VC_TR: // c = 1
				if (i == 0) {
					if (n == 1) {
						MOVI2R(SCRATCH1, 0x31);
					} else {
						MOVI2R(SCRATCH1, 1ULL << i);
					}
				} else {
					ORRI2R(SCRATCH1, SCRATCH1, 1ULL << i);
				}
				break;

			case VC_ES: // c = my_isnan(s[i]) || my_isinf(s[i]); break;   // Tekken Dark Resurrection
			case VC_NS: // c = !(my_isnan(s[i]) || my_isinf(s[i])); break;
				// For these, we use the integer ALU as there is no support on ARM for testing for INF.
				// Testing for nan or inf is the same as testing for &= 0x7F800000 == 0x7F800000.
				// We need an extra temporary register so we store away SCRATCH1.
				STR(INDEX_UNSIGNED, SCRATCH1, CTXREG, offsetof(MIPSState, temp));
				fpr.MapRegV(sregs[i], 0);
				MOVI2R(SCRATCH1, 0x7F800000);
				fp.FMOV(SCRATCH2, fpr.V(sregs[i]));
				AND(SCRATCH2, SCRATCH2, SCRATCH1);
				CMP(SCRATCH2, SCRATCH1);   // (SCRATCH2 & 0x7F800000) == 0x7F800000
				flag = cond == VC_ES ? CC_EQ : CC_NEQ;
				LDR(INDEX_UNSIGNED, SCRATCH1, CTXREG, offsetof(MIPSState, temp));
				break;

			case VC_EN: // c = my_isnan(s[i]); break;  // Tekken 6
				// Should we involve T? Where I found this used, it compared a register with itself so should be fine.
				fpr.MapInInV(sregs[i], tregs[i]);
				fp.FCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				flag = CC_VS;  // overflow = unordered : http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0204j/Chdhcfbc.html
				break;

			case VC_NN: // c = !my_isnan(s[i]); break;
				// Should we involve T? Where I found this used, it compared a register with itself so should be fine.
				fpr.MapInInV(sregs[i], tregs[i]);
				fp.FCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				flag = CC_VC;  // !overflow = !unordered : http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0204j/Chdhcfbc.html
				break;

			case VC_EQ: // c = s[i] == t[i]
				fpr.MapInInV(sregs[i], tregs[i]);
				fp.FCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				flag = CC_EQ;
				break;

			case VC_LT: // c = s[i] < t[i]
				fpr.MapInInV(sregs[i], tregs[i]);
				fp.FCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				flag = CC_LO;
				break;

			case VC_LE: // c = s[i] <= t[i]; 
				fpr.MapInInV(sregs[i], tregs[i]);
				fp.FCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				flag = CC_LS;
				break;

			case VC_NE: // c = s[i] != t[i]
				fpr.MapInInV(sregs[i], tregs[i]);
				fp.FCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				flag = CC_NEQ;
				break;

			case VC_GE: // c = s[i] >= t[i]
				fpr.MapInInV(sregs[i], tregs[i]);
				fp.FCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				flag = CC_GE;
				break;

			case VC_GT: // c = s[i] > t[i]
				fpr.MapInInV(sregs[i], tregs[i]);
				fp.FCMP(fpr.V(sregs[i]), fpr.V(tregs[i]));
				flag = CC_GT;
				break;

			case VC_EZ: // c = s[i] == 0.0f || s[i] == -0.0f
				fpr.MapRegV(sregs[i]);
				fp.FCMP(fpr.V(sregs[i])); // vcmp(sregs[i], #0.0)
				flag = CC_EQ;
				break;

			case VC_NZ: // c = s[i] != 0
				fpr.MapRegV(sregs[i]);
				fp.FCMP(fpr.V(sregs[i])); // vcmp(sregs[i], #0.0)
				flag = CC_NEQ;
				break;

			default:
				DISABLE;
			}
			if (flag != CC_AL) {
				FixupBranch b = B(InvertCond(flag));
				if (i == 0) {
					if (n == 1) {
						MOVI2R(SCRATCH1, 0x31);
					} else {
						MOVI2R(SCRATCH1, 1);  // 1 << i, but i == 0
					}
				} else {
					ORRI2R(SCRATCH1, SCRATCH1, 1ULL << i);
				}
				SetJumpTarget(b);
			}

			affected_bits |= 1 << i;
		}

		// Aggregate the bits. Urgh, expensive. Can optimize for the case of one comparison, which is the most common
		// after all.
		if (n > 1) {
			CMP(SCRATCH1, affected_bits & 0xF);
			FixupBranch skip1 = B(CC_NEQ);
			ORRI2R(SCRATCH1, SCRATCH1, 1 << 5);
			SetJumpTarget(skip1);

			CMP(SCRATCH1, 0);
			FixupBranch skip2 = B(CC_EQ);
			ORRI2R(SCRATCH1, SCRATCH1, 1 << 4);
			SetJumpTarget(skip2);
		}

		gpr.MapReg(MIPS_REG_VFPUCC, MAP_DIRTY);
		ANDI2R(gpr.R(MIPS_REG_VFPUCC), gpr.R(MIPS_REG_VFPUCC), ~affected_bits, SCRATCH2);
		ORR(gpr.R(MIPS_REG_VFPUCC), gpr.R(MIPS_REG_VFPUCC), SCRATCH1);

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_Vcmov(MIPSOpcode op) {
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
			TSTI2R(gpr.R(MIPS_REG_VFPUCC), 1ULL << imm3);
			// TODO: Use fsel?
			FixupBranch b = B(tf ? CC_NEQ : CC_EQ);
			for (int i = 0; i < n; i++) {
				fp.FMOV(fpr.V(dregs[i]), fpr.V(sregs[i]));
			}
			SetJumpTarget(b);
		} else {
			// Look at the bottom four bits of CC to individually decide if the subregisters should be copied.
			fpr.MapRegsAndSpillLockV(dregs, sz, MAP_DIRTY);
			fpr.MapRegsAndSpillLockV(sregs, sz, 0);
			gpr.MapReg(MIPS_REG_VFPUCC);
			for (int i = 0; i < n; i++) {
				TSTI2R(gpr.R(MIPS_REG_VFPUCC), 1ULL << i);
				FixupBranch b = B(tf ? CC_NEQ : CC_EQ);
				fp.FMOV(fpr.V(dregs[i]), fpr.V(sregs[i]));
				SetJumpTarget(b);
			}
		}

		ApplyPrefixD(dregs, sz);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_Viim(MIPSOpcode op) {
		CONDITIONAL_DISABLE(VFPU_XFER);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

		u8 dreg;
		GetVectorRegs(&dreg, V_Single, _VT);

		s32 imm = SignExtend16ToS32(op);
		fpr.MapRegV(dreg, MAP_DIRTY | MAP_NOINIT);
		fp.MOVI2F(fpr.V(dreg), (float)imm, SCRATCH1);

		ApplyPrefixD(&dreg, V_Single);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_Vfim(MIPSOpcode op) {
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
		fp.MOVI2F(fpr.V(dreg), fval.f, SCRATCH1);

		ApplyPrefixD(&dreg, V_Single);
		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_Vcst(MIPSOpcode op) {
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

		MOVP2R(SCRATCH1_64, (void *)&cst_constants[conNum]);
		fp.LDR(32, INDEX_UNSIGNED, S0, SCRATCH1_64, 0);
		for (int i = 0; i < n; ++i)
			fp.FMOV(fpr.V(dregs[i]), S0);

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
				ERROR_LOG(Log::JIT, "Bad what in vrot");
				break;
			}
		}
	}

	// Very heavily used by FF:CC. Should be replaced by a fast approximation instead of
	// calling the math library.
	void Arm64Jit::Comp_VRot(MIPSOpcode op) {
		// VRot probably doesn't accept prefixes anyway.
		CONDITIONAL_DISABLE(VFPU_VEC);
		if (js.HasUnknownPrefix()) {
			DISABLE;
		}

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

		// Don't need to SaveStaticRegs here as long as they are all in callee-save regs - this callee won't read them.

		bool negSin1 = (imm & 0x10) ? true : false;

		fpr.MapRegV(sreg);
		fp.FMOV(S0, fpr.V(sreg));
		QuickCallFunction(SCRATCH2_64, negSin1 ? (void *)&SinCosNegSin : (void *)&SinCos);
		// Here, sin and cos are stored together in Q0.d. On ARM32 we could use it directly
		// but with ARM64's register organization, we need to split it up.
		fp.INS(32, Q1, 0, Q0, 1);

		CompVrotShuffle(dregs, imm, sz, false);
		if (vd2 != -1) {
			// If the negsin setting differs between the two joint invocations, we need to flip the second one.
			bool negSin2 = (imm2 & 0x10) ? true : false;
			CompVrotShuffle(dregs2, imm2, sz, negSin1 != negSin2);
			EatInstruction(nextOp);
		}

		fpr.ReleaseSpillLocksAndDiscardTemps();
	}

	void Arm64Jit::Comp_Vsgn(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vocp(MIPSOpcode op) {
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

		fp.MOVI2F(S0, 1.0f, SCRATCH1);
		for (int i = 0; i < n; ++i) {
			fpr.MapDirtyInInV(tempregs[i], sregs[i], tregs[i]);
			fp.FADD(fpr.V(tempregs[i]), fpr.V(tregs[i]), fpr.V(sregs[i]));
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

	void Arm64Jit::Comp_ColorConv(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_Vbfy(MIPSOpcode op) {
		DISABLE;
	}
}

#endif // PPSSPP_ARCH(ARM64)
