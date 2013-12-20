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

// I think these have to be pretty accurate as these are libc replacements,
// but we can probably get away with approximating the VFPU vsin/vcos and vrot
// pretty roughly.
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
	u32 srcPtr = PARAM(1);
	u32 bytes = PARAM(2);
	if (bytes != 0) {
		u8 *dst = Memory::GetPointerUnchecked(destPtr);
		u8 *src = Memory::GetPointerUnchecked(srcPtr);
		memmove(dst, src, bytes);
	}
	RETURN(destPtr);
	return 10 + bytes / 4;  // approximation
}

static int Replace_memmove() {
	u32 destPtr = PARAM(0);
	u32 srcPtr = PARAM(1);
	u32 bytes = PARAM(2);
	if (bytes != 0) {
		u8 *dst = Memory::GetPointerUnchecked(destPtr);
		u8 *src = Memory::GetPointerUnchecked(srcPtr);
		memmove(dst, src, bytes);
	}
	RETURN(destPtr);
	return 10 + bytes / 4;  // approximation
}

static int Replace_memset() {
	u32 destPtr = PARAM(0);
	u8 *dst = Memory::GetPointerUnchecked(destPtr);
	u8 value = PARAM(1);
	u32 bytes = PARAM(2);
	memset(dst, value, bytes);
	RETURN(destPtr);
	return 10 + bytes / 4;  // approximation
}

static int Replace_strlen() {
	u32 srcPtr = PARAM(0);
	const char *src = (const char *)Memory::GetPointerUnchecked(srcPtr);
	u32 len = (u32)strlen(src);
	RETURN(len);
	return 4 + len;  // approximation
}

static int Replace_strcpy() {
	u32 destPtr = PARAM(0);
	char *dst = (char *)Memory::GetPointerUnchecked(destPtr);
	const char *src = (const char *)Memory::GetPointerUnchecked(PARAM(1));
	strcpy(dst, src);
	RETURN(destPtr);
	return 10;  // approximation
}

static int Replace_strncpy() {
	u32 destPtr = PARAM(0);
	char *dst = (char *)Memory::GetPointerUnchecked(destPtr);
	const char *src = (const char *)Memory::GetPointerUnchecked(PARAM(1));
	u32 bytes = PARAM(2);
	strncpy(dst, src, bytes);
	RETURN(destPtr);
	return 10;  // approximation
}

static int Replace_strcmp() {
	const char *a = (const char *)Memory::GetPointerUnchecked(PARAM(0));
	const char *b = (const char *)Memory::GetPointerUnchecked(PARAM(1));
	RETURN(strcmp(a, b));
	return 10;  // approximation
}

static int Replace_strncmp() {
	const char *a = (const char *)Memory::GetPointerUnchecked(PARAM(0));
	const char *b = (const char *)Memory::GetPointerUnchecked(PARAM(1));
	u32 bytes = PARAM(2);
	RETURN(strncmp(a, b, bytes));
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

// TODO: Inline into a few NEON or SSE instructions - especially if a1 is a known immediate!
// Anyway, not sure if worth it. There's not that many matrices written per frame normally.
static int Replace_dl_write_matrix() {
	u32 *dlStruct = (u32 *)Memory::GetPointerUnchecked(PARAM(0));
	u32 *dlPtr = (u32 *)Memory::GetPointerUnchecked(dlStruct[2]);
	u32 *dataPtr = (u32 *)Memory::GetPointerUnchecked(PARAM(2));

	u32 matrix;
	int count = 12;
	switch (PARAM(1)) {
	case 3:
		matrix = 0x40000000;  // tex mtx
		break;
	case 2:
		matrix = 0x3A000000;
		break;
	case 1:
		matrix = 0x3C000000;
		break;
	case 0:
		matrix = 0x3E000000;
		count = 16;
		break;
	}
	
	*dlPtr++ = matrix;
	matrix += 0x01000000;

	if (count == 16) {
		// Ultra SIMD friendly!
		for (int i = 0; i < count; i++) {
			dlPtr[i] = matrix | (dataPtr[i] >> 8);
		}
	} else {
		// Bit tricky to SIMD (note the offsets) but should be doable
		dlPtr[0] = matrix | (dataPtr[0] >> 8);
		dlPtr[1] = matrix | (dataPtr[1] >> 8);
		dlPtr[2] = matrix | (dataPtr[2] >> 8);
		dlPtr[3] = matrix | (dataPtr[4] >> 8);
		dlPtr[4] = matrix | (dataPtr[5] >> 8);
		dlPtr[5] = matrix | (dataPtr[6] >> 8);
		dlPtr[6] = matrix | (dataPtr[8] >> 8);
		dlPtr[7] = matrix | (dataPtr[9] >> 8);
		dlPtr[8] = matrix | (dataPtr[10] >> 8);
		dlPtr[9] = matrix | (dataPtr[12] >> 8);
		dlPtr[10] = matrix | (dataPtr[13] >> 8);
		dlPtr[11] = matrix | (dataPtr[14] >> 8);
	}

	dlStruct[2] += (1 + count) * 4;
	RETURN(dlStruct[2]);
	return 60;
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
	/*
	{ "memcpy", &Replace_memcpy, 0, 0},
	{ "memmove", &Replace_memmove, 0, 0},
	{ "memset", &Replace_memset, 0, 0},
	{ "strlen", &Replace_strlen, 0, 0},
	{ "strcpy", &Replace_strcpy, 0, 0},
	{ "strncpy", &Replace_strncpy, 0, 0},
	{ "strcmp", &Replace_strcmp, 0, 0},
	{ "strncmp", &Replace_strncmp, 0, 0},
	*/
	{ "fabsf", 0, &MIPSComp::Jit::Replace_fabsf, REPFLAG_ALLOWINLINE},
	{ "dl_write_matrix", &Replace_dl_write_matrix, 0, 0},
	{ "dl_write_matrix_2", &Replace_dl_write_matrix, 0, 0},
	// dl_write_matrix_3 doesn't take the dl as a parameter, it accesses a global instead. Need to extract the address of the global from the code when replacing...
	// dunno about write_matrix_3 and 4

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
		if (MIPS_IS_REPLACEMENT(prevInstr)) {
			return;
		}
		if (MIPS_IS_RUNBLOCK(prevInstr)) {
			// Likely already both replaced and jitted. Ignore.
			return;
		}
		replacedInstructions[address] = prevInstr;
		INFO_LOG(HLE, "Replaced %s at %08x", entries[index].name, address);
		Memory::Write_U32(MIPS_EMUHACK_CALL_REPLACEMENT | (int)index, address);
	}
}

bool GetReplacedOpAt(u32 address, u32 *op) {
	u32 instr = Memory::Read_Opcode_JIT(address).encoding;
	if (MIPS_IS_REPLACEMENT(instr)) {
		auto iter = replacedInstructions.find(address);
		if (iter != replacedInstructions.end()) {
			*op = iter->second;
			return true;
		} else {
			return false;
		}
	}
	return false;
}
