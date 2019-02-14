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
#include "IndexGenerator.h"

#include "Common/Common.h"

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

void IndexGenerator::AddStrip(int numVerts, bool clockwise) {
	int wind = clockwise ? 1 : 2;
	const int numTris = numVerts - 2;
	u16 *outInds = inds_;
	int ibase = index_;
	for (int i = 0; i < numTris; i++) {
		*outInds++ = ibase;
		*outInds++ = ibase + wind;
		wind ^= 3;  // toggle between 1 and 2
		*outInds++ = ibase + wind;
		ibase++;
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
