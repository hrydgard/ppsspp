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

BinManager::BinManager() {
	queueRange_.x1 = 0x7FFFFFFF;
	queueRange_.y1 = 0x7FFFFFFF;
	queueRange_.x2 = 0;
	queueRange_.y2 = 0;
}

void BinManager::UpdateState() {
	stateIndex_ = (int)states_.size();
	states_.resize(states_.size() + 1);
	ComputeRasterizerState(&states_.back());

	DrawingCoords scissorTL(gstate.getScissorX1(), gstate.getScissorY1(), 0);
	DrawingCoords scissorBR(gstate.getScissorX2(), gstate.getScissorY2(), 0);
	ScreenCoords screenScissorTL = TransformUnit::DrawingToScreen(scissorTL);
	ScreenCoords screenScissorBR = TransformUnit::DrawingToScreen(scissorBR);

	scissor_.x1 = screenScissorTL.x;
	scissor_.y1 = screenScissorTL.y;
	scissor_.x2 = screenScissorBR.x + 15;
	scissor_.y2 = screenScissorBR.y + 15;
}

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

	// Was it fully outside the scissor?
	const BinCoords range = Range(v0, v1, v2);
	if (range.x2 < range.x1 || range.y2 < range.y1)
		return;

	queue_.push_back(BinItem{ BinItemType::TRIANGLE, stateIndex_, range, v0, v1, v2 });
	Expand(range);
}

void BinManager::AddClearRect(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.x2 < range.x1 || range.y2 < range.y1)
		return;

	queue_.push_back(BinItem{ BinItemType::CLEAR_RECT, stateIndex_, range, v0, v1 });
	Expand(range);
}

void BinManager::AddSprite(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.x2 < range.x1 || range.y2 < range.y1)
		return;

	queue_.push_back(BinItem{ BinItemType::SPRITE, stateIndex_, range, v0, v1 });
	Expand(range);
}

void BinManager::AddLine(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.x2 < range.x1 || range.y2 < range.y1)
		return;

	queue_.push_back(BinItem{ BinItemType::LINE, stateIndex_, range, v0, v1 });
	Expand(range);
}

void BinManager::AddPoint(const VertexData &v0) {
	const BinCoords range = Range(v0);
	if (range.x2 < range.x1 || range.y2 < range.y1)
		return;

	queue_.push_back(BinItem{ BinItemType::POINT, stateIndex_, range, v0 });
	Expand(range);
}

void BinManager::Flush() {
	for (const BinItem &item : queue_) {
		switch (item.type) {
		case BinItemType::TRIANGLE:
			Rasterizer::DrawTriangle(item.v0, item.v1, item.v2, item.range, states_[item.stateIndex]);
			break;

		case BinItemType::CLEAR_RECT:
			Rasterizer::ClearRectangle(item.v0, item.v1, item.range, states_[item.stateIndex]);
			break;

		case BinItemType::SPRITE:
			Rasterizer::DrawSprite(item.v0, item.v1, item.range, states_[item.stateIndex]);
			break;

		case BinItemType::LINE:
			Rasterizer::DrawLine(item.v0, item.v1, item.range, states_[item.stateIndex]);
			break;

		case BinItemType::POINT:
			Rasterizer::DrawPoint(item.v0, item.range, states_[item.stateIndex]);
			break;
		}
	}
	queue_.clear();
	states_.clear();
	stateIndex_ = -1;

	queueRange_.x1 = 0x7FFFFFFF;
	queueRange_.y1 = 0x7FFFFFFF;
	queueRange_.x2 = 0;
	queueRange_.y2 = 0;
}

BinCoords BinManager::Scissor(BinCoords range) {
	range.x1 = std::max(range.x1, scissor_.x1);
	range.y1 = std::max(range.y1, scissor_.y1);
	range.x2 = std::min(range.x2, scissor_.x2);
	range.y2 = std::min(range.y2, scissor_.y2);
	return range;
}

BinCoords BinManager::Range(const VertexData &v0, const VertexData &v1, const VertexData &v2) {
	BinCoords range;
	range.x1 = std::min(std::min(v0.screenpos.x, v1.screenpos.x), v2.screenpos.x) & ~0xF;
	range.y1 = std::min(std::min(v0.screenpos.y, v1.screenpos.y), v2.screenpos.y) & ~0xF;
	range.x2 = std::max(std::max(v0.screenpos.x, v1.screenpos.x), v2.screenpos.x) | 0xF;
	range.y2 = std::max(std::max(v0.screenpos.y, v1.screenpos.y), v2.screenpos.y) | 0xF;
	return Scissor(range);
}

BinCoords BinManager::Range(const VertexData &v0, const VertexData &v1) {
	BinCoords range;
	range.x1 = std::min(v0.screenpos.x, v1.screenpos.x) & ~0xF;
	range.y1 = std::min(v0.screenpos.y, v1.screenpos.y) & ~0xF;
	range.x2 = std::max(v0.screenpos.x, v1.screenpos.x) | 0xF;
	range.y2 = std::max(v0.screenpos.y, v1.screenpos.y) | 0xF;
	return Scissor(range);
}

BinCoords BinManager::Range(const VertexData &v0) {
	BinCoords range;
	range.x1 = v0.screenpos.x & ~0xF;
	range.y1 = v0.screenpos.y & ~0xF;
	range.x2 = v0.screenpos.x | 0xF;
	range.y2 = v0.screenpos.y | 0xF;
	return Scissor(range);
}

void BinManager::Expand(const BinCoords &range) {
	queueRange_.x1 = std::min(queueRange_.x1, range.x1);
	queueRange_.y1 = std::min(queueRange_.y1, range.y1);
	queueRange_.x2 = std::max(queueRange_.x2, range.x2);
	queueRange_.y2 = std::max(queueRange_.y2, range.y2);
}
