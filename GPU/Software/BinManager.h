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

#pragma once

#include "GPU/Software/Rasterizer.h"

enum class BinItemType {
	TRIANGLE,
	CLEAR_RECT,
	SPRITE,
	LINE,
	POINT,
};

struct BinCoords {
	int x1;
	int y1;
	int x2;
	int y2;
};

struct BinItem {
	BinItemType type;
	int stateIndex;
	BinCoords range;
	VertexData v0;
	VertexData v1;
	VertexData v2;
};

class BinManager {
public:
	BinManager();

	void UpdateState();

	const Rasterizer::RasterizerState &State() {
		return states_.back();
	}

	void AddTriangle(const VertexData &v0, const VertexData &v1, const VertexData &v2);
	void AddClearRect(const VertexData &v0, const VertexData &v1);
	void AddSprite(const VertexData &v0, const VertexData &v1);
	void AddLine(const VertexData &v0, const VertexData &v1);
	void AddPoint(const VertexData &v0);

	void Flush();

private:
	std::vector<Rasterizer::RasterizerState> states_;
	int stateIndex_;
	BinCoords scissor_;
	std::vector<BinItem> queue_;
	BinCoords queueRange_;

	BinCoords Scissor(BinCoords range);
	BinCoords Range(const VertexData &v0, const VertexData &v1, const VertexData &v2);
	BinCoords Range(const VertexData &v0, const VertexData &v1);
	BinCoords Range(const VertexData &v0);
	void Expand(const BinCoords &range);
};
