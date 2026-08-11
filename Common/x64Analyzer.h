// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

#include "Common/CommonTypes.h"

// What kind of register regOperandReg (and otherReg, if used as a source/dest rather than
// just an address component) refers to, and thus how the instruction should be interpreted.
enum class InstructionClass {
	GPR,      // General-purpose register (mov, movzx, movsx, ...)
	FP,       // Scalar floating point (movss, movsd, ...)
	FP_SIMD,  // Full vector register (movups, movaps, movdqa, ...)
};

struct LSInstructionInfo {
	int operandSizeInBytes;  // 1, 2, 4, 8 (in bytes, despite the field name suggesting bits)
	int instructionSize;
	int regOperandReg;
	int otherReg;
	int scaledReg;
	bool zeroExtend;
	bool signExtend;
	bool hasImmediate;
	bool isMemoryWrite;
	u64 immediate;
	s32 displacement;
	InstructionClass instructionClass;

	int OperandSizeInBytes() const { return operandSizeInBytes; }
};

struct ModRM
{
	int mod, reg, rm;
	ModRM(u8 modRM, u8 rex)
	{
		mod = modRM >> 6;
		reg = ((modRM >> 3) & 7) | ((rex & 4)?8:0);
		rm = modRM & 7;
	}
};

enum {
	MOVZX_BYTE      = 0xB6, //movzx on byte
	MOVZX_SHORT     = 0xB7, //movzx on short
	MOVSX_BYTE      = 0xBE, //movsx on byte
	MOVSX_SHORT     = 0xBF, //movsx on short
	MOVE_8BIT	    = 0xC6, //move 8-bit immediate
	MOVE_16_32BIT   = 0xC7, //move 16 or 32-bit immediate
	MOVE_REG_TO_MEM = 0x89, //move reg to memory
	MOVE_MEM_TO_REG = 0x8B, //move memory to reg
	// These two opcodes are shared between MOVUPS (no mandatory prefix) and MOVSS (mandatory 0xF3 prefix).
	MOVUPS_MOVSS_FROM_RM = 0x10, //movups/movss xmm, xmm/m
	MOVUPS_MOVSS_TO_RM   = 0x11, //movups/movss xmm/m, xmm
	MOVAPS_FROM_RM  = 0x28, //movaps xmm, xmm/m
	MOVAPS_TO_RM    = 0x29, //movaps xmm/m, xmm
};

enum AccessType {
	OP_ACCESS_READ = 0,
	OP_ACCESS_WRITE = 1
};

bool X86AnalyzeMOV(const unsigned char *codePtr, LSInstructionInfo &info);
