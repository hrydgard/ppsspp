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
#include "Common/Profiler/Profiler.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/TimeUtil.h"
#include "Core/System.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Software/BinManager.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/RasterizerRectangle.h"

// Sometimes useful for debugging.
static constexpr bool FORCE_SINGLE_THREAD = false;

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

	case BinItemType::RECT:
		DrawRectangle(item.v0, item.v1, item.range, state);
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
	DrawBinItemsTask(BinWaitable *notify, BinManager::BinItemQueue &items, std::atomic<bool> &status, const BinManager::BinStateQueue &states)
		: notify_(notify), items_(items), status_(status), states_(states) {
	}

	TaskType Type() const override {
		return TaskType::CPU_COMPUTE;
	}

	TaskPriority Priority() const override {
		// Let priority emulation tasks win over this.
		return TaskPriority::NORMAL;
	}

	void Run() override {
		ProcessItems();
		status_ = false;
		// In case of any atomic issues, do another pass.
		ProcessItems();
		notify_->Drain();
	}

	void Release() override {
		// Don't delete, this is statically allocated.
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
	BinManager::BinItemQueue &items_;
	std::atomic<bool> &status_;
	const BinManager::BinStateQueue &states_;
};

constexpr int BinManager::MAX_POSSIBLE_TASKS;

BinManager::BinManager() {
	queueRange_.x1 = 0x7FFFFFFF;
	queueRange_.y1 = 0x7FFFFFFF;
	queueRange_.x2 = 0;
	queueRange_.y2 = 0;

	waitable_ = new BinWaitable();
	for (auto &s : taskStatus_)
		s = false;

	int maxInitTasks = std::min(g_threadManager.GetNumLooperThreads(), MAX_POSSIBLE_TASKS);
	for (int i = 0; i < maxInitTasks; ++i) {
		taskQueues_[i].Setup();
		for (DrawBinItemsTask *&task : taskLists_[i].tasks)
			task = new DrawBinItemsTask(waitable_, taskQueues_[i], taskStatus_[i], states_);
	}
	states_.Setup();
	cluts_.Setup();
	queue_.Setup();
}

BinManager::~BinManager() {
	delete waitable_;

	for (int i = 0; i < MAX_POSSIBLE_TASKS; ++i) {
		for (DrawBinItemsTask *task : taskLists_[i].tasks)
			delete task;
	}
}

void BinManager::UpdateState() {
	PROFILE_THIS_SCOPE("bin_state");
	if (HasDirty(SoftDirty::PIXEL_ALL | SoftDirty::SAMPLER_ALL | SoftDirty::RAST_ALL)) {
		if (states_.Full())
			Flush("states");
		creatingState_ = true;
		stateIndex_ = (uint16_t)states_.Push(RasterizerState());
		// When new funcs are compiled, we need to flush if WX exclusive.
		ComputeRasterizerState(&states_[stateIndex_], this);
		states_[stateIndex_].samplerID.cached.clut = cluts_[clutIndex_].readable;
		creatingState_ = false;

		ClearDirty(SoftDirty::PIXEL_ALL | SoftDirty::SAMPLER_ALL | SoftDirty::RAST_ALL);
	}

	if (lastFlipstats_ != gpuStats.numFlips) {
		lastFlipstats_ = gpuStats.numFlips;
		ResetStats();
	}

	const auto &state = State();
	const bool hadDepth = pendingWrites_[1].base != 0;

	if (HasDirty(SoftDirty::BINNER_RANGE)) {
		DrawingCoords scissorTL(gstate.getScissorX1(), gstate.getScissorY1());
		DrawingCoords scissorBR(std::min(gstate.getScissorX2(), gstate.getRegionX2()), std::min(gstate.getScissorY2(), gstate.getRegionY2()));
		ScreenCoords screenScissorTL = TransformUnit::DrawingToScreen(scissorTL, 0);
		ScreenCoords screenScissorBR = TransformUnit::DrawingToScreen(scissorBR, 0);

		scissor_.x1 = screenScissorTL.x;
		scissor_.y1 = screenScissorTL.y;
		scissor_.x2 = screenScissorBR.x + SCREEN_SCALE_FACTOR - 1;
		scissor_.y2 = screenScissorBR.y + SCREEN_SCALE_FACTOR - 1;

		// If we're about to texture from something still pending (i.e. depth), flush.
		if (HasTextureWrite(state))
			Flush("tex");

		// Okay, now update what's pending.
		MarkPendingWrites(state);

		ClearDirty(SoftDirty::BINNER_RANGE);
	} else if (pendingOverlap_) {
		if (HasTextureWrite(state)) {
			Flush("tex");

			// We need the pending writes set, which flushing cleared.  Set them again.
			MarkPendingWrites(state);
		}
	}

	if (HasDirty(SoftDirty::BINNER_OVERLAP)) {
		// This is a good place to record any dependencies for block transfer overlap.
		MarkPendingReads(state);

		// Disallow threads when rendering to the target, even offset.
		bool selfRender = HasTextureWrite(state);
		int newMaxTasks = selfRender || FORCE_SINGLE_THREAD ? 1 : g_threadManager.GetNumLooperThreads();
		if (newMaxTasks > MAX_POSSIBLE_TASKS)
			newMaxTasks = MAX_POSSIBLE_TASKS;
		// We don't want to overlap wrong, so flush any pending.
		if (maxTasks_ != newMaxTasks) {
			maxTasks_ = newMaxTasks;
			Flush("selfrender");
		}
		pendingOverlap_ = pendingOverlap_ || selfRender;

		// Lastly, we have to check if we're newly writing depth we were texturing before.
		// This happens in Call of Duty (depth clear after depth texture), for example.
		if (!hadDepth && state.pixelID.depthWrite) {
			for (size_t i = 0; i < states_.Size(); ++i) {
				if (HasTextureWrite(states_.Peek(i))) {
					Flush("selfdepth");
				}
			}
		}
		ClearDirty(SoftDirty::BINNER_OVERLAP);
	}
}

bool BinManager::HasTextureWrite(const RasterizerState &state) {
	if (!state.enableTextures)
		return false;

	const uint8_t textureBits = textureBitsPerPixel[state.samplerID.texfmt];
	for (int i = 0; i <= state.maxTexLevel; ++i) {
		int byteStride = (state.texbufw[i] * textureBits) / 8;
		int byteWidth = (state.samplerID.cached.sizes[i].w * textureBits) / 8;
		int h = state.samplerID.cached.sizes[i].h;
		if (HasPendingWrite(state.texaddr[i], byteStride, byteWidth, h))
			return true;
	}

	return false;
}

bool BinManager::IsExactSelfRender(const Rasterizer::RasterizerState &state, const BinItem &item) {
	if (item.type != BinItemType::SPRITE && item.type != BinItemType::RECT)
		return false;
	if (state.textureProj || state.maxTexLevel > 0)
		return false;

	// Only possible if the texture is 1:1.
	if ((state.texaddr[0] & 0x0F1FFFFF) != (gstate.getFrameBufAddress() & 0x0F1FFFFF))
		return false;
	int bufferPixelWidth = BufferFormatBytesPerPixel(state.pixelID.FBFormat());
	int texturePixelWidth = textureBitsPerPixel[state.samplerID.texfmt] / 8;
	if (bufferPixelWidth != texturePixelWidth)
		return false;

	Vec4f tc = Vec4f(item.v0.texturecoords.x, item.v0.texturecoords.y, item.v1.texturecoords.x, item.v1.texturecoords.y);
	if (state.throughMode) {
		// Already at texels, convert to screen.
		tc = tc * SCREEN_SCALE_FACTOR;
	} else {
		// Need to also multiply by width/height in transform mode.
		int w = state.samplerID.cached.sizes[0].w * SCREEN_SCALE_FACTOR;
		int h = state.samplerID.cached.sizes[0].h * SCREEN_SCALE_FACTOR;
		tc = tc * Vec4f(w, h, w, h);
	}

	Vec4<int> tci = tc.Cast<int>();
	if (tci.x != item.v0.screenpos.x || tci.y != item.v0.screenpos.y)
		return false;
	if (tci.z != item.v1.screenpos.x || tci.w != item.v1.screenpos.y)
		return false;

	return true;
}

void BinManager::MarkPendingReads(const Rasterizer::RasterizerState &state) {
	if (!state.enableTextures)
		return;

	const uint8_t textureBits = textureBitsPerPixel[state.samplerID.texfmt];
	for (int i = 0; i <= state.maxTexLevel; ++i) {
		uint32_t byteStride = (state.texbufw[i] * textureBits) / 8;
		uint32_t byteWidth = (state.samplerID.cached.sizes[i].w * textureBits) / 8;
		uint32_t h = state.samplerID.cached.sizes[i].h;
		auto it = pendingReads_.find(state.texaddr[i]);
		if (it != pendingReads_.end()) {
			uint32_t total = byteStride * (h - 1) + byteWidth;
			uint32_t existing = it->second.strideBytes * (it->second.height - 1) + it->second.widthBytes;
			if (existing < total) {
				it->second.strideBytes = std::max(it->second.strideBytes, byteStride);
				it->second.widthBytes = std::max(it->second.widthBytes, byteWidth);
				it->second.height = std::max(it->second.height, h);
			}
		} else {
			auto &range = pendingReads_[state.texaddr[i]];
			range.base = state.texaddr[i];
			range.strideBytes = byteStride;
			range.widthBytes = byteWidth;
			range.height = h;
		}
	}
}

void BinManager::MarkPendingWrites(const Rasterizer::RasterizerState &state) {
	DrawingCoords scissorTL(gstate.getScissorX1(), gstate.getScissorY1());
	DrawingCoords scissorBR(std::min(gstate.getScissorX2(), gstate.getRegionX2()), std::min(gstate.getScissorY2(), gstate.getRegionY2()));

	constexpr uint32_t mirrorMask = 0x041FFFFF;
	const uint32_t bpp = state.pixelID.FBFormat() == GE_FORMAT_8888 ? 4 : 2;
	pendingWrites_[0].Expand(gstate.getFrameBufAddress() & mirrorMask, bpp, gstate.FrameBufStride(), scissorTL, scissorBR);
	if (state.pixelID.depthWrite)
		pendingWrites_[1].Expand(gstate.getDepthBufAddress() & mirrorMask, 2, gstate.DepthBufStride(), scissorTL, scissorBR);
}

inline void BinDirtyRange::Expand(uint32_t newBase, uint32_t bpp, uint32_t stride, const DrawingCoords &tl, const DrawingCoords &br) {
	const uint32_t w = br.x - tl.x + 1;
	const uint32_t h = br.y - tl.y + 1;

	newBase += tl.y * stride * bpp + tl.x * bpp;
	if (base == 0) {
		base = newBase;
		strideBytes = stride * bpp;
		widthBytes = w * bpp;
		height = h;
		return;
	}

	height = std::max(height, h);
	if (base == newBase && strideBytes == stride * bpp) {
		widthBytes = std::max(widthBytes, w * bpp);
		return;
	}

	if (stride != 0)
		height += ((int)base - (int)newBase) / (stride * bpp);
	base = std::min(base, newBase);
	strideBytes = std::max(strideBytes, stride * bpp);
	widthBytes = strideBytes;
}

void BinManager::UpdateClut(const void *src) {
	PROFILE_THIS_SCOPE("bin_clut");
	if (cluts_.Full())
		Flush("cluts");
	BinClut &clut = cluts_.PeekPush();
	memcpy(clut.readable, src, sizeof(BinClut));
	clutIndex_ = (uint16_t)cluts_.PushPeeked();
}

void BinManager::AddTriangle(const VertexData &v0, const VertexData &v1, const VertexData &v2) {
	Vec2<int> d01((int)v0.screenpos.x - (int)v1.screenpos.x, (int)v0.screenpos.y - (int)v1.screenpos.y);
	Vec2<int> d02((int)v0.screenpos.x - (int)v2.screenpos.x, (int)v0.screenpos.y - (int)v2.screenpos.y);
	Vec2<int> d12((int)v1.screenpos.x - (int)v2.screenpos.x, (int)v1.screenpos.y - (int)v2.screenpos.y);

	// Drop primitives which are not in CCW order by checking the cross product.
	static_assert(SCREEN_SCALE_FACTOR <= 16, "Fails if scale factor is too high");
	if (d01.x * d02.y - d01.y * d02.x < 0)
		return;
	// If all points have identical coords, we'll have 0 weights and not skip properly, so skip here.
	if ((d01.x == 0 && d02.x == 0) || (d01.y == 0 && d02.y == 0))
		return;

	// Was it fully outside the scissor?
	const BinCoords range = Range(v0, v1, v2);
	if (range.Invalid())
		return;

	if (queue_.Full())
		Drain();
	queue_.Push(BinItem{ BinItemType::TRIANGLE, stateIndex_, range, v0, v1, v2 });
	CalculateRasterStateFlags(&states_[stateIndex_], v0, v1, v2);
	Expand(range);
}

void BinManager::AddClearRect(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.Invalid())
		return;

	if (queue_.Full())
		Drain();
	queue_.Push(BinItem{ BinItemType::CLEAR_RECT, stateIndex_, range, v0, v1 });
	CalculateRasterStateFlags(&states_[stateIndex_], v0, v1, true);
	Expand(range);
}

void BinManager::AddRect(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.Invalid())
		return;

	if (queue_.Full())
		Drain();
	queue_.Push(BinItem{ BinItemType::RECT, stateIndex_, range, v0, v1 });
	CalculateRasterStateFlags(&states_[stateIndex_], v0, v1, true);
	Expand(range);
}

void BinManager::AddSprite(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.Invalid())
		return;

	if (queue_.Full())
		Drain();
	queue_.Push(BinItem{ BinItemType::SPRITE, stateIndex_, range, v0, v1 });
	CalculateRasterStateFlags(&states_[stateIndex_], v0, v1, true);
	Expand(range);
}

void BinManager::AddLine(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.Invalid())
		return;

	if (queue_.Full())
		Drain();
	queue_.Push(BinItem{ BinItemType::LINE, stateIndex_, range, v0, v1 });
	CalculateRasterStateFlags(&states_[stateIndex_], v0, v1, false);
	Expand(range);
}

void BinManager::AddPoint(const VertexData &v0) {
	const BinCoords range = Range(v0);
	if (range.Invalid())
		return;

	if (queue_.Full())
		Drain();
	queue_.Push(BinItem{ BinItemType::POINT, stateIndex_, range, v0 });
	CalculateRasterStateFlags(&states_[stateIndex_], v0);
	Expand(range);
}

void BinManager::Drain(bool flushing) {
	PROFILE_THIS_SCOPE("bin_drain");

	// If the waitable has fully drained, we can update our binning decisions.
	if (!tasksSplit_ || waitable_->Empty()) {
		int w2 = (queueRange_.x2 - queueRange_.x1 + (SCREEN_SCALE_FACTOR * 2 - 1)) / (SCREEN_SCALE_FACTOR * 2);
		int h2 = (queueRange_.y2 - queueRange_.y1 + (SCREEN_SCALE_FACTOR * 2 - 1)) / (SCREEN_SCALE_FACTOR * 2);

		// Always bin the entire possible range, but focus on the drawn area.
		ScreenCoords tl(0, 0, 0);
		ScreenCoords br(1024 * SCREEN_SCALE_FACTOR, 1024 * SCREEN_SCALE_FACTOR, 0);

		if (pendingOverlap_ && maxTasks_ == 1 && flushing && queue_.Size() == 1 && !FORCE_SINGLE_THREAD) {
			// If the drawing is 1:1, we can potentially use threads.  It's worth checking.
			const auto &item = queue_.PeekNext();
			const auto &state = states_[item.stateIndex];
			if (IsExactSelfRender(state, item))
				maxTasks_ = std::min(g_threadManager.GetNumLooperThreads(), MAX_POSSIBLE_TASKS);
		}

		taskRanges_.clear();
		if (h2 >= 18 && w2 >= h2 * 4) {
			int bin_w = std::max(4, (w2 + maxTasks_ - 1) / maxTasks_) * SCREEN_SCALE_FACTOR * 2;
			taskRanges_.push_back(BinCoords{ tl.x, tl.y, queueRange_.x1 + bin_w - 1, br.y - 1 });
			for (int x = queueRange_.x1 + bin_w; x <= queueRange_.x2; x += bin_w) {
				int x2 = x + bin_w > queueRange_.x2 ? br.x : x + bin_w;
				taskRanges_.push_back(BinCoords{ x, tl.y, x2 - 1, br.y - 1 });
			}
		} else if (h2 >= 18 && w2 >= 18) {
			int bin_h = std::max(4, (h2 + maxTasks_ - 1) / maxTasks_) * SCREEN_SCALE_FACTOR * 2;
			taskRanges_.push_back(BinCoords{ tl.x, tl.y, br.x - 1, queueRange_.y1 + bin_h - 1 });
			for (int y = queueRange_.y1 + bin_h; y <= queueRange_.y2; y += bin_h) {
				int y2 = y + bin_h > queueRange_.y2 ? br.y : y + bin_h;
				taskRanges_.push_back(BinCoords{ tl.x, y, br.x - 1, y2 - 1 });
			}
		}

		tasksSplit_ = true;
	}

	// Let's try to optimize states, if we can.
	OptimizePendingStates(pendingStateIndex_, stateIndex_);
	pendingStateIndex_ = stateIndex_;

	if (taskRanges_.size() <= 1) {
		PROFILE_THIS_SCOPE("bin_drain_single");
		while (!queue_.Empty()) {
			const BinItem &item = queue_.PeekNext();
			DrawBinItem(item, states_[item.stateIndex]);
			queue_.SkipNext();
		}
	} else {
		int max = flushing ? QUEUED_PRIMS : QUEUED_PRIMS / 2;
		while (!queue_.Empty()) {
			const BinItem &item = queue_.PeekNext();
			for (int i = 0; i < (int)taskRanges_.size(); ++i) {
				const BinCoords range = taskRanges_[i].Intersect(item.range);
				if (range.Invalid())
					continue;

				if (taskQueues_[i].NearFull()) {
					// This shouldn't often happen, but if it does, wait for space.
					if (taskQueues_[i].Full())
						waitable_->Wait();
					// If we're not flushing and not near full, let's just continue later.
					// Near full means we'd drain on next prim, so better to finish it now.
					else if (!flushing && !queue_.NearFull())
						max = 0;
				}

				BinItem &taskItem = taskQueues_[i].PeekPush();
				taskItem = item;
				taskItem.range = range;
				taskQueues_[i].PushPeeked();
			}
			queue_.SkipNext();
			if (--max <= 0)
				break;
		}

		int threads = 0;
		for (int i = 0; i < (int)taskRanges_.size(); ++i) {
			if (taskQueues_[i].Empty())
				continue;
			threads++;
			if (taskStatus_[i])
				continue;

			waitable_->Fill();
			taskStatus_[i] = true;
			g_threadManager.EnqueueTaskOnThread(i, taskLists_[i].Next());
			enqueues_++;
		}

		mostThreads_ = std::max(mostThreads_, threads);
	}
}

void BinManager::Flush(const char *reason) {
	if (queueRange_.x1 == 0x7FFFFFFF)
		return;

	double st;
	if (coreCollectDebugStats)
		st = time_now_d();
	Drain(true);
	waitable_->Wait();
	taskRanges_.clear();
	tasksSplit_ = false;

	queue_.Reset();
	while (states_.Size() > 1)
		states_.SkipNext();
	while (cluts_.Size() > 1)
		cluts_.SkipNext();

	Rasterizer::FlushJit();
	Sampler::FlushJit();

	queueRange_.x1 = 0x7FFFFFFF;
	queueRange_.y1 = 0x7FFFFFFF;
	queueRange_.x2 = 0;
	queueRange_.y2 = 0;

	for (auto &pending : pendingWrites_)
		pending.base = 0;
	pendingOverlap_ = false;
	pendingReads_.clear();

	// We'll need to set the pending writes and reads again, since we just flushed it.
	dirty_ |= SoftDirty::BINNER_RANGE | SoftDirty::BINNER_OVERLAP;

	if (coreCollectDebugStats) {
		double et = time_now_d();
		flushReasonTimes_[reason] += et - st;
		if (et - st > slowestFlushTime_) {
			slowestFlushTime_ = et - st;
			slowestFlushReason_ = reason;
		}
	}
}

void BinManager::OptimizePendingStates(uint16_t first, uint16_t last) {
	// We can sometimes hit this when compiling new funcs while creating a state.
	// At that point, the state isn't loaded fully yet, so don't touch it.
	if (creatingState_ && last == stateIndex_) {
		if (first == last)
			return;
		last--;
	}

	int count = (QUEUED_STATES + last - first) % QUEUED_STATES + 1;
	for (int i = 0; i < count; ++i) {
		size_t pos = (first + i) % QUEUED_STATES;
		OptimizeRasterState(&states_[pos]);
	}
}

bool BinManager::HasPendingWrite(uint32_t start, uint32_t stride, uint32_t w, uint32_t h) {
	// We can only write to VRAM.
	if (!Memory::IsVRAMAddress(start))
		return false;
	// Ignore mirrors for overlap detection.
	start &= 0x041FFFFF;

	uint32_t size = stride * (h - 1) + w;
	for (const auto &range : pendingWrites_) {
		if (range.base == 0 || range.strideBytes == 0)
			continue;
		if (start >= range.base + range.height * range.strideBytes || start + size <= range.base)
			continue;

		// Let's simply go through each line.  Might be in the stride gap.
		uint32_t row = start;
		for (uint32_t y = 0; y < h; ++y) {
			int32_t offset = row - range.base;
			int32_t rangeY = offset / (int32_t)range.strideBytes;
			uint32_t rangeX = offset % (int32_t)range.strideBytes;
			if (rangeY >= 0 && (uint32_t)rangeY < range.height) {
				// If this row is either within width, or extends beyond stride, overlap.
				if (rangeX < range.widthBytes || rangeX + w >= range.strideBytes)
					return true;
			}

			row += stride;
		}
	}

	return false;
}

bool BinManager::HasPendingRead(uint32_t start, uint32_t stride, uint32_t w, uint32_t h) {
	if (Memory::IsVRAMAddress(start)) {
		// Ignore VRAM mirrors.
		start &= 0x041FFFFF;
	} else {
		// Ignore only regular RAM mirrors.
		start &= 0x3FFFFFFF;
	}

	uint32_t size = stride * (h - 1) + w;
	for (const auto &pair : pendingReads_) {
		const auto &range = pair.second;
		if (start >= range.base + range.height * range.strideBytes || start + size <= range.base)
			continue;

		// Stride gaps are uncommon with reads, so don't bother.
		return true;
	}

	return false;
}

void BinManager::GetStats(char *buffer, size_t bufsize) {
	double allTotal = 0.0;
	double slowestTotalTime = 0.0;
	const char *slowestTotalReason = nullptr;
	for (auto &it : flushReasonTimes_) {
		if (it.second > slowestTotalTime) {
			slowestTotalTime = it.second;
			slowestTotalReason = it.first;
		}
		allTotal += it.second;
	}

	// Many games are 30 FPS, so check last frame too for better stats.
	double recentTotal = allTotal;
	double slowestRecentTime = slowestTotalTime;
	const char *slowestRecentReason = slowestTotalReason;
	for (auto &it : lastFlushReasonTimes_) {
		if (it.second > slowestRecentTime) {
			slowestRecentTime = it.second;
			slowestRecentReason = it.first;
		}
		recentTotal += it.second;
	}

	snprintf(buffer, bufsize,
		"Slowest individual flush: %s (%0.4f)\n"
		"Slowest frame flush: %s (%0.4f)\n"
		"Slowest recent flush: %s (%0.4f)\n"
		"Total flush time: %0.4f (%05.2f%%, last 2: %05.2f%%)\n"
		"Thread enqueues: %d, count %d",
		slowestFlushReason_, slowestFlushTime_,
		slowestTotalReason, slowestTotalTime,
		slowestRecentReason, slowestRecentTime,
		allTotal, allTotal * (6000.0 / 1.001), recentTotal * (3000.0 / 1.001),
		enqueues_, mostThreads_);
}

void BinManager::ResetStats() {
	lastFlushReasonTimes_ = std::move(flushReasonTimes_);
	flushReasonTimes_.clear();
	slowestFlushReason_ = nullptr;
	slowestFlushTime_ = 0.0;
	enqueues_ = 0;
	mostThreads_ = 0;
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
	range.x1 = std::min(std::min(v0.screenpos.x, v1.screenpos.x), v2.screenpos.x) & ~(SCREEN_SCALE_FACTOR - 1);
	range.y1 = std::min(std::min(v0.screenpos.y, v1.screenpos.y), v2.screenpos.y) & ~(SCREEN_SCALE_FACTOR - 1);
	range.x2 = std::max(std::max(v0.screenpos.x, v1.screenpos.x), v2.screenpos.x) | (SCREEN_SCALE_FACTOR - 1);
	range.y2 = std::max(std::max(v0.screenpos.y, v1.screenpos.y), v2.screenpos.y) | (SCREEN_SCALE_FACTOR - 1);
	return Scissor(range);
}

BinCoords BinManager::Range(const VertexData &v0, const VertexData &v1) {
	BinCoords range;
	range.x1 = std::min(v0.screenpos.x, v1.screenpos.x) & ~(SCREEN_SCALE_FACTOR - 1);
	range.y1 = std::min(v0.screenpos.y, v1.screenpos.y) & ~(SCREEN_SCALE_FACTOR - 1);
	range.x2 = std::max(v0.screenpos.x, v1.screenpos.x) | (SCREEN_SCALE_FACTOR - 1);
	range.y2 = std::max(v0.screenpos.y, v1.screenpos.y) | (SCREEN_SCALE_FACTOR - 1);
	return Scissor(range);
}

BinCoords BinManager::Range(const VertexData &v0) {
	BinCoords range;
	range.x1 = v0.screenpos.x & ~(SCREEN_SCALE_FACTOR - 1);
	range.y1 = v0.screenpos.y & ~(SCREEN_SCALE_FACTOR - 1);
	range.x2 = v0.screenpos.x | (SCREEN_SCALE_FACTOR - 1);
	range.y2 = v0.screenpos.y | (SCREEN_SCALE_FACTOR - 1);
	return Scissor(range);
}

void BinManager::Expand(const BinCoords &range) {
	queueRange_.x1 = std::min(queueRange_.x1, range.x1);
	queueRange_.y1 = std::min(queueRange_.y1, range.y1);
	queueRange_.x2 = std::max(queueRange_.x2, range.x2);
	queueRange_.y2 = std::max(queueRange_.y2, range.y2);

	if (maxTasks_ == 1 || (queueRange_.y2 - queueRange_.y1 >= 224 * SCREEN_SCALE_FACTOR && enqueues_ < 36 * maxTasks_)) {
		if (pendingOverlap_)
			Flush("expand");
		else
			Drain();
	}
}
