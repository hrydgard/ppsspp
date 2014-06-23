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

#include "Core/MIPS/MIPS.h"
#include "Core/MIPS/MIPSTables.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/Host.h"
#include "Core/MemMap.h"

namespace MIPSCodeUtils
{

#define FULLOP_JR_RA 0x03e00008
#define OP_SYSCALL   0x0000000c
#define OP_SYSCALL_MASK 0xFC00003F
#define _RS   ((op>>21) & 0x1F)
#define _RT   ((op>>16) & 0x1F)

	u32 GetJumpTarget(u32 addr)
	{
		MIPSOpcode op = Memory::Read_Instruction(addr, true);
		if (op != 0)
		{
			MIPSInfo info = MIPSGetInfo(op);
			if ((info & IS_JUMP) && (info & IN_IMM26))
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
		MIPSOpcode op = Memory::Read_Instruction(addr, true);
		if (op != 0)
		{
			MIPSInfo info = MIPSGetInfo(op);
			if (info & IS_CONDBRANCH)
			{
				return addr + 4 + ((signed short)(op&0xFFFF)<<2);
			}
			else
				return INVALIDTARGET;
		}
		else
			return INVALIDTARGET;
	}

	u32 GetBranchTargetNoRA(u32 addr)
	{
		MIPSOpcode op = Memory::Read_Instruction(addr, true);
		return GetBranchTargetNoRA(addr, op);
	}

	u32 GetBranchTargetNoRA(u32 addr, MIPSOpcode op)
	{
		if (op != 0)
		{
			MIPSInfo info = MIPSGetInfo(op);
			if ((info & IS_CONDBRANCH) && !(info & OUT_RA))
			{
				return addr + 4 + ((signed short)(op&0xFFFF)<<2);
			}
			else
				return INVALIDTARGET;
		}
		else
			return INVALIDTARGET;
	}

	u32 GetSureBranchTarget(u32 addr)
	{
		MIPSOpcode op = Memory::Read_Instruction(addr, true);
		if (op != 0)
		{
			MIPSInfo info = MIPSGetInfo(op);
			if (info & IS_CONDBRANCH)
			{
				bool sure;
				bool takeBranch;
				switch (info & CONDTYPE_MASK)
				{
				case CONDTYPE_EQ:
					sure = _RS == _RT;
					takeBranch = true;
					break;

				case CONDTYPE_NE:
					sure = _RS == _RT;
					takeBranch = false;
					break;

				case CONDTYPE_LEZ:
				case CONDTYPE_GEZ:
					sure = _RS == 0;
					takeBranch = true;
					break;

				case CONDTYPE_LTZ:
				case CONDTYPE_GTZ:
					sure = _RS == 0;
					takeBranch = false;
					break;

				default:
					sure = false;
				}

				if (sure && takeBranch)
					return addr + 4 + ((signed short)(op&0xFFFF)<<2);
				else if (sure && !takeBranch)
					return addr + 8;
				else
					return INVALIDTARGET;
			}
			else
				return INVALIDTARGET;
		}
		else
			return INVALIDTARGET;
	}

	bool IsVFPUBranch(MIPSOpcode op) {
		return (MIPSGetInfo(op) & (IS_VFPU | IS_CONDBRANCH)) == (IS_VFPU | IS_CONDBRANCH);
	}

	bool IsBranch(MIPSOpcode op) {
		return (MIPSGetInfo(op) & IS_CONDBRANCH) == IS_CONDBRANCH;
	}
}
