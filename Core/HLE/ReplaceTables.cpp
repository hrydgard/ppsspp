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

#include "ppsspp_config.h"
#include <algorithm>
#include <map>
#include <unordered_map>

#include "Common/CommonTypes.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Log.h"
#include "Common/Swap.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MemMap.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/MIPS/MIPSCodeUtils.h"
#include "Core/MIPS/MIPSAnalyst.h"
#include "Core/HLE/ReplaceTables.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceDisplay.h"

#include "GPU/Math3D.h"
#include "GPU/GPU.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
#include <emmintrin.h>
#endif

enum class GPUReplacementSkip {
	MEMSET = 1,
	MEMCPY = 2,
	MEMMOVE = 4,
};

static int skipGPUReplacements = 0;

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
	bool skip = false;
	if (!bytes) {
		RETURN(destPtr);
		return 10;
	}

	// Some games use memcpy on executable code.  We need to flush emuhack ops.
	currentMIPS->InvalidateICache(srcPtr, bytes);
	if ((skipGPUReplacements & (int)GPUReplacementSkip::MEMCPY) == 0) {
		if (Memory::IsVRAMAddress(destPtr) || Memory::IsVRAMAddress(srcPtr)) {
			skip = gpu->PerformMemoryCopy(destPtr, srcPtr, bytes);
		}
	}
	if (!skip && bytes != 0) {
		u8 *dst = Memory::GetPointer(destPtr);
		const u8 *src = Memory::GetPointer(srcPtr);

		if (!dst || !src) {
			// Already logged.
		} else if (std::min(destPtr, srcPtr) + bytes > std::max(destPtr, srcPtr)) {
			// Overlap.  Star Ocean breaks if it's not handled in 16 bytes blocks.
			const u32 blocks = bytes & ~0x0f;
			for (u32 offset = 0; offset < blocks; offset += 0x10) {
				memcpy(dst + offset, src + offset, 0x10);
			}
			for (u32 offset = blocks; offset < bytes; ++offset) {
				dst[offset] = src[offset];
			}
		} else {
			memmove(dst, src, bytes);
		}
	}
	RETURN(destPtr);

	if (MemBlockInfoDetailed(bytes)) {
		const std::string tag = "ReplaceMemcpy/" + GetMemWriteTagAt(srcPtr, bytes);
		NotifyMemInfo(MemBlockFlags::READ, srcPtr, bytes, tag.c_str(), tag.size());
		NotifyMemInfo(MemBlockFlags::WRITE, destPtr, bytes, tag.c_str(), tag.size());

		// It's pretty common that games will copy video data.
		if (tag == "ReplaceMemcpy/VideoDecode" || tag == "ReplaceMemcpy/VideoDecodeRange") {
			if (bytes == 512 * 272 * 4) {
				gpu->NotifyVideoUpload(destPtr, bytes, 512, GE_FORMAT_8888);
			}
		}
	}

	return 10 + bytes / 4;  // approximation
}

static int Replace_memcpy_jak() {
	u32 destPtr = PARAM(0);
	u32 srcPtr = PARAM(1);
	u32 bytes = PARAM(2);
	bool skip = false;
	if (bytes == 0) {
		RETURN(destPtr);
		return 5;
	}
	currentMIPS->InvalidateICache(srcPtr, bytes);
	if ((skipGPUReplacements & (int)GPUReplacementSkip::MEMCPY) == 0) {
		if (Memory::IsVRAMAddress(destPtr) || Memory::IsVRAMAddress(srcPtr)) {
			skip = gpu->PerformMemoryCopy(destPtr, srcPtr, bytes);
		}
	}
	if (!skip && bytes != 0) {
		u8 *dst = Memory::GetPointer(destPtr);
		const u8 *src = Memory::GetPointer(srcPtr);

		if (!dst || !src) {
		} else {
			// Jak style overlap.
			for (u32 i = 0; i < bytes; i++) {
				dst[i] = src[i];
			}
		}
	}

	// Jak relies on more registers coming out right than the ABI specifies.
	// See the disassembly of the function for the explanations for these...
	currentMIPS->r[MIPS_REG_T0] = 0;
	currentMIPS->r[MIPS_REG_A0] = -1;
	currentMIPS->r[MIPS_REG_A2] = 0;
	currentMIPS->r[MIPS_REG_A3] = destPtr + bytes;
	RETURN(destPtr);

	if (MemBlockInfoDetailed(bytes)) {
		const std::string tag = "ReplaceMemcpy/" + GetMemWriteTagAt(srcPtr, bytes);
		NotifyMemInfo(MemBlockFlags::READ, srcPtr, bytes, tag.c_str(), tag.size());
		NotifyMemInfo(MemBlockFlags::WRITE, destPtr, bytes, tag.c_str(), tag.size());

		// It's pretty common that games will copy video data.
		if (tag == "ReplaceMemcpy/VideoDecode" || tag == "ReplaceMemcpy/VideoDecodeRange") {
			if (bytes == 512 * 272 * 4) {
				gpu->NotifyVideoUpload(destPtr, bytes, 512, GE_FORMAT_8888);
			}
		}
	}

	return 5 + bytes * 8 + 2;  // approximation. This is a slow memcpy - a byte copy loop..
}

static int Replace_memcpy16() {
	u32 destPtr = PARAM(0);
	u32 srcPtr = PARAM(1);
	u32 bytes = PARAM(2) * 16;
	bool skip = false;

	// Some games use memcpy on executable code.  We need to flush emuhack ops.
	currentMIPS->InvalidateICache(srcPtr, bytes);
	if ((skipGPUReplacements & (int)GPUReplacementSkip::MEMCPY) == 0) {
		if (Memory::IsVRAMAddress(destPtr) || Memory::IsVRAMAddress(srcPtr)) {
			skip = gpu->PerformMemoryCopy(destPtr, srcPtr, bytes);
		}
	}
	if (!skip && bytes != 0) {
		u8 *dst = Memory::GetPointer(destPtr);
		const u8 *src = Memory::GetPointer(srcPtr);
		if (dst && src) {
			memmove(dst, src, bytes);
		}
	}
	RETURN(destPtr);

	if (MemBlockInfoDetailed(bytes)) {
		const std::string tag = "ReplaceMemcpy16/" + GetMemWriteTagAt(srcPtr, bytes);
		NotifyMemInfo(MemBlockFlags::READ, srcPtr, bytes, tag.c_str(), tag.size());
		NotifyMemInfo(MemBlockFlags::WRITE, destPtr, bytes, tag.c_str(), tag.size());
	}

	return 10 + bytes / 4;  // approximation
}

static int Replace_memcpy_swizzled() {
	u32 destPtr = PARAM(0);
	u32 srcPtr = PARAM(1);
	u32 pitch = PARAM(2);
	u32 h = PARAM(4);
	if ((skipGPUReplacements & (int)GPUReplacementSkip::MEMCPY) == 0) {
		if (Memory::IsVRAMAddress(srcPtr)) {
			gpu->PerformMemoryDownload(srcPtr, pitch * h);
		}
	}
	u8 *dstp = Memory::GetPointer(destPtr);
	const u8 *srcp = Memory::GetPointer(srcPtr);

	if (dstp && srcp) {
		const u8 *ysrcp = srcp;
		for (u32 y = 0; y < h; y += 8) {
			const u8 *xsrcp = ysrcp;
			for (u32 x = 0; x < pitch; x += 16) {
				const u8 *src = xsrcp;
				for (int n = 0; n < 8; ++n) {
					memcpy(dstp, src, 16);
					src += pitch;
					dstp += 16;
				}
				xsrcp += 16;
			}
			ysrcp += 8 * pitch;
		}
	}

	RETURN(0);

	if (MemBlockInfoDetailed(pitch * h)) {
		const std::string tag = "ReplaceMemcpySwizzle/" + GetMemWriteTagAt(srcPtr, pitch * h);
		NotifyMemInfo(MemBlockFlags::READ, srcPtr, pitch * h, tag.c_str(), tag.size());
		NotifyMemInfo(MemBlockFlags::WRITE, destPtr, pitch * h, tag.c_str(), tag.size());
	}

	return 10 + (pitch * h) / 4;  // approximation
}

static int Replace_memmove() {
	u32 destPtr = PARAM(0);
	u32 srcPtr = PARAM(1);
	u32 bytes = PARAM(2);
	bool skip = false;

	// Some games use memcpy on executable code.  We need to flush emuhack ops.
	if ((skipGPUReplacements & (int)GPUReplacementSkip::MEMMOVE) == 0) {
		currentMIPS->InvalidateICache(srcPtr, bytes);
		if (Memory::IsVRAMAddress(destPtr) || Memory::IsVRAMAddress(srcPtr)) {
			skip = gpu->PerformMemoryCopy(destPtr, srcPtr, bytes);
		}
	}
	if (!skip && bytes != 0) {
		u8 *dst = Memory::GetPointer(destPtr);
		const u8 *src = Memory::GetPointer(srcPtr);
		if (dst && src) {
			memmove(dst, src, bytes);
		}
	}
	RETURN(destPtr);

	if (MemBlockInfoDetailed(bytes)) {
		const std::string tag = "ReplaceMemmove/" + GetMemWriteTagAt(srcPtr, bytes);
		NotifyMemInfo(MemBlockFlags::READ, srcPtr, bytes, tag.c_str(), tag.size());
		NotifyMemInfo(MemBlockFlags::WRITE, destPtr, bytes, tag.c_str(), tag.size());
	}

	return 10 + bytes / 4;  // approximation
}

static int Replace_memset() {
	u32 destPtr = PARAM(0);
	u8 value = PARAM(1);
	u32 bytes = PARAM(2);
	bool skip = false;
	if (Memory::IsVRAMAddress(destPtr) && (skipGPUReplacements & (int)GPUReplacementSkip::MEMSET) == 0) {
		skip = gpu->PerformMemorySet(destPtr, value, bytes);
	}
	if (!skip && bytes != 0) {
		u8 *dst = Memory::GetPointer(destPtr);
		if (dst) {
			memset(dst, value, bytes);
		}
	}
	RETURN(destPtr);

	NotifyMemInfo(MemBlockFlags::WRITE, destPtr, bytes, "ReplaceMemset");

	return 10 + bytes / 4;  // approximation
}

static int Replace_memset_jak() {
	u32 destPtr = PARAM(0);
	u8 value = PARAM(1);
	u32 bytes = PARAM(2);

	if (bytes == 0) {
		RETURN(destPtr);
		return 5;
	}

	bool skip = false;
	if (Memory::IsVRAMAddress(destPtr) && (skipGPUReplacements & (int)GPUReplacementSkip::MEMSET) == 0) {
		skip = gpu->PerformMemorySet(destPtr, value, bytes);
	}
	if (!skip && bytes != 0) {
		u8 *dst = Memory::GetPointer(destPtr);
		if (dst) {
			memset(dst, value, bytes);
		}
	}

	currentMIPS->r[MIPS_REG_T0] = destPtr + bytes;
	currentMIPS->r[MIPS_REG_A2] = -1;
	currentMIPS->r[MIPS_REG_A3] = -1;
	RETURN(destPtr);

	NotifyMemInfo(MemBlockFlags::WRITE, destPtr, bytes, "ReplaceMemset");

	return 5 + bytes * 6 + 2;  // approximation (hm, inspecting the disasm this should be 5 + 6 * bytes + 2, but this is what works..)
}

static int Replace_strlen() {
	u32 srcPtr = PARAM(0);
	const char *src = (const char *)Memory::GetPointer(srcPtr);
	u32 len = src ? (u32)strlen(src) : 0UL;
	RETURN(len);
	return 7 + len * 4;  // approximation
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

static int Replace_strncpy() {
	u32 destPtr = PARAM(0);
	char *dst = (char *)Memory::GetPointer(destPtr);
	const char *src = (const char *)Memory::GetPointer(PARAM(1));
	u32 bytes = PARAM(2);
	if (dst && src && bytes != 0) {
		strncpy(dst, src, bytes);
	}
	RETURN(destPtr);
	return 10;  // approximation
}

static int Replace_strcmp() {
	const char *a = (const char *)Memory::GetPointer(PARAM(0));
	const char *b = (const char *)Memory::GetPointer(PARAM(1));
	if (a && b) {
		RETURN(strcmp(a, b));
	} else {
		RETURN(0);
	}
	return 10;  // approximation
}

static int Replace_strncmp() {
	const char *a = (const char *)Memory::GetPointer(PARAM(0));
	const char *b = (const char *)Memory::GetPointer(PARAM(1));
	u32 bytes = PARAM(2);
	if (a && b && bytes != 0) {
		RETURN(strncmp(a, b, bytes));
	} else {
		RETURN(0);
	}
	return 10 + bytes / 4;  // approximation
}

static int Replace_fabsf() {
	RETURNF(fabsf(PARAMF(0)));
	return 4;
}

static int Replace_vmmul_q_transp() {
	float_le *out = (float_le *)Memory::GetPointer(PARAM(0));
	const float_le *a = (const float_le *)Memory::GetPointer(PARAM(1));
	const float_le *b = (const float_le *)Memory::GetPointer(PARAM(2));

	// TODO: Actually use an optimized matrix multiply here...
	if (out && b && a) {
#ifdef COMMON_BIG_ENDIAN
		float outn[16], an[16], bn[16];
		for (int i = 0; i < 16; ++i) {
			an[i] = a[i];
			bn[i] = b[i];
		}
		Matrix4ByMatrix4(outn, bn, an);
		for (int i = 0; i < 16; ++i) {
			out[i] = outn[i];
		}
#else
		Matrix4ByMatrix4(out, b, a);
#endif
	}
	return 16;
}

// a0 = pointer to destination address
// a1 = matrix
// a2 = source address
static int Replace_gta_dl_write_matrix() {
	u32_le *ptr = (u32_le *)Memory::GetPointer(PARAM(0));
	u32_le *src = (u32_le *)Memory::GetPointer(PARAM(2));
	u32 matrix = PARAM(1) << 24;

	if (!ptr || !src) {
		RETURN(0);
		return 38;
	}

	u32_le *dest = (u32_le *)Memory::GetPointer(ptr[0]);
	if (!dest) {
		RETURN(0);
		return 38;
	}

#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
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
	u32_le *dlStruct = (u32_le *)Memory::GetPointer(PARAM(0));
	u32_le *src = (u32_le *)Memory::GetPointer(PARAM(2));

	if (!dlStruct || !src) {
		RETURN(0);
		return 60;
	}

	u32_le *dest = (u32_le *)Memory::GetPointer(dlStruct[2]);
	if (!dest) {
		RETURN(0);
		return 60;
	}

	u32 matrix = 0;
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
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
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
#if PPSSPP_ARCH(X86) || PPSSPP_ARCH(AMD64)
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

	NotifyMemInfo(MemBlockFlags::READ, PARAM(2), count * sizeof(float), "ReplaceDLWriteMatrix");
	NotifyMemInfo(MemBlockFlags::WRITE, PARAM(0) + 2 * sizeof(u32), sizeof(u32), "ReplaceDLWriteMatrix");
	NotifyMemInfo(MemBlockFlags::WRITE, dlStruct[2], (count + 1) * sizeof(u32), "ReplaceDLWriteMatrix");

	dlStruct[2] += (1 + count) * 4;
	RETURN(dlStruct[2]);
	return 60;
}

static bool GetMIPSStaticAddress(u32 &addr, s32 lui_offset, s32 lw_offset) {
	const MIPSOpcode upper = Memory::Read_Instruction(currentMIPS->pc + lui_offset, true);
	if (upper != MIPS_MAKE_LUI(MIPS_GET_RT(upper), upper & 0xffff)) {
		return false;
	}
	const MIPSOpcode lower = Memory::Read_Instruction(currentMIPS->pc + lw_offset, true);
	if (lower != MIPS_MAKE_LW(MIPS_GET_RT(lower), MIPS_GET_RS(lower), lower & 0xffff)) {
		if (lower != MIPS_MAKE_ORI(MIPS_GET_RT(lower), MIPS_GET_RS(lower), lower & 0xffff)) {
			return false;
		}
	}
	addr = ((upper & 0xffff) << 16) + (s16)(lower & 0xffff);
	return true;
}

static bool GetMIPSGPAddress(u32 &addr, s32 offset) {
	const MIPSOpcode loadOp = Memory::Read_Instruction(currentMIPS->pc + offset, true);
	if (MIPS_GET_RS(loadOp) == MIPS_REG_GP) {
		s16 gpoff = (s16)(u16)(loadOp & 0x0000FFFF);
		addr = currentMIPS->r[MIPS_REG_GP] + gpoff;
		return true;
	}

	return false;
}

static int Hook_godseaterburst_blit_texture() {
	u32 texaddr;
	// Only if there's no texture.
	if (!GetMIPSStaticAddress(texaddr, 0x000c, 0x0030)) {
		return 0;
	}
	u32 fb_infoaddr;
	if (Memory::Read_U32(texaddr) != 0 || !GetMIPSStaticAddress(fb_infoaddr, 0x01d0, 0x01d4)) {
		return 0;
	}

	const u32 fb_info = Memory::Read_U32(fb_infoaddr);
	const u32 fb_address = Memory::Read_U32(fb_info);
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00044000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00044000, "godseaterburst_blit_texture");
	}
	return 0;
}

static int Hook_hexyzforce_monoclome_thread() {
	u32 fb_info;
	if (!GetMIPSStaticAddress(fb_info, -4, 0)) {
		return 0;
	}

	const u32 fb_address = Memory::Read_U32(fb_info);
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "hexyzforce_monoclome_thread");
	}
	return 0;
}

static int Hook_starocean_write_stencil() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_T7];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformStencilUpload(fb_address, 0x00088000);
	}
	return 0;
}

static int Hook_topx_create_saveicon() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_V0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00044000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00044000, "topx_create_saveicon");
	}
	return 0;
}

static int Hook_ff1_battle_effect() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A1];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "ff1_battle_effect");
	}
	return 0;
}

static int Hook_dissidia_recordframe_avi() {
	// This is called once per frame, and records that frame's data to avi.
	const u32 fb_address = currentMIPS->r[MIPS_REG_A1];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00044000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00044000, "dissidia_recordframe_avi");
	}
	return 0;
}

static int Hook_brandish_download_frame() {
	u32 fb_infoaddr;
	if (!GetMIPSStaticAddress(fb_infoaddr, 0x2c, 0x30)) {
		return 0;
	}
	const u32 fb_info = Memory::Read_U32(fb_infoaddr);
	const MIPSOpcode fb_index_load = Memory::Read_Instruction(currentMIPS->pc + 0x38, true);
	if (fb_index_load != MIPS_MAKE_LW(MIPS_GET_RT(fb_index_load), MIPS_GET_RS(fb_index_load), fb_index_load & 0xffff)) {
		return 0;
	}
	const int fb_index_offset = (s16)(fb_index_load & 0xffff);
	const u32 fb_index = (Memory::Read_U32(fb_info + fb_index_offset) + 1) & 1;
	const u32 fb_address = 0x4000000 + (0x44000 * fb_index);
	const u32 dest_address = currentMIPS->r[MIPS_REG_A1];
	if (Memory::IsRAMAddress(dest_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00044000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00044000, "brandish_download_frame");
	}
	return 0;
}

static int Hook_growlanser_create_saveicon() {
	const u32 fb_address = Memory::Read_U32(currentMIPS->r[MIPS_REG_SP] + 4);
	const u32 fmt = Memory::Read_U32(currentMIPS->r[MIPS_REG_SP]);
	const u32 sz = fmt == GE_FORMAT_8888 ? 0x00088000 : 0x00044000;
	if (Memory::IsVRAMAddress(fb_address) && fmt <= 3) {
		gpu->PerformMemoryDownload(fb_address, sz);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, sz, "growlanser_create_saveicon");
	}
	return 0;
}

static int Hook_sd_gundam_g_generation_download_frame() {
	const u32 fb_address = Memory::Read_U32(currentMIPS->r[MIPS_REG_SP] + 8);
	const u32 fmt = Memory::Read_U32(currentMIPS->r[MIPS_REG_SP] + 4);
	const u32 sz = fmt == GE_FORMAT_8888 ? 0x00088000 : 0x00044000;
	if (Memory::IsVRAMAddress(fb_address) && fmt <= 3) {
		gpu->PerformMemoryDownload(fb_address, sz);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, sz, "sd_gundam_g_generation_download_frame");
	}
	return 0;
}

static int Hook_narisokonai_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_V0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00044000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00044000, "narisokonai_download_frame");
	}
	return 0;
}

static int Hook_kirameki_school_life_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A2];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "kirameki_school_life_download_frame");
	}
	return 0;
}

static int Hook_orenoimouto_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A4];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "orenoimouto_download_frame");
	}
	return 0;
}

static int Hook_sakurasou_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_V0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "sakurasou_download_frame");
	}
	return 0;
}

static int Hook_suikoden1_and_2_download_frame_1() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_S4];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "suikoden1_and_2_download_frame_1");
	}
	return 0;
}

static int Hook_suikoden1_and_2_download_frame_2() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_S2];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "suikoden1_and_2_download_frame_2");
	}
	return 0;
}

static int Hook_rezel_cross_download_frame() {
	const u32 fb_address = Memory::Read_U32(currentMIPS->r[MIPS_REG_SP] + 0x1C);
	const u32 fmt = Memory::Read_U32(currentMIPS->r[MIPS_REG_SP] + 0x14);
	const u32 sz = fmt == GE_FORMAT_8888 ? 0x00088000 : 0x00044000;
	if (Memory::IsVRAMAddress(fb_address) && fmt <= 3) {
		gpu->PerformMemoryDownload(fb_address, sz);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, sz, "rezel_cross_download_frame");
	}
	return 0;
}

static int Hook_kagaku_no_ensemble_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_V0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "kagaku_no_ensemble_download_frame");
	}
	return 0;
}

static int Hook_soranokiseki_fc_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A2];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00044000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00044000, "soranokiseki_fc_download_frame");
	}
	return 0;
}

static int Hook_soranokiseki_sc_download_frame() {
	u32 fb_infoaddr;
	if (!GetMIPSStaticAddress(fb_infoaddr, 0x28, 0x2C)) {
		return 0;
	}
	const u32 fb_info = Memory::Read_U32(fb_infoaddr);
	const MIPSOpcode fb_index_load = Memory::Read_Instruction(currentMIPS->pc + 0x34, true);
	if (fb_index_load != MIPS_MAKE_LW(MIPS_GET_RT(fb_index_load), MIPS_GET_RS(fb_index_load), fb_index_load & 0xffff)) {
		return 0;
	}
	const int fb_index_offset = (s16)(fb_index_load & 0xffff);
	const u32 fb_index = (Memory::Read_U32(fb_info + fb_index_offset) + 1) & 1;
	const u32 fb_address = 0x4000000 + (0x44000 * fb_index);
	const u32 dest_address = currentMIPS->r[MIPS_REG_A1];
	if (Memory::IsRAMAddress(dest_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00044000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00044000, "soranokiseki_sc_download_frame");
	}
	return 0;
}

static int Hook_bokunonatsuyasumi4_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A3];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00044000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00044000, "bokunonatsuyasumi4_download_frame");
	}
	return 0;
}

static int Hook_danganronpa2_1_download_frame() {
	const u32 fb_base = currentMIPS->r[MIPS_REG_V0];
	const u32 fb_offset = currentMIPS->r[MIPS_REG_V1];
	const u32 fb_offset_fix = fb_offset & 0xFFFFFFFC;
	const u32 fb_address = fb_base + fb_offset_fix;
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "danganronpa2_1_download_frame");
	}
	return 0;
}

static int Hook_danganronpa2_2_download_frame() {
	const u32 fb_base = currentMIPS->r[MIPS_REG_V0];
	const u32 fb_offset = currentMIPS->r[MIPS_REG_V1];
	const u32 fb_offset_fix = fb_offset & 0xFFFFFFFC;
	const u32 fb_address = fb_base + fb_offset_fix;
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "danganronpa2_2_download_frame");
	}
	return 0;
}

static int Hook_danganronpa1_1_download_frame() {
	const u32 fb_base = currentMIPS->r[MIPS_REG_A5];
	const u32 fb_offset = currentMIPS->r[MIPS_REG_V0];
	const u32 fb_offset_fix = fb_offset & 0xFFFFFFFC;
	const u32 fb_address = fb_base + fb_offset_fix;
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "danganronpa1_1_download_frame");
	}
	return 0;
}

static int Hook_danganronpa1_2_download_frame() {
	const MIPSOpcode instruction = Memory::Read_Instruction(currentMIPS->pc + 0x8, true);
	const int reg_num = instruction >> 11 & 31;
	const u32 fb_base = currentMIPS->r[reg_num];
	const u32 fb_offset = currentMIPS->r[MIPS_REG_V0];
	const u32 fb_offset_fix = fb_offset & 0xFFFFFFFC;
	const u32 fb_address = fb_base + fb_offset_fix;
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "danganronpa1_2_download_frame");
	}
	return 0;
}

static int Hook_kankabanchoutbr_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A1];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00044000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00044000, "kankabanchoutbr_download_frame");
	}
	return 0;
}

static int Hook_orenoimouto_download_frame_2() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A4];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "orenoimouto_download_frame_2");
	}
	return 0;
}

static int Hook_rewrite_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "rewrite_download_frame");
	}
	return 0;
}

static int Hook_kudwafter_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "kudwafter_download_frame");
	}
	return 0;
}

static int Hook_kumonohatateni_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "kumonohatateni_download_frame");
}
	return 0;
}

static int Hook_otomenoheihou_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "otomenoheihou_download_frame");
}
	return 0;
}

static int Hook_grisaianokajitsu_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "grisaianokajitsu_download_frame");
	}
	return 0;
}

static int Hook_kokoroconnect_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A3];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "kokoroconnect_download_frame");
	}
	return 0;
}

static int Hook_toheart2_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A1];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00044000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00044000, "toheart2_download_frame");
}
	return 0;
}

static int Hook_toheart2_download_frame_2() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "toheart2_download_frame_2");
	}
	return 0;
}

static int Hook_flowers_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "flowers_download_frame");
	}
	return 0;
}

static int Hook_motorstorm_download_frame() {
	const u32 fb_address = Memory::Read_U32(currentMIPS->r[MIPS_REG_A1] + 0x18);
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "motorstorm_download_frame");
	}
	return 0;
}

static int Hook_utawarerumono_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "utawarerumono_download_frame");
	}
	return 0;
}

static int Hook_photokano_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A1];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "photokano_download_frame");
	}
	return 0;
}

static int Hook_photokano_download_frame_2() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A1];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "photokano_download_frame_2");
	}
	return 0;
}

static int Hook_gakuenheaven_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "gakuenheaven_download_frame");
	}
	return 0;
}

static int Hook_youkosohitsujimura_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_V0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "youkosohitsujimura_download_frame");
	}
	return 0;
}

static int Hook_zettai_hero_update_minimap_tex() {
	const MIPSOpcode storeOffset = Memory::Read_Instruction(currentMIPS->pc + 4, true);
	const uint32_t texAddr = currentMIPS->r[MIPS_REG_A0] + SignExtend16ToS32(storeOffset);
	const uint32_t texSize = 64 * 64 * 1;
	const uint32_t writeAddr = currentMIPS->r[MIPS_REG_V1] + SignExtend16ToS32(storeOffset);
	if (Memory::IsValidRange(texAddr, texSize) && writeAddr >= texAddr && writeAddr < texAddr + texSize) {
		const uint8_t currentValue = Memory::Read_U8(writeAddr);
		if (currentValue != currentMIPS->r[MIPS_REG_A3]) {
			gpu->InvalidateCache(texAddr, texSize, GPU_INVALIDATE_FORCE);
		}
	}
	return 0;
}

static int Hook_tonyhawkp8_upload_tutorial_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryUpload(fb_address, 0x00088000);
	}
	return 0;
}

static int Hook_sdgundamggenerationportable_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A3];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "sdgundamggenerationportable_download_frame");
	}
	return 0;
}

static int Hook_atvoffroadfurypro_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_S2];
	const u32 fb_size = (currentMIPS->r[MIPS_REG_S4] >> 3) * currentMIPS->r[MIPS_REG_S3];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, fb_size);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, fb_size, "atvoffroadfurypro_download_frame");
	}
	return 0;
}

static int Hook_atvoffroadfuryblazintrails_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_S5];
	const u32 fb_size = (currentMIPS->r[MIPS_REG_S3] >> 3) * currentMIPS->r[MIPS_REG_S2];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, fb_size);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, fb_size, "atvoffroadfuryblazintrails_download_frame");
	}
	return 0;
}

static int Hook_littlebustersce_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_A0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "littlebustersce_download_frame");
	}
	return 0;
}

static int Hook_shinigamitoshoujo_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_S2];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "shinigamitoshoujo_download_frame");
	}
	return 0;
}

static int Hook_atvoffroadfuryprodemo_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_S5];
	const u32 fb_size = ((currentMIPS->r[MIPS_REG_A0] + currentMIPS->r[MIPS_REG_A1]) >> 3) * currentMIPS->r[MIPS_REG_S2];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, fb_size);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, fb_size, "atvoffroadfuryprodemo_download_frame");
	}
	return 0;
}

static int Hook_unendingbloodycall_download_frame() {
	const u32 fb_address = currentMIPS->r[MIPS_REG_T3];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00088000, "unendingbloodycall_download_frame");
	}
	return 0;
}

static int Hook_omertachinmokunookitethelegacy_download_frame() {
	const u32 fb_address = Memory::Read_U32(currentMIPS->r[MIPS_REG_SP] + 4);
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryDownload(fb_address, 0x00044000);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, 0x00044000, "omertachinmokunookitethelegacy_download_frame");
	}
	return 0;
}

static int Hook_katamari_render_check() {
	const u32 fb_address = Memory::Read_U32(currentMIPS->r[MIPS_REG_A0] + 0x3C);
	const u32 fbInfoPtr = Memory::Read_U32(currentMIPS->r[MIPS_REG_A0] + 0x40);
	if (Memory::IsVRAMAddress(fb_address) && fbInfoPtr != 0) {
		const u32 sizeInfoPtr = Memory::Read_U32(fbInfoPtr + 0x0C);
		// These are the values it uses to control the loop.
		// Width in memory appears to be stride / 8.
		const u32 width = Memory::Read_U16(sizeInfoPtr + 0x08) * 8;
		// Height in memory is also divided by 8 (but this one isn't hardcoded.)
		const u32 heightBlocks = Memory::Read_U16(sizeInfoPtr + 0x0A);
		// For some reason this is the number of heightBlocks less 1.
		const u32 heightBlockCount = Memory::Read_U8(fbInfoPtr + 0x08) + 1;

		const u32 totalBytes = width * heightBlocks * heightBlockCount;
		gpu->PerformMemoryDownload(fb_address, totalBytes);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, totalBytes, "katamari_render_check");
	}
	return 0;
}

static int Hook_katamari_screenshot_to_565() {
	u32 fb_address;
	if (GetMIPSStaticAddress(fb_address, 0x0040, 0x0044)) {
		gpu->PerformMemoryDownload(0x04000000 | fb_address, 0x00088000);
		NotifyMemInfo(MemBlockFlags::WRITE, 0x04000000 | fb_address, 0x00088000, "katamari_screenshot_to_565");
	}
	return 0;
}

static int Hook_mytranwars_upload_frame() {
	u32 fb_address = currentMIPS->r[MIPS_REG_S0];
	if (Memory::IsVRAMAddress(fb_address)) {
		gpu->PerformMemoryUpload(fb_address, 0x00088000);
	}
	return 0;
}

static u32 marvelalliance1_copy_src = 0;
static u32 marvelalliance1_copy_dst = 0;
static u32 marvelalliance1_copy_size = 0;

static int Hook_marvelalliance1_copy_a1_before() {
	marvelalliance1_copy_src = currentMIPS->r[MIPS_REG_A1];
	marvelalliance1_copy_dst = currentMIPS->r[MIPS_REG_V1];
	marvelalliance1_copy_size = currentMIPS->r[MIPS_REG_V0] - currentMIPS->r[MIPS_REG_V1];

	gpu->PerformMemoryDownload(marvelalliance1_copy_src, marvelalliance1_copy_size);
	NotifyMemInfo(MemBlockFlags::WRITE, marvelalliance1_copy_src, marvelalliance1_copy_size, "marvelalliance1_copy_a1_before");

	return 0;
}

static int Hook_marvelalliance1_copy_a2_before() {
	marvelalliance1_copy_src = currentMIPS->r[MIPS_REG_A2];
	marvelalliance1_copy_dst = currentMIPS->r[MIPS_REG_V0];
	marvelalliance1_copy_size = currentMIPS->r[MIPS_REG_A1] - currentMIPS->r[MIPS_REG_A2];

	gpu->PerformMemoryDownload(marvelalliance1_copy_src, marvelalliance1_copy_size);
	NotifyMemInfo(MemBlockFlags::WRITE, marvelalliance1_copy_src, marvelalliance1_copy_size, "marvelalliance1_copy_a2_before");

	return 0;
}

static int Hook_marvelalliance1_copy_after() {
	gpu->PerformMemoryUpload(marvelalliance1_copy_dst, marvelalliance1_copy_size);
	NotifyMemInfo(MemBlockFlags::READ, marvelalliance1_copy_dst, marvelalliance1_copy_size, "marvelalliance1_copy_after");

	return 0;
}

static int Hook_starocean_clear_framebuf_before() {
	skipGPUReplacements |= (int)GPUReplacementSkip::MEMSET;
	return 0;
}

static int Hook_starocean_clear_framebuf_after() {
	skipGPUReplacements &= ~(int)GPUReplacementSkip::MEMSET;

	// This hook runs after the copy, this is the final memcpy destination.
	u32 framebuf = currentMIPS->r[MIPS_REG_V0] - 512 * 4 * 271;
	u32 y_address, h_address;

	if (GetMIPSGPAddress(y_address, -204) && GetMIPSGPAddress(h_address, -200)) {
		int y = (s16)Memory::Read_U16(y_address);
		int h = (s16)Memory::Read_U16(h_address);

		DEBUG_LOG(HLE, "starocean_clear_framebuf() - %08x y=%d-%d", framebuf, y, h);
		// TODO: This is always clearing to 0, actually, which could be faster than an upload.
		gpu->PerformMemoryUpload(framebuf + 512 * y * 4, 512 * h * 4);
	}
	return 0;
}

static int Hook_motorstorm_pixel_read() {
	u32 fb_address = Memory::Read_U32(currentMIPS->r[MIPS_REG_A0] + 0x18);
	u32 fb_height = Memory::Read_U16(currentMIPS->r[MIPS_REG_A0] + 0x26);
	u32 fb_stride = Memory::Read_U16(currentMIPS->r[MIPS_REG_A0] + 0x28);
	gpu->PerformMemoryDownload(fb_address, fb_height * fb_stride);
	NotifyMemInfo(MemBlockFlags::WRITE, fb_address, fb_height * fb_stride, "motorstorm_pixel_read");
	return 0;
}

static int Hook_worms_copy_normalize_alpha() {
	// At this point in the function (0x0CC), s1 is the framebuf and a2 is the size.
	u32 fb_address = currentMIPS->r[MIPS_REG_S1];
	u32 fb_size = currentMIPS->r[MIPS_REG_A2];
	if (Memory::IsVRAMAddress(fb_address) && Memory::IsValidRange(fb_address, fb_size)) {
		gpu->PerformMemoryDownload(fb_address, fb_size);
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address, fb_size, "worms_copy_normalize_alpha");
	}
	return 0;
}

static int Hook_openseason_data_decode() {
	static u32 firstWritePtr = 0;

	u32 curWritePtr = currentMIPS->r[MIPS_REG_A0];
	u32 endPtr = currentMIPS->r[MIPS_REG_A1];
	u32 writeBytes = currentMIPS->r[MIPS_REG_V0];
	u32 startPtr = curWritePtr - writeBytes;
	if (Memory::IsVRAMAddress(startPtr) && (firstWritePtr == 0 || startPtr < firstWritePtr)) {
		firstWritePtr = startPtr;
	}
	if (Memory::IsVRAMAddress(endPtr) && curWritePtr == endPtr) {
		gpu->PerformMemoryUpload(firstWritePtr, endPtr - firstWritePtr);
		firstWritePtr = 0;
	}
	return 0;
}

static int Hook_soltrigger_render_ucschar() {
	u32 targetInfoPtrPtr = currentMIPS->r[MIPS_REG_A2];
	u32 targetInfoPtr = Memory::IsValidRange(targetInfoPtrPtr, 4) ? Memory::ReadUnchecked_U32(targetInfoPtrPtr) : 0;
	if (Memory::IsValidRange(targetInfoPtr, 32)) {
		u32 targetPtr = Memory::Read_U32(targetInfoPtr + 8);
		u32 targetByteStride = Memory::Read_U32(targetInfoPtr + 16);

		// We don't know the height specifically.
		gpu->InvalidateCache(targetPtr, targetByteStride * 512, GPU_INVALIDATE_HINT);
	}
	return 0;
}

static int Hook_gow_fps_hack() {
	if (PSP_CoreParameter().compat.flags().GoWFramerateHack60 || PSP_CoreParameter().compat.flags().GoWFramerateHack30) {
		if (PSP_CoreParameter().compat.flags().GoWFramerateHack30) {
			__DisplayWaitForVblanks("vblank start waited", 2);
		} else {
			__DisplayWaitForVblanks("vblank start waited", 1);
		}
	}
	return 0;
}

static int Hook_gow_vortex_hack() {
	if (PSP_CoreParameter().compat.flags().GoWFramerateHack60) {
		// from my tests both ==0x3F800000 and !=0x3F800000 takes around 1:40-1:50, that seems to match correct behaviour
		if (currentMIPS->r[MIPS_REG_S1] == 0 && currentMIPS->r[MIPS_REG_A0] == 0xC0 && currentMIPS->r[MIPS_REG_T4] != 0x3F800000) {
			currentMIPS->r[MIPS_REG_S1] = 1;
		}
	}
	return 0;
}

#define JITFUNC(f) (&MIPSComp::MIPSFrontendInterface::f)

// Can either replace with C functions or functions emitted in Asm/ArmAsm.
static const ReplacementTableEntry entries[] = {
	// TODO: I think some games can be helped quite a bit by implementing the
	// double-precision soft-float routines: __adddf3, __subdf3 and so on. These
	// should of course be implemented JIT style, inline.

	/*  These two collide (same hash) and thus can't be replaced :/
	{ "asinf", &Replace_asinf, 0, REPFLAG_DISABLED },
	{ "acosf", &Replace_acosf, 0, REPFLAG_DISABLED },
	*/

	{ "sinf", &Replace_sinf, 0, REPFLAG_DISABLED },
	{ "cosf", &Replace_cosf, 0, REPFLAG_DISABLED },
	{ "tanf", &Replace_tanf, 0, REPFLAG_DISABLED },
	{ "atanf", &Replace_atanf, 0, REPFLAG_DISABLED },
	{ "sqrtf", &Replace_sqrtf, 0, REPFLAG_DISABLED },
	{ "atan2f", &Replace_atan2f, 0, REPFLAG_DISABLED },
	{ "floorf", &Replace_floorf, 0, REPFLAG_DISABLED },
	{ "ceilf", &Replace_ceilf, 0, REPFLAG_DISABLED },

	{ "memcpy", &Replace_memcpy, 0, 0 },
	{ "memcpy_jak", &Replace_memcpy_jak, 0, 0 },
	{ "memcpy16", &Replace_memcpy16, 0, 0 },
	{ "memcpy_swizzled", &Replace_memcpy_swizzled, 0, 0 },
	{ "memmove", &Replace_memmove, 0, 0 },
	{ "memset", &Replace_memset, 0, 0 },
	{ "memset_jak", &Replace_memset_jak, 0, 0 },
	{ "strlen", &Replace_strlen, 0, REPFLAG_DISABLED },
	{ "strcpy", &Replace_strcpy, 0, REPFLAG_DISABLED },
	{ "strncpy", &Replace_strncpy, 0, REPFLAG_DISABLED },
	{ "strcmp", &Replace_strcmp, 0, REPFLAG_DISABLED },
	{ "strncmp", &Replace_strncmp, 0, REPFLAG_DISABLED },
	{ "fabsf", &Replace_fabsf, JITFUNC(Replace_fabsf), REPFLAG_ALLOWINLINE | REPFLAG_DISABLED },
	{ "dl_write_matrix", &Replace_dl_write_matrix, 0, REPFLAG_DISABLED }, // &MIPSComp::Jit::Replace_dl_write_matrix, REPFLAG_DISABLED },
	{ "dl_write_matrix_2", &Replace_dl_write_matrix, 0, REPFLAG_DISABLED },
	{ "gta_dl_write_matrix", &Replace_gta_dl_write_matrix, 0, REPFLAG_DISABLED },
	// dl_write_matrix_3 doesn't take the dl as a parameter, it accesses a global instead. Need to extract the address of the global from the code when replacing...
	// Haven't investigated write_matrix_4 and 5 but I think they are similar to 1 and 2.

	// { "vmmul_q_transp", &Replace_vmmul_q_transp, 0, REPFLAG_DISABLED },

	{ "godseaterburst_blit_texture", &Hook_godseaterburst_blit_texture, 0, REPFLAG_HOOKENTER },
	{ "hexyzforce_monoclome_thread", &Hook_hexyzforce_monoclome_thread, 0, REPFLAG_HOOKENTER, 0x58 },
	{ "starocean_write_stencil", &Hook_starocean_write_stencil, 0, REPFLAG_HOOKENTER, 0x260 },
	{ "topx_create_saveicon", &Hook_topx_create_saveicon, 0, REPFLAG_HOOKENTER, 0x34 },
	{ "ff1_battle_effect", &Hook_ff1_battle_effect, 0, REPFLAG_HOOKENTER },
	// This is actually used in other games, not just Dissidia.
	{ "dissidia_recordframe_avi", &Hook_dissidia_recordframe_avi, 0, REPFLAG_HOOKENTER },
	{ "brandish_download_frame", &Hook_brandish_download_frame, 0, REPFLAG_HOOKENTER },
	{ "growlanser_create_saveicon", &Hook_growlanser_create_saveicon, 0, REPFLAG_HOOKENTER, 0x7C },
	{ "sd_gundam_g_generation_download_frame", &Hook_sd_gundam_g_generation_download_frame, 0, REPFLAG_HOOKENTER, 0x48},
	{ "narisokonai_download_frame", &Hook_narisokonai_download_frame, 0, REPFLAG_HOOKENTER, 0x14 },
	{ "kirameki_school_life_download_frame", &Hook_kirameki_school_life_download_frame, 0, REPFLAG_HOOKENTER },
	{ "orenoimouto_download_frame", &Hook_orenoimouto_download_frame, 0, REPFLAG_HOOKENTER },
	{ "sakurasou_download_frame", &Hook_sakurasou_download_frame, 0, REPFLAG_HOOKENTER, 0xF8 },
	{ "suikoden1_and_2_download_frame_1", &Hook_suikoden1_and_2_download_frame_1, 0, REPFLAG_HOOKENTER, 0x9C },
	{ "suikoden1_and_2_download_frame_2", &Hook_suikoden1_and_2_download_frame_2, 0, REPFLAG_HOOKENTER, 0x48 },
	{ "rezel_cross_download_frame", &Hook_rezel_cross_download_frame, 0, REPFLAG_HOOKENTER, 0x54 },
	{ "kagaku_no_ensemble_download_frame", &Hook_kagaku_no_ensemble_download_frame, 0, REPFLAG_HOOKENTER, 0x38 },
	{ "soranokiseki_fc_download_frame", &Hook_soranokiseki_fc_download_frame, 0, REPFLAG_HOOKENTER, 0x180 },
	{ "soranokiseki_sc_download_frame", &Hook_soranokiseki_sc_download_frame, 0, REPFLAG_HOOKENTER, },
	{ "bokunonatsuyasumi4_download_frame", &Hook_bokunonatsuyasumi4_download_frame, 0, REPFLAG_HOOKENTER, 0x8C },
	{ "danganronpa2_1_download_frame", &Hook_danganronpa2_1_download_frame, 0, REPFLAG_HOOKENTER, 0x68 },
	{ "danganronpa2_2_download_frame", &Hook_danganronpa2_2_download_frame, 0, REPFLAG_HOOKENTER, 0x94 },
	{ "danganronpa1_1_download_frame", &Hook_danganronpa1_1_download_frame, 0, REPFLAG_HOOKENTER, 0x78 },
	{ "danganronpa1_2_download_frame", &Hook_danganronpa1_2_download_frame, 0, REPFLAG_HOOKENTER, 0xA8 },
	{ "kankabanchoutbr_download_frame", &Hook_kankabanchoutbr_download_frame, 0, REPFLAG_HOOKENTER, },
	{ "orenoimouto_download_frame_2", &Hook_orenoimouto_download_frame_2, 0, REPFLAG_HOOKENTER, },
	{ "rewrite_download_frame", &Hook_rewrite_download_frame, 0, REPFLAG_HOOKENTER, 0x5C },
	{ "kudwafter_download_frame", &Hook_kudwafter_download_frame, 0, REPFLAG_HOOKENTER, 0x58 },
	{ "kumonohatateni_download_frame", &Hook_kumonohatateni_download_frame, 0, REPFLAG_HOOKENTER, },
	{ "otomenoheihou_download_frame", &Hook_otomenoheihou_download_frame, 0, REPFLAG_HOOKENTER, 0x14 },
	{ "grisaianokajitsu_download_frame", &Hook_grisaianokajitsu_download_frame, 0, REPFLAG_HOOKENTER, 0x14 },
	{ "kokoroconnect_download_frame", &Hook_kokoroconnect_download_frame, 0, REPFLAG_HOOKENTER, 0x60 },
	{ "toheart2_download_frame", &Hook_toheart2_download_frame, 0, REPFLAG_HOOKENTER, },
	{ "toheart2_download_frame_2", &Hook_toheart2_download_frame_2, 0, REPFLAG_HOOKENTER, 0x18 },
	{ "flowers_download_frame", &Hook_flowers_download_frame, 0, REPFLAG_HOOKENTER, 0x44 },
	{ "motorstorm_download_frame", &Hook_motorstorm_download_frame, 0, REPFLAG_HOOKENTER, },
	{ "utawarerumono_download_frame", &Hook_utawarerumono_download_frame, 0, REPFLAG_HOOKENTER, },
	{ "photokano_download_frame", &Hook_photokano_download_frame, 0, REPFLAG_HOOKENTER, 0x2C },
	{ "photokano_download_frame_2", &Hook_photokano_download_frame_2, 0, REPFLAG_HOOKENTER, },
	{ "gakuenheaven_download_frame", &Hook_gakuenheaven_download_frame, 0, REPFLAG_HOOKENTER, },
	{ "youkosohitsujimura_download_frame", &Hook_youkosohitsujimura_download_frame, 0, REPFLAG_HOOKENTER, 0x94 },
	{ "zettai_hero_update_minimap_tex", &Hook_zettai_hero_update_minimap_tex, 0, REPFLAG_HOOKEXIT, },
	{ "tonyhawkp8_upload_tutorial_frame", &Hook_tonyhawkp8_upload_tutorial_frame, 0, REPFLAG_HOOKENTER, },
	{ "sdgundamggenerationportable_download_frame", &Hook_sdgundamggenerationportable_download_frame, 0, REPFLAG_HOOKENTER, 0x34 },
	{ "atvoffroadfurypro_download_frame", &Hook_atvoffroadfurypro_download_frame, 0, REPFLAG_HOOKENTER, 0xA0 },
	{ "atvoffroadfuryblazintrails_download_frame", &Hook_atvoffroadfuryblazintrails_download_frame, 0, REPFLAG_HOOKENTER, 0x80 },
	{ "littlebustersce_download_frame", &Hook_littlebustersce_download_frame, 0, REPFLAG_HOOKENTER, },
	{ "shinigamitoshoujo_download_frame", &Hook_shinigamitoshoujo_download_frame, 0, REPFLAG_HOOKENTER, 0xBC },
	{ "atvoffroadfuryprodemo_download_frame", &Hook_atvoffroadfuryprodemo_download_frame, 0, REPFLAG_HOOKENTER, 0x80 },
	{ "unendingbloodycall_download_frame", &Hook_unendingbloodycall_download_frame, 0, REPFLAG_HOOKENTER, 0x54 },
	{ "omertachinmokunookitethelegacy_download_frame", &Hook_omertachinmokunookitethelegacy_download_frame, 0, REPFLAG_HOOKENTER, 0x88 },
	{ "katamari_render_check", &Hook_katamari_render_check, 0, REPFLAG_HOOKENTER, 0, },
	{ "katamari_screenshot_to_565", &Hook_katamari_screenshot_to_565, 0, REPFLAG_HOOKENTER, 0 },
	{ "mytranwars_upload_frame", &Hook_mytranwars_upload_frame, 0, REPFLAG_HOOKENTER, 0x128 },
	{ "marvelalliance1_copy", &Hook_marvelalliance1_copy_a1_before, 0, REPFLAG_HOOKENTER, 0x284 },
	{ "marvelalliance1_copy", &Hook_marvelalliance1_copy_after, 0, REPFLAG_HOOKENTER, 0x2bc },
	{ "marvelalliance1_copy", &Hook_marvelalliance1_copy_a1_before, 0, REPFLAG_HOOKENTER, 0x2e8 },
	{ "marvelalliance1_copy", &Hook_marvelalliance1_copy_after, 0, REPFLAG_HOOKENTER, 0x320 },
	{ "marvelalliance1_copy", &Hook_marvelalliance1_copy_a2_before, 0, REPFLAG_HOOKENTER, 0x3b0 },
	{ "marvelalliance1_copy", &Hook_marvelalliance1_copy_after, 0, REPFLAG_HOOKENTER, 0x3e8 },
	{ "marvelalliance1_copy", &Hook_marvelalliance1_copy_a2_before, 0, REPFLAG_HOOKENTER, 0x410 },
	{ "marvelalliance1_copy", &Hook_marvelalliance1_copy_after, 0, REPFLAG_HOOKENTER, 0x448 },
	{ "marvelalliance1_copy", &Hook_marvelalliance1_copy_a1_before, 0, REPFLAG_HOOKENTER, 0x600 },
	{ "marvelalliance1_copy", &Hook_marvelalliance1_copy_after, 0, REPFLAG_HOOKENTER, 0x638 },
	{ "marvelalliance1_copy", &Hook_marvelalliance1_copy_a1_before, 0, REPFLAG_HOOKENTER, 0x664 },
	{ "marvelalliance1_copy", &Hook_marvelalliance1_copy_after, 0, REPFLAG_HOOKENTER, 0x69c },
	{ "starocean_clear_framebuf", &Hook_starocean_clear_framebuf_before, 0, REPFLAG_HOOKENTER, 0 },
	{ "starocean_clear_framebuf", &Hook_starocean_clear_framebuf_after, 0, REPFLAG_HOOKEXIT, 0 },
	{ "motorstorm_pixel_read", &Hook_motorstorm_pixel_read, 0, REPFLAG_HOOKENTER, 0 },
	{ "worms_copy_normalize_alpha", &Hook_worms_copy_normalize_alpha, 0, REPFLAG_HOOKENTER, 0x0CC },
	{ "openseason_data_decode", &Hook_openseason_data_decode, 0, REPFLAG_HOOKENTER, 0x2F0 },
	{ "soltrigger_render_ucschar", &Hook_soltrigger_render_ucschar, 0, REPFLAG_HOOKENTER, 0 },
	{ "gow_fps_hack", &Hook_gow_fps_hack, 0, REPFLAG_HOOKEXIT , 0 },
	{ "gow_vortex_hack", &Hook_gow_vortex_hack, 0, REPFLAG_HOOKENTER, 0x60 },
	{}
};


static std::map<u32, u32> replacedInstructions;
static std::unordered_map<std::string, std::vector<int> > replacementNameLookup;

void Replacement_Init() {
	for (int i = 0; i < (int)ARRAY_SIZE(entries); i++) {
		const auto entry = &entries[i];
		if (!entry->name || (entry->flags & REPFLAG_DISABLED) != 0)
			continue;
		replacementNameLookup[entry->name].push_back(i);
	}

	skipGPUReplacements = 0;
}

void Replacement_Shutdown() {
	replacedInstructions.clear();
	replacementNameLookup.clear();
}

int GetNumReplacementFuncs() {
	return ARRAY_SIZE(entries);
}

std::vector<int> GetReplacementFuncIndexes(u64 hash, int funcSize) {
	const char *name = MIPSAnalyst::LookupHash(hash, funcSize);
	std::vector<int> emptyResult;
	if (!name) {
		return emptyResult;
	}

	auto index = replacementNameLookup.find(name);
	if (index != replacementNameLookup.end()) {
		return index->second;
	}
	return emptyResult;
}

const ReplacementTableEntry *GetReplacementFunc(int i) {
	return &entries[i];
}

static bool WriteReplaceInstruction(u32 address, int index) {
	u32 prevInstr = Memory::Read_Instruction(address, false).encoding;
	if (MIPS_IS_REPLACEMENT(prevInstr)) {
		int prevIndex = prevInstr & MIPS_EMUHACK_VALUE_MASK;
		if (prevIndex == index) {
			return false;
		}
		WARN_LOG(HLE, "Replacement func changed at %08x (%d -> %d)", address, prevIndex, index);
		// Make sure we don't save the old replacement.
		prevInstr = replacedInstructions[address];
	}

	if (MIPS_IS_RUNBLOCK(Memory::Read_U32(address))) {
		WARN_LOG(HLE, "Replacing jitted func address %08x", address);
	}
	replacedInstructions[address] = prevInstr;
	Memory::Write_U32(MIPS_EMUHACK_CALL_REPLACEMENT | index, address);
	return true;
}

void WriteReplaceInstructions(u32 address, u64 hash, int size) {
	std::vector<int> indexes = GetReplacementFuncIndexes(hash, size);
	for (int index : indexes) {
		bool didReplace = false;
		auto entry = GetReplacementFunc(index);
		if (entry->flags & REPFLAG_HOOKEXIT) {
			// When hooking func exit, we search for jr ra, and replace those.
			for (u32 offset = 0; offset < (u32)size; offset += 4) {
				const u32 op = Memory::Read_Instruction(address + offset, false).encoding;
				if (op == MIPS_MAKE_JR_RA()) {
					if (WriteReplaceInstruction(address + offset, index)) {
						didReplace = true;
					}
				}
			}
		} else if (entry->flags & REPFLAG_HOOKENTER) {
			if (WriteReplaceInstruction(address + entry->hookOffset, index)) {
				didReplace = true;
			}
		} else {
			if (WriteReplaceInstruction(address, index)) {
				didReplace = true;
			}
		}

		if (didReplace) {
			INFO_LOG(HLE, "Replaced %s at %08x with hash %016llx", entries[index].name, address, hash);
		}
	}
}

void RestoreReplacedInstruction(u32 address) {
	const u32 curInstr = Memory::Read_U32(address);
	if (MIPS_IS_REPLACEMENT(curInstr)) {
		Memory::Write_U32(replacedInstructions[address], address);
		NOTICE_LOG(HLE, "Restored replaced func at %08x", address);
	} else {
		NOTICE_LOG(HLE, "Replaced func changed at %08x", address);
	}
	replacedInstructions.erase(address);
}

void RestoreReplacedInstructions(u32 startAddr, u32 endAddr) {
	if (endAddr == startAddr)
		return;
	// Need to be in order, or we'll hang.
	if (endAddr < startAddr)
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

std::map<u32, u32> SaveAndClearReplacements() {
	std::map<u32, u32> saved;
	for (auto it = replacedInstructions.begin(), end = replacedInstructions.end(); it != end; ++it) {
		const u32 addr = it->first;
		const u32 curInstr = Memory::Read_U32(addr);
		if (MIPS_IS_REPLACEMENT(curInstr)) {
			saved[addr] = curInstr;
			Memory::Write_U32(it->second, addr);
		}
	}
	return saved;
}

void RestoreSavedReplacements(const std::map<u32, u32> &saved) {
	for (auto it = saved.begin(), end = saved.end(); it != end; ++it) {
		const u32 addr = it->first;
		// Just put the replacements back.
		Memory::Write_U32(it->second, addr);
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

bool CanReplaceJalTo(u32 dest, const ReplacementTableEntry **entry, u32 *funcSize) {
	MIPSOpcode op(Memory::Read_Opcode_JIT(dest));
	if (!MIPS_IS_REPLACEMENT(op.encoding))
		return false;

	// Make sure we don't replace if there are any breakpoints inside.
	*funcSize = g_symbolMap->GetFunctionSize(dest);
	if (*funcSize == SymbolMap::INVALID_ADDRESS) {
		if (CBreakPoints::IsAddressBreakPoint(dest)) {
			return false;
		}
		*funcSize = (u32)sizeof(u32);
	} else {
		if (CBreakPoints::RangeContainsBreakPoint(dest, *funcSize)) {
			return false;
		}
	}

	int index = op.encoding & MIPS_EMUHACK_VALUE_MASK;
	*entry = GetReplacementFunc(index);
	if (!*entry) {
		ERROR_LOG(HLE, "ReplaceJalTo: Invalid replacement op %08x at %08x", op.encoding, dest);
		return false;
	}

	if ((*entry)->flags & (REPFLAG_HOOKENTER | REPFLAG_HOOKEXIT | REPFLAG_DISABLED)) {
		// If it's a hook, we can't replace the jal, we have to go inside the func.
		return false;
	}
	return true;
}
