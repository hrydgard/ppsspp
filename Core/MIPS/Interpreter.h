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

#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"

int MIPS_InterpretSingleStep(MIPSState *mips);

namespace MIPSInt
{
	void Int_Syscall(MIPSState *mips, MIPSOpcode op);

	void Int_mxc1(MIPSState *mips, MIPSOpcode op);
	void Int_Cop0(MIPSState *mips, MIPSOpcode op);
	void Int_RelBranch(MIPSState *mips, MIPSOpcode op);
	void Int_RelBranchRI(MIPSState *mips, MIPSOpcode op);
	void Int_IType(MIPSState *mips, MIPSOpcode op);
	void Int_ITypeMem(MIPSState *mips, MIPSOpcode op);
	void Int_RType2(MIPSState *mips, MIPSOpcode op);
	void Int_RType3(MIPSState *mips, MIPSOpcode op);
	void Int_ShiftType(MIPSState *mips, MIPSOpcode op);
	void Int_MulDivType(MIPSState *mips, MIPSOpcode op);
	void Int_JumpType(MIPSState *mips, MIPSOpcode op);
	void Int_JumpRegType(MIPSState *mips, MIPSOpcode op);
	void Int_Allegrex2(MIPSState *mips, MIPSOpcode op);
	void Int_FPULS(MIPSState *mips, MIPSOpcode op);
	void Int_FPU3op(MIPSState *mips, MIPSOpcode op);
	void Int_FPU2op(MIPSState *mips, MIPSOpcode op);
	void Int_Allegrex(MIPSState *mips, MIPSOpcode op);
	void Int_FPUComp(MIPSState *mips, MIPSOpcode op);
	void Int_FPUBranch(MIPSState *mips, MIPSOpcode op);
	void Int_Emuhack(MIPSState *mips, MIPSOpcode op);
	void Int_Special2(MIPSState *mips, MIPSOpcode op);
	void Int_Special3(MIPSState *mips, MIPSOpcode op);
	void Int_Interrupt(MIPSState *mips, MIPSOpcode op);
	void Int_Cache(MIPSState *mips, MIPSOpcode op);
	void Int_Sync(MIPSState *mips, MIPSOpcode op);
	void Int_Break(MIPSState *mips, MIPSOpcode op);
	void Int_StoreSync(MIPSState *mips, MIPSOpcode op);
}
