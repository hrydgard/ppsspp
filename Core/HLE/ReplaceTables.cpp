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
#include "Core/MemMap.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/HLE/FunctionWrappers.h"

#include "GPU/Math3D.h"

#if defined(_M_IX86) || defined(_M_X64)
#include <emmintrin.h>
#endif

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

static int Replace_tanf() {
	float f = PARAMF(0);
	RETURNF(tanf(f));
	return 80;  // guess number of cycles
}

static int Replace_acosf() {
	float f = PARAMF(0);
	RETURNF(acosf(f));
	return 80;  // guess number of cycles
}

static int Replace_asinf() {
	float f = PARAMF(0);
	RETURNF(asinf(f));
	return 80;  // guess number of cycles
}

static int Replace_atanf() {
	float f = PARAMF(0);
	RETURNF(atanf(f));
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

static int Replace_floorf() {
	float f1 = PARAMF(0);
	RETURNF(floorf(f1));
	return 30;  // guess number of cycles
}

static int Replace_ceilf() {
	float f1 = PARAMF(0);
	RETURNF(ceilf(f1));
	return 30;  // guess number of cycles
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

static int Replace_memcpy16() {
	u32 destPtr = PARAM(0);
	u32 srcPtr = PARAM(1);
	u32 bytes = PARAM(2) * 16;
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

// a0 = pointer to destination address
// a1 = matrix
// a2 = source address
static int Replace_gta_dl_write_matrix() {
	u32 *ptr = (u32 *)Memory::GetPointerUnchecked(PARAM(0));
	u32 *dest = (u32_le *)Memory::GetPointerUnchecked(ptr[0]);
	u32 *src = (u32_le *)Memory::GetPointerUnchecked(PARAM(2));
	u32 matrix = PARAM(1) << 24;

#if defined(_M_IX86) || defined(_M_X64)
	__m128i topBytes = _mm_set1_epi32(matrix);
	__m128i m0 = _mm_loadu_si128((const __m128i *)src);
	__m128i m1 = _mm_loadu_si128((const __m128i *)(src + 4));
	__m128i m2 = _mm_loadu_si128((const __m128i *)(src + 8));
	__m128i m3 = _mm_loadu_si128((const __m128i *)(src + 12));
	m0 = _mm_or_si128(_mm_srli_epi32(m0, 8), topBytes);
	m1 = _mm_or_si128(_mm_srli_epi32(m1, 8), topBytes);
	m2 = _mm_or_si128(_mm_srli_epi32(m2, 8), topBytes);
	m3 = _mm_or_si128(_mm_srli_epi32(m3, 8), topBytes);
	// These three stores overlap by a word, due to the offsets.
	_mm_storeu_si128((__m128i *)dest, m0);
	_mm_storeu_si128((__m128i *)(dest + 3), m1);
	_mm_storeu_si128((__m128i *)(dest + 6), m2);
	// Store the last one in parts to not overwrite forwards (probably mostly risk free though)
	_mm_storel_epi64((__m128i *)(dest + 9), m3);
	m3 = _mm_srli_si128(m3, 8);
	_mm_store_ss((float *)(dest + 11), _mm_castsi128_ps(m3));
#else
	// Bit tricky to SIMD (note the offsets) but should be doable if not perfect
	dest[0] = matrix | (src[0] >> 8);
	dest[1] = matrix | (src[1] >> 8);
	dest[2] = matrix | (src[2] >> 8);
	dest[3] = matrix | (src[4] >> 8);
	dest[4] = matrix | (src[5] >> 8);
	dest[5] = matrix | (src[6] >> 8);
	dest[6] = matrix | (src[8] >> 8);
	dest[7] = matrix | (src[9] >> 8);
	dest[8] = matrix | (src[10] >> 8);
	dest[9] = matrix | (src[12] >> 8);
	dest[10] = matrix | (src[13] >> 8);
	dest[11] = matrix | (src[14] >> 8);
#endif

	(*ptr) += 0x30;
	RETURN(0);
	return 38;
}


// TODO: Inline into a few NEON or SSE instructions - especially if a1 is a known immediate!
// Anyway, not sure if worth it. There's not that many matrices written per frame normally.
static int Replace_dl_write_matrix() {
	u32 *dlStruct = (u32 *)Memory::GetPointerUnchecked(PARAM(0));
	u32 *dest = (u32 *)Memory::GetPointerUnchecked(dlStruct[2]);
	u32 *src = (u32 *)Memory::GetPointerUnchecked(PARAM(2));

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
	
	*dest++ = matrix;
	matrix += 0x01000000;

	if (count == 16) {
		// Ultra SIMD friendly! These intrinsics generate pretty much perfect code,
		// no point in hand rolling.
#if defined(_M_IX86) || defined(_M_X64)
		__m128i topBytes = _mm_set1_epi32(matrix);
		__m128i m0 = _mm_loadu_si128((const __m128i *)src);
		__m128i m1 = _mm_loadu_si128((const __m128i *)(src + 4));
		__m128i m2 = _mm_loadu_si128((const __m128i *)(src + 8));
		__m128i m3 = _mm_loadu_si128((const __m128i *)(src + 12));
		m0 = _mm_or_si128(_mm_srli_epi32(m0, 8), topBytes);
		m1 = _mm_or_si128(_mm_srli_epi32(m1, 8), topBytes);
		m2 = _mm_or_si128(_mm_srli_epi32(m2, 8), topBytes);
		m3 = _mm_or_si128(_mm_srli_epi32(m3, 8), topBytes);
		_mm_storeu_si128((__m128i *)dest, m0);
		_mm_storeu_si128((__m128i *)(dest + 4), m1);
		_mm_storeu_si128((__m128i *)(dest + 8), m2);
		_mm_storeu_si128((__m128i *)(dest + 12), m3);
#else
#if 0
		//TODO: Finish NEON, make conditional somehow
		uint32x4_t topBytes = vdupq_n_u32(matrix);
		uint32x4_t m0 = vld1q_u32(dataPtr);
		uint32x4_t m1 = vld1q_u32(dataPtr + 4);
		uint32x4_t m2 = vld1q_u32(dataPtr + 8);
		uint32x4_t m3 = vld1q_u32(dataPtr + 12);
		m0 = vorr_u32(vsri_n_u32(m0, 8), topBytes);  // TODO: look into VSRI
		m1 = vorr_u32(vshr_n_u32(m1, 8), topBytes);
		m2 = vorr_u32(vshr_n_u32(m2, 8), topBytes);
		m3 = vorr_u32(vshr_n_u32(m3, 8), topBytes);
		vst1q_u32(dlPtr, m0);
		vst1q_u32(dlPtr + 4, m1);
		vst1q_u32(dlPtr + 8, m2);
		vst1q_u32(dlPtr + 12, m3);
#endif
		for (int i = 0; i < count; i++) {
			dest[i] = matrix | (src[i] >> 8);
		}
#endif
	} else {
#if defined(_M_IX86) || defined(_M_X64)
		__m128i topBytes = _mm_set1_epi32(matrix);
		__m128i m0 = _mm_loadu_si128((const __m128i *)src);
		__m128i m1 = _mm_loadu_si128((const __m128i *)(src + 4));
		__m128i m2 = _mm_loadu_si128((const __m128i *)(src + 8));
		__m128i m3 = _mm_loadu_si128((const __m128i *)(src + 12));
		m0 = _mm_or_si128(_mm_srli_epi32(m0, 8), topBytes);
		m1 = _mm_or_si128(_mm_srli_epi32(m1, 8), topBytes);
		m2 = _mm_or_si128(_mm_srli_epi32(m2, 8), topBytes);
		m3 = _mm_or_si128(_mm_srli_epi32(m3, 8), topBytes);
		// These three stores overlap by a word, due to the offsets.
		_mm_storeu_si128((__m128i *)dest, m0);
		_mm_storeu_si128((__m128i *)(dest + 3), m1);
		_mm_storeu_si128((__m128i *)(dest + 6), m2);
		// Store the last one in parts to not overwrite forwards (probably mostly risk free though)
		_mm_storel_epi64((__m128i *)(dest + 9), m3);
		m3 = _mm_srli_si128(m3, 8);
		_mm_store_ss((float *)(dest + 11), _mm_castsi128_ps(m3));
#else
		// Bit tricky to SIMD (note the offsets) but should be doable if not perfect
		dest[0] = matrix | (src[0] >> 8);
		dest[1] = matrix | (src[1] >> 8);
		dest[2] = matrix | (src[2] >> 8);
		dest[3] = matrix | (src[4] >> 8);
		dest[4] = matrix | (src[5] >> 8);
		dest[5] = matrix | (src[6] >> 8);
		dest[6] = matrix | (src[8] >> 8);
		dest[7] = matrix | (src[9] >> 8);
		dest[8] = matrix | (src[10] >> 8);
		dest[9] = matrix | (src[12] >> 8);
		dest[10] = matrix | (src[13] >> 8);
		dest[11] = matrix | (src[14] >> 8);
#endif
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

	{ "tanf", &Replace_tanf, 0, 0},

	/*  These two collide (same hash) and thus can't be replaced :/
	{ "asinf", &Replace_asinf, 0, 0},
	{ "acosf", &Replace_acosf, 0, 0},
	*/

	{ "atanf", &Replace_atanf, 0, 0},
	{ "sqrtf", &Replace_sqrtf, 0, 0},
	{ "atan2f", &Replace_atan2f, 0, 0},
	{ "floorf", &Replace_floorf, 0, 0},
	{ "ceilf", &Replace_ceilf, 0, 0},

	{ "memcpy", &Replace_memcpy, 0, 0},
	{ "memcpy16", &Replace_memcpy16, 0, 0},
	{ "memmove", &Replace_memmove, 0, 0},
	{ "memset", &Replace_memset, 0, 0},
	{ "strlen", &Replace_strlen, 0, 0},
	{ "strcpy", &Replace_strcpy, 0, 0},
	{ "strncpy", &Replace_strncpy, 0, 0},
	{ "strcmp", &Replace_strcmp, 0, 0},
	{ "strncmp", &Replace_strncmp, 0, 0},

	{ "fabsf", 0, &MIPSComp::Jit::Replace_fabsf, REPFLAG_ALLOWINLINE},
	{ "dl_write_matrix", &Replace_dl_write_matrix, 0, 0}, // &MIPSComp::Jit::Replace_dl_write_matrix, 0},
	{ "dl_write_matrix_2", &Replace_dl_write_matrix, 0, 0},
	{ "gta_dl_write_matrix", &Replace_gta_dl_write_matrix, 0, 0},
	// dl_write_matrix_3 doesn't take the dl as a parameter, it accesses a global instead. Need to extract the address of the global from the code when replacing...
	// Haven't investigated write_matrix_4 and 5 but I think they are similar to 1 and 2.

	// { "vmmul_q_transp", &Replace_vmmul_q_transp, 0, 0},
	{}
};

static std::map<u32, u32> replacedInstructions;

void Replacement_Init() {
}

void Replacement_Shutdown() {
	replacedInstructions.clear();
}

// TODO: Do something on load state?

int GetNumReplacementFuncs() {
	return ARRAY_SIZE(entries);
}

int GetReplacementFuncIndex(u64 hash, int funcSize) {
	const char *name = MIPSAnalyst::LookupHash(hash, funcSize);
	if (!name) {
		return -1;
	}

	// TODO: Build a lookup and keep it around
	for (size_t i = 0; i < ARRAY_SIZE(entries); i++) {
		if (!entries[i].name)
			continue;
		if (!strcmp(name, entries[i].name)) {
			return (int)i;
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
		INFO_LOG(HLE, "Replaced %s at %08x with hash %016llx", entries[index].name, address, hash);
		Memory::Write_U32(MIPS_EMUHACK_CALL_REPLACEMENT | (int)index, address);
	}
}

void RestoreReplacedInstruction(u32 address) {
	const u32 curInstr = Memory::Read_U32(address);
	if (MIPS_IS_REPLACEMENT(curInstr)) {
		Memory::Write_U32(replacedInstructions[address], address);
	}
	INFO_LOG(HLE, "Restored replaced func at %08x", address);
	replacedInstructions.erase(address);
}

void RestoreReplacedInstructions(u32 startAddr, u32 endAddr) {
	// Need to be in order, or we'll hang.
	if (endAddr > startAddr)
		std::swap(endAddr, startAddr);
	const auto start = replacedInstructions.lower_bound(startAddr);
	const auto end = replacedInstructions.upper_bound(endAddr);
	int restored = 0;
	for (auto it = start; it != end; ++it) {
		const u32 addr = it->first;
		const u32 curInstr = Memory::Read_U32(addr);
		if (MIPS_IS_REPLACEMENT(curInstr)) {
			Memory::Write_U32(it->second, addr);
			++restored;
		}
	}
	INFO_LOG(HLE, "Restored %d replaced funcs between %08x-%08x", restored, startAddr, endAddr);
	replacedInstructions.erase(start, end);
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
