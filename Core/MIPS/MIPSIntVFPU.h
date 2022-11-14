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
	void Int_SV(MIPSOpcode op);
	void Int_SVQ(MIPSOpcode op);
	void Int_Mftv(MIPSOpcode op);
	void Int_VecDo3(MIPSOpcode op);
	void Int_Vcst(MIPSOpcode op);
	void Int_VMatrixInit(MIPSOpcode op);
	void Int_VVectorInit(MIPSOpcode op);
	void Int_Vmmul(MIPSOpcode op);
	void Int_Vmscl(MIPSOpcode op);
	void Int_Vmmov(MIPSOpcode op);
	void Int_VV2Op(MIPSOpcode op);
	void Int_Vrot(MIPSOpcode op);
	void Int_VDot(MIPSOpcode op);
	void Int_VHdp(MIPSOpcode op);
	void Int_Vavg(MIPSOpcode op);
	void Int_Vfad(MIPSOpcode op);
	void Int_Vocp(MIPSOpcode op);
	void Int_Vsocp(MIPSOpcode op);
	void Int_Vsgn(MIPSOpcode op);
	void Int_Vtfm(MIPSOpcode op);
	void Int_Viim(MIPSOpcode op);
	void Int_VScl(MIPSOpcode op);
	void Int_Vidt(MIPSOpcode op);
	void Int_Vcmp(MIPSOpcode op);
	void Int_Vminmax(MIPSOpcode op);
	void Int_Vscmp(MIPSOpcode op);
	void Int_Vcrs(MIPSOpcode op);
	void Int_Vdet(MIPSOpcode op);
	void Int_Vcmov(MIPSOpcode op);
	void Int_CrossQuat(MIPSOpcode op);
	void Int_VPFX(MIPSOpcode op);
	void Int_Vflush(MIPSOpcode op);
	void Int_Vbfy(MIPSOpcode op);
	void Int_Vsrt1(MIPSOpcode op);
	void Int_Vsrt2(MIPSOpcode op);
	void Int_Vsrt3(MIPSOpcode op);
	void Int_Vsrt4(MIPSOpcode op);
	void Int_Vf2i(MIPSOpcode op);
	void Int_Vi2f(MIPSOpcode op);
	void Int_Vi2x(MIPSOpcode op);
	void Int_Vx2i(MIPSOpcode op);
	void Int_VBranch(MIPSOpcode op);
	void Int_Vrnds(MIPSOpcode op);
	void Int_VrndX(MIPSOpcode op);
	void Int_ColorConv(MIPSOpcode op);
	void Int_Vh2f(MIPSOpcode op);
	void Int_Vf2h(MIPSOpcode op);
	void Int_Vsge(MIPSOpcode op);
	void Int_Vslt(MIPSOpcode op);
	void Int_Vmfvc(MIPSOpcode op);
	void Int_Vmtvc(MIPSOpcode op);
	void Int_Vlgb(MIPSOpcode op);
	void Int_Vwbn(MIPSOpcode op);
	void Int_Vsbn(MIPSOpcode op);
	void Int_Vsbz(MIPSOpcode op);
}
