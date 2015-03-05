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

#include "Core/Config.h"
#include "Core/MemMap.h"
#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSTables.h"

#include "Core/MIPS/ARM64/Arm64Jit.h"
#include "Core/MIPS/ARM64/Arm64RegCache.h"
#include "Common/CPUDetect.h"

#define _RS MIPS_GET_RS(op)
#define _RT MIPS_GET_RT(op)
#define _RD MIPS_GET_RD(op)
#define _FS MIPS_GET_FS(op)
#define _FT MIPS_GET_FT(op)
#define _FD MIPS_GET_FD(op)
#define _SA MIPS_GET_SA(op)
#define _POS  ((op>> 6) & 0x1F)
#define _SIZE ((op>>11) & 0x1F)
#define _IMM16 (signed short)(op & 0xFFFF)
#define _IMM26 (op & 0x03FFFFFF)

// All functions should have CONDITIONAL_DISABLE, so we can narrow things down to a file quickly.
// Currently known non working ones should have DISABLE.

//#define CONDITIONAL_DISABLE { Comp_Generic(op); return; }
#define CONDITIONAL_DISABLE ;
#define DISABLE { Comp_Generic(op); return; }

namespace MIPSComp
{
	using namespace Arm64Gen;
	using namespace Arm64JitConstants;

void Arm64Jit::Comp_FPU3op(MIPSOpcode op)
{ 
	DISABLE;
}

void Arm64Jit::Comp_FPULS(MIPSOpcode op)
{
	DISABLE;
}

void Arm64Jit::Comp_FPUComp(MIPSOpcode op) {
	DISABLE;
}

void Arm64Jit::Comp_FPU2op(MIPSOpcode op) {
	DISABLE;
}

void Arm64Jit::Comp_mxc1(MIPSOpcode op)
{
	DISABLE;
}

}	// namespace MIPSComp
