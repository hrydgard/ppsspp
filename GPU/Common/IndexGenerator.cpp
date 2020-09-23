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

#include "CPUDetect.h"
#include "Common.h"

#ifdef _M_SSE
#include <emmintrin.h>
#endif

#include "IndexGenerator.h"

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

void IndexGenerator::AddPrim(int prim, int vertexCount, bool clockwise) {
	switch (prim) {
	case GE_PRIM_POINTS: AddPoints(vertexCount); break;
	case GE_PRIM_LINES: AddLineList(vertexCount); break;
	case GE_PRIM_LINE_STRIP: AddLineStrip(vertexCount); break;
	case GE_PRIM_TRIANGLES: AddList(vertexCount, clockwise); break;
	case GE_PRIM_TRIANGLE_STRIP: AddStrip(vertexCount, clockwise); break;
	case GE_PRIM_TRIANGLE_FAN: AddFan(vertexCount, clockwise); break;
	case GE_PRIM_RECTANGLES: AddRectangles(vertexCount); break;  // Same
	}
}

void IndexGenerator::AddPoints(int numVerts) {
	u16 *outInds = inds_;
	const int startIndex = index_;
	for (int i = 0; i < numVerts; i++)
		*outInds++ = startIndex + i;
	inds_ = outInds;
	// ignore overflow verts
	index_ += numVerts;
	count_ += numVerts;
	prim_ = GE_PRIM_POINTS;
	seenPrims_ |= 1 << GE_PRIM_POINTS;
}

void IndexGenerator::AddList(int numVerts, bool clockwise) {
	u16 *outInds = inds_;
	const int startIndex = index_;
	const int v1 = clockwise ? 1 : 2;
	const int v2 = clockwise ? 2 : 1;
	for (int i = 0; i < numVerts; i += 3) {
		*outInds++ = startIndex + i;
		*outInds++ = startIndex + i + v1;
		*outInds++ = startIndex + i + v2;
	}
	inds_ = outInds;
	// ignore overflow verts
	index_ += numVerts;
	count_ += numVerts;
	prim_ = GE_PRIM_TRIANGLES;
	seenPrims_ |= 1 << GE_PRIM_TRIANGLES;
	if (!clockwise) {
		// Make sure we don't treat this as pure.
		seenPrims_ |= 1 << GE_PRIM_TRIANGLE_STRIP;
	}
}

inline __m128i mm_set_epi16_backwards(short w0, short w1, short w2, short w3, short w4, short w5, short w6, short w7) {
	return _mm_set_epi16(w7, w6, w5, w4, w3, w2, w1, w0);
}

void IndexGenerator::AddStrip(int numVerts, bool clockwise) {
	int wind = clockwise ? 1 : 2;
	int numTris = numVerts - 2;
	u16 *outInds = inds_;
	int ibase = index_;

	int remainingTris = numTris;
#ifdef _M_SSE
	// In an SSE2 register we can fit 8 16-bit integers.
	// However, we need to output a multiple of 3 indices.
	// The first such multiple is 24, which means we'll generate 24 indices per cycle,
	// which corresponds to 8 triangles. That's pretty cool.

	int numChunks = numTris / 8;
	if (numChunks) {
		__m128i ibase8 = _mm_set1_epi16(ibase);
		__m128i increment = _mm_set1_epi16(8);
		// TODO: Precompute two sets of these depending on wind, and just load directly.
		__m128i offsets0 = mm_set_epi16_backwards(0, 0 + wind, (wind ^ 3), /**/  1, 1 + (wind ^ 3), 1 + wind, /**/ 2, 2 + wind);
		__m128i offsets1 = mm_set_epi16_backwards(2 + (wind ^ 3), /**/  3, 3 + (wind ^ 3), 3 + wind, /**/ 4, 4 + wind, 4 + (wind ^ 3), /**/ 5);
		__m128i offsets2 = mm_set_epi16_backwards(5 + (wind ^ 3), 5 + wind, /**/  6, 6 + wind, 6 + (wind ^ 3), /**/ 7, 7 + (wind ^ 3), 7 + wind);
		__m128i *dst = (__m128i *)outInds;
		for (int i = 0; i < numChunks; i++) {
			_mm_storeu_si128(dst, _mm_add_epi16(ibase8, offsets0));
			_mm_storeu_si128(dst + 1, _mm_add_epi16(ibase8, offsets1));
			_mm_storeu_si128(dst + 2, _mm_add_epi16(ibase8, offsets2));
			ibase8 = _mm_add_epi16(ibase8, increment);
			dst += 3;
		}
		remainingTris -= numChunks * 8;
		outInds += numChunks * 24;
		ibase += numChunks * 8;
	}
	// wind doesn't need to be updated, an even number of triangles have been drawn.
#endif

	size_t numPairs = remainingTris / 2;
	while (numPairs > 0) {
		*outInds++ = ibase;
		*outInds++ = ibase + wind;
		*outInds++ = ibase + (wind ^ 3);
		*outInds++ = ibase + 1;
		*outInds++ = ibase + 1 + (wind ^ 3);
		*outInds++ = ibase + 1 + wind;
		// *outInds++ = ibase + 2;
		// *outInds++ = ibase + 2 + wind;
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
	index_ += numVerts;
	if (numTris > 0)
		count_ += numTris * 3;
	// This is so we can detect one single strip by just looking at seenPrims_.
	if (!seenPrims_ && clockwise) {
		seenPrims_ = 1 << GE_PRIM_TRIANGLE_STRIP;
		prim_ = GE_PRIM_TRIANGLE_STRIP;
		pureCount_ = numVerts;
	} else {
		seenPrims_ |= (1 << GE_PRIM_TRIANGLE_STRIP) | (1 << GE_PRIM_TRIANGLES);
		prim_ = GE_PRIM_TRIANGLES;
		pureCount_ = 0;
	}
}

void IndexGenerator::AddFan(int numVerts, bool clockwise) {
	const int numTris = numVerts - 2;
	u16 *outInds = inds_;
	const int startIndex = index_;
	const int v1 = clockwise ? 1 : 2;
	const int v2 = clockwise ? 2 : 1;
	for (int i = 0; i < numTris; i++) {
		*outInds++ = startIndex;
		*outInds++ = startIndex + i + v1;
		*outInds++ = startIndex + i + v2;
	}
	inds_ = outInds;
	index_ += numVerts;
	count_ += numTris * 3;
	prim_ = GE_PRIM_TRIANGLES;
	seenPrims_ |= 1 << GE_PRIM_TRIANGLE_FAN;
	if (!clockwise) {
		// Make sure we don't treat this as pure.
		seenPrims_ |= 1 << GE_PRIM_TRIANGLE_STRIP;
	}
}

//Lines
void IndexGenerator::AddLineList(int numVerts) {
	u16 *outInds = inds_;
	const int startIndex = index_;
	for (int i = 0; i < numVerts; i += 2) {
		*outInds++ = startIndex + i;
		*outInds++ = startIndex + i + 1;
	}
	inds_ = outInds;
	index_ += numVerts;
	count_ += numVerts;
	prim_ = GE_PRIM_LINES;
	seenPrims_ |= 1 << prim_;
}

void IndexGenerator::AddLineStrip(int numVerts) {
	const int numLines = numVerts - 1;
	u16 *outInds = inds_;
	const int startIndex = index_;
	for (int i = 0; i < numLines; i++) {
		*outInds++ = startIndex + i;
		*outInds++ = startIndex + i + 1;
	}
	inds_ = outInds;
	index_ += numVerts;
	count_ += numLines * 2;
	prim_ = GE_PRIM_LINES;
	seenPrims_ |= 1 << GE_PRIM_LINE_STRIP;
}

void IndexGenerator::AddRectangles(int numVerts) {
	u16 *outInds = inds_;
	const int startIndex = index_;
	//rectangles always need 2 vertices, disregard the last one if there's an odd number
	numVerts = numVerts & ~1;
	for (int i = 0; i < numVerts; i += 2) {
		*outInds++ = startIndex + i;
		*outInds++ = startIndex + i + 1;
	}
	inds_ = outInds;
	index_ += numVerts;
	count_ += numVerts;
	prim_ = GE_PRIM_RECTANGLES;
	seenPrims_ |= 1 << GE_PRIM_RECTANGLES;
}

template <class ITypeLE, int flag>
void IndexGenerator::TranslatePoints(int numInds, const ITypeLE *inds, int indexOffset) {
	indexOffset = index_ - indexOffset;
	u16 *outInds = inds_;
	for (int i = 0; i < numInds; i++)
		*outInds++ = indexOffset + inds[i];
	inds_ = outInds;
	count_ += numInds;
	prim_ = GE_PRIM_POINTS;
	seenPrims_ |= (1 << GE_PRIM_POINTS) | flag;
}

template <class ITypeLE, int flag>
void IndexGenerator::TranslateLineList(int numInds, const ITypeLE *inds, int indexOffset) {
	indexOffset = index_ - indexOffset;
	u16 *outInds = inds_;
	numInds = numInds & ~1;
	for (int i = 0; i < numInds; i += 2) {
		*outInds++ = indexOffset + inds[i];
		*outInds++ = indexOffset + inds[i + 1];
	}
	inds_ = outInds;
	count_ += numInds;
	prim_ = GE_PRIM_LINES;
	seenPrims_ |= (1 << GE_PRIM_LINES) | flag;
}

template <class ITypeLE, int flag>
void IndexGenerator::TranslateLineStrip(int numInds, const ITypeLE *inds, int indexOffset) {
	indexOffset = index_ - indexOffset;
	int numLines = numInds - 1;
	u16 *outInds = inds_;
	for (int i = 0; i < numLines; i++) {
		*outInds++ = indexOffset + inds[i];
		*outInds++ = indexOffset + inds[i + 1];
	}
	inds_ = outInds;
	count_ += numLines * 2;
	prim_ = GE_PRIM_LINES;
	seenPrims_ |= (1 << GE_PRIM_LINE_STRIP) | flag;
}

template <class ITypeLE, int flag>
void IndexGenerator::TranslateList(int numInds, const ITypeLE *inds, int indexOffset, bool clockwise) {
	indexOffset = index_ - indexOffset;
	// We only bother doing this minor optimization in triangle list, since it's by far the most
	// common operation that can benefit.
	if (sizeof(ITypeLE) == sizeof(inds_[0]) && indexOffset == 0 && clockwise) {
		memcpy(inds_, inds, numInds * sizeof(ITypeLE));
		inds_ += numInds;
		count_ += numInds;
	} else {
		u16 *outInds = inds_;
		int numTris = numInds / 3;  // Round to whole triangles
		numInds = numTris * 3;
		const int v1 = clockwise ? 1 : 2;
		const int v2 = clockwise ? 2 : 1;
		for (int i = 0; i < numInds; i += 3) {
			*outInds++ = indexOffset + inds[i];
			*outInds++ = indexOffset + inds[i + v1];
			*outInds++ = indexOffset + inds[i + v2];
		}
		inds_ = outInds;
		count_ += numInds;
	}
	prim_ = GE_PRIM_TRIANGLES;
	seenPrims_ |= (1 << GE_PRIM_TRIANGLES) | flag;
}

template <class ITypeLE, int flag>
void IndexGenerator::TranslateStrip(int numInds, const ITypeLE *inds, int indexOffset, bool clockwise) {
	int wind = clockwise ? 1 : 2;
	indexOffset = index_ - indexOffset;
	int numTris = numInds - 2;
	u16 *outInds = inds_;
	for (int i = 0; i < numTris; i++) {
		*outInds++ = indexOffset + inds[i];
		*outInds++ = indexOffset + inds[i + wind];
		wind ^= 3;  // Toggle between 1 and 2
		*outInds++ = indexOffset + inds[i + wind];
	}
	inds_ = outInds;
	count_ += numTris * 3;
	prim_ = GE_PRIM_TRIANGLES;
	seenPrims_ |= (1 << GE_PRIM_TRIANGLE_STRIP) | flag;
}

template <class ITypeLE, int flag>
void IndexGenerator::TranslateFan(int numInds, const ITypeLE *inds, int indexOffset, bool clockwise) {
	if (numInds <= 0) return;
	indexOffset = index_ - indexOffset;
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
	count_ += numTris * 3;
	prim_ = GE_PRIM_TRIANGLES;
	seenPrims_ |= (1 << GE_PRIM_TRIANGLE_FAN) | flag;
}

template <class ITypeLE, int flag>
inline void IndexGenerator::TranslateRectangles(int numInds, const ITypeLE *inds, int indexOffset) {
	indexOffset = index_ - indexOffset;
	u16 *outInds = inds_;
	//rectangles always need 2 vertices, disregard the last one if there's an odd number
	numInds = numInds & ~1;
	for (int i = 0; i < numInds; i += 2) {
		*outInds++ = indexOffset + inds[i];
		*outInds++ = indexOffset + inds[i+1];
	}
	inds_ = outInds;
	count_ += numInds;
	prim_ = GE_PRIM_RECTANGLES;
	seenPrims_ |= (1 << GE_PRIM_RECTANGLES) | flag;
}

// Could template this too, but would have to define in header.
void IndexGenerator::TranslatePrim(int prim, int numInds, const u8 *inds, int indexOffset, bool clockwise) {
	switch (prim) {
	case GE_PRIM_POINTS: TranslatePoints<u8, SEEN_INDEX8>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINES: TranslateLineList<u8, SEEN_INDEX8>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINE_STRIP: TranslateLineStrip<u8, SEEN_INDEX8>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLES: TranslateList<u8, SEEN_INDEX8>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_STRIP: TranslateStrip<u8, SEEN_INDEX8>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_FAN: TranslateFan<u8, SEEN_INDEX8>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_RECTANGLES: TranslateRectangles<u8, SEEN_INDEX8>(numInds, inds, indexOffset); break;  // Same
	}
}

void IndexGenerator::TranslatePrim(int prim, int numInds, const u16_le *inds, int indexOffset, bool clockwise) {
	switch (prim) {
	case GE_PRIM_POINTS: TranslatePoints<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINES: TranslateLineList<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINE_STRIP: TranslateLineStrip<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLES: TranslateList<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_STRIP: TranslateStrip<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_FAN: TranslateFan<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_RECTANGLES: TranslateRectangles<u16_le, SEEN_INDEX16>(numInds, inds, indexOffset); break;  // Same
	}
}

void IndexGenerator::TranslatePrim(int prim, int numInds, const u32_le *inds, int indexOffset, bool clockwise) {
	switch (prim) {
	case GE_PRIM_POINTS: TranslatePoints<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINES: TranslateLineList<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset); break;
	case GE_PRIM_LINE_STRIP: TranslateLineStrip<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset); break;
	case GE_PRIM_TRIANGLES: TranslateList<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_STRIP: TranslateStrip<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_TRIANGLE_FAN: TranslateFan<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset, clockwise); break;
	case GE_PRIM_RECTANGLES: TranslateRectangles<u32_le, SEEN_INDEX32>(numInds, inds, indexOffset); break;  // Same
	}
}
