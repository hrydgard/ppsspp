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

#include <atomic>
#include <condition_variable>
#include <mutex>
#include "Common/Thread/ThreadManager.h"
#include "GPU/Software/BinManager.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/RasterizerRectangle.h"

using namespace Rasterizer;

struct BinWaitable : public Waitable {
public:
	BinWaitable() {
		count_ = 0;
	}

	void Fill() {
		count_++;
	}

	bool Empty() {
		return count_ == 0;
	}

	void Drain() {
		int result = --count_;
		if (result == 0) {
			// We were the last one to increment.
			std::unique_lock<std::mutex> lock(mutex_);
			cond_.notify_all();
		}
	}

	void Wait() override {
		std::unique_lock<std::mutex> lock(mutex_);
		while (count_ != 0) {
			cond_.wait(lock);
		}
	}

	std::atomic<int> count_;
	std::mutex mutex_;
	std::condition_variable cond_;
};

static inline void DrawBinItem(const BinItem &item, const RasterizerState &state) {
	switch (item.type) {
	case BinItemType::TRIANGLE:
		DrawTriangle(item.v0, item.v1, item.v2, item.range, state);
		break;

	case BinItemType::CLEAR_RECT:
		ClearRectangle(item.v0, item.v1, item.range, state);
		break;

	case BinItemType::SPRITE:
		DrawSprite(item.v0, item.v1, item.range, state);
		break;

	case BinItemType::LINE:
		DrawLine(item.v0, item.v1, item.range, state);
		break;

	case BinItemType::POINT:
		DrawPoint(item.v0, item.range, state);
		break;
	}
}

class DrawBinItemsTask : public Task {
public:
	DrawBinItemsTask(BinWaitable *notify, BinQueue<BinItem, 1024> &items, std::atomic<bool> &status, const BinQueue<RasterizerState, 64> &states)
		: notify_(notify), items_(items), status_(status), states_(states) {
	}

	TaskType Type() const override {
		return TaskType::CPU_COMPUTE;
	}

	void Run() override {
		ProcessItems();
		status_ = false;
		// In case of any atomic issues, do another pass.
		ProcessItems();
		notify_->Drain();
	}

private:
	void ProcessItems() {
		while (!items_.Empty()) {
			const BinItem &item = items_.PeekNext();
			DrawBinItem(item, states_[item.stateIndex]);
			items_.SkipNext();
		}
	}

	BinWaitable *notify_;
	BinQueue<BinItem, 1024> &items_;
	std::atomic<bool> &status_;
	const BinQueue<RasterizerState, 64> &states_;
};

BinManager::BinManager() {
	queueRange_.x1 = 0x7FFFFFFF;
	queueRange_.y1 = 0x7FFFFFFF;
	queueRange_.x2 = 0;
	queueRange_.y2 = 0;

	waitable_ = new BinWaitable();
	for (auto &s : taskStatus_)
		s = false;
}

BinManager::~BinManager() {
	delete waitable_;
}

void BinManager::UpdateState() {
	if (states_.Full())
		Flush();
	stateIndex_ = (int)states_.Push(RasterizerState());
	ComputeRasterizerState(&states_[stateIndex_]);
	states_[stateIndex_].samplerID.cached.clut = cluts_[clutIndex_].readable;

	DrawingCoords scissorTL(gstate.getScissorX1(), gstate.getScissorY1());
	DrawingCoords scissorBR(gstate.getScissorX2(), gstate.getScissorY2());
	ScreenCoords screenScissorTL = TransformUnit::DrawingToScreen(scissorTL, 0);
	ScreenCoords screenScissorBR = TransformUnit::DrawingToScreen(scissorBR, 0);

	scissor_.x1 = screenScissorTL.x;
	scissor_.y1 = screenScissorTL.y;
	scissor_.x2 = screenScissorBR.x + 15;
	scissor_.y2 = screenScissorBR.y + 15;

	// Disallow threads when rendering to target.
	const uint32_t renderTarget = gstate.getFrameBufAddress() & 0x0FFFFFFF;
	bool selfRender = (gstate.getTextureAddress(0) & 0x0FFFFFFF) == renderTarget;
	if (gstate.isMipmapEnabled()) {
		for (int i = 0; i <= gstate.getTextureMaxLevel(); ++i)
			selfRender = selfRender || (gstate.getTextureAddress(i) & 0x0FFFFFFF) == renderTarget;
	}

	int newMaxTasks = selfRender ? 1 : g_threadManager.GetNumLooperThreads();
	if (newMaxTasks > MAX_POSSIBLE_TASKS)
		newMaxTasks = MAX_POSSIBLE_TASKS;
	if (maxTasks_ != newMaxTasks) {
		maxTasks_ = newMaxTasks;
		tasksSplit_ = false;
	}
}

void BinManager::UpdateClut(void *src) {
	if (cluts_.Full())
		Flush();
	clutIndex_ = (int)cluts_.Push(BinClut());
	memcpy(cluts_[clutIndex_].readable, src, sizeof(BinClut));
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
	if (range.Invalid())
		return;

	if (queue_.Full())
		Drain();
	queue_.Push(BinItem{ BinItemType::TRIANGLE, stateIndex_, range, v0, v1, v2 });
	Expand(range);
}

void BinManager::AddClearRect(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.Invalid())
		return;

	if (queue_.Full())
		Drain();
	queue_.Push(BinItem{ BinItemType::CLEAR_RECT, stateIndex_, range, v0, v1 });
	Expand(range);
}

void BinManager::AddSprite(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.Invalid())
		return;

	if (queue_.Full())
		Drain();
	queue_.Push(BinItem{ BinItemType::SPRITE, stateIndex_, range, v0, v1 });
	Expand(range);
}

void BinManager::AddLine(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.Invalid())
		return;

	if (queue_.Full())
		Drain();
	queue_.Push(BinItem{ BinItemType::LINE, stateIndex_, range, v0, v1 });
	Expand(range);
}

void BinManager::AddPoint(const VertexData &v0) {
	const BinCoords range = Range(v0);
	if (range.Invalid())
		return;

	if (queue_.Full())
		Drain();
	queue_.Push(BinItem{ BinItemType::POINT, stateIndex_, range, v0 });
	Expand(range);
}

void BinManager::Drain() {
	// If the waitable has fully drained, we can update our binning decisions.
	if (!tasksSplit_ || waitable_->Empty()) {
		int w2 = (queueRange_.x2 - queueRange_.x1 + 31) / 32;
		int h2 = (queueRange_.y2 - queueRange_.y1 + 31) / 32;

		// Always bin the entire possible range, but focus on the drawn area.
		ScreenCoords tl = TransformUnit::DrawingToScreen(DrawingCoords(0, 0), 0);
		ScreenCoords br = TransformUnit::DrawingToScreen(DrawingCoords(1024, 1024), 0);

		taskRanges_.clear();
		if (h2 >= 18 && w2 >= h2 * 4) {
			int bin_w = std::max(4, (w2 + maxTasks_ - 1) / maxTasks_) * 32;
			taskRanges_.push_back(BinCoords{ tl.x, tl.y, queueRange_.x1 + bin_w - 1, br.y - 1 });
			for (int x = queueRange_.x1 + bin_w; x <= queueRange_.x2; x += bin_w) {
				int x2 = x + bin_w > queueRange_.x2 ? br.x : x + bin_w;
				taskRanges_.push_back(BinCoords{ x, tl.y, x2 - 1, br.y - 1 });
			}
		} else if (h2 >= 18 && w2 >= 18) {
			int bin_h = std::max(4, (h2 + maxTasks_ - 1) / maxTasks_) * 32;
			taskRanges_.push_back(BinCoords{ tl.x, tl.y, br.x - 1, queueRange_.y1 + bin_h - 1 });
			for (int y = queueRange_.y1 + bin_h; y <= queueRange_.y2; y += bin_h) {
				int y2 = y + bin_h > queueRange_.y2 ? br.y : y + bin_h;
				taskRanges_.push_back(BinCoords{ tl.x, y, br.x - 1, y2 - 1 });
			}
		}

		tasksSplit_ = true;
	}

	if (taskRanges_.size() <= 1) {
		while (!queue_.Empty()) {
			const BinItem &item = queue_.PeekNext();
			DrawBinItem(item, states_[item.stateIndex]);
			queue_.SkipNext();
		}
	} else {
		while (!queue_.Empty()) {
			const BinItem &item = queue_.PeekNext();
			for (int i = 0; i < (int)taskRanges_.size(); ++i) {
				const BinCoords range = taskRanges_[i].Intersect(item.range);
				if (range.Invalid())
					continue;

				// This shouldn't often happen, but if it does, wait for space.
				if (taskQueues_[i].Full())
					waitable_->Wait();

				BinItem &taskItem = taskQueues_[i].PeekPush();
				taskItem = item;
				taskItem.range = range;
				taskQueues_[i].PushPeeked();

			}
			queue_.SkipNext();
		}

		for (int i = 0; i < (int)taskRanges_.size(); ++i) {
			if (taskQueues_[i].Empty() || taskStatus_[i])
				continue;

			waitable_->Fill();
			taskStatus_[i] = true;
			DrawBinItemsTask *task = new DrawBinItemsTask(waitable_, taskQueues_[i], taskStatus_[i], states_);
			g_threadManager.EnqueueTaskOnThread(i, task, true);
		}
	}
}

void BinManager::Flush() {
	Drain();
	waitable_->Wait();
	taskRanges_.clear();
	tasksSplit_ = false;

	queue_.Reset();
	while (states_.Size() > 1)
		states_.SkipNext();
	while (cluts_.Size() > 1)
		cluts_.SkipNext();

	queueRange_.x1 = 0x7FFFFFFF;
	queueRange_.y1 = 0x7FFFFFFF;
	queueRange_.x2 = 0;
	queueRange_.y2 = 0;
}

inline BinCoords BinCoords::Intersect(const BinCoords &range) const {
	BinCoords sub;
	sub.x1 = std::max(x1, range.x1);
	sub.y1 = std::max(y1, range.y1);
	sub.x2 = std::min(x2, range.x2);
	sub.y2 = std::min(y2, range.y2);
	return sub;
}

BinCoords BinManager::Scissor(BinCoords range) {
	return range.Intersect(scissor_);
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

	if (maxTasks_ == 1 || queueRange_.y2 - queueRange_.y1 >= 224 * 16) {
		Drain();
	}
}
