// Copyright (c) 2022- PPSSPP Project.

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

#include "GPU/Software/BinManager.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/RasterizerRectangle.h"

void BinManager::AddTriangle(const VertexData &v0, const VertexData &v1, const VertexData &v2) {
	Vec2<int> d01((int)v0.screenpos.x - (int)v1.screenpos.x, (int)v0.screenpos.y - (int)v1.screenpos.y);
	Vec2<int> d02((int)v0.screenpos.x - (int)v2.screenpos.x, (int)v0.screenpos.y - (int)v2.screenpos.y);
	Vec2<int> d12((int)v1.screenpos.x - (int)v2.screenpos.x, (int)v1.screenpos.y - (int)v2.screenpos.y);

	// Drop primitives which are not in CCW order by checking the cross product.
	if (d01.x * d02.y - d01.y * d02.x < 0)
		return;
	// If all points have identical coords, we'll have 0 weights and not skip properly, so skip here.
	if (d01.x == 0 && d01.y == 0 && d02.x == 0 && d02.y == 0)
		return;

	// TODO
	Rasterizer::DrawTriangle(v0, v1, v2, enqueueState_);
}

void BinManager::AddClearRect(const VertexData &v0, const VertexData &v1) {
	// TODO
	Rasterizer::ClearRectangle(v0, v1, enqueueState_);
}

void BinManager::AddSprite(const VertexData &v0, const VertexData &v1) {
	// TODO
	Rasterizer::DrawSprite(v0, v1, enqueueState_);
}

void BinManager::AddLine(const VertexData &v0, const VertexData &v1) {
	// TODO
	Rasterizer::DrawLine(v0, v1, enqueueState_);
}

void BinManager::AddPoint(const VertexData &v0) {
	// TODO
	Rasterizer::DrawPoint(v0, enqueueState_);
}

void BinManager::Flush() {
	// TODO
}
