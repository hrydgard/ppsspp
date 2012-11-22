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

#include "../../Globals.h"

u32 MIPS_GetNextPC();
void MIPS_ClearDelaySlot();
int MIPS_SingleStep();
float round_ieee_754(float num);

namespace MIPSInt
{
	void Int_Unknown(u32 op);
	void Int_Unimpl(u32 op);
	void Int_Syscall(u32 op);

	void Int_mxc1(u32 op);
	void Int_RelBranch(u32 op);
	void Int_RelBranchRI(u32 op);
	void Int_IType(u32 op);
	void Int_ITypeMem(u32 op);
	void Int_RType2(u32 op);
	void Int_RType3(u32 op);
	void Int_ShiftType(u32 op);
	void Int_MulDivType(u32 op);
	void Int_JumpType(u32 op);
	void Int_JumpRegType(u32 op);
	void Int_Allegrex2(u32 op);
	void Int_FPULS(u32 op);
	void Int_FPU3op(u32 op);
	void Int_FPU2op(u32 op);
	void Int_Allegrex(u32 op);
	void Int_FPUComp(u32 op);
	void Int_FPUBranch(u32 op);
	void Int_Emuhack(u32 op);
	void Int_Special3(u32 op);
	void Int_Interrupt(u32 op);
	void Int_Cache(u32 op);
	void Int_Sync(u32 op);
	void Int_Break(u32 op);
	void Int_StoreSync(u32 op);
}
