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

#include <cmath>
#include "math/math_util.h"

#include "Common/CPUDetect.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/Config.h"
#include "Core/Reporting.h"

#include "Core/MIPS/ARM/ArmJit.h"
#include "Core/MIPS/ARM/ArmRegCache.h"

// TODO: Somehow #ifdef away on ARMv5eabi, without breaking the linker.

#define CONDITIONAL_DISABLE ;
#define DISABLE { fpr.ReleaseSpillLocksAndDiscardTemps(); Comp_Generic(op); return; }

namespace MIPSComp {

void Jit::CompNEON_SV(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_SVQ(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VVectorInit(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VMatrixInit(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VDot(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VecDo3(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VV2Op(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Mftv(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vmtvc(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vmmov(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VScl(MIPSOpcode op) {
	DISABLE;
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
}

void Jit::CompNEON_Vcst(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vhoriz(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VRot(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_VIdt(MIPSOpcode op) {
	DISABLE;
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

void Jit::CompNEON_VCrossQuat(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vsgn(MIPSOpcode op) {
	DISABLE;
}

void Jit::CompNEON_Vocp(MIPSOpcode op) {
	DISABLE;
}

}
// namespace MIPSComp