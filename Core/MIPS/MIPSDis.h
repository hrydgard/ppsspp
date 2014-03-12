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

extern u32 disPC;

namespace MIPSDis
{
	void Dis_Unknown(MIPSOpcode op, char *out);
	void Dis_Unimpl(MIPSOpcode op, char *out);
	void Dis_Syscall(MIPSOpcode op, char *out);

	void Dis_mxc1(MIPSOpcode op, char *out);
	void Dis_addi(MIPSOpcode op, char *out);
	void Dis_addu(MIPSOpcode op, char *out);
	void Dis_RelBranch2(MIPSOpcode op, char *out);
	void Dis_RelBranch(MIPSOpcode op, char *out);
	void Dis_Generic(MIPSOpcode op, char *out);
	void Dis_Cache(MIPSOpcode op, char *out);
	void Dis_IType(MIPSOpcode op, char *out);
	void Dis_IType1(MIPSOpcode op, char *out);
	void Dis_ITypeMem(MIPSOpcode op, char *out);
	void Dis_RType2(MIPSOpcode op, char *out);
	void Dis_RType3(MIPSOpcode op, char *out);
	void Dis_MulDivType(MIPSOpcode op, char *out);
	void Dis_ShiftType(MIPSOpcode op, char *out);
	void Dis_VarShiftType(MIPSOpcode op, char *out);
	void Dis_FPU3op(MIPSOpcode op, char *out);
	void Dis_FPU2op(MIPSOpcode op, char *out);
	void Dis_FPULS(MIPSOpcode op, char *out);
	void Dis_FPUComp(MIPSOpcode op, char *out);
	void Dis_FPUBranch(MIPSOpcode op, char *out);
	void Dis_ori(MIPSOpcode op, char *out);
	void Dis_Special3(MIPSOpcode op, char *out);

	void Dis_ToHiloTransfer(MIPSOpcode op, char *out);
	void Dis_FromHiloTransfer(MIPSOpcode op, char *out);
	void Dis_JumpType(MIPSOpcode op, char *out);
	void Dis_JumpRegType(MIPSOpcode op, char *out);

	void Dis_Allegrex(MIPSOpcode op, char *out);
	void Dis_Allegrex2(MIPSOpcode op, char *out);

	void Dis_Emuhack(MIPSOpcode op, char *out);
}
