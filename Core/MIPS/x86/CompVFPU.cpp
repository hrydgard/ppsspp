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

#include "../../MemMap.h"
#include "../../Config.h"
#include "../MIPSAnalyst.h"

#include "Jit.h"
#include "../MIPSVFPUUtils.h"
#include "RegCache.h"

// VERY UNFINISHED

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

// #define CONDITIONAL_DISABLE Comp_Generic(op); return;
#define CONDITIONAL_DISABLE ;
#define DISABLE Comp_Generic(op); return;


#define _RS ((op>>21) & 0x1F)
#define _RT ((op>>16) & 0x1F)
#define _RD ((op>>11) & 0x1F)
#define _FS ((op>>11) & 0x1F)
#define _FT ((op>>16) & 0x1F)
#define _FD ((op>>6 ) & 0x1F)
#define _POS  ((op>>6 ) & 0x1F)
#define _SIZE ((op>>11 ) & 0x1F)

using namespace Gen;

namespace MIPSComp
{

static const float one = 1.0f;
static const float minus_one = -1.0f;
static const float zero = -1.0f;

const u32 GC_ALIGNED16( noSignMask[4] ) = {0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF};
const u32 GC_ALIGNED16( signBitLower[4] ) = {0x80000000, 0, 0, 0};

void Jit::Comp_VPFX(u32 op)
{
	int data = op & 0xFFFFF;
	int regnum = (op >> 24) & 3;
	switch (regnum) {
	case 0:  // S
		js.prefixS = data;
		js.prefixSKnown = true;
		break;
	case 1:  // T
		js.prefixT = data;
		js.prefixTKnown = true;
		break;
	case 2:  // D
		js.prefixD = data;
		js.prefixDKnown = true;
		break;
	}
	// TODO: Defer this to end of block
	MOV(32, M((void *)&mips_->vfpuCtrl[VFPU_CTRL_SPREFIX + regnum]), Imm32(data));
}


// TODO:  Got register value ownership issues. We need to be sure that if we modify input
// like this, it does NOT get written back!
void Jit::ApplyPrefixST(u8 *vregs, u32 prefix, VectorSize sz) {
	if (prefix == 0xE4) return;

	int n = GetNumVectorElements(sz);
	u8 origV[4];
	static const float constantArray[8] = {0.f, 1.f, 2.f, 0.5f, 3.f, 1.f/3.f, 0.25f, 1.f/6.f};

	for (int i = 0; i < n; i++)
	{
		origV[i] = vregs[i];
	}

	for (int i = 0; i < n; i++)
	{
		int regnum = (prefix >> (i*2)) & 3;
		int abs    = (prefix >> (8+i)) & 1;
		int negate = (prefix >> (16+i)) & 1;
		int constants = (prefix >> (12+i)) & 1;

		if (!constants) {
			vregs[i] = origV[regnum];
			if (abs) {
				ANDPS(fpr.VX(vregs[i]), M((void *)&noSignMask));
			}
		}	else {
			MOVSS(fpr.VX(vregs[i]), M((void *)&constantArray[regnum + (abs<<2)]));
		}

		if (negate)
			XORPS(fpr.VX(vregs[i]), M((void *)&signBitLower));
	}
}

void Jit::ApplyPrefixD(const u8 *vregs, u32 prefix, VectorSize sz, bool onlyWriteMask) {
	_assert_(js.prefixDKnown);
	if (!prefix) return;

	int n = GetNumVectorElements(sz);
	for (int i = 0; i < n; i++)
	{
		int mask = (prefix >> (8 + i)) & 1;
		js.writeMask[i] = mask ? true : false;
		if (onlyWriteMask)
			continue;
		if (!mask) {
			int sat = (prefix >> (i * 2)) & 3;
			if (sat == 1)
			{
				MAXSS(fpr.VX(vregs[i]), M((void *)&zero));
				MINSS(fpr.VX(vregs[i]), M((void *)&one));
			}
			else if (sat == 3)
			{
				MAXSS(fpr.VX(vregs[i]), M((void *)&minus_one));
				MINSS(fpr.VX(vregs[i]), M((void *)&one));
			}
		}
	}
}


void Jit::Comp_SVQ(u32 op)
{
	int imm = (signed short)(op&0xFFFC);
	int rs = _RS;
	int vt = (((op >> 16) & 0x1f)) | ((op&1) << 5);

	switch (op >> 26)
	{
	case 54: //lv.q
		{
			if (!g_Config.bFastMemory) {
				DISABLE;
			}
			gpr.BindToRegister(rs, true, true);
	
			u8 vregs[4];
			GetVectorRegs(vregs, V_Quad, vt);
			MOV(32, R(EAX), gpr.R(rs));
			// Just copy 4 words the easiest way while not wasting registers.
#ifndef _M_X64
			AND(32, R(EAX), Imm32(0x3FFFFFFF));
#endif
			fpr.MapRegsV(vregs, V_Quad, MAP_DIRTY | MAP_NOINIT);

			// MOVSS to prime any crazy cache mechanism that might assume that there's a float somewhere...
			for (int i = 0; i < 4; i++) {
#ifdef _M_X64
				MOVSS(fpr.VX(vregs[i]), MComplex(RBX, EAX, 1, i * 4 + imm));
#else
				MOVSS(fpr.VX(vregs[i]), MDisp(EAX, (u32)(Memory::base + i * 4 + imm)));
#endif
			}

			gpr.UnlockAll();
			fpr.ReleaseSpillLocks();
		}
		break;

	case 62: //sv.q
		{
			if (!g_Config.bFastMemory) {
				DISABLE;
			}
			fpr.Flush();
			gpr.BindToRegister(rs, true, true);

			u8 vregs[4];
			GetVectorRegs(vregs, V_Quad, vt);

			MOV(32, R(EAX), gpr.R(rs));
			// Just copy 4 words the easiest way while not wasting registers.
#ifndef _M_X64
			AND(32, R(EAX), Imm32(0x3FFFFFFF));
#endif
			// MOVSS to prime any crazy cache mechanism that might assume that there's a float somewhere...

			// It would be pretty nice to have these in registers for the next instruction...
			// Even if we don't use real SIMD there's still 8 or 16 scalar float registers.
			
			fpr.MapRegsV(vregs, V_Quad, 0);

			for (int i = 0; i < 4; i++) {
#ifdef _M_X64
				MOVSS(MComplex(RBX, EAX, 1, i * 4 + imm), fpr.VX(vregs[i]));
#else
				MOVSS(MDisp(EAX, (u32)(Memory::base + i * 4 + imm)), fpr.VX(vregs[i]));
#endif
			}

			fpr.ReleaseSpillLocks();
			gpr.UnlockAll();
		}
		break;

	default:
		DISABLE;
		break;
	}
}


void Jit::Comp_VDot(u32 op) {
	DISABLE;
	// WARNING: No prefix support!

	int vd = _VD;
	int vs = _VS;
	int vt = _VT;
	VectorSize sz = GetVecSize(op);
	
	// TODO: Force read one of them into regs? probably not.
	u8 sregs[4], tregs[4], dregs[4];
	GetVectorRegs(sregs, sz, vs);
	GetVectorRegs(tregs, sz, vt);
	GetVectorRegs(dregs, sz, vd);

	// TODO: applyprefixST here somehow (shuffle, etc...)

	MOVSS(XMM0, fpr.V(sregs[0]));
	MULSS(XMM0, fpr.V(tregs[0]));

	float sum = 0.0f;
	int n = GetNumVectorElements(sz);
	for (int i = 1; i < n; i++)
	{
		// sum += s[i]*t[i];
		MOVSS(XMM1, fpr.V(sregs[i]));
		MULSS(XMM1, fpr.V(tregs[i]));
		ADDSS(XMM0, R(XMM1));
	}
	fpr.ReleaseSpillLocks();

	fpr.MapRegsV(dregs, V_Single, MAP_NOINIT);

	// TODO: applyprefixD here somehow (write mask etc..)

	MOVSS(fpr.V(vd), XMM0);

	fpr.ReleaseSpillLocks();

	js.EatPrefix();
}


}