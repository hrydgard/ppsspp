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

#include "MIPS.h"
#include "MIPSTables.h"
#include "MIPSCodeUtils.h"
#include "../Host.h"

namespace MIPSCodeUtils
{

#define FULLOP_JR_RA 0x03e00008
#define OP_SYSCALL   0x0000000c
#define OP_SYSCALL_MASK 0xFC00003F

	u32 GetJumpTarget(u32 addr)
	{
		u32 op = Memory::Read_Instruction(addr);
		if (op)
		{
			op &= 0xFC000000;
			if (op == 0x0C000000 || op == 0x08000000) //jal
			{
				u32 target = (addr & 0xF0000000) | ((op&0x03FFFFFF) << 2);
				return target;
			}
			else
				return INVALIDTARGET;
		}
		else
			return INVALIDTARGET;
	}

	u32 GetBranchTarget(u32 addr)
	{
		u32 op = Memory::Read_Instruction(addr);
		if (op)
		{
			u32 info = MIPSGetInfo(op);
			if (info & IS_CONDBRANCH)
			{
				return addr + ((signed short)(op&0xFFFF)<<2);
			}
			else
				return INVALIDTARGET;
		}
		else
			return INVALIDTARGET;
	}

	u32 GetBranchTargetNoRA(u32 addr)
	{
		u32 op = Memory::Read_Instruction(addr);
		if (op)
		{
			u32 info = MIPSGetInfo(op);
			if ((info & IS_CONDBRANCH) && !(info & OUT_RA))
			{
				return addr + ((signed short)(op&0xFFFF)<<2);
			}
			else
				return INVALIDTARGET;
		}
		else
			return INVALIDTARGET;
	}

	u32 GetSureBranchTarget(u32 addr)
	{
		u32 op = Memory::Read_Instruction(addr);
		if (op)
		{
			u32 info = MIPSGetInfo(op);
			if (info & IS_CONDBRANCH)
			{
				//TODO: safer check
				if ((op & 0xFFFF0000) == 0x10000000)
					return addr + ((signed short)(op&0xFFFF)<<2);
				else
					return INVALIDTARGET;
			}
			else
				return INVALIDTARGET;
		}
		else
			return INVALIDTARGET;
	}
}
