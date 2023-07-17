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

namespace MIPSDis
{
	void Dis_Mftv(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vmfvc(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vmtvc(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);

	void Dis_SV(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_SVQ(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_SVLRQ(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);

	void Dis_MatrixSet1(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_MatrixSet2(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_MatrixSet3(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_MatrixMult(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vmscl(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);

	void Dis_VectorDot(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vfad(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_VectorSet1(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_VectorSet2(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_VectorSet3(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_VRot(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_VScl(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);

	void Dis_VPFXST(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_VPFXD(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vcrs(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Viim(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vcst(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_CrossQuat(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vtfm(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vcmp(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vcmov(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vflush(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vbfy(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vf2i(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vi2x(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vs2i(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vwbn(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vf2h(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vh2f(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_Vrnds(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_VrndX(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
	void Dis_ColorConv(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);

	void Dis_VBranch(MIPSOpcode op, uint32_t pc, char *out, size_t outSize);
}
