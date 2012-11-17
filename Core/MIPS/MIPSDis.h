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

extern u32 disPC;

namespace MIPSDis
{
	void Dis_Unknown(u32 op, char *out);
	void Dis_Unimpl(u32 op, char *out);
	void Dis_Syscall(u32 op, char *out);

	void Dis_mxc1(u32 op, char *out);
	void Dis_addi(u32 op, char *out);
	void Dis_addu(u32 op, char *out);
	void Dis_RelBranch2(u32 op, char *out);
	void Dis_RelBranch(u32 op, char *out);
	void Dis_Generic(u32 op, char *out);
	void Dis_IType(u32 op, char *out);
	void Dis_IType1(u32 op, char *out);
	void Dis_ITypeMem(u32 op, char *out);
	void Dis_RType2(u32 op, char *out);
	void Dis_RType3(u32 op, char *out);
	void Dis_MulDivType(u32 op, char *out);
	void Dis_ShiftType(u32 op, char *out);
	void Dis_VarShiftType(u32 op, char *out);
	void Dis_FPU3op(u32 op, char *out);
	void Dis_FPU2op(u32 op, char *out);
	void Dis_FPULS(u32 op, char *out);
	void Dis_FPUComp(u32 op, char *out);
	void Dis_FPUBranch(u32 op, char *out);
	void Dis_ori(u32 op, char *out);
	void Dis_Special3(u32 op, char *out);

	void Dis_ToHiloTransfer(u32 op, char *out);
	void Dis_FromHiloTransfer(u32 op, char *out);
	void Dis_JumpType(u32 op, char *out);
	void Dis_JumpRegType(u32 op, char *out);

	void Dis_Allegrex(u32 op, char *out);
	void Dis_Allegrex2(u32 op, char *out);

	void Dis_Emuhack(u32 op, char *out);
}
