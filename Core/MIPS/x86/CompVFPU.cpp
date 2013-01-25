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
			for (int i = 0; i < 4; i++) {
#ifdef _M_X64
				MOVSS((X64Reg)(XMM0 + i), MComplex(RBX, EAX, 1, i * 4 + imm));
#else
				MOVSS((X64Reg)(XMM0 + i), MDisp(EAX, (u32)(Memory::base + i * 4 + imm)));
#endif
			}

			// It would be pretty nice to have these in registers for the next instruction...
			// Even if we don't use real SIMD there's still 8 or 16 scalar float registers.
			for (int i = 0; i < 4; i++) {
				MOVSS(M((void *)&mips_->v[vregs[i]]), (X64Reg)(XMM0 + i));
			}
			gpr.UnlockAll();
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
			for (int i = 0; i < 4; i++) {
				MOVSS((X64Reg)(XMM0 + i), M((void *)&mips_->v[vregs[i]]));
			}

			for (int i = 0; i < 4; i++) {
#ifdef _M_X64
				MOVSS(MComplex(RBX, EAX, 1, i * 4 + imm), (X64Reg)(XMM0 + i));
#else
				MOVSS(MDisp(EAX, (u32)(Memory::base + i * 4 + imm)), (X64Reg)(XMM0 + i));
#endif
			}

			gpr.UnlockAll();
		}
		break;

	default:
		DISABLE;
		break;
	}
}

}