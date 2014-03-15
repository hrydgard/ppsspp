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

struct MIPSInfo {
	MIPSInfo() {
		value = 0;
	}

	explicit MIPSInfo(u32 v) : value(v) {
	}

	u32 operator & (const u32 &arg) const {
		return value & arg;
	}

	u32 value;
};

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
#define MEMTYPE_MASK    0x00000007
#define MEMTYPE_BYTE    0x00000001
#define MEMTYPE_HWORD   0x00000002
#define MEMTYPE_WORD    0x00000003
#define MEMTYPE_FLOAT   0x00000004
#define MEMTYPE_VQUAD   0x00000005

#define IS_CONDMOVE     0x00000008
#define DELAYSLOT       0x00000010
#define BAD_INSTRUCTION 0x00000020
#define LIKELY          0x00000040
#define IS_CONDBRANCH   0x00000080
#define IS_JUMP         0x00000100

#define IN_RS           0x00000200
#define IN_RS_ADDR      (0x00000400 | IN_RS)
#define IN_RS_SHIFT     (0x00000800 | IN_RS)
#define IN_RT           0x00001000
#define IN_SA           0x00002000
#define IN_IMM16        0x00004000
#define IN_IMM26        0x00008000
#define IN_MEM          0x00010000
#define IN_OTHER        0x00020000
#define IN_FPUFLAG      0x00040000
#define IN_VFPU_CC      0x00080000

#define OUT_RT          0x00100000
#define OUT_RD          0x00200000
#define OUT_RA          0x00400000
#define OUT_MEM         0x00800000
#define OUT_OTHER       0x01000000
#define OUT_FPUFLAG     0x02000000
#define OUT_VFPU_CC     0x04000000
#define OUT_EAT_PREFIX  0x08000000

#define VFPU_NO_PREFIX  0x10000000
#define IS_VFPU         0x20000000
#define IS_FPU          0x40000000

#ifndef CDECL
#define CDECL
#endif

typedef void (CDECL *MIPSDisFunc)(MIPSOpcode opcode, char *out);
typedef void (CDECL *MIPSInterpretFunc)(MIPSOpcode opcode);


void MIPSCompileOp(MIPSOpcode op);
void MIPSDisAsm(MIPSOpcode op, u32 pc, char *out, bool tabsToSpaces = false);
MIPSInfo MIPSGetInfo(MIPSOpcode op);
void MIPSInterpret(MIPSOpcode op); //only for those rare ones
int MIPSInterpret_RunUntil(u64 globalTicks);
MIPSInterpretFunc MIPSGetInterpretFunc(MIPSOpcode op);

int MIPSGetInstructionCycleEstimate(MIPSOpcode op);
const char *MIPSGetName(MIPSOpcode op);
const char *MIPSDisasmAt(u32 compilerPC);

void FillMIPSTables();
