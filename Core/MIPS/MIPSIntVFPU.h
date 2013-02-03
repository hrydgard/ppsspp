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

namespace MIPSInt
{
	void Int_SV(u32 op);
	void Int_SVQ(u32 op);
	void Int_Mftv(u32 op);
	void Int_MatrixSet(u32 op);
	void Int_VecDo3(u32 op);
	void Int_Vcst(u32 op);
	void Int_VMatrixInit(u32 op);
	void Int_VVectorInit(u32 op);
	void Int_Vmmul(u32 op);
	void Int_Vmscl(u32 op);
	void Int_Vmmov(u32 op);
	void Int_VV2Op(u32 op);
	void Int_Vrot(u32 op);
	void Int_VDot(u32 op);
	void Int_VHdp(u32 op);
	void Int_Vavg(u32 op);
	void Int_Vfad(u32 op);
	void Int_Vocp(u32 op);
	void Int_Vsocp(u32 op);
	void Int_Vsgn(u32 op);
	void Int_Vtfm(u32 op);
	void Int_Viim(u32 op);
	void Int_VScl(u32 op);
	void Int_Vidt(u32 op);
	void Int_Vcmp(u32 op);
	void Int_Vminmax(u32 op);
	void Int_Vscmp(u32 op);
	void Int_Vcrs(u32 op);
	void Int_Vdet(u32 op);
	void Int_Vcmov(u32 op);
	void Int_CrossQuat(u32 op);
	void Int_VPFX(u32 op);
	void Int_Vflush(u32 op);
	void Int_Vbfy(u32 op);
	void Int_Vsrt1(u32 op);
	void Int_Vsrt2(u32 op);
	void Int_Vsrt3(u32 op);
	void Int_Vsrt4(u32 op);
	void Int_Vf2i(u32 op);
	void Int_Vi2f(u32 op);
	void Int_Vi2x(u32 op);
	void Int_Vx2i(u32 op);
	void Int_VBranch(u32 op);
	void Int_Vrnds(u32 op);
	void Int_VrndX(u32 op);
	void Int_ColorConv(u32 op);
	void Int_Vh2f(u32 op);
	void Int_Vf2h(u32 op);
	void Int_Vsge(u32 op);
	void Int_Vslt(u32 op);
	void Int_Vmfvc(u32 op);
	void Int_Vmtvc(u32 op);
	void Int_Vlgb(u32 op);
	void Int_Vwbn(u32 op);
	void Int_Vsbn(u32 op);
	void Int_Vsbz(u32 op);
}