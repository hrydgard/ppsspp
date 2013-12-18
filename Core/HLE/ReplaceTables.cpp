// Copyright (c) 2013- PPSSPP Project.

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

#include <map>

#include "base/basictypes.h"
#include "base/logging.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/HLE/FunctionWrappers.h"

#include "GPU/Math3D.h"

// I think these have to be pretty accurate, but we can probably
// get away with approximating the VFPU vsin/vcos and vrot pretty roughly.
static int Replace_sinf() {
	float f = PARAMF(0);
	RETURNF(sinf(f));
	return 80;  // guess number of cycles
}

static int Replace_cosf() {
	float f = PARAMF(0);
	RETURNF(cosf(f));
	return 80;  // guess number of cycles
}

static int Replace_sqrtf() {
	float f = PARAMF(0);
	RETURNF(sqrtf(f));
	return 80;  // guess number of cycles
}

static int Replace_atan2f() {
	float f1 = PARAMF(0);
	float f2 = PARAMF(1);
	RETURNF(atan2f(f1, f2));
	return 120;  // guess number of cycles
}

// Should probably do JIT versions of this, possibly ones that only delegate
// large copies to a C function.
static int Replace_memcpy() {
	u32 destPtr = PARAM(0);
	u8 *dst = Memory::GetPointer(destPtr);
	u8 *src = Memory::GetPointer(PARAM(1));
	u32 bytes = PARAM(2);
	if (dst && src) {
		memcpy(dst, src, bytes);
	}
	RETURN(destPtr);
	return 10 + bytes / 4;  // approximation
}

static int Replace_memmove() {
	u32 destPtr = PARAM(0);
	u8 *dst = Memory::GetPointer(destPtr);
	u8 *src = Memory::GetPointer(PARAM(1));
	u32 bytes = PARAM(2);
	if (dst && src) {
		memcpy(dst, src, bytes);
	}
	RETURN(destPtr);
	return 10 + bytes / 4;  // approximation
}

static int Replace_memset() {
	u32 destPtr = PARAM(0);
	u8 *dst = Memory::GetPointer(destPtr);
	u8 value = PARAM(1);
	u32 bytes = PARAM(2);
	if (dst) {
		memset(dst, value, bytes);
	}
	RETURN(destPtr);
	return 10 + bytes / 4;  // approximation
}

static int Replace_strlen() {
	u32 srcPtr = PARAM(0);
	const char *src = (const char *)Memory::GetPointer(srcPtr);
	u32 len = (u32)strlen(src);
	RETURN(len);
	return 4 + len;  // approximation
}

static int Replace_strcpy() {
	u32 destPtr = PARAM(0);
	char *dst = (char *)Memory::GetPointer(destPtr);
	const char *src = (const char *)Memory::GetPointer(PARAM(1));
	if (dst && src) {
		strcpy(dst, src);
	}
	RETURN(destPtr);
	return 10;  // approximation
}

static int Replace_strcmp() {
	const char *a = (const char *)Memory::GetPointer(PARAM(0));
	const char *b = (const char *)Memory::GetPointer(PARAM(1));
	if (a && b) {
		RETURN(strcmp(a, b));
	}
	return 10;  // approximation
}

static int Replace_strncmp() {
	const char *a = (const char *)Memory::GetPointer(PARAM(0));
	const char *b = (const char *)Memory::GetPointer(PARAM(1));
	u32 bytes = PARAM(2);
	if (a && b) {
		RETURN(strncmp(a, b, bytes));
	}
	return 10 + bytes / 4;  // approximation
}

static int Replace_vmmul_q_transp() {
	float *out = (float *)Memory::GetPointerUnchecked(PARAM(0));
	const float *a = (const float *)Memory::GetPointerUnchecked(PARAM(1));
	const float *b = (const float *)Memory::GetPointerUnchecked(PARAM(2));

	// TODO: Actually use an optimized matrix multiply here...
	Matrix4ByMatrix4(out, b, a);
	return 16;
}

// Can either replace with C functions or functions emitted in Asm/ArmAsm.
static const ReplacementTableEntry entries[] = {
	// TODO: I think some games can be helped quite a bit by implementing the
	// double-precision soft-float routines: __adddf3, __subdf3 and so on. These
	// should of course be implemented JIT style, inline.

	{ "sinf", &Replace_sinf, 0, 0},
	{ "cosf", &Replace_cosf, 0, 0},
	{ "sqrtf", &Replace_sqrtf, 0, 0},
	{ "atan2f", &Replace_atan2f, 0, 0},
	{ "memcpy", &Replace_memcpy, 0, 0},
	{ "memmove", &Replace_memmove, 0, 0},
	{ "memset", &Replace_memset, 0, 0},
	{ "strlen", &Replace_strlen, 0, 0},
	{ "strcpy", &Replace_strcpy, 0, 0},
	{ "strcmp", &Replace_strcmp, 0, 0},
	{ "strncmp", &Replace_strncmp, 0, 0},
	{ "fabsf", 0, &MIPSComp::Jit::Replace_fabsf, REPFLAG_ALLOWINLINE},
	// { "vmmul_q_transp", &Replace_vmmul_q_transp, 0, 0},
	{}
};

static std::map<u32, u32> replacedInstructions;

void Replacement_Init() {
}

void Replacement_Shutdown() {
	replacedInstructions.clear();
}

int GetNumReplacementFuncs() {
	return ARRAY_SIZE(entries);
}

int GetReplacementFuncIndex(u64 hash, int funcSize) {
	const char *name = MIPSAnalyst::LookupHash(hash, funcSize);
	if (!name) {
		return -1;
	}

	// TODO: Build a lookup and keep it around
	for (int i = 0; i < ARRAY_SIZE(entries); i++) {
		if (!entries[i].name)
			continue;
		if (!strcmp(name, entries[i].name)) {
			return i;
		}
	}
	return -1;
}

const ReplacementTableEntry *GetReplacementFunc(int i) {
	return &entries[i];
}

void WriteReplaceInstruction(u32 address, u64 hash, int size) {
	int index = GetReplacementFuncIndex(hash, size);
	if (index >= 0) {
		u32 prevInstr = Memory::Read_U32(address);
		if (MIPS_IS_REPLACEMENT(prevInstr))
			return;
		replacedInstructions[address] = prevInstr;
		INFO_LOG(HLE, "Replaced %s at %08x", entries[index].name, address);
		Memory::Write_U32(MIPS_EMUHACK_CALL_REPLACEMENT | (int)index, address);
	}
}

bool GetReplacedOpAt(u32 address, u32 *op) {
	u32 instr = Memory::Read_U32(address);
	if (MIPS_IS_REPLACEMENT(instr)) {
		auto iter = replacedInstructions.find(address);
		if (iter != replacedInstructions.end()) {
			*op = iter->second;
			return true;
		} else {
			return false;
		}
	}
	*op = instr;
	return true;
}
