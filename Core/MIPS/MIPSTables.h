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

#define IS_CONDBRANCH 0x100
#define IS_JUMP 0x200
#define IS_VFPU 0x80000000
#define UNCONDITIONAL 0x40
#define BAD_INSTRUCTION 0x20
#define DELAYSLOT 0x10

#define IN_RS_ADDR   0x800
#define IN_RS_SHIFT  0x400
#define IN_RS   0x1000
#define IN_RT   0x2000
#define IN_SA   0x4000
#define IN_IMM16 0x8000
#define IN_IMM26 0x10000
#define IN_MEM   0x20000
#define IN_OTHER 0x40000
#define IN_FPUFLAG 0x80000

#define OUT_RT  0x100000
#define OUT_RD  0x200000
#define OUT_RA  0x400000
#define OUT_MEM 0x800000
#define OUT_OTHER 0x1000000
#define OUT_FPUFLAG 0x2000000

#ifndef CDECL
#define CDECL
#endif

typedef void (CDECL *MIPSDisFunc)(u32 opcode, char *out);
typedef void (CDECL *MIPSInterpretFunc)(u32 opcode);


void MIPSCompileOp(u32 op);
void MIPSDisAsm(u32 op, u32 pc, char *out, bool tabsToSpaces = false);
u32  MIPSGetInfo(u32 op);
void MIPSInterpret(u32 op); //only for those rare ones
int MIPSInterpret_RunFastUntil(u64 globalTicks);
int MIPSInterpret_RunUntil(u64 globalTicks);
MIPSInterpretFunc MIPSGetInterpretFunc(u32 op);

int MIPSGetInstructionCycleEstimate(u32 op);
const char *MIPSGetName(u32 op);


void FillMIPSTables();
