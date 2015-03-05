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

	void Arm64Jit::Comp_VPFX(MIPSOpcode op)
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
		default:
			ERROR_LOG(CPU, "VPFX - bad regnum %i : data=%08x", regnum, data);
			break;
		}
	}

	void Arm64Jit::Comp_SV(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_SVQ(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_VVectorInit(MIPSOpcode op)
	{
		DISABLE;
	}

	void Arm64Jit::Comp_VIdt(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_VMatrixInit(MIPSOpcode op)
	{
		DISABLE;
	}

	void Arm64Jit::Comp_VHdp(MIPSOpcode op) {
		DISABLE;
	}

	static const float MEMORY_ALIGNED16(vavg_table[4]) = { 1.0f, 1.0f / 2.0f, 1.0f / 3.0f, 1.0f / 4.0f };

	void Arm64Jit::Comp_Vhoriz(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_VDot(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_VecDo3(MIPSOpcode op) {
		DISABLE;
	}

	void Arm64Jit::Comp_VV2Op(MIPSOpcode op) {
		DISABLE;
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
		DISABLE;
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
		DISABLE;
	}

	// Very heavily used by FF:CC. Should be replaced by a fast approximation instead of
	// calling the math library.
	// Apparently this may not work on hardfp. I don't think we have any platforms using this though.
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
