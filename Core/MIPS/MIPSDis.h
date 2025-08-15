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

namespace MIPSDis
{
	void Dis_Unknown(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Unimpl(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Syscall(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);

	void Dis_mxc1(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_addi(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_addu(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_RelBranch2(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_RelBranch(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Generic(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Cache(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_IType(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_IType1(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_ITypeMem(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_RType2(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_RType3(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_MulDivType(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_ShiftType(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_VarShiftType(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_FPU3op(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_FPU2op(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_FPULS(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_FPUComp(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_FPUBranch(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_ori(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Special3(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);

	void Dis_ToHiloTransfer(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_FromHiloTransfer(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_JumpType(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_JumpRegType(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);

	void Dis_Allegrex(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Allegrex2(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);

	void Dis_Emuhack(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
}
