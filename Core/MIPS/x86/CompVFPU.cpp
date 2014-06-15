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
#include <limits>
#include <xmmintrin.h>

#include "base/logging.h"
#include "math/math_util.h"

#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSVFPUUtils.h"
#include "Core/MIPS/x86/Jit.h"
#include "Core/MIPS/x86/RegCache.h"

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE { fpr.ReleaseSpillLocks(); Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { fpr.ReleaseSpillLocks(); Comp_Generic(op); return; }

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


using namespace Gen;

namespace MIPSComp
{

static const float one = 1.0f;
static const float minus_one = -1.0f;
static const float zero = 0.0f;

const u32 MEMORY_ALIGNED16( noSignMask[4] ) = {0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF};
const u32 MEMORY_ALIGNED16( signBitLower[4] ) = {0x80000000, 0, 0, 0};
const float MEMORY_ALIGNED16( oneOneOneOne[4] ) = {1.0f, 1.0f, 1.0f, 1.0f};
const u32 MEMORY_ALIGNED16( solidOnes[4] ) = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
const u32 MEMORY_ALIGNED16( fourinfnan[4] ) = {0x7F800000, 0x7F800000, 0x7F800000, 0x7F800000};

void Jit::Comp_VPFX(MIPSOpcode op)
{
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
			MOVSS(fpr.VX(vregs[i]), fpr.V(origV[regnum]));
			if (abs) {
				ANDPS(fpr.VX(vregs[i]), M(&noSignMask));
			}
		} else {
			MOVSS(fpr.VX(vregs[i]), M(&constantArray[regnum + (abs<<2)]));
		}

		if (negate)
			XORPS(fpr.VX(vregs[i]), M(&signBitLower));

		// TODO: This probably means it will swap out soon, inefficiently...
		fpr.ReleaseSpillLockV(vregs[i]);
	}
}

void Jit::GetVectorRegsPrefixD(u8 *regs, VectorSize sz, int vectorReg) {
	_assert_(js.prefixDFlag & JitState::PREFIX_KNOWN);

	GetVectorRegs(regs, sz, vectorReg);
	if (js.prefixD == 0)
		return;

	int n = GetNumVectorElements(sz);
	for (int i = 0; i < n; i++)
	{
		// Hopefully this is rare, we'll just write it into a reg we drop.
		if (js.VfpuWriteMask(i))
			regs[i] = fpr.GetTempV();
	}
}

void Jit::ApplyPrefixD(const u8 *vregs, VectorSize sz) {
	_assert_(js.prefixDFlag & JitState::PREFIX_KNOWN);
	if (!js.prefixD) return;

	int n = GetNumVectorElements(sz);
	for (int i = 0; i < n; i++)
	{
		if (js.VfpuWriteMask(i))
			continue;

		int sat = (js.prefixD >> (i * 2)) & 3;
		if (sat == 1)
		{
			fpr.MapRegV(vregs[i], MAP_DIRTY);

			// Zero out XMM0 if it was <= +0.0f (but skip NAN.)
			MOVSS(R(XMM0), fpr.VX(vregs[i]));
			CMPLESS(XMM0, M(&zero));
			ANDNPS(XMM0, fpr.V(vregs[i]));

			// Retain a NAN in XMM0 (must be second operand.)
			MOVSS(fpr.VX(vregs[i]), M(&one));
			MINSS(fpr.VX(vregs[i]), R(XMM0));
		}
		else if (sat == 3)
		{
			fpr.MapRegV(vregs[i], MAP_DIRTY);

			// Check for < -1.0f, but careful of NANs.
			MOVSS(XMM1, M(&minus_one));
			MOVSS(R(XMM0), fpr.VX(vregs[i]));
			CMPLESS(XMM0, R(XMM1));
			// If it was NOT less, the three ops below do nothing.
			// Otherwise, they replace the value with -1.0f.
			ANDPS(XMM1, R(XMM0));
			ANDNPS(XMM0, fpr.V(vregs[i]));
			ORPS(XMM0, R(XMM1));

			// Retain a NAN in XMM0 (must be second operand.)
			MOVSS(fpr.VX(vregs[i]), M(&one));
			MINSS(fpr.VX(vregs[i]), R(XMM0));
		}
	}
}

// Vector regs can overlap in all sorts of swizzled ways.
// This does allow a single overlap in sregs[i].
bool IsOverlapSafeAllowS(int dreg, int di, int sn, u8 sregs[], int tn = 0, u8 tregs[] = NULL)
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

bool IsOverlapSafe(int dreg, int di, int sn, u8 sregs[], int tn = 0, u8 tregs[] = NULL)
{
	return IsOverlapSafeAllowS(dreg, di, sn, sregs, tn, tregs) && sregs[di] != dreg;
}

static u32 MEMORY_ALIGNED16(ssLoadStoreTemp);

void Jit::Comp_SV(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	s32 imm = (signed short)(op&0xFFFC);
	int vt = ((op >> 16) & 0x1f) | ((op & 3) << 5);
	MIPSGPReg rs = _RS;

	switch (op >> 26)
	{
	case 50: //lv.s  // VI(vt) = Memory::Read_U32(addr);
		{
			gpr.MapReg(rs, true, false);
			fpr.MapRegV(vt, MAP_NOINIT);

			JitSafeMem safe(this, rs, imm);
			safe.SetFar();
			OpArg src;
			if (safe.PrepareRead(src, 4))
			{
				MOVSS(fpr.VX(vt), safe.NextFastAddress(0));
			}
			if (safe.PrepareSlowRead(safeMemFuncs.readU32))
			{
				MOVD_xmm(fpr.VX(vt), R(EAX));
			}
			safe.Finish();

			gpr.UnlockAll();
			fpr.ReleaseSpillLocks();
		}
		break;

	case 58: //sv.s   // Memory::Write_U32(VI(vt), addr);
		{
			gpr.MapReg(rs, true, true);

			// Even if we don't use real SIMD there's still 8 or 16 scalar float registers.
			fpr.MapRegV(vt, 0);

			JitSafeMem safe(this, rs, imm);
			safe.SetFar();
			OpArg dest;
			if (safe.PrepareWrite(dest, 4))
			{
				MOVSS(safe.NextFastAddress(0), fpr.VX(vt));
			}
			if (safe.PrepareSlowWrite())
			{
				MOVSS(M(&ssLoadStoreTemp), fpr.VX(vt));
				safe.DoSlowWrite(safeMemFuncs.writeU32, M(&ssLoadStoreTemp), 0);
			}
			safe.Finish();

			fpr.ReleaseSpillLocks();
			gpr.UnlockAll();
		}
		break;

	default:
		DISABLE;
	}
}

void Jit::Comp_SVQ(MIPSOpcode op)
{
	CONDITIONAL_DISABLE;

	int imm = (signed short)(op&0xFFFC);
	int vt = (((op >> 16) & 0x1f)) | ((op&1) << 5);
	MIPSGPReg rs = _RS;

	switch (op >> 26)
	{
	case 53: //lvl.q/lvr.q
		{
			if (!g_Config.bFastMemory) {
				DISABLE;
			}
			DISABLE;

			gpr.MapReg(rs, true, true);
			gpr.FlushLockX(ECX);
			u8 vregs[4];
			GetVectorRegs(vregs, V_Quad, vt);
			MOV(32, R(EAX), gpr.R(rs));
			ADD(32, R(EAX), Imm32(imm));
#ifdef _M_IX86
			AND(32, R(EAX), Imm32(Memory::MEMVIEW32_MASK));
#endif
			MOV(32, R(ECX), R(EAX));
			SHR(32, R(EAX), Imm8(2));
			AND(32, R(EAX), Imm32(0x3));
			CMP(32, R(EAX), Imm32(0));
			FixupBranch next = J_CC(CC_NE);

			fpr.MapRegsV(vregs, V_Quad, MAP_DIRTY);

			// Offset = 0
			MOVSS(fpr.RX(vregs[3]), MRegSum(RBX, RAX));

			FixupBranch skip0 = J();
			SetJumpTarget(next);
			CMP(32, R(EAX), Imm32(1));
			next = J_CC(CC_NE);

			// Offset = 1
			MOVSS(fpr.RX(vregs[3]), MComplex(RBX, RAX, 1, 4));
			MOVSS(fpr.RX(vregs[2]), MComplex(RBX, RAX, 1, 0));

			FixupBranch skip1 = J();
			SetJumpTarget(next);
			CMP(32, R(EAX), Imm32(2));
			next = J_CC(CC_NE);

			// Offset = 2
			MOVSS(fpr.RX(vregs[3]), MComplex(RBX, RAX, 1, 8));
			MOVSS(fpr.RX(vregs[2]), MComplex(RBX, RAX, 1, 4));
			MOVSS(fpr.RX(vregs[1]), MComplex(RBX, RAX, 1, 0));

			FixupBranch skip2 = J();
			SetJumpTarget(next);
			CMP(32, R(EAX), Imm32(3));
			next = J_CC(CC_NE);

			// Offset = 3
			MOVSS(fpr.RX(vregs[3]), MComplex(RBX, RAX, 1, 12));
			MOVSS(fpr.RX(vregs[2]), MComplex(RBX, RAX, 1, 8));
			MOVSS(fpr.RX(vregs[1]), MComplex(RBX, RAX, 1, 4));
			MOVSS(fpr.RX(vregs[0]), MComplex(RBX, RAX, 1, 0));

			SetJumpTarget(next);
			SetJumpTarget(skip0);
			SetJumpTarget(skip1);
			SetJumpTarget(skip2);

			gpr.UnlockAll();
			fpr.ReleaseSpillLocks();
		}
		break;

	case 54: //lv.q
		{
			gpr.MapReg(rs, true, true);
	
			u8 vregs[4];
			GetVectorRegs(vregs, V_Quad, vt);
			fpr.MapRegsV(vregs, V_Quad, MAP_DIRTY | MAP_NOINIT);

			JitSafeMem safe(this, rs, imm);
			safe.SetFar();
			OpArg src;
			if (safe.PrepareRead(src, 16))
			{
				// Just copy 4 words the easiest way while not wasting registers.
				for (int i = 0; i < 4; i++)
					MOVSS(fpr.VX(vregs[i]), safe.NextFastAddress(i * 4));
			}
			if (safe.PrepareSlowRead(safeMemFuncs.readU32))
			{
				for (int i = 0; i < 4; i++)
				{
					safe.NextSlowRead(safeMemFuncs.readU32, i * 4);
					MOVD_xmm(fpr.VX(vregs[i]), R(EAX));
				}
			}
			safe.Finish();

			gpr.UnlockAll();
			fpr.ReleaseSpillLocks();
		}
		break;

	case 62: //sv.q
		{
			gpr.MapReg(rs, true, true);

			u8 vregs[4];
			GetVectorRegs(vregs, V_Quad, vt);
			// Even if we don't use real SIMD there's still 8 or 16 scalar float registers.
			fpr.MapRegsV(vregs, V_Quad, 0);

			JitSafeMem safe(this, rs, imm);
			safe.SetFar();
			OpArg dest;
			if (safe.PrepareWrite(dest, 16))
			{
				for (int i = 0; i < 4; i++)
					MOVSS(safe.NextFastAddress(i * 4), fpr.VX(vregs[i]));
			}
			if (safe.PrepareSlowWrite())
			{
				for (int i = 0; i < 4; i++)
				{
					MOVSS(M(&ssLoadStoreTemp), fpr.VX(vregs[i]));
					safe.DoSlowWrite(safeMemFuncs.writeU32, M(&ssLoadStoreTemp), i * 4);
				}
			}
			safe.Finish();

			gpr.UnlockAll();
			fpr.ReleaseSpillLocks();
		}
		break;

	default:
		DISABLE;
		break;
	}
}

void Jit::Comp_VVectorInit(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	switch ((op >> 16) & 0xF)
	{
	case 6: // v=zeros; break;  //vzero
		MOVSS(XMM0, M(&zero));
		break;
	case 7: // v=ones; break;   //vone
		MOVSS(XMM0, M(&one));
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
		MOVSS(fpr.VX(dregs[i]), R(XMM0));
	ApplyPrefixD(dregs, sz);

	fpr.ReleaseSpillLocks();
}



void Jit::Comp_VIdt(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	if (js.HasUnknownPrefix())
		DISABLE;

	int vd = _VD;
	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);
	XORPS(XMM0, R(XMM0));
	MOVSS(XMM1, M(&one));
	u8 dregs[4];
	GetVectorRegsPrefixD(dregs, sz, _VD);
	fpr.MapRegsV(dregs, sz, MAP_NOINIT | MAP_DIRTY);
	switch (sz)
	{
	case V_Pair:
		MOVSS(fpr.VX(dregs[0]), R((vd&1)==0 ? XMM1 : XMM0));
		MOVSS(fpr.VX(dregs[1]), R((vd&1)==1 ? XMM1 : XMM0));
		break;
	case V_Quad:
		MOVSS(fpr.VX(dregs[0]), R((vd&3)==0 ? XMM1 : XMM0));
		MOVSS(fpr.VX(dregs[1]), R((vd&3)==1 ? XMM1 : XMM0));
		MOVSS(fpr.VX(dregs[2]), R((vd&3)==2 ? XMM1 : XMM0));
		MOVSS(fpr.VX(dregs[3]), R((vd&3)==3 ? XMM1 : XMM0));
		break;
	default:
		_dbg_assert_msg_(CPU,0,"Trying to interpret instruction that can't be interpreted");
		break;
	}
	ApplyPrefixD(dregs, sz);
	fpr.ReleaseSpillLocks();
}


void Jit::Comp_VDot(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);
	
	// TODO: Force read one of them into regs? probably not.
	u8 sregs[4], tregs[4], dregs[1];
	GetVectorRegsPrefixS(sregs, sz, _VS);
	GetVectorRegsPrefixT(tregs, sz, _VT);
	GetVectorRegsPrefixD(dregs, V_Single, _VD);

	X64Reg tempxreg = XMM0;
	if (IsOverlapSafe(dregs[0], 0, n, sregs, n, tregs))
	{
		fpr.MapRegsV(dregs, V_Single, MAP_NOINIT);
		tempxreg = fpr.VX(dregs[0]);
	}

	// Need to start with +0.0f so it doesn't result in -0.0f.
	MOVSS(tempxreg, fpr.V(sregs[0]));
	MULSS(tempxreg, fpr.V(tregs[0]));
	for (int i = 1; i < n; i++)
	{
		// sum += s[i]*t[i];
		MOVSS(XMM1, fpr.V(sregs[i]));
		MULSS(XMM1, fpr.V(tregs[i]));
		ADDSS(tempxreg, R(XMM1));
	}

	if (!fpr.V(dregs[0]).IsSimpleReg(tempxreg)) {
		fpr.MapRegsV(dregs, V_Single, MAP_NOINIT);
		MOVSS(fpr.V(dregs[0]), tempxreg);
	}

	ApplyPrefixD(dregs, V_Single);

	fpr.ReleaseSpillLocks();
}


void Jit::Comp_VHdp(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	u8 sregs[4], tregs[4], dregs[1];
	GetVectorRegsPrefixS(sregs, sz, _VS);
	GetVectorRegsPrefixT(tregs, sz, _VT);
	GetVectorRegsPrefixD(dregs, V_Single, _VD);

	X64Reg tempxreg = XMM0;
	if (IsOverlapSafe(dregs[0], 0, n, sregs, n, tregs))
	{
		fpr.MapRegsV(dregs, V_Single, MAP_NOINIT);
		tempxreg = fpr.VX(dregs[0]);
	}

	// Need to start with +0.0f so it doesn't result in -0.0f.
	MOVSS(tempxreg, fpr.V(sregs[0]));
	MULSS(tempxreg, fpr.V(tregs[0]));
	for (int i = 1; i < n; i++)
	{
		// sum += (i == n-1) ? t[i] : s[i]*t[i];
		if (i == n - 1) {
			ADDSS(tempxreg, fpr.V(tregs[i]));
		} else {
			MOVSS(XMM1, fpr.V(sregs[i]));
			MULSS(XMM1, fpr.V(tregs[i]));
			ADDSS(tempxreg, R(XMM1));
		}
	}

	if (!fpr.V(dregs[0]).IsSimpleReg(tempxreg)) {
		fpr.MapRegsV(dregs, V_Single, MAP_NOINIT);
		MOVSS(fpr.V(dregs[0]), tempxreg);
	}

	ApplyPrefixD(dregs, V_Single);

	fpr.ReleaseSpillLocks();
}

void Jit::Comp_VCrossQuat(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	u8 sregs[4], tregs[4], dregs[4];
	GetVectorRegs(sregs, sz, _VS);
	GetVectorRegs(tregs, sz, _VT);
	GetVectorRegs(dregs, sz, _VD);

	if (sz == V_Triple) {
		// Cross product vcrsp.t

		fpr.MapRegsV(sregs, sz, 0);
	
		// Compute X
		MOVSS(XMM0, fpr.V(sregs[1]));
		MULSS(XMM0, fpr.V(tregs[2]));
		MOVSS(XMM1, fpr.V(sregs[2]));
		MULSS(XMM1, fpr.V(tregs[1]));
		SUBSS(XMM0, R(XMM1));
		MOVSS(fpr.V(dregs[0]), XMM0);

		// Compute Y
		MOVSS(XMM0, fpr.V(sregs[2]));
		MULSS(XMM0, fpr.V(tregs[0]));
		MOVSS(XMM1, fpr.V(sregs[0]));
		MULSS(XMM1, fpr.V(tregs[2]));
		SUBSS(XMM0, R(XMM1));
		MOVSS(fpr.V(dregs[1]), XMM0);

		// Compute Z
		MOVSS(XMM0, fpr.V(sregs[0]));
		MULSS(XMM0, fpr.V(tregs[1]));
		MOVSS(XMM1, fpr.V(sregs[1]));
		MULSS(XMM1, fpr.V(tregs[0]));
		SUBSS(XMM0, R(XMM1));
		MOVSS(fpr.V(dregs[2]), XMM0);
	} else if (sz == V_Quad) {
		// Quaternion product  vqmul.q  untested
		DISABLE;

		fpr.MapRegsV(sregs, sz, 0);

		// Compute X
		MOVSS(XMM0, fpr.V(sregs[0]));
		MULSS(XMM0, fpr.V(tregs[3]));
		MOVSS(XMM1, fpr.V(sregs[1]));
		MULSS(XMM1, fpr.V(tregs[2]));
		ADDSS(XMM0, R(XMM1));
		MOVSS(XMM1, fpr.V(sregs[2]));
		MULSS(XMM1, fpr.V(tregs[1]));
		SUBSS(XMM0, R(XMM1));
		MOVSS(XMM1, fpr.V(sregs[3]));
		MULSS(XMM1, fpr.V(tregs[0]));
		ADDSS(XMM0, R(XMM1));
		MOVSS(fpr.V(dregs[0]), XMM0);

		// Compute Y
		MOVSS(XMM0, fpr.V(sregs[1]));
		MULSS(XMM0, fpr.V(tregs[3]));
		MOVSS(XMM1, fpr.V(sregs[2]));
		MULSS(XMM1, fpr.V(tregs[0]));
		ADDSS(XMM0, R(XMM1));
		MOVSS(XMM1, fpr.V(sregs[3]));
		MULSS(XMM1, fpr.V(tregs[1]));
		ADDSS(XMM0, R(XMM1));
		MOVSS(XMM1, fpr.V(sregs[0]));
		MULSS(XMM1, fpr.V(tregs[2]));
		SUBSS(XMM0, R(XMM1));
		MOVSS(fpr.V(dregs[1]), XMM0);

		// Compute Z
		MOVSS(XMM0, fpr.V(sregs[0]));
		MULSS(XMM0, fpr.V(tregs[1]));
		MOVSS(XMM1, fpr.V(sregs[1]));
		MULSS(XMM1, fpr.V(tregs[0]));
		SUBSS(XMM0, R(XMM1));
		MOVSS(XMM1, fpr.V(sregs[2]));
		MULSS(XMM1, fpr.V(tregs[3]));
		ADDSS(XMM0, R(XMM1));
		MOVSS(XMM1, fpr.V(sregs[3]));
		MULSS(XMM1, fpr.V(tregs[2]));
		ADDSS(XMM0, R(XMM1));
		MOVSS(fpr.V(dregs[2]), XMM0);

		// Compute W
		MOVSS(XMM0, fpr.V(sregs[3]));
		MULSS(XMM0, fpr.V(tregs[3]));
		MOVSS(XMM1, fpr.V(sregs[1]));
		MULSS(XMM1, fpr.V(tregs[1]));
		SUBSS(XMM0, R(XMM1));
		MOVSS(XMM1, fpr.V(sregs[2]));
		MULSS(XMM1, fpr.V(tregs[2]));
		ADDSS(XMM0, R(XMM1));
		MOVSS(XMM1, fpr.V(sregs[0]));
		MULSS(XMM1, fpr.V(tregs[0]));
		SUBSS(XMM0, R(XMM1));
		MOVSS(fpr.V(dregs[3]), XMM0);
	}

	fpr.ReleaseSpillLocks();
}

void Jit::Comp_Vcmov(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

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
		TEST(32, M(&currentMIPS->vfpuCtrl[VFPU_CTRL_CC]), Imm32(1 << imm3));
		fpr.MapRegsV(dregs, sz, MAP_DIRTY);
		FixupBranch skip = J_CC(tf ? CC_NZ : CC_Z, true);
		for (int i = 0; i < n; i++) {
			MOVSS(fpr.VX(dregs[i]), fpr.V(sregs[i]));
		}
		SetJumpTarget(skip);
	} else {
		// Look at the bottom four bits of CC to individually decide if the subregisters should be copied.
		MOV(32, R(EAX), M(&currentMIPS->vfpuCtrl[VFPU_CTRL_CC]));

		fpr.MapRegsV(dregs, sz, MAP_DIRTY);
		for (int i = 0; i < n; i++) {
			TEST(32, R(EAX), Imm32(1 << i));
			FixupBranch skip = J_CC(tf ? CC_NZ : CC_Z, true);
			MOVSS(fpr.VX(dregs[i]), fpr.V(sregs[i]));
			SetJumpTarget(skip);
		}
	}

	ApplyPrefixD(dregs, sz);

	fpr.ReleaseSpillLocks();
}

void Jit::Comp_VecDo3(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	// Check that we can support the ops, and prepare temporary values for ops that need it.
	switch (op >> 26) {
	case 24: //VFPU0
		switch ((op >> 23) & 7) {
		case 0: // d[i] = s[i] + t[i]; break; //vadd
		case 1: // d[i] = s[i] - t[i]; break; //vsub
		case 7: // d[i] = s[i] / t[i]; break; //vdiv
			break;
		default:
			DISABLE;
		}
		break;
	case 25: //VFPU1
		switch ((op >> 23) & 7) {
		case 0: // d[i] = s[i] * t[i]; break; //vmul
			break;
		default:
			DISABLE;
		}
		break;
	case 27: //VFPU3
		switch ((op >> 23) & 7) {
		case 2:  // vmin
		case 3:  // vmax
			break;
		case 6:  // vsge
		case 7:  // vslt
			break;
		default:
			DISABLE;
		}
		break;
	default:
		DISABLE;
		break;
	}

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	u8 sregs[4], tregs[4], dregs[4];
	GetVectorRegsPrefixS(sregs, sz, _VS);
	GetVectorRegsPrefixT(tregs, sz, _VT);
	GetVectorRegsPrefixD(dregs, sz, _VD);

	X64Reg tempxregs[4];
	for (int i = 0; i < n; ++i)
	{
		if (!IsOverlapSafeAllowS(dregs[i], i, n, sregs, n, tregs))
		{
			// On 32-bit we only have 6 xregs for mips regs, use XMM0/XMM1 if possible.
			if (i < 2)
				tempxregs[i] = (X64Reg) (XMM0 + i);
			else
			{
				int reg = fpr.GetTempV();
				fpr.MapRegV(reg, MAP_NOINIT | MAP_DIRTY);
				fpr.SpillLockV(reg);
				tempxregs[i] = fpr.VX(reg);
			}
		}
		else
		{
			fpr.MapRegV(dregs[i], (dregs[i] == sregs[i] ? 0 : MAP_NOINIT) | MAP_DIRTY);
			fpr.SpillLockV(dregs[i]);
			tempxregs[i] = fpr.VX(dregs[i]);
		}
	}

	for (int i = 0; i < n; ++i)
	{
		if (!fpr.V(sregs[i]).IsSimpleReg(tempxregs[i]))
			MOVSS(tempxregs[i], fpr.V(sregs[i]));
	}

	for (int i = 0; i < n; ++i) {
		switch (op >> 26) {
		case 24: //VFPU0
			switch ((op >> 23) & 7) {
			case 0: // d[i] = s[i] + t[i]; break; //vadd
				ADDSS(tempxregs[i], fpr.V(tregs[i]));
				break;
			case 1: // d[i] = s[i] - t[i]; break; //vsub
				SUBSS(tempxregs[i], fpr.V(tregs[i]));
				break;
			case 7: // d[i] = s[i] / t[i]; break; //vdiv
				DIVSS(tempxregs[i], fpr.V(tregs[i]));
				break;
			}
			break;
		case 25: //VFPU1
			switch ((op >> 23) & 7)
			{
			case 0: // d[i] = s[i] * t[i]; break; //vmul
				MULSS(tempxregs[i], fpr.V(tregs[i]));
				break;
			}
			break;
		case 27: //VFPU3
			switch ((op >> 23) & 7)
			{
			case 2:  // vmin
				// TODO: Mishandles NaN.
				MINSS(tempxregs[i], fpr.V(tregs[i]));
				break;
			case 3:  // vmax
				// TODO: Mishandles NaN.
				MAXSS(tempxregs[i], fpr.V(tregs[i]));
				break;
			case 6:  // vsge
				// TODO: Mishandles NaN.
				CMPNLTSS(tempxregs[i], fpr.V(tregs[i]));
				ANDPS(tempxregs[i], M(&oneOneOneOne));
				break;
			case 7:  // vslt
				CMPLTSS(tempxregs[i], fpr.V(tregs[i]));
				ANDPS(tempxregs[i], M(&oneOneOneOne));
				break;
			}
			break;
		}
	}

	for (int i = 0; i < n; ++i)
	{
		if (!fpr.V(dregs[i]).IsSimpleReg(tempxregs[i]))
			MOVSS(fpr.V(dregs[i]), tempxregs[i]);
	}

	ApplyPrefixD(dregs, sz);

	fpr.ReleaseSpillLocks();
}

static float ssCompareTemp;

void Jit::Comp_Vcmp(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	VCondition cond = (VCondition)(op & 0xF);

	u8 sregs[4], tregs[4];
	GetVectorRegsPrefixS(sregs, sz, _VS);
	GetVectorRegsPrefixT(tregs, sz, _VT);

	// Some, we just fall back to the interpreter.
	switch (cond) {
	case VC_EI: // c = my_isinf(s[i]); break;
	case VC_NI: // c = !my_isinf(s[i]); break;
		DISABLE;
		break;
	case VC_ES: // c = my_isnan(s[i]) || my_isinf(s[i]); break;   // Tekken Dark Resurrection
	case VC_NS: // c = !my_isnan(s[i]) && !my_isinf(s[i]); break;
	case VC_EN: // c = my_isnan(s[i]); break;
	case VC_NN: // c = !my_isnan(s[i]); break;
		if (_VS != _VT)
			DISABLE;
		break;
	default:
		break;
	}

	// First, let's get the trivial ones.

	static const int true_bits[4] = {0x31, 0x33, 0x37, 0x3f};

	if (cond == VC_TR) {
		OR(32, M(&currentMIPS->vfpuCtrl[VFPU_CTRL_CC]), Imm32(true_bits[n-1]));
		return;
	} else if (cond == VC_FL) {
		AND(32, M(&currentMIPS->vfpuCtrl[VFPU_CTRL_CC]), Imm32(~true_bits[n-1]));
		return;
	}

	gpr.FlushLockX(ECX);
	if (cond == VC_EZ || cond == VC_NZ)
		XORPS(XMM0, R(XMM0));

	int affected_bits = (1 << 4) | (1 << 5);  // 4 and 5
	for (int i = 0; i < n; ++i) {
		fpr.MapRegV(sregs[i], 0);
		// Let's only handle the easy ones, and fall back on the interpreter for the rest.
		bool compareTwo = false;
		bool compareToZero = false;
		int comparison = -1;
		bool flip = false;
		bool inverse = false;

		switch (cond) {
		case VC_ES:
			comparison = -1;  // We will do the compare up here. XMM1 will have the bits.
			MOVSS(XMM1, fpr.V(sregs[i]));
			ANDPS(XMM1, M(&fourinfnan));
			PCMPEQD(XMM1, M(&fourinfnan));  // Integer comparison
			break;

		case VC_NS:
			comparison = -1;  // We will do the compare up here. XMM1 will have the bits.
			MOVSS(XMM1, fpr.V(sregs[i]));
			ANDPS(XMM1, M(&fourinfnan));
			PCMPEQD(XMM1, M(&fourinfnan));  // Integer comparison
			XORPS(XMM1, M(&solidOnes));
			break;

		case VC_EN:
			comparison = CMP_UNORD;
			compareTwo = true;
			break;

		case VC_NN:
			comparison = CMP_UNORD;
			compareTwo = true;
			inverse = true;
			break;

		case VC_EQ: // c = s[i] == t[i]; break;
			comparison = CMP_EQ;
			compareTwo = true;
			break;

		case VC_LT: // c = s[i] < t[i]; break;
			comparison = CMP_LT;
			compareTwo = true;
			break;

		case VC_LE: // c = s[i] <= t[i]; break;
			comparison = CMP_LE;
			compareTwo = true;
			break;

		case VC_NE: // c = s[i] != t[i]; break;
			comparison = CMP_NEQ;
			compareTwo = true;
			break;

		case VC_GE: // c = s[i] >= t[i]; break;
			comparison = CMP_LE;
			flip = true;
			compareTwo = true;
			break;

		case VC_GT: // c = s[i] > t[i]; break;
			comparison = CMP_LT;
			flip = true;
			compareTwo = true;
			break;

		case VC_EZ: // c = s[i] == 0.0f || s[i] == -0.0f; break;
			comparison = CMP_EQ;
			compareToZero = true;
			break;

		case VC_NZ: // c = s[i] != 0; break;
			comparison = CMP_NEQ;
			compareToZero = true;
			break;

		default:
			DISABLE;
		}

		if (comparison != -1) {
			if (compareTwo) {
				if (!flip) {
					MOVSS(XMM1, fpr.V(sregs[i]));
					CMPSS(XMM1, fpr.V(tregs[i]), comparison);
				} else {
					MOVSS(XMM1, fpr.V(tregs[i]));
					CMPSS(XMM1, fpr.V(sregs[i]), comparison);
				}
			} else if (compareToZero) {
				MOVSS(XMM1, fpr.V(sregs[i]));
				CMPSS(XMM1, R(XMM0), comparison);
			}
			if (inverse) {
				XORPS(XMM1, M(&solidOnes));
			}
		}

		MOVSS(M(&ssCompareTemp), XMM1);
		if (i == 0 && n == 1) {
			MOV(32, R(EAX), M(&ssCompareTemp));
			AND(32, R(EAX), Imm32(0x31));
		} else if (i == 0) {
			MOV(32, R(EAX), M(&ssCompareTemp));
			AND(32, R(EAX), Imm32(1 << i));
		} else {
			MOV(32, R(ECX), M(&ssCompareTemp));
			AND(32, R(ECX), Imm32(1 << i));
			OR(32, R(EAX), R(ECX));
		}
		affected_bits |= 1 << i;
	}

	// Aggregate the bits. Urgh, expensive. Can optimize for the case of one comparison, which is the most common
	// after all.
	if (n > 1) {
		CMP(32, R(EAX), Imm8(affected_bits & 0xF));
		SETcc(CC_E, R(ECX));
		SHL(32, R(ECX), Imm8(5));
		OR(32, R(EAX), R(ECX));

		CMP(32, R(EAX), Imm8(0));
		SETcc(CC_NZ, R(ECX));
		SHL(32, R(ECX), Imm8(4));
		OR(32, R(EAX), R(ECX));
	}

	MOV(32, R(ECX), M(&currentMIPS->vfpuCtrl[VFPU_CTRL_CC]));
	AND(32, R(ECX), Imm32(~affected_bits));
	OR(32, R(ECX), R(EAX));
	MOV(32, M(&currentMIPS->vfpuCtrl[VFPU_CTRL_CC]), R(ECX));

	fpr.ReleaseSpillLocks();
	gpr.UnlockAllX();
}

// There are no immediates for floating point, so we need to load these
// from RAM. Might as well have a table ready.
extern const float mulTableVi2f[32] = {
	1.0f/(1UL<<0),1.0f/(1UL<<1),1.0f/(1UL<<2),1.0f/(1UL<<3),
	1.0f/(1UL<<4),1.0f/(1UL<<5),1.0f/(1UL<<6),1.0f/(1UL<<7),
	1.0f/(1UL<<8),1.0f/(1UL<<9),1.0f/(1UL<<10),1.0f/(1UL<<11),
	1.0f/(1UL<<12),1.0f/(1UL<<13),1.0f/(1UL<<14),1.0f/(1UL<<15),
	1.0f/(1UL<<16),1.0f/(1UL<<17),1.0f/(1UL<<18),1.0f/(1UL<<19),
	1.0f/(1UL<<20),1.0f/(1UL<<21),1.0f/(1UL<<22),1.0f/(1UL<<23),
	1.0f/(1UL<<24),1.0f/(1UL<<25),1.0f/(1UL<<26),1.0f/(1UL<<27),
	1.0f/(1UL<<28),1.0f/(1UL<<29),1.0f/(1UL<<30),1.0f/(1UL<<31),
};

void Jit::Comp_Vi2f(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	int imm = (op >> 16) & 0x1f;
	const float *mult = &mulTableVi2f[imm];

	u8 sregs[4], dregs[4];
	GetVectorRegsPrefixS(sregs, sz, _VS);
	GetVectorRegsPrefixD(dregs, sz, _VD);

	int tempregs[4];
	for (int i = 0; i < n; ++i) {
		if (!IsOverlapSafe(dregs[i], i, n, sregs)) {
			tempregs[i] = fpr.GetTempV();
		} else {
			tempregs[i] = dregs[i];
		}
	}

	if (*mult != 1.0f)
		MOVSS(XMM1, M(mult));
	for (int i = 0; i < n; i++) {
		if (fpr.V(sregs[i]).IsSimpleReg())
			MOVD_xmm(R(EAX), fpr.VX(sregs[i]));
		else
			MOV(32, R(EAX), fpr.V(sregs[i]));
		CVTSI2SS(XMM0, R(EAX));
		if (*mult != 1.0f)
			MULSS(XMM0, R(XMM1));
		fpr.MapRegV(tempregs[i], MAP_DIRTY);
		MOVSS(fpr.V(tempregs[i]), XMM0);
	}

	for (int i = 0; i < n; ++i) {
		if (dregs[i] != tempregs[i]) {
			fpr.MapRegV(dregs[i], MAP_DIRTY | MAP_NOINIT);
			MOVSS(fpr.VX(dregs[i]), fpr.V(tempregs[i]));
		}
	}

	ApplyPrefixD(dregs, sz);
	fpr.ReleaseSpillLocks();
}

// Planning for true SIMD

// Sequence for gathering sparse registers into one SIMD:
// MOVSS(XMM0, fpr.R(sregs[0]));
// MOVSS(XMM1, fpr.R(sregs[1]));
// MOVSS(XMM2, fpr.R(sregs[2]));
// MOVSS(XMM3, fpr.R(sregs[3]));
// SHUFPS(XMM0, R(XMM1), _MM_SHUFFLE(0, 0, 0, 0));   // XMM0 = S1 S1 S0 S0
// SHUFPS(XMM2, R(XMM3), _MM_SHUFFLE(0, 0, 0, 0));   // XMM2 = S3 S3 S2 S2
// SHUFPS(XMM0, R(XMM2), _MM_SHUFFLE(2, 0, 2, 0));   // XMM0 = S3 S2 S1 S0
// Some punpckwd etc would also work.
// Alternatively, MOVSS and three PINSRD (SSE4) with mem source.
// Why PINSRD instead of INSERTPS?
// http://software.intel.com/en-us/blogs/2009/01/07/using-sse41-for-mp3-encoding-quantization

// Sequence for scattering a SIMD register to sparse registers:
// (Very serial though, better methods may be possible)
// MOVSS(fpr.R(sregs[0]), XMM0);
// SHUFPS(XMM0, R(XMM0), _MM_SHUFFLE(3, 3, 2, 1));
// MOVSS(fpr.R(sregs[1]), XMM0);
// SHUFPS(XMM0, R(XMM0), _MM_SHUFFLE(3, 3, 2, 1));
// MOVSS(fpr.R(sregs[2]), XMM0);
// SHUFPS(XMM0, R(XMM0), _MM_SHUFFLE(3, 3, 2, 1));
// MOVSS(fpr.R(sregs[3]), XMM0);
// On SSE4 we should use EXTRACTPS.

// Translation of ryg's half_to_float5_SSE2
void Jit::Comp_Vh2f(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	if (js.HasUnknownPrefix())
		DISABLE;

#define SSE_CONST4(name, val) static const u32 MEMORY_ALIGNED16(name[4]) = { (val), (val), (val), (val) }

	SSE_CONST4(mask_nosign,         0x7fff);
	SSE_CONST4(magic,               (254 - 15) << 23);
	SSE_CONST4(was_infnan,          0x7bff);
	SSE_CONST4(exp_infnan,          255 << 23);
	
#undef SSE_CONST4
	VectorSize sz = GetVecSize(op);
	VectorSize outsize;
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

	u8 sregs[4], dregs[4];
	GetVectorRegsPrefixS(sregs, sz, _VS);
	GetVectorRegsPrefixD(dregs, outsize, _VD);

	// Force ourselves an extra xreg as temp space.
	X64Reg tempR = fpr.GetFreeXReg();
	
	MOVSS(XMM0, fpr.V(sregs[0]));
 	if (sz != V_Single) {
		MOVSS(XMM1, fpr.V(sregs[1]));
		PUNPCKLDQ(XMM0, R(XMM1));
	}
	XORPS(XMM1, R(XMM1));
	PUNPCKLWD(XMM0, R(XMM1));

	// OK, 16 bits in each word.
	// Let's go. Deep magic here.
	MOVAPS(XMM1, R(XMM0));
	ANDPS(XMM0, M(mask_nosign)); // xmm0 = expmant
	XORPS(XMM1, R(XMM0));  // xmm1 = justsign = expmant ^ xmm0
	MOVAPS(tempR, R(XMM0));
	PCMPGTD(tempR, M(was_infnan));  // xmm2 = b_wasinfnan
	PSLLD(XMM0, 13);
	MULPS(XMM0, M(magic));  /// xmm0 = scaled
	PSLLD(XMM1, 16);  // xmm1 = sign
	ANDPS(tempR, M(exp_infnan));
	ORPS(XMM1, R(tempR));
	ORPS(XMM0, R(XMM1));

	fpr.MapRegsV(dregs, outsize, MAP_NOINIT | MAP_DIRTY);  

	// TODO: Could apply D-prefix in parallel here...

	MOVSS(fpr.V(dregs[0]), XMM0);
	SHUFPS(XMM0, R(XMM0), _MM_SHUFFLE(3, 3, 2, 1));
	MOVSS(fpr.V(dregs[1]), XMM0);

	if (sz != V_Single) {
		SHUFPS(XMM0, R(XMM0), _MM_SHUFFLE(3, 3, 2, 1));
		MOVSS(fpr.V(dregs[2]), XMM0);
		SHUFPS(XMM0, R(XMM0), _MM_SHUFFLE(3, 3, 2, 1));
		MOVSS(fpr.V(dregs[3]), XMM0);
	}

	ApplyPrefixD(dregs, outsize);
	gpr.UnlockAllX();
	fpr.ReleaseSpillLocks();
}

void Jit::Comp_Vx2i(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	if (js.HasUnknownPrefix())
		DISABLE;

	switch ((op >> 16) & 3) {
	case 0:  // vuc2i  
	case 1:  // vc2i
	case 2:  // vus2i
		DISABLE;

	case 3:  // vs2i - used heavily by GTA
		break;
	default:
		DISABLE;
	}

	// OK this is vs2i. Unpacks pairs of 16-bit integers into 32-bit integers, with the values
	// at the top.
	// Let's do this similarly as h2f - we do a solution that works for both singles and pairs
	// then use it for both.

	VectorSize sz = GetVecSize(op);
	VectorSize outsize;
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

	u8 sregs[4], dregs[4];
	GetVectorRegsPrefixS(sregs, sz, _VS);
	GetVectorRegsPrefixD(dregs, outsize, _VD);

	MOVSS(XMM1, fpr.V(sregs[0]));
	if (sz != V_Single) {
		MOVSS(XMM0, fpr.V(sregs[1]));
		PUNPCKLDQ(XMM1, R(XMM0));
	}

	// Unpack 16-bit words into 32-bit words, upper position, and we're done!
	XORPS(XMM0, R(XMM0));
	PUNPCKLWD(XMM0, R(XMM1));
	
	// Done! TODO: The rest of this should be possible to extract into a function.
	fpr.MapRegsV(dregs, outsize, MAP_NOINIT | MAP_DIRTY);  

	// TODO: Could apply D-prefix in parallel here...

	MOVSS(fpr.V(dregs[0]), XMM0);
	SHUFPS(XMM0, R(XMM0), _MM_SHUFFLE(3, 3, 2, 1));
	MOVSS(fpr.V(dregs[1]), XMM0);

	if (sz != V_Single) {
		SHUFPS(XMM0, R(XMM0), _MM_SHUFFLE(3, 3, 2, 1));
		MOVSS(fpr.V(dregs[2]), XMM0);
		SHUFPS(XMM0, R(XMM0), _MM_SHUFFLE(3, 3, 2, 1));
		MOVSS(fpr.V(dregs[3]), XMM0);
	}

	ApplyPrefixD(dregs, outsize);
	gpr.UnlockAllX();
	fpr.ReleaseSpillLocks();
}

extern const double mulTableVf2i[32] = {
	(1ULL<<0),(1ULL<<1),(1ULL<<2),(1ULL<<3),
	(1ULL<<4),(1ULL<<5),(1ULL<<6),(1ULL<<7),
	(1ULL<<8),(1ULL<<9),(1ULL<<10),(1ULL<<11),
	(1ULL<<12),(1ULL<<13),(1ULL<<14),(1ULL<<15),
	(1ULL<<16),(1ULL<<17),(1ULL<<18),(1ULL<<19),
	(1ULL<<20),(1ULL<<21),(1ULL<<22),(1ULL<<23),
	(1ULL<<24),(1ULL<<25),(1ULL<<26),(1ULL<<27),
	(1ULL<<28),(1ULL<<29),(1ULL<<30),(1ULL<<31),
};

static const float half = 0.5f;

static double maxIntAsDouble = (double)0x7fffffff;  // that's not equal to 0x80000000
static double minIntAsDouble = (double)(int)0x80000000;

void Jit::Comp_Vf2i(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	if (js.HasUnknownPrefix())
		DISABLE;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	int imm = (op >> 16) & 0x1f;
	const double *mult = &mulTableVf2i[imm];

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

	u8 tempregs[4];
	for (int i = 0; i < n; ++i) {
		if (!IsOverlapSafe(dregs[i], i, n, sregs)) {
			tempregs[i] = fpr.GetTempV();
		} else {
			tempregs[i] = dregs[i];
		}
	}

	if (*mult != 1.0f)
		MOVSD(XMM1, M(mult));

	fpr.MapRegsV(tempregs, sz, MAP_DIRTY | MAP_NOINIT);
	for (int i = 0; i < n; i++) {
		// Need to do this in double precision to clamp correctly as float
		// doesn't have enough precision to represent 0x7fffffff for example exactly.
		MOVSS(XMM0, fpr.V(sregs[i]));
		CVTSS2SD(XMM0, R(XMM0)); // convert to double precision
		if (*mult != 1.0f) {
			MULSD(XMM0, R(XMM1));
		}
		MINSD(XMM0, M(&maxIntAsDouble));
		MAXSD(XMM0, M(&minIntAsDouble));
		switch ((op >> 21) & 0x1f) {
		case 16: /* TODO */ break; //n  (round_vfpu_n causes issue #3011 but seems right according to tests...)
		case 17: CVTTSD2SI(EAX, R(XMM0)); break; //z - truncate
		case 18: /* TODO */ break; //u
		case 19: /* TODO */ break; //d
		}
		MOVD_xmm(fpr.VX(tempregs[i]), R(EAX));
	}

	for (int i = 0; i < n; ++i) {
		if (dregs[i] != tempregs[i]) {
			fpr.MapRegV(dregs[i], MAP_DIRTY | MAP_NOINIT);
			MOVSS(fpr.VX(dregs[i]), fpr.V(tempregs[i]));
		}
	}

	ApplyPrefixD(dregs, sz);
	fpr.ReleaseSpillLocks();
}

void Jit::Comp_Vcst(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	int conNum = (op >> 16) & 0x1f;
	int vd = _VD;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	u8 dregs[4];
	GetVectorRegsPrefixD(dregs, sz, _VD);

	MOVSS(XMM0, M(&cst_constants[conNum]));
	fpr.MapRegsV(dregs, sz, MAP_NOINIT | MAP_DIRTY);
	for (int i = 0; i < n; i++) {
		MOVSS(fpr.V(dregs[i]), XMM0);
	}
	ApplyPrefixD(dregs, sz);
	fpr.ReleaseSpillLocks();
}

void Jit::Comp_Vsgn(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	u8 sregs[4], dregs[4];
	GetVectorRegsPrefixS(sregs, sz, _VS);
	GetVectorRegsPrefixD(dregs, sz, _VD);

	X64Reg tempxregs[4];
	for (int i = 0; i < n; ++i)
	{
		if (!IsOverlapSafeAllowS(dregs[i], i, n, sregs))
		{
			int reg = fpr.GetTempV();
			fpr.MapRegV(reg, MAP_NOINIT | MAP_DIRTY);
			fpr.SpillLockV(reg);
			tempxregs[i] = fpr.VX(reg);
		}
		else
		{
			fpr.MapRegV(dregs[i], (dregs[i] == sregs[i] ? 0 : MAP_NOINIT) | MAP_DIRTY);
			fpr.SpillLockV(dregs[i]);
			tempxregs[i] = fpr.VX(dregs[i]);
		}
	}

	for (int i = 0; i < n; ++i)
	{
		XORPS(XMM0, R(XMM0));
		CMPEQSS(XMM0, fpr.V(sregs[i]));  // XMM0 = s[i] == 0.0f
		MOVSS(XMM1, fpr.V(sregs[i]));
		// Preserve sign bit, replace rest with ones
		ANDPS(XMM1, M(&signBitLower));
		ORPS(XMM1, M(&oneOneOneOne));
		// If really was equal to zero, zap. Note that ANDN negates the destination.
		ANDNPS(XMM0, R(XMM1));
		MOVAPS(tempxregs[i], R(XMM0));
	}

	for (int i = 0; i < n; ++i) {
		if (!fpr.V(dregs[i]).IsSimpleReg(tempxregs[i]))
			MOVSS(fpr.V(dregs[i]), tempxregs[i]);
	}

	ApplyPrefixD(dregs, sz);

	fpr.ReleaseSpillLocks();
}

void Jit::Comp_Vocp(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	u8 sregs[4], dregs[4];
	GetVectorRegsPrefixS(sregs, sz, _VS);
	GetVectorRegsPrefixD(dregs, sz, _VD);

	X64Reg tempxregs[4];
	for (int i = 0; i < n; ++i)
	{
		if (!IsOverlapSafeAllowS(dregs[i], i, n, sregs))
		{
			int reg = fpr.GetTempV();
			fpr.MapRegV(reg, MAP_NOINIT | MAP_DIRTY);
			fpr.SpillLockV(reg);
			tempxregs[i] = fpr.VX(reg);
		}
		else
		{
			fpr.MapRegV(dregs[i], (dregs[i] == sregs[i] ? 0 : MAP_NOINIT) | MAP_DIRTY);
			fpr.SpillLockV(dregs[i]);
			tempxregs[i] = fpr.VX(dregs[i]);
		}
	}

	MOVSS(XMM1, M(&one));
	for (int i = 0; i < n; ++i)
	{
		MOVSS(XMM0, R(XMM1));
		SUBSS(XMM0, fpr.V(sregs[i]));
		MOVSS(tempxregs[i], R(XMM0));
	}

	for (int i = 0; i < n; ++i) {
		if (!fpr.V(dregs[i]).IsSimpleReg(tempxregs[i]))
			MOVSS(fpr.V(dregs[i]), tempxregs[i]);
	}

	ApplyPrefixD(dregs, sz);

	fpr.ReleaseSpillLocks();
}

void Jit::Comp_VV2Op(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

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

	X64Reg tempxregs[4];
	for (int i = 0; i < n; ++i)
	{
		if (!IsOverlapSafeAllowS(dregs[i], i, n, sregs))
		{
			int reg = fpr.GetTempV();
			fpr.MapRegV(reg, MAP_NOINIT | MAP_DIRTY);
			fpr.SpillLockV(reg);
			tempxregs[i] = fpr.VX(reg);
		}
		else
		{
			fpr.MapRegV(dregs[i], (dregs[i] == sregs[i] ? 0 : MAP_NOINIT) | MAP_DIRTY);
			fpr.SpillLockV(dregs[i]);
			tempxregs[i] = fpr.VX(dregs[i]);
		}
	}

	// Warning: sregs[i] and tempxregs[i] may be the same reg.
	// Helps for vmov, hurts for vrcp, etc.
	for (int i = 0; i < n; ++i)
	{
		switch ((op >> 16) & 0x1f)
		{
		case 0: // d[i] = s[i]; break; //vmov
			// Probably for swizzle.
			if (!fpr.V(sregs[i]).IsSimpleReg(tempxregs[i]))
				MOVSS(tempxregs[i], fpr.V(sregs[i]));
			break;
		case 1: // d[i] = fabsf(s[i]); break; //vabs
			if (!fpr.V(sregs[i]).IsSimpleReg(tempxregs[i]))
				MOVSS(tempxregs[i], fpr.V(sregs[i]));
			ANDPS(tempxregs[i], M(&noSignMask));
			break;
		case 2: // d[i] = -s[i]; break; //vneg
			if (!fpr.V(sregs[i]).IsSimpleReg(tempxregs[i]))
				MOVSS(tempxregs[i], fpr.V(sregs[i]));
			XORPS(tempxregs[i], M(&signBitLower));
			break;
		case 4: // if (s[i] < 0) d[i] = 0; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;    // vsat0
			if (!fpr.V(sregs[i]).IsSimpleReg(tempxregs[i]))
				MOVSS(tempxregs[i], fpr.V(sregs[i]));

			// Zero out XMM0 if it was <= +0.0f (but skip NAN.)
			MOVSS(R(XMM0), tempxregs[i]);
			CMPLESS(XMM0, M(&zero));
			ANDNPS(XMM0, R(tempxregs[i]));

			// Retain a NAN in XMM0 (must be second operand.)
			MOVSS(tempxregs[i], M(&one));
			MINSS(tempxregs[i], R(XMM0));
			break;
		case 5: // if (s[i] < -1.0f) d[i] = -1.0f; else {if(s[i] > 1.0f) d[i] = 1.0f; else d[i] = s[i];} break;  // vsat1
			if (!fpr.V(sregs[i]).IsSimpleReg(tempxregs[i]))
				MOVSS(tempxregs[i], fpr.V(sregs[i]));

			// Check for < -1.0f, but careful of NANs.
			MOVSS(XMM1, M(&minus_one));
			MOVSS(R(XMM0), tempxregs[i]);
			CMPLESS(XMM0, R(XMM1));
			// If it was NOT less, the three ops below do nothing.
			// Otherwise, they replace the value with -1.0f.
			ANDPS(XMM1, R(XMM0));
			ANDNPS(XMM0, R(tempxregs[i]));
			ORPS(XMM0, R(XMM1));

			// Retain a NAN in XMM0 (must be second operand.)
			MOVSS(tempxregs[i], M(&one));
			MINSS(tempxregs[i], R(XMM0));
			break;
		case 16: // d[i] = 1.0f / s[i]; break; //vrcp
			MOVSS(XMM0, M(&one));
			DIVSS(XMM0, fpr.V(sregs[i]));
			MOVSS(tempxregs[i], R(XMM0));
			break;
		case 17: // d[i] = 1.0f / sqrtf(s[i]); break; //vrsq
			SQRTSS(XMM0, fpr.V(sregs[i]));
			MOVSS(tempxregs[i], M(&one));
			DIVSS(tempxregs[i], R(XMM0));
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
			SQRTSS(tempxregs[i], fpr.V(sregs[i]));
			ANDPS(tempxregs[i], M(&noSignMask));
			break;
		case 23: // d[i] = asinf(s[i] * (float)M_2_PI); break; //vasin
			DISABLE;
			break;
		case 24: // d[i] = -1.0f / s[i]; break; // vnrcp
			MOVSS(XMM0, M(&minus_one));
			DIVSS(XMM0, fpr.V(sregs[i]));
			MOVSS(tempxregs[i], R(XMM0));
			break;
		case 26: // d[i] = -sinf((float)M_PI_2 * s[i]); break; // vnsin
			DISABLE;
			break;
		case 28: // d[i] = 1.0f / expf(s[i] * (float)M_LOG2E); break; // vrexp2
			DISABLE;
			break;
		}
	}
	for (int i = 0; i < n; ++i)
	{
		if (!fpr.V(dregs[i]).IsSimpleReg(tempxregs[i]))
			MOVSS(fpr.V(dregs[i]), tempxregs[i]);
	}

	ApplyPrefixD(dregs, sz);

	fpr.ReleaseSpillLocks();
}

void Jit::Comp_Mftv(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	int imm = op & 0xFF;
	MIPSGPReg rt = _RT;
	switch ((op >> 21) & 0x1f)
	{
	case 3: //mfv / mfvc
		// rt = 0, imm = 255 appears to be used as a CPU interlock by some games.
		if (rt != MIPS_REG_ZERO) {
			if (imm < 128) {  //R(rt) = VI(imm);
				fpr.MapRegV(imm, 0);  // TODO: Seems the V register becomes dirty here? It shouldn't.
				gpr.MapReg(rt, false, true);
				MOVD_xmm(gpr.R(rt), fpr.VX(imm));
			} else if (imm < 128 + VFPU_CTRL_MAX) { //mfvc
				// In case we have a saved prefix.
				FlushPrefixV();
				gpr.MapReg(rt, false, true);
				MOV(32, gpr.R(rt), M(&currentMIPS->vfpuCtrl[imm - 128]));
			} else {
				//ERROR - maybe need to make this value too an "interlock" value?
				_dbg_assert_msg_(CPU,0,"mfv - invalid register");
			}
		}
		break;

	case 7: //mtv
		if (imm < 128) { // VI(imm) = R(rt);
			fpr.MapRegV(imm, MAP_DIRTY | MAP_NOINIT);  // TODO: Seems the V register becomes dirty here? It shouldn't.
			gpr.MapReg(rt, true, false);
			MOVD_xmm(fpr.VX(imm), gpr.R(rt));
		} else if (imm < 128 + VFPU_CTRL_MAX) { //mtvc //currentMIPS->vfpuCtrl[imm - 128] = R(rt);
			gpr.MapReg(rt, true, false);
			MOV(32, M(&currentMIPS->vfpuCtrl[imm - 128]), gpr.R(rt));

			// TODO: Optimization if rt is Imm?
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
}

void Jit::Comp_Vmtvc(MIPSOpcode op) {
	CONDITIONAL_DISABLE;
	int vs = _VS;
	int imm = op & 0xFF;
	if (imm >= 128 && imm < 128 + VFPU_CTRL_MAX) {
		fpr.MapRegV(vs, 0);
		MOVSS(M(&currentMIPS->vfpuCtrl[imm - 128]), fpr.VX(vs));
		fpr.ReleaseSpillLocks();

		if (imm - 128 == VFPU_CTRL_SPREFIX) {
			js.prefixSFlag = JitState::PREFIX_UNKNOWN;
		} else if (imm - 128 == VFPU_CTRL_TPREFIX) {
			js.prefixTFlag = JitState::PREFIX_UNKNOWN;
		} else if (imm - 128 == VFPU_CTRL_DPREFIX) {
			js.prefixDFlag = JitState::PREFIX_UNKNOWN;
		}
	}
}

void Jit::Comp_VMatrixInit(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	MatrixSize sz = GetMtxSize(op);
	int n = GetMatrixSide(sz);

	u8 dregs[16];
	GetMatrixRegs(dregs, sz, _VD);

	switch ((op >> 16) & 0xF) {
	case 3: // vmidt
		MOVSS(XMM0, M(&zero));
		MOVSS(XMM1, M(&one));
		for (int a = 0; a < n; a++) {
			for (int b = 0; b < n; b++) {
				MOVSS(fpr.V(dregs[a * 4 + b]), a == b ? XMM1 : XMM0);
			}
		}
		break;
	case 6: // vmzero
		MOVSS(XMM0, M(&zero));
		for (int a = 0; a < n; a++) {
			for (int b = 0; b < n; b++) {
				MOVSS(fpr.V(dregs[a * 4 + b]), XMM0);
			}
		}
		break;
	case 7: // vmone
		MOVSS(XMM0, M(&one));
		for (int a = 0; a < n; a++) {
			for (int b = 0; b < n; b++) {
				MOVSS(fpr.V(dregs[a * 4 + b]), XMM0);
			}
		}
		break;
	}

	fpr.ReleaseSpillLocks();
}

void Jit::Comp_Vmmov(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	// TODO: This probably ignores prefixes?
	if (js.HasUnknownPrefix())
		DISABLE;

	MatrixSize sz = GetMtxSize(op);
	int n = GetMatrixSide(sz);

	u8 sregs[16], dregs[16];
	GetMatrixRegs(sregs, sz, _VS);
	GetMatrixRegs(dregs, sz, _VD);

	// TODO: gas doesn't allow overlap, what does the PSP do?
	// Potentially detect overlap or the safe direction to move in, or just DISABLE?
	// This is very not optimal, blows the regcache everytime.
	u8 tempregs[16];
	for (int a = 0; a < n; a++)
	{
		for (int b = 0; b < n; b++)
		{
			u8 temp = (u8) fpr.GetTempV();
			fpr.MapRegV(temp, MAP_NOINIT | MAP_DIRTY);
			MOVSS(fpr.VX(temp), fpr.V(sregs[a * 4 + b]));
			fpr.StoreFromRegisterV(temp);
			tempregs[a * 4 + b] = temp;
		}
	}
	for (int a = 0; a < n; a++)
	{
		for (int b = 0; b < n; b++)
		{
			u8 temp = tempregs[a * 4 + b];
			fpr.MapRegV(temp, 0);
			MOVSS(fpr.V(dregs[a * 4 + b]), fpr.VX(temp));
		}
	}

	fpr.ReleaseSpillLocks();
}

void Jit::Comp_VScl(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	VectorSize sz = GetVecSize(op);
	int n = GetNumVectorElements(sz);

	u8 sregs[4], dregs[4], scale;
	GetVectorRegsPrefixS(sregs, sz, _VS);
	// TODO: Prefixes seem strange...
	GetVectorRegsPrefixT(&scale, V_Single, _VT);
	GetVectorRegsPrefixD(dregs, sz, _VD);

	// Move to XMM0 early, so we don't have to worry about overlap with scale.
	MOVSS(XMM0, fpr.V(scale));

	X64Reg tempxregs[4];
	for (int i = 0; i < n; ++i)
	{
		if (dregs[i] != scale || !IsOverlapSafeAllowS(dregs[i], i, n, sregs))
		{
			int reg = fpr.GetTempV();
			fpr.MapRegV(reg, MAP_NOINIT | MAP_DIRTY);
			fpr.SpillLockV(reg);
			tempxregs[i] = fpr.VX(reg);
		}
		else
		{
			fpr.MapRegV(dregs[i], (dregs[i] == sregs[i] ? 0 : MAP_NOINIT) | MAP_DIRTY);
			fpr.SpillLockV(dregs[i]);
			tempxregs[i] = fpr.VX(dregs[i]);
		}
	}
	for (int i = 0; i < n; ++i)
	{
		if (!fpr.V(sregs[i]).IsSimpleReg(tempxregs[i]))
			MOVSS(tempxregs[i], fpr.V(sregs[i]));
		MULSS(tempxregs[i], R(XMM0));
	}
	for (int i = 0; i < n; ++i)
	{
		if (!fpr.V(dregs[i]).IsSimpleReg(tempxregs[i]))
			MOVSS(fpr.V(dregs[i]), tempxregs[i]);
	}
	ApplyPrefixD(dregs, sz);

	fpr.ReleaseSpillLocks();
}

void Jit::Comp_Vmmul(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	// TODO: This probably ignores prefixes?
	if (js.HasUnknownPrefix())
		DISABLE;

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
		u8 tempregs[16];
		for (int a = 0; a < n; a++) {
			for (int b = 0; b < n; b++) {
				MOVSS(XMM0, fpr.V(sregs[b * 4]));
				MULSS(XMM0, fpr.V(tregs[a * 4]));
				for (int c = 1; c < n; c++) {
					MOVSS(XMM1, fpr.V(sregs[b * 4 + c]));
					MULSS(XMM1, fpr.V(tregs[a * 4 + c]));
					ADDSS(XMM0, R(XMM1));
				}
				u8 temp = (u8) fpr.GetTempV();
				fpr.MapRegV(temp, MAP_NOINIT | MAP_DIRTY);
				MOVSS(fpr.VX(temp), R(XMM0));
				fpr.StoreFromRegisterV(temp);
				tempregs[a * 4 + b] = temp;
			}
		}
		for (int a = 0; a < n; a++) {
			for (int b = 0; b < n; b++) {
				u8 temp = tempregs[a * 4 + b];
				fpr.MapRegV(temp, 0);
				MOVSS(fpr.V(dregs[a * 4 + b]), fpr.VX(temp));
			}
		}
	} else {
		for (int a = 0; a < n; a++) {
			for (int b = 0; b < n; b++) {
				MOVSS(XMM0, fpr.V(sregs[b * 4]));
				MULSS(XMM0, fpr.V(tregs[a * 4]));
				for (int c = 1; c < n; c++) {
					MOVSS(XMM1, fpr.V(sregs[b * 4 + c]));
					MULSS(XMM1, fpr.V(tregs[a * 4 + c]));
					ADDSS(XMM0, R(XMM1));
				}
				MOVSS(fpr.V(dregs[a * 4 + b]), XMM0);
			}
		}
	}
	fpr.ReleaseSpillLocks();
}

void Jit::Comp_Vmscl(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	// TODO: This probably ignores prefixes?
	if (js.HasUnknownPrefix())
		DISABLE;

	MatrixSize sz = GetMtxSize(op);
	int n = GetMatrixSide(sz);

	u8 sregs[16], dregs[16], scale;
	GetMatrixRegs(sregs, sz, _VS);
	GetVectorRegs(&scale, V_Single, _VT);
	GetMatrixRegs(dregs, sz, _VD);

	// Move to XMM0 early, so we don't have to worry about overlap with scale.
	MOVSS(XMM0, fpr.V(scale));

	// TODO: test overlap, optimize.
	u8 tempregs[16];
	for (int a = 0; a < n; a++)
	{
		for (int b = 0; b < n; b++)
		{
			u8 temp = (u8) fpr.GetTempV();
			fpr.MapRegV(temp, MAP_NOINIT | MAP_DIRTY);
			MOVSS(fpr.VX(temp), fpr.V(sregs[a * 4 + b]));
			MULSS(fpr.VX(temp), R(XMM0));
			fpr.StoreFromRegisterV(temp);
			tempregs[a * 4 + b] = temp;
		}
	}
	for (int a = 0; a < n; a++)
	{
		for (int b = 0; b < n; b++)
		{
			u8 temp = tempregs[a * 4 + b];
			fpr.MapRegV(temp, 0);
			MOVSS(fpr.V(dregs[a * 4 + b]), fpr.VX(temp));
		}
	}

	fpr.ReleaseSpillLocks();
}

void Jit::Comp_Vtfm(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	// TODO: This probably ignores prefixes?  Or maybe uses D?
	if (js.HasUnknownPrefix())
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
	else if (n != ins + 1)
		DISABLE;

	u8 sregs[16], dregs[4], tregs[4];
	GetMatrixRegs(sregs, msz, _VS);
	GetVectorRegs(tregs, sz, _VT);
	GetVectorRegs(dregs, sz, _VD);

	// TODO: test overlap, optimize.
	u8 tempregs[4];
	for (int i = 0; i < n; i++) {
		MOVSS(XMM0, fpr.V(sregs[i * 4]));
		MULSS(XMM0, fpr.V(tregs[0]));
		for (int k = 1; k < n; k++)
		{
			MOVSS(XMM1, fpr.V(sregs[i * 4 + k]));
			if (!homogenous || k != n - 1)
				MULSS(XMM1, fpr.V(tregs[k]));
			ADDSS(XMM0, R(XMM1));
		}

		u8 temp = (u8) fpr.GetTempV();
		fpr.MapRegV(temp, MAP_NOINIT | MAP_DIRTY);
		MOVSS(fpr.VX(temp), R(XMM0));
		fpr.StoreFromRegisterV(temp);
		tempregs[i] = temp;
	}
	for (int i = 0; i < n; i++) {
		u8 temp = tempregs[i];
		fpr.MapRegV(temp, 0);
		MOVSS(fpr.V(dregs[i]), fpr.VX(temp));
	}

	fpr.ReleaseSpillLocks();
}

void Jit::Comp_VCrs(MIPSOpcode op) {
	DISABLE;
}

void Jit::Comp_VDet(MIPSOpcode op) {
	DISABLE;
}

void Jit::Comp_Vi2x(MIPSOpcode op) {
	DISABLE;
}

void Jit::Comp_Vhoriz(MIPSOpcode op) {
	DISABLE;

	// Do any games use these a noticable amount?
	switch ((op >> 16) & 31) {
	case 6:  // vfad
		break;
	case 7:  // vavg
		break;
	}
}

void Jit::Comp_Viim(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	u8 dreg;
	GetVectorRegs(&dreg, V_Single, _VT);

	s32 imm = (s32)(s16)(u16)(op & 0xFFFF);
	FP32 fp;
	fp.f = (float)imm;
	MOV(32, R(EAX), Imm32(fp.u));
	fpr.MapRegV(dreg, MAP_DIRTY | MAP_NOINIT);
	MOVD_xmm(fpr.VX(dreg), R(EAX));

	ApplyPrefixD(&dreg, V_Single);
	fpr.ReleaseSpillLocks();
}

void Jit::Comp_Vfim(MIPSOpcode op) {
	CONDITIONAL_DISABLE;

	if (js.HasUnknownPrefix())
		DISABLE;

	u8 dreg;
	GetVectorRegs(&dreg, V_Single, _VT);

	FP16 half;
	half.u = op & 0xFFFF;
	FP32 fval = half_to_float_fast5(half);
	MOV(32, R(EAX), Imm32(fval.u));
	fpr.MapRegV(dreg, MAP_DIRTY | MAP_NOINIT);
	MOVD_xmm(fpr.VX(dreg), R(EAX));

	ApplyPrefixD(&dreg, V_Single);
	fpr.ReleaseSpillLocks();
}

static float sincostemp[2];

union u32float {
	u32 u;
	float f;

	operator float() const {
		return f;
	}

	inline u32float &operator *=(const float &other) {
		f *= other;
		return *this;
	}
};

#ifdef _M_X64
typedef float SinCosArg;
#else
typedef u32float SinCosArg;
#endif

void SinCos(SinCosArg angle) {
	vfpu_sincos(angle, sincostemp[0], sincostemp[1]);
}

void SinCosNegSin(SinCosArg angle) {
	vfpu_sincos(angle, sincostemp[0], sincostemp[1]);
	sincostemp[0] = -sincostemp[0];
}

// Very heavily used by FF:CC
void Jit::Comp_VRot(MIPSOpcode op) {
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
	fpr.Flush();

	bool negSin = (imm & 0x10) ? true : false;

#ifdef _M_X64
	MOVSS(XMM0, fpr.V(sreg));
	ABI_CallFunction(negSin ? (const void *)&SinCosNegSin : (const void *)&SinCos);
#else
	// Sigh, passing floats with cdecl isn't pretty, ends up on the stack.
	ABI_CallFunctionA(negSin ? (const void *)&SinCosNegSin : (const void *)&SinCos, fpr.V(sreg));
#endif

	MOVSS(XMM0, M(&sincostemp[0]));
	MOVSS(XMM1, M(&sincostemp[1]));
	
	char what[4] = {'0', '0', '0', '0'};
	if (((imm >> 2) & 3) == (imm & 3)) {
		for (int i = 0; i < 4; i++)
			what[i] = 'S';
	}
	what[(imm >> 2) & 3] = 'S';
	what[imm & 3] = 'C';

	for (int i = 0; i < n; i++) {
		fpr.MapRegV(dregs[i], MAP_DIRTY | MAP_NOINIT);
		switch (what[i]) {
		case 'C': MOVSS(fpr.V(dregs[i]), XMM1); break;
		case 'S': MOVSS(fpr.V(dregs[i]), XMM0); break;
		case '0':
			{
				XORPS(fpr.VX(dregs[i]), fpr.V(dregs[i]));
				break;
			}
		default:
			ERROR_LOG(JIT, "Bad what in vrot");
			break;
		}
	}

	fpr.ReleaseSpillLocks();
}

}
