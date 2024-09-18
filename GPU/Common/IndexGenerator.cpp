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

#include <cstring>

#include "ppsspp_config.h"

#include "Common/CPUDetect.h"
#include "Common/Common.h"
#include "Common/Log.h"

#ifdef _M_SSE
#include <emmintrin.h>
#endif
#if PPSSPP_ARCH(ARM_NEON)

#if defined(_MSC_VER) && PPSSPP_ARCH(ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif

#include "GPU/Common/IndexGenerator.h"

// Points don't need indexing...
const u8 IndexGenerator::indexedPrimitiveType[7] = {
	GE_PRIM_POINTS,
	GE_PRIM_LINES,
	GE_PRIM_LINES,
	GE_PRIM_TRIANGLES,
	GE_PRIM_TRIANGLES,
	GE_PRIM_TRIANGLES,
	GE_PRIM_RECTANGLES,
};

void IndexGenerator::Setup(u16 *inds) {
	this->indsBase_ = inds;
	Reset();
}

void IndexGenerator::AddPrim(int prim, int vertexCount, int indexOffset, bool clockwise) {
	switch (prim) {
	case GE_PRIM_POINTS: AddPoints(vertexCount, indexOffset); break;
	case GE_PRIM_LINES: AddLineList(vertexCount, indexOffset); break;
	case GE_PRIM_LINE_STRIP: AddLineStrip(vertexCount, indexOffset); break;
	case GE_PRIM_TRIANGLES: AddList(vertexCount, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_STRIP: AddStrip(vertexCount, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_FAN: AddFan(vertexCount, indexOffset, clockwise); break;
	case GE_PRIM_RECTANGLES: AddRectangles(vertexCount, indexOffset); break;  // Same
	}
}

void IndexGenerator::AddPoints(int numVerts, int indexOffset) {
	u16 *outInds = inds_;
	for (int i = 0; i < numVerts; i++)
		*outInds++ = indexOffset + i;
	inds_ = outInds;
}

void IndexGenerator::AddList(int numVerts, int indexOffset, bool clockwise) {
	u16 *outInds = inds_;
	const int v1 = clockwise ? 1 : 2;
	const int v2 = clockwise ? 2 : 1;
	for (int i = 0; i < numVerts; i += 3) {
		*outInds++ = indexOffset + i;
		*outInds++ = indexOffset + i + v1;
		*outInds++ = indexOffset + i + v2;
	}
	inds_ = outInds;
}

alignas(16) static const u16 offsets_clockwise[24] = {
	0, (u16)(0 + 1), (u16)(0 + 2),
	(u16)(1 + 1), 1, (u16)(1 + 2),
	2, (u16)(2 + 1), (u16)(2 + 2),
	(u16)(3 + 1), 3, (u16)(3 + 2),
	4, (u16)(4 + 1), (u16)(4 + 2),
	(u16)(5 + 1), 5, (u16)(5 + 2),
	6, (u16)(6 + 1), (u16)(6 + 2),
	(u16)(7 + 1), 7, (u16)(7 + 2),
};

alignas(16) static const uint16_t offsets_counter_clockwise[24] = {
	0, (u16)(0 + 2), (u16)(0 + 1),
	1, (u16)(1 + 1), (u16)(1 + 2),
	2, (u16)(2 + 2), (u16)(2 + 1),
	3, (u16)(3 + 1), (u16)(3 + 2),
	4, (u16)(4 + 2), (u16)(4 + 1),
	5, (u16)(5 + 1), (u16)(5 + 2),
	6, (u16)(6 + 2), (u16)(6 + 1),
	7, (u16)(7 + 1), (u16)(7 + 2),
};

void IndexGenerator::AddStrip(int numVerts, int indexOffset, bool clockwise) {
	int numTris = numVerts - 2;
	if (numTris <= 0) {
		return;
	}
#ifdef _M_SSE
	// In an SSE2 register we can fit 8 16-bit integers.
	// However, we need to output a multiple of 3 indices.
	// The first such multiple is 24, which means we'll generate 24 indices per cycle,
	// which corresponds to 8 triangles. That's pretty cool.

	// We allow ourselves to write some extra indices to avoid the fallback loop.
	// That's alright as we're appending to a buffer - they will get overwritten anyway.
	__m128i ibase8 = _mm_set1_epi16(indexOffset);
	const __m128i *offsets = (const __m128i *)(clockwise ? offsets_clockwise : offsets_counter_clockwise);
	__m128i *dst = (__m128i *)inds_;
	__m128i offsets0 = _mm_add_epi16(ibase8, _mm_load_si128(offsets));
	// A single store is always enough for two triangles, which is a very common case.
	_mm_storeu_si128(dst, offsets0);
	if (numTris > 2) {
		__m128i offsets1 = _mm_add_epi16(ibase8, _mm_load_si128(offsets + 1));
		_mm_storeu_si128(dst + 1, offsets1);
		if (numTris > 5) {
			__m128i offsets2 = _mm_add_epi16(ibase8, _mm_load_si128(offsets + 2));
			_mm_storeu_si128(dst + 2, offsets2);
			__m128i increment = _mm_set1_epi16(8);
			int numChunks = (numTris + 7) >> 3;
			for (int i = 1; i < numChunks; i++) {
				dst += 3;
				offsets0 = _mm_add_epi16(offsets0, increment);
				offsets1 = _mm_add_epi16(offsets1, increment);
				offsets2 = _mm_add_epi16(offsets2, increment);
				_mm_storeu_si128(dst, offsets0);
				_mm_storeu_si128(dst + 1, offsets1);
				_mm_storeu_si128(dst + 2, offsets2);
			}
		}
	}
	inds_ += numTris * 3;
	// wind doesn't need to be updated, an even number of triangles have been drawn.
#elif PPSSPP_ARCH(ARM_NEON)
	uint16x8_t ibase8 = vdupq_n_u16(indexOffset);
	const u16 *offsets = clockwise ? offsets_clockwise : offsets_counter_clockwise;
	u16 *dst = inds_;
	uint16x8_t offsets0 = vaddq_u16(ibase8, vld1q_u16(offsets));
	vst1q_u16(dst, offsets0);
	if (numTris > 2) {
		uint16x8_t offsets1 = vaddq_u16(ibase8, vld1q_u16(offsets + 8));
		vst1q_u16(dst + 8, offsets1);
		if (numTris > 5) {
			uint16x8_t offsets2 = vaddq_u16(ibase8, vld1q_u16(offsets + 16));
			vst1q_u16(dst + 16, offsets2);
			uint16x8_t increment = vdupq_n_u16(8);
			int numChunks = (numTris + 7) >> 3;
			for (int i = 1; i < numChunks; i++) {
				dst += 3 * 8;
				offsets0 = vaddq_u16(offsets0, increment);
				offsets1 = vaddq_u16(offsets1, increment);
				offsets2 = vaddq_u16(offsets2, increment);
				vst1q_u16(dst, offsets0);
				vst1q_u16(dst + 8, offsets1);
				vst1q_u16(dst + 16, offsets2);
			}
		}
	}
	inds_ += numTris * 3;
#else
	// Slow fallback loop.
	int wind = clockwise ? 1 : 2;
	int ibase = indexOffset;
	size_t numPairs = numTris / 2;
	u16 *outInds = inds_;
	while (numPairs > 0) {
		*outInds++ = ibase;
		*outInds++ = ibase + wind;
		*outInds++ = ibase + (wind ^ 3);
		*outInds++ = ibase + 1;
		*outInds++ = ibase + 1 + (wind ^ 3);
		*outInds++ = ibase + 1 + wind;
		ibase += 2;
		numPairs--;
	}
	if (numTris & 1) {
		*outInds++ = ibase;
		*outInds++ = ibase + wind;
		wind ^= 3;  // toggle between 1 and 2
		*outInds++ = ibase + wind;
	}
	inds_ = outInds;
#endif
}

void IndexGenerator::AddFan(int numVerts, int indexOffset, bool clockwise) {
	const int numTris = numVerts - 2;
	u16 *outInds = inds_;
	const int v1 = clockwise ? 1 : 2;
	const int v2 = clockwise ? 2 : 1;
	for (int i = 0; i < numTris; i++) {
		*outInds++ = indexOffset;
		*outInds++ = indexOffset + i + v1;
		*outInds++ = indexOffset + i + v2;
	}
	inds_ = outInds;
}

//Lines
void IndexGenerator::AddLineList(int numVerts, int indexOffset) {
	u16 *outInds = inds_;
	numVerts &= ~1;
	for (int i = 0; i < numVerts; i += 2) {
		*outInds++ = indexOffset + i;
		*outInds++ = indexOffset + i + 1;
	}
	inds_ = outInds;
}

void IndexGenerator::AddLineStrip(int numVerts, int indexOffset) {
	const int numLines = numVerts - 1;
	u16 *outInds = inds_;
	for (int i = 0; i < numLines; i++) {
		*outInds++ = indexOffset + i;
		*outInds++ = indexOffset + i + 1;
	}
	inds_ = outInds;
}

void IndexGenerator::AddRectangles(int numVerts, int indexOffset) {
	u16 *outInds = inds_;
	//rectangles always need 2 vertices, disregard the last one if there's an odd number
	numVerts = numVerts & ~1;
	for (int i = 0; i < numVerts; i += 2) {
		*outInds++ = indexOffset + i;
		*outInds++ = indexOffset + i + 1;
	}
	inds_ = outInds;
}

template <class ITypeLE>
void IndexGenerator::TranslatePoints(int numInds, const ITypeLE *inds, int indexOffset) {
	u16 *outInds = inds_;
	for (int i = 0; i < numInds; i++)
		*outInds++ = indexOffset + inds[i];
	inds_ = outInds;
}

template <class ITypeLE>
void IndexGenerator::TranslateLineList(int numInds, const ITypeLE *inds, int indexOffset) {
	u16 *outInds = inds_;
	numInds = numInds & ~1;
	for (int i = 0; i < numInds; i += 2) {
		*outInds++ = indexOffset + inds[i];
		*outInds++ = indexOffset + inds[i + 1];
	}
	inds_ = outInds;
}

template <class ITypeLE>
void IndexGenerator::TranslateLineStrip(int numInds, const ITypeLE *inds, int indexOffset) {
	int numLines = numInds - 1;
	u16 *outInds = inds_;
	for (int i = 0; i < numLines; i++) {
		*outInds++ = indexOffset + inds[i];
		*outInds++ = indexOffset + inds[i + 1];
	}
	inds_ = outInds;
}

template <class ITypeLE>
void IndexGenerator::TranslateList(int numInds, const ITypeLE *inds, int indexOffset, bool clockwise) {
	// We only bother doing this minor optimization in triangle list, since it's by far the most
	// common operation that can benefit.
	if (sizeof(ITypeLE) == sizeof(inds_[0]) && indexOffset == 0 && clockwise) {
		memcpy(inds_, inds, numInds * sizeof(ITypeLE));
		inds_ += numInds;
	} else {
		u16 *outInds = inds_;
		int numTris = numInds / 3;  // Round to whole triangles
		numInds = numTris * 3;
		const int v1 = clockwise ? 1 : 2;
		const int v2 = clockwise ? 2 : 1;
		// TODO: This can actually be SIMD-d, although will need complex shuffles if clockwise.
		for (int i = 0; i < numInds; i += 3) {
			*outInds++ = indexOffset + inds[i];
			*outInds++ = indexOffset + inds[i + v1];
			*outInds++ = indexOffset + inds[i + v2];
		}
		inds_ = outInds;
	}
}

template <class ITypeLE>
void IndexGenerator::TranslateStrip(int numInds, const ITypeLE *inds, int indexOffset, bool clockwise) {
	int wind = clockwise ? 1 : 2;
	int numTris = numInds - 2;
	u16 *outInds = inds_;
	for (int i = 0; i < numTris; i++) {
		*outInds++ = indexOffset + inds[i];
		*outInds++ = indexOffset + inds[i + wind];
		wind ^= 3;  // Toggle between 1 and 2
		*outInds++ = indexOffset + inds[i + wind];
	}
	inds_ = outInds;
}

template <class ITypeLE>
void IndexGenerator::TranslateFan(int numInds, const ITypeLE *inds, int indexOffset, bool clockwise) {
	if (numInds <= 0) return;
	int numTris = numInds - 2;
	u16 *outInds = inds_;
	const int v1 = clockwise ? 1 : 2;
	const int v2 = clockwise ? 2 : 1;
	for (int i = 0; i < numTris; i++) {
		*outInds++ = indexOffset + inds[0];
		*outInds++ = indexOffset + inds[i + v1];
		*outInds++ = indexOffset + inds[i + v2];
	}
	inds_ = outInds;
}

template <class ITypeLE>
inline void IndexGenerator::TranslateRectangles(int numInds, const ITypeLE *inds, int indexOffset) {
	u16 *outInds = inds_;
	//rectangles always need 2 vertices, disregard the last one if there's an odd number
	numInds = numInds & ~1;
	for (int i = 0; i < numInds; i += 2) {
		*outInds++ = indexOffset + inds[i];
		*outInds++ = indexOffset + inds[i+1];
	}
	inds_ = outInds;
}

// Could template this too, but would have to define in header.
void IndexGenerator::TranslatePrim(int prim, int numInds, const u8 *inds, int indexOffset, bool clockwise) {
	switch (prim) {
	case GE_PRIM_POINTS: TranslatePoints<u8>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINES: TranslateLineList<u8>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINE_STRIP: TranslateLineStrip<u8>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLES: TranslateList<u8>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_STRIP: TranslateStrip<u8>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_FAN: TranslateFan<u8>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_RECTANGLES: TranslateRectangles<u8>(numInds, inds, indexOffset); break;  // Same
	}
}

void IndexGenerator::TranslatePrim(int prim, int numInds, const u16_le *inds, int indexOffset, bool clockwise) {
	switch (prim) {
	case GE_PRIM_POINTS: TranslatePoints<u16_le>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINES: TranslateLineList<u16_le>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINE_STRIP: TranslateLineStrip<u16_le>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLES: TranslateList<u16_le>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_STRIP: TranslateStrip<u16_le>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_FAN: TranslateFan<u16_le>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_RECTANGLES: TranslateRectangles<u16_le>(numInds, inds, indexOffset); break;  // Same
	}
}

void IndexGenerator::TranslatePrim(int prim, int numInds, const u32_le *inds, int indexOffset, bool clockwise) {
	switch (prim) {
	case GE_PRIM_POINTS: TranslatePoints<u32_le>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINES: TranslateLineList<u32_le>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINE_STRIP: TranslateLineStrip<u32_le>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLES: TranslateList<u32_le>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_STRIP: TranslateStrip<u32_le>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_FAN: TranslateFan<u32_le>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_RECTANGLES: TranslateRectangles<u32_le>(numInds, inds, indexOffset); break;  // Same
	}
}
