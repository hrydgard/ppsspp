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

#include "JitCommon/JitCommon.h"

#include "../HLE/HLE.h"

// Invalid branch target address
#define INVALIDTARGET 0xFFFFFFFF

#define MIPS_MAKE_J(addr)   (0x08000000 | ((addr)>>2))
#define MIPS_MAKE_JAL(addr) (0x0C000000 | ((addr)>>2))
#define MIPS_MAKE_JR_RA()   (0x03e00008)
#define MIPS_MAKE_NOP()     (0)

#define MIPS_MAKE_ADDIU(dreg, sreg, immval) ((9 << 26) | ((dreg) << 16) | ((sreg) << 21) | (immval))
#define MIPS_MAKE_LUI(reg, immval) (0x3c000000 | ((reg) << 16) | (immval))
#define MIPS_MAKE_SYSCALL(module, function) GetSyscallOp(module, GetNibByName(module, function))
#define MIPS_MAKE_BREAK() (13)  // ! :)

#define MIPS_GET_RS(op) ((op>>21) & 0x1F)
#define MIPS_GET_RT(op) ((op>>16) & 0x1F)
#define MIPS_GET_RD(op) ((op>>11) & 0x1F)

#define MIPS_GET_FS(op) ((op>>11) & 0x1F)
#define MIPS_GET_FT(op) ((op>>16) & 0x1F)
#define MIPS_GET_FD(op) ((op>>6 ) & 0x1F)


namespace MIPSCodeUtils
{
	u32 GetCallTarget(u32 addr);
	u32 GetBranchTarget(u32 addr);
	// Ignores bltzal/etc. instructions that change RA.
	u32 GetBranchTargetNoRA(u32 addr);
	u32 GetJumpTarget(u32 addr);
	u32 GetSureBranchTarget(u32 addr);
	void RewriteSysCalls(u32 startAddr, u32 endAddr);
}
