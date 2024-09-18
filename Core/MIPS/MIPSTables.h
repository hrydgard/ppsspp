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

#include <string>
#include <stdint.h>
#include "Common/CommonTypes.h"
#include "Core/MIPS/MIPS.h"

#define CONDTYPE_MASK   0x00000007
#define CONDTYPE_EQ     0x00000001
#define CONDTYPE_NE     0x00000002
#define CONDTYPE_LEZ    0x00000003
#define CONDTYPE_GTZ    0x00000004
#define CONDTYPE_LTZ    0x00000005
#define CONDTYPE_GEZ    0x00000006

#define CONDTYPE_FPUFALSE   CONDTYPE_EQ
#define CONDTYPE_FPUTRUE    CONDTYPE_NE

// as long as the other flags are checked,
// there is no way to misinterpret these
// as CONDTYPE_X
#define MEMTYPE_MASK    0x00000007ULL
#define MEMTYPE_BYTE    0x00000001ULL
#define MEMTYPE_HWORD   0x00000002ULL
#define MEMTYPE_WORD    0x00000003ULL
#define MEMTYPE_FLOAT   0x00000004ULL
#define MEMTYPE_VQUAD   0x00000005ULL

#define IS_CONDMOVE     0x00000008ULL
#define DELAYSLOT       0x00000010ULL
#define BAD_INSTRUCTION 0x00000020ULL
#define LIKELY          0x00000040ULL
#define IS_CONDBRANCH   0x00000080ULL
#define IS_JUMP         0x00000100ULL

#define IN_RS           0x00000200ULL
#define IN_RS_ADDR      (0x00000400ULL | IN_RS)
#define IN_RS_SHIFT     (0x00000800ULL | IN_RS)
#define IN_RT           0x00001000ULL
#define IN_SA           0x00002000ULL
#define IN_IMM16        0x00004000ULL
#define IN_IMM26        0x00008000ULL
#define IN_MEM          0x00010000ULL
#define IN_OTHER        0x00020000ULL
#define IN_FPUFLAG      0x00040000ULL
#define IN_VFPU_CC      0x00080000ULL

#define OUT_RT          0x00100000ULL
#define OUT_RD          0x00200000ULL
#define OUT_RA          0x00400000ULL
#define OUT_MEM         0x00800000ULL
#define OUT_OTHER       0x01000000ULL
#define OUT_FPUFLAG     0x02000000ULL
#define OUT_VFPU_CC     0x04000000ULL
#define OUT_EAT_PREFIX  0x08000000ULL

#define VFPU_NO_PREFIX  0x10000000ULL
#define OUT_VFPU_PREFIX 0x20000000ULL
#define IS_VFPU         0x40000000ULL
#define IS_FPU          0x80000000ULL

#define IN_FS           0x000100000000ULL
#define IN_FT           0x000200000000ULL
#define IN_LO           0x000400000000ULL
#define IN_HI           0x000800000000ULL

#define OUT_FD          0x001000000000ULL
#define OUT_FS          0x002000000000ULL
#define OUT_LO          0x004000000000ULL
#define OUT_HI          0x008000000000ULL

#define IN_VS           0x010000000000ULL
#define IN_VT           0x020000000000ULL
#define OUT_FT          0x040000000000ULL

#define OUT_VD          0x100000000000ULL

#define IS_SYSCALL      0x200000000000ULL

#ifndef CDECL
#define CDECL
#endif

struct MIPSInfo {
	MIPSInfo() {
		value = 0;
		cycles = 0;
	}

	explicit MIPSInfo(u64 v, u16 c = 0) : value(v), cycles(c) {
		if (c == 0) {
			cycles = 1;
			if (v & IS_VFPU)
				cycles++;
		}
	}

	u64 operator & (const u64 &arg) const {
		return value & arg;
	}

	u64 value : 48;
	u64 cycles : 16;
};

typedef void (CDECL *MIPSDisFunc)(MIPSOpcode opcode, uint32_t pc, char *out, size_t outSize);
typedef void (CDECL *MIPSInterpretFunc)(MIPSOpcode opcode);

namespace MIPSComp {
	class MIPSFrontendInterface;
}

void MIPSCompileOp(MIPSOpcode op, MIPSComp::MIPSFrontendInterface *jit);
void MIPSDisAsm(MIPSOpcode op, u32 pc, char *out, size_t outSize, bool tabsToSpaces = false);
MIPSInfo MIPSGetInfo(MIPSOpcode op);
void MIPSInterpret(MIPSOpcode op); //only for those rare ones
int MIPSInterpret_RunUntil(u64 globalTicks);
MIPSInterpretFunc MIPSGetInterpretFunc(MIPSOpcode op);

int MIPSGetInstructionCycleEstimate(MIPSOpcode op);
int MIPSGetMemoryAccessSize(MIPSOpcode op);
const char *MIPSGetName(MIPSOpcode op);
std::string MIPSDisasmAt(u32 compilerPC);
