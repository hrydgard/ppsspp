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

#include "CommonTypes.h"
#include "../ge_constants.h"

class IndexGenerator
{
public:
	void Setup(u16 *indexptr);
	void Reset();
	static bool PrimCompatible(int prim1, int prim2);
	bool PrimCompatible(int prim);
	int Prim() const { return prim_; }

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

	void TranslatePoints(int numVerts, const u8 *inds, int offset);	
	void TranslatePoints(int numVerts, const u16 *inds, int offset);
	// Translates already indexed lists
	void TranslateLineList(int numVerts, const u8 *inds, int offset);
	void TranslateLineList(int numVerts, const u16 *inds, int offset);
	void TranslateLineStrip(int numVerts, const u8 *inds, int offset);
	void TranslateLineStrip(int numVerts, const u16 *inds, int offset);

	void TranslateRectangles(int numVerts, const u8 *inds, int offset);
	void TranslateRectangles(int numVerts, const u16 *inds, int offset);

	void TranslateList(int numVerts, const u8 *inds, int offset);
	void TranslateStrip(int numVerts, const u8 *inds, int offset);
	void TranslateFan(int numVerts, const u8 *inds, int offset);
	void TranslateList(int numVerts, const u16 *inds, int offset);
	void TranslateStrip(int numVerts, const u16 *inds, int offset);
	void TranslateFan(int numVerts, const u16 *inds, int offset);

	int MaxIndex() { return index_; }
	int VertexCount() { return count_; }

	bool Empty() { return index_ == 0; }

	void SetIndex(int ind) { index_ = ind; }
	int SeenPrims() const { return seenPrims_; }

	bool SeenOnlyPurePrims() const {
		return seenPrims_ == (1 << GE_PRIM_TRIANGLES) ||
			     seenPrims_ == (1 << GE_PRIM_LINES) ||
					 seenPrims_ == (1 << GE_PRIM_POINTS);
	}

	bool SeenIndices() const {
		return (seenPrims_ & (SEEN_INDEX8 | SEEN_INDEX16)) != 0;
	}

private:
	enum {
		SEEN_INDEX8 = 1 << 16,
		SEEN_INDEX16 = 1 << 17
	};

	u16 *indsBase_;
	u16 *inds_;
	int index_;
	int count_;
	int prim_;
	int seenPrims_;
};

