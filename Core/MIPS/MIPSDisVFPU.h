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

extern u32 disPC;

namespace MIPSDis
{
	void Dis_Mftv(MIPSOpcode op, char *out);
	void Dis_Vmftvc(MIPSOpcode op, char *out);

	void Dis_SV(MIPSOpcode op, char *out);
	void Dis_SVQ(MIPSOpcode op, char *out);
	void Dis_SVLRQ(MIPSOpcode op, char *out);

	void Dis_MatrixSet1(MIPSOpcode op, char *out);
	void Dis_MatrixSet2(MIPSOpcode op, char *out);
	void Dis_MatrixSet3(MIPSOpcode op, char *out);
	void Dis_MatrixMult(MIPSOpcode op, char *out);
	void Dis_Vmscl(MIPSOpcode op, char *out);

	void Dis_VectorDot(MIPSOpcode op, char *out);
	void Dis_Vfad(MIPSOpcode op, char *out);
	void Dis_VectorSet1(MIPSOpcode op, char *out);
	void Dis_VectorSet2(MIPSOpcode op, char *out);
	void Dis_VectorSet3(MIPSOpcode op, char *out);
	void Dis_VRot(MIPSOpcode op, char *out);
	void Dis_VScl(MIPSOpcode op, char *out);

	void Dis_VPFXST(MIPSOpcode op, char *out);
	void Dis_VPFXD(MIPSOpcode op, char *out);
	void Dis_Vcrs(MIPSOpcode op, char *out);
	void Dis_Viim(MIPSOpcode op, char *out);
	void Dis_Vcst(MIPSOpcode op, char *out);
	void Dis_CrossQuat(MIPSOpcode op, char *out);
	void Dis_Vtfm(MIPSOpcode op, char *out);
	void Dis_Vcmp(MIPSOpcode op, char *out);
	void Dis_Vcmov(MIPSOpcode op, char *out);
	void Dis_Vflush(MIPSOpcode op, char *out);
	void Dis_Vbfy(MIPSOpcode op, char *out);
	void Dis_Vf2i(MIPSOpcode op, char *out);
	void Dis_Vi2x(MIPSOpcode op, char *out);
	void Dis_Vs2i(MIPSOpcode op, char *out);
	void Dis_Vwbn(MIPSOpcode op, char *out);
	void Dis_Vf2h(MIPSOpcode op, char *out);
	void Dis_Vh2f(MIPSOpcode op, char *out);
	void Dis_Vrnds(MIPSOpcode op, char *out);
	void Dis_VrndX(MIPSOpcode op, char *out);
	void Dis_ColorConv(MIPSOpcode op, char *out);

	void Dis_VBranch(MIPSOpcode op, char *out);
}
