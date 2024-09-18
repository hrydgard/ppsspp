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

int MIPS_SingleStep();

namespace MIPSInt
{
	void Int_Syscall(MIPSOpcode op);

	void Int_mxc1(MIPSOpcode op);
	void Int_RelBranch(MIPSOpcode op);
	void Int_RelBranchRI(MIPSOpcode op);
	void Int_IType(MIPSOpcode op);
	void Int_ITypeMem(MIPSOpcode op);
	void Int_RType2(MIPSOpcode op);
	void Int_RType3(MIPSOpcode op);
	void Int_ShiftType(MIPSOpcode op);
	void Int_MulDivType(MIPSOpcode op);
	void Int_JumpType(MIPSOpcode op);
	void Int_JumpRegType(MIPSOpcode op);
	void Int_Allegrex2(MIPSOpcode op);
	void Int_FPULS(MIPSOpcode op);
	void Int_FPU3op(MIPSOpcode op);
	void Int_FPU2op(MIPSOpcode op);
	void Int_Allegrex(MIPSOpcode op);
	void Int_FPUComp(MIPSOpcode op);
	void Int_FPUBranch(MIPSOpcode op);
	void Int_Emuhack(MIPSOpcode op);
	void Int_Special2(MIPSOpcode op);
	void Int_Special3(MIPSOpcode op);
	void Int_Interrupt(MIPSOpcode op);
	void Int_Cache(MIPSOpcode op);
	void Int_Sync(MIPSOpcode op);
	void Int_Break(MIPSOpcode op);
	void Int_StoreSync(MIPSOpcode op);
}
