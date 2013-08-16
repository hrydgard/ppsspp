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

#pragma once

#include "../../../Globals.h"

namespace MIPSComp
{

class Jit
{
public:
	// Compiled ops should ignore delay slots
	// the compiler will take care of them by itself
	// OR NOT
	void Comp_Generic(u32 op);
	
	void EatInstruction(u32 op);
	void Comp_RunBlock(u32 op);
	
	// TODO: Eat VFPU prefixes here.
	void EatPrefix() { }

	// Ops
	void Comp_ITypeMem(u32 op);

	void Comp_RelBranch(u32 op);
	void Comp_RelBranchRI(u32 op);
	void Comp_FPUBranch(u32 op);
	void Comp_FPULS(u32 op);
	void Comp_FPUComp(u32 op);
	void Comp_Jump(u32 op);
	void Comp_JumpReg(u32 op);
	void Comp_Syscall(u32 op);
	void Comp_Break(u32 op);

	void Comp_IType(u32 op);
	void Comp_RType2(u32 op);
	void Comp_RType3(u32 op);
	void Comp_ShiftType(u32 op);
	void Comp_Allegrex(u32 op);
	void Comp_Allegrex2(u32 op);
	void Comp_VBranch(u32 op);
	void Comp_MulDivType(u32 op);
	void Comp_Special3(u32 op);

	void Comp_FPU3op(u32 op);
	void Comp_FPU2op(u32 op);
	void Comp_mxc1(u32 op);

	void Comp_DoNothing(u32 op);

	void Comp_SV(u32 op);
	void Comp_SVQ(u32 op);
	void Comp_VPFX(u32 op);
	void Comp_VVectorInit(u32 op);	
	void Comp_VMatrixInit(u32 op);
	void Comp_VDot(u32 op);
	void Comp_VecDo3(u32 op);
	void Comp_VV2Op(u32 op);
	void Comp_Mftv(u32 op);
	void Comp_Vmtvc(u32 op);
	void Comp_Vmmov(u32 op);
	void Comp_VScl(u32 op);
	void Comp_Vmmul(u32 op);
	void Comp_Vmscl(u32 op);
	void Comp_Vtfm(u32 op);
	void Comp_VHdp(u32 op);
	void Comp_VCrs(u32 op);
	void Comp_VDet(u32 op);
	void Comp_Vi2x(u32 op);
	void Comp_Vx2i(u32 op);
	void Comp_Vf2i(u32 op);
	void Comp_Vi2f(u32 op);
	void Comp_Vcst(u32 op);
	void Comp_Vhoriz(u32 op);	
	void Comp_VRot(u32 op);
	void Comp_VIdt(u32 op);

	void ClearCache();
	void ClearCacheAt(u32 em_address);
};

typedef void (Jit::*MIPSCompileFunc)(u32 opcode);

}	// namespace MIPSComp

