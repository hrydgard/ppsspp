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
	void Dis_Mftv(u32 op, char *out);

	void Dis_SV(u32 op, char *out);
	void Dis_SVQ(u32 op, char *out);
	void Dis_SVLRQ(u32 op, char *out);

	void Dis_MatrixSet1(u32 op, char *out);
	void Dis_MatrixSet2(u32 op, char *out);
	void Dis_MatrixSet3(u32 op, char *out);
	void Dis_MatrixMult(u32 op, char *out);

	void Dis_VectorDot(u32 op, char *out);
	void Dis_Vfad(u32 op, char *out);
	void Dis_VectorSet1(u32 op, char *out);
	void Dis_VectorSet2(u32 op, char *out);
	void Dis_VectorSet3(u32 op, char *out);
	void Dis_VRot(u32 op, char *out);
	void Dis_VScl(u32 op, char *out);

	void Dis_VPFXST(u32 op, char *out);
	void Dis_VPFXD(u32 op, char *out);
	void Dis_Vcrs(u32 op, char *out);
	void Dis_Viim(u32 op, char *out);
	void Dis_Vcst(u32 op, char *out);
	void Dis_CrossQuat(u32 op, char *out);
	void Dis_Vtfm(u32 op, char *out);
	void Dis_Vcmp(u32 op, char *out);
	void Dis_Vcmov(u32 op, char *out);
	void Dis_Vflush(u32 op, char *out);
	void Dis_Vbfy(u32 op, char *out);
	void Dis_Vf2i(u32 op, char *out);
	void Dis_Vi2x(u32 op, char *out);
	void Dis_Vs2i(u32 op, char *out);
	void Dis_VBranch(u32 op, char *out);
}
