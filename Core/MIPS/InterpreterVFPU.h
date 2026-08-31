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

namespace MIPSInt
{
	void Int_SV(MIPSState *mips, MIPSOpcode op);
	void Int_SVQ(MIPSState *mips, MIPSOpcode op);
	void Int_Mftv(MIPSState *mips, MIPSOpcode op);
	void Int_VecDo3(MIPSState *mips, MIPSOpcode op);
	void Int_Vcst(MIPSState *mips, MIPSOpcode op);
	void Int_VMatrixInit(MIPSState *mips, MIPSOpcode op);
	void Int_VVectorInit(MIPSState *mips, MIPSOpcode op);
	void Int_Vmmul(MIPSState *mips, MIPSOpcode op);
	void Int_Vmscl(MIPSState *mips, MIPSOpcode op);
	void Int_Vmmov(MIPSState *mips, MIPSOpcode op);
	void Int_VV2Op(MIPSState *mips, MIPSOpcode op);
	void Int_Vrot(MIPSState *mips, MIPSOpcode op);
	void Int_VDot(MIPSState *mips, MIPSOpcode op);
	void Int_VHdp(MIPSState *mips, MIPSOpcode op);
	void Int_Vavg(MIPSState *mips, MIPSOpcode op);
	void Int_Vfad(MIPSState *mips, MIPSOpcode op);
	void Int_Vocp(MIPSState *mips, MIPSOpcode op);
	void Int_Vsocp(MIPSState *mips, MIPSOpcode op);
	void Int_Vsgn(MIPSState *mips, MIPSOpcode op);
	void Int_Vtfm(MIPSState *mips, MIPSOpcode op);
	void Int_Viim(MIPSState *mips, MIPSOpcode op);
	void Int_VScl(MIPSState *mips, MIPSOpcode op);
	void Int_Vidt(MIPSState *mips, MIPSOpcode op);
	void Int_Vcmp(MIPSState *mips, MIPSOpcode op);
	void Int_Vminmax(MIPSState *mips, MIPSOpcode op);
	void Int_Vscmp(MIPSState *mips, MIPSOpcode op);
	void Int_Vcrs(MIPSState *mips, MIPSOpcode op);
	void Int_Vdet(MIPSState *mips, MIPSOpcode op);
	void Int_Vcmov(MIPSState *mips, MIPSOpcode op);
	void Int_CrossQuat(MIPSState *mips, MIPSOpcode op);
	void Int_VPFX(MIPSState *mips, MIPSOpcode op);
	void Int_Vflush(MIPSState *mips, MIPSOpcode op);
	void Int_Vbfy(MIPSState *mips, MIPSOpcode op);
	void Int_Vsrt1(MIPSState *mips, MIPSOpcode op);
	void Int_Vsrt2(MIPSState *mips, MIPSOpcode op);
	void Int_Vsrt3(MIPSState *mips, MIPSOpcode op);
	void Int_Vsrt4(MIPSState *mips, MIPSOpcode op);
	void Int_Vf2i(MIPSState *mips, MIPSOpcode op);
	void Int_Vi2f(MIPSState *mips, MIPSOpcode op);
	void Int_Vi2x(MIPSState *mips, MIPSOpcode op);
	void Int_Vx2i(MIPSState *mips, MIPSOpcode op);
	void Int_VBranch(MIPSState *mips, MIPSOpcode op);
	void Int_Vrnds(MIPSState *mips, MIPSOpcode op);
	void Int_VrndX(MIPSState *mips, MIPSOpcode op);
	void Int_ColorConv(MIPSState *mips, MIPSOpcode op);
	void Int_Vh2f(MIPSState *mips, MIPSOpcode op);
	void Int_Vf2h(MIPSState *mips, MIPSOpcode op);
	void Int_Vsge(MIPSState *mips, MIPSOpcode op);
	void Int_Vslt(MIPSState *mips, MIPSOpcode op);
	void Int_Vmfvc(MIPSState *mips, MIPSOpcode op);
	void Int_Vmtvc(MIPSState *mips, MIPSOpcode op);
	void Int_Vlgb(MIPSState *mips, MIPSOpcode op);
	void Int_Vwbn(MIPSState *mips, MIPSOpcode op);
	void Int_Vsbn(MIPSState *mips, MIPSOpcode op);
	void Int_Vsbz(MIPSState *mips, MIPSOpcode op);
}
