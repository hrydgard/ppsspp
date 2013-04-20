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

#include <algorithm>
#include "CommonTypes.h"
#include "../ge_constants.h"
#undef max

class IndexGenerator
{
public:
	void Setup(u16 *indexptr);
	void Reset();
	static bool PrimCompatible(int prim1, int prim2);
	bool PrimCompatible(int prim);
	int Prim() const { return prim_; }

	void AddPrim(int prim, int vertexCount);
	void TranslatePrim(int prim, int numInds, const u8 *inds, int indexOffset);
	void TranslatePrim(int prim, int numInds, const u16 *inds, int indexOffset);

	void Advance(int numVerts) {
		index_ += numVerts;
	}

	void SetIndex(int ind) { index_ = ind; }
	int MaxIndex() const { return index_; }
	int VertexCount() const { return count_; }
	bool Empty() const { return index_ == 0; }
	int SeenPrims() const { return seenPrims_; }
	int PureCount() const { return pureCount_; }
	bool SeenOnlyPurePrims() const {
		return seenPrims_ == (1 << GE_PRIM_TRIANGLES) ||
			seenPrims_ == (1 << GE_PRIM_LINES) ||
			seenPrims_ == (1 << GE_PRIM_POINTS) ||
			seenPrims_ == (1 << GE_PRIM_TRIANGLE_STRIP);
	}

private:
	// Points (why index these? code simplicity)
	void AddPoints(int numVerts);
	// Triangles
	void AddList(int numVerts);
	void AddStrip(int numVerts);
	void AddFan(int numVerts);
	// Lines
	void AddLineList(int numVerts);
	void AddLineStrip(int numVerts);
	// Rectangles
	void AddRectangles(int numVerts);

	void TranslatePoints(int numVerts, const u8 *inds, int indexOffset);	
	void TranslatePoints(int numVerts, const u16 *inds, int indexOffset);
	// Translates already indexed lists
	void TranslateLineList(int numVerts, const u8 *inds, int indexOffset);
	void TranslateLineList(int numVerts, const u16 *inds, int indexOffset);
	void TranslateLineStrip(int numVerts, const u8 *inds, int indexOffset);
	void TranslateLineStrip(int numVerts, const u16 *inds, int indexOffset);

	void TranslateRectangles(int numVerts, const u8 *inds, int indexOffset);
	void TranslateRectangles(int numVerts, const u16 *inds, int indexOffset);

	void TranslateList(int numVerts, const u8 *inds, int indexOffset);
	void TranslateList(int numVerts, const u16 *inds, int indexOffset);
	void TranslateStrip(int numVerts, const u8 *inds, int indexOffset);
	void TranslateStrip(int numVerts, const u16 *inds, int indexOffset);
	void TranslateFan(int numVerts, const u8 *inds, int indexOffset);
	void TranslateFan(int numVerts, const u16 *inds, int indexOffset);

	enum {
		SEEN_INDEX8 = 1 << 16,
		SEEN_INDEX16 = 1 << 17
	};

	u16 *indsBase_;
	u16 *inds_;
	int index_;
	int count_;
	int pureCount_;
	int prim_;
	int seenPrims_;
};

