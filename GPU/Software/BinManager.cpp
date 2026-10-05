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
#include <mutex>
#include <condition_variable>
#include <thread>

#include "Common/BitSet.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Thread/ParallelLoop.h"
#include "Common/Thread/ThreadManager.h"
#include "Common/Data/Text/StringWriter.h"
#include "Common/TimeUtil.h"
#include "Core/System.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Software/BinManager.h"
#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/RasterizerRectangle.h"

// Sometimes useful for debugging.
static constexpr bool FORCE_SINGLE_THREAD = false;
// With threads, how many queued items are put in tiles at once.
static constexpr int DISTRIBUTE_BATCH = 16;
// How large a primitive drawn in order (DrawSplit) has to be for threads to share it.
static constexpr int SPLIT_MIN_ROW_PAIRS = 8;
static constexpr int SPLIT_MIN_PIXELS = 128 * 64;
// How many tile pieces of work are worth waking the threads for.
static constexpr int WAKE_ENTRIES = 48;

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

static inline void DrawBinItem(const BinItem &item, const BinCoords &range, const RasterizerState &state) {
	switch (item.type) {
	case BinItemType::TRIANGLE:
		DrawTriangle(item.v0, item.v1, item.v2, range, state);
		break;

	case BinItemType::CLEAR_RECT:
		ClearRectangle(item.v0, item.v1, range, state);
		break;

	case BinItemType::RECT:
		DrawRectangle(item.v0, item.v1, range, state);
		break;

	case BinItemType::SPRITE:
		DrawSprite(item.v0, item.v1, range, state);
		break;

	case BinItemType::LINE:
		DrawLine(item.v0, item.v1, range, state);
		break;

	case BinItemType::POINT:
		DrawPoint(item.v0, range, state);
		break;
	}
}

static inline void DrawBinItem(const BinItem &item, const RasterizerState &state) {
	DrawBinItem(item, item.range, state);
}

class DrawBinItemsTask : public Task {
public:
	DrawBinItemsTask(BinWaitable *notify, BinManager *binner, int index, std::atomic<bool> &status)
		: notify_(notify), binner_(binner), index_(index), status_(status) {
	}

	TaskType Type() const override {
		return TaskType::CPU_COMPUTE;
	}

	TaskPriority Priority() const override {
		// Let priority emulation tasks win over this.
		return TaskPriority::NORMAL;
	}

	void Run() override {
		binner_->WakeChained();
		binner_->ProcessTiles(index_);
		status_ = false;
		// Work queued after the last look, but before status_ said we were done, would otherwise wait.
		binner_->ProcessTiles(index_);
		notify_->Drain();
	}

	void Release() override {
		// Don't delete, this is statically allocated.
	}

private:
	BinWaitable *notify_;
	BinManager *binner_;
	int index_;
	std::atomic<bool> &status_;
};

constexpr int BinManager::MAX_POSSIBLE_TASKS;

BinManager::BinManager() {
	waitable_ = new BinWaitable();
	for (auto &s : taskStatus_)
		s = false;

	int maxInitTasks = std::min(g_threadManager.GetNumLooperThreads(), MAX_POSSIBLE_TASKS);
	maxTasks_ = FORCE_SINGLE_THREAD ? 1 : maxInitTasks;
	for (int i = 0; i < maxInitTasks; ++i) {
		for (DrawBinItemsTask *&task : taskLists_[i].tasks)
			task = new DrawBinItemsTask(waitable_, this, i, taskStatus_[i]);
	}
	PickTileSize(maxInitTasks);
	tiles_ = (Tile *)AllocateAlignedMemory(sizeof(Tile) * tilesX_ * tilesY_, 64);
	for (int i = 0; i < tilesX_ * tilesY_; ++i) {
		tiles_[i].head = 0;
		tiles_[i].tail = 0;
		tiles_[i].busy = false;
	}
	for (auto &r : itemRefs_)
		r = 0;
	states_.Setup();
	cluts_.Setup();
	queue_.Setup();
}

BinManager::~BinManager() {
	delete waitable_;
	FreeAlignedMemory(tiles_);

	for (int i = 0; i < MAX_POSSIBLE_TASKS; ++i) {
		for (DrawBinItemsTask *task : taskLists_[i].tasks)
			delete task;
	}
}

static int JitGeneration() {
	return Rasterizer::JitClearGeneration() + Sampler::JitClearGeneration();
}

// A new current state, from gstate, unoptimized and without primitives yet.
void BinManager::PushState() {
	if (states_.Full()) {
		Flush("states");
		// Nothing's drawing now, so the ring can move.
		if (states_.Capacity() < MAX_QUEUED_STATES) {
			const size_t oldHead = states_.head_;
			const size_t oldCapacity = states_.Capacity();
			states_.Grow(oldCapacity * 2);
			auto moved = [&](uint16_t index) {
				return (uint16_t)((index + oldCapacity - oldHead) % oldCapacity);
			};
			stateIndex_ = moved(stateIndex_);
			pendingStateIndex_ = moved(pendingStateIndex_);
			INFO_LOG(Log::G3D, "Software: state ring grown to %d", (int)states_.Capacity());
		}
	}
	creatingState_ = true;
	stateIndex_ = (uint16_t)states_.Push(RasterizerState());
	// When new funcs are compiled, we need to flush if WX exclusive. Compiling can also clear the caches,
	// losing the funcs picked before it, so then compute it again.
	for (int tries = 0; tries < 3; ++tries) {
		jitGen_ = JitGeneration();
		ComputeRasterizerState(&states_[stateIndex_], this);
		if (jitGen_ == JitGeneration()) {
			break;
		}
	}
	states_[stateIndex_].samplerID.cached.clut = cluts_[clutIndex_].readable;
	creatingState_ = false;
}

void BinManager::UpdateState() {
	PROFILE_THIS_SCOPE("bin_state");
	auto jitGen = JitGeneration;
	// A JIT clear frees the code the current state's function pointers point into.
	if (jitGen_ != jitGen()) {
		dirty_ |= SoftDirty::PIXEL_ALL | SoftDirty::SAMPLER_ALL | SoftDirty::RAST_ALL;
	}
	if (HasDirty(SoftDirty::PIXEL_ALL | SoftDirty::SAMPLER_ALL | SoftDirty::RAST_ALL)) {
		PushState();
		ClearDirty(SoftDirty::PIXEL_ALL | SoftDirty::SAMPLER_ALL | SoftDirty::RAST_ALL);
	}

	if (lastFlipstats_ != gpuStats.totals.numFlips) {
		lastFlipstats_ = gpuStats.totals.numFlips;
		ResetStats();
	}

	const auto &state = State();
	const bool hadDepth = pendingWrites_[1].base != 0;

	if (HasDirty(SoftDirty::BINNER_RANGE)) {
		drawTargetAddr_ = gstate.getFrameBufAddress();
		DrawingCoords scissorTL(gstate.getScissorX1(), gstate.getScissorY1());
		DrawingCoords scissorBR(std::min(gstate.getScissorX2(), gstate.getRegionX2()), std::min(gstate.getScissorY2(), gstate.getRegionY2()));
		ScreenCoords screenScissorTL = TransformUnit::DrawingToScreen(scissorTL, 0);
		ScreenCoords screenScissorBR = TransformUnit::DrawingToScreen(scissorBR, 0);

		scissor_.x1 = screenScissorTL.x;
		scissor_.y1 = screenScissorTL.y;
		scissor_.x2 = screenScissorBR.x + SCREEN_SCALE_FACTOR - 1;
		scissor_.y2 = screenScissorBR.y + SCREEN_SCALE_FACTOR - 1;

		// Pixels past the stride land in the next row, which another task may be drawing (Tokimeki
		// Memorial's 128 wide blur buffer under a full screen scissor).
		const bool pastStride = scissorBR.x >= gstate.FrameBufStride();
		if (pastStride != pastStride_) {
			pastStride_ = pastStride;
			dirty_ |= SoftDirty::BINNER_OVERLAP;
		}

		// Okay, now update what's pending. Texturing from it is ordered per primitive (NeedsOrder), so
		// whether the texture overlaps has to be decided again.
		MarkPendingWrites(state);
		dirty_ |= SoftDirty::BINNER_OVERLAP;

		ClearDirty(SoftDirty::BINNER_RANGE);
	}

	if (HasDirty(SoftDirty::BINNER_OVERLAP)) {
		// This is a good place to record any dependencies for block transfer overlap.
		MarkPendingReads(state);

		// Drawing that textures from its target, or with a scissor past the stride, stays threaded: a
		// primitive that reads what's being drawn or reaches past the stride is drawn alone, in order (ItemQueued).
		bool selfRender = HasTextureWrite(state);

		// Lastly, we have to check if we're newly writing depth we were texturing before.
		// This happens in Call of Duty (depth clear after depth texture), for example.
		bool flushed = false;
		if (!hadDepth && state.pixelID.depthWrite) {
			for (size_t i = 0; i < states_.Size(); ++i) {
				if (HasTextureWrite(states_.Peek(i))) {
					Flush("selfdepth");
					flushed = true;
					break;
				}
			}
		}

		if (flushed) {
			// The flush forgot what this draw writes and reads, so record it again.
			MarkPendingWrites(state);
			MarkPendingReads(state);
			ClearDirty(SoftDirty::BINNER_RANGE);
		}
		selfRender_ = selfRender;
		ClearDirty(SoftDirty::BINNER_OVERLAP);
	}
	states_[stateIndex_].selfTexture = selfRender_;
	states_[stateIndex_].texFlushGen = texFlushGen_;
}

// The GE samples through its texture cache, so a primitive that textures from the buffer it draws to
// mostly sees that buffer as it was before the primitive (exp81, exp159, exp160). The cache actually
// fills 8-row blocks as the primitive first reads them, so rows drawn before then show through; this
// doesn't model that. A texture that fits in the 8 KB cache stays there until TEXFLUSH, so later
// primitives and draws see it as it was when first read (FF Type-0's 16x16 4444 blur, three passes over
// one buffer without a flush).
const RasterizerState &BinManager::SelfTextureSnapshot(const BinItem &item, const RasterizerState &state) {
	constexpr uint32_t mirrorMask = 0x041FFFFF;
	const uint32_t bits = textureBitsPerPixel[state.samplerID.texfmt];
	const uint32_t fbBpp = state.pixelID.FBFormat() == GE_FORMAT_8888 ? 4 : 2;
	const uint32_t fbStrideBytes = state.pixelID.cached.framebufStride * fbBpp;

	selfTexState_ = state;
	uint32_t totalBytes = 0;
	bool sameTexture = true;
	for (int i = 0; i <= state.maxTexLevel; ++i) {
		const uint32_t bytes = state.samplerID.cached.sizes[i].w * bits / 8 * state.samplerID.cached.sizes[i].h;
		totalBytes += bytes;
		sameTexture = sameTexture && selfTexAddr_[i] == state.texaddr[i] && selfTexBuf_[i].size() == state.texbufw[i] * bits / 8 * state.samplerID.cached.sizes[i].h;
	}
	const bool cacheSized = totalBytes <= 8192;
	if (cacheSized && selfTexCached_ && sameTexture && selfTexFlushGen_ == state.texFlushGen) {
		for (int i = 0; i <= state.maxTexLevel; ++i) {
			if (!selfTexBuf_[i].empty())
				selfTexState_.texptr[i] = selfTexBuf_[i].data();
		}
		return selfTexState_;
	}
	if (cacheSized && !(selfTexCached_ && sameTexture))
		selfTexValid_ = false;
	selfTexCached_ = cacheSized;
	selfTexFlushGen_ = state.texFlushGen;

	for (int i = 0; i <= state.maxTexLevel; ++i) {
		const u8 *src = state.texptr[i];
		const uint32_t bytes = state.texbufw[i] * bits / 8 * state.samplerID.cached.sizes[i].h;
		if (!src || !Memory::IsValidRange(state.texaddr[i], bytes))
			continue;
		std::vector<u8> &buf = selfTexBuf_[i];
		if (!selfTexValid_ || selfTexAddr_[i] != state.texaddr[i] || buf.size() != bytes) {
			buf.assign(src, src + bytes);
			selfTexAddr_[i] = state.texaddr[i];
		} else {
			// Only what the previous primitive drew has changed: its columns, in its rows (by address, so a
			// pixel past the stride is in the next row's bytes).
			const DrawingCoords tl = TransformUnit::ScreenToDrawing(selfTexLastRange_.x1, selfTexLastRange_.y1);
			const DrawingCoords br = TransformUnit::ScreenToDrawing(selfTexLastRange_.x2, selfTexLastRange_.y2);
			const int64_t texStart = state.texaddr[i] & mirrorMask;
			const int64_t texEnd = texStart + bytes;
			const int64_t colStart = (int64_t)tl.x * fbBpp, colEnd = (int64_t)(br.x + 1) * fbBpp;
			for (int y = tl.y; y <= br.y; ++y) {
				const int64_t row = (int64_t)(drawTargetAddr_ & mirrorMask) + (int64_t)y * fbStrideBytes;
				const int64_t start = std::max(row + colStart, texStart) - texStart;
				const int64_t end = std::min(row + colEnd, texEnd) - texStart;
				if (start < end)
					memcpy(buf.data() + start, src + start, (size_t)(end - start));
			}
		}
		selfTexState_.texptr[i] = buf.data();
	}

	// A depth write could change the texture outside the color rows, so then copy all of it each time.
	selfTexValid_ = !state.pixelID.depthWrite;
	selfTexLastRange_ = item.range;
	return selfTexState_;
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
	if (state.pixelID.depthWrite) {
		pendingWrites_[1].Expand(gstate.getDepthBufAddress() & mirrorMask, 2, gstate.DepthBufStride(), scissorTL, scissorBR);
	} else if (gstate.isDepthTestEnabled() && !gstate.isModeClear()) {
		// Testing without writing still reads the depth buffer, so a transfer into it has to wait.
		const uint32_t depthAddr = gstate.getDepthBufAddress() & mirrorMask;
		pendingReads_[depthAddr].Expand(depthAddr, 2, gstate.DepthBufStride(), scissorTL, scissorBR);
	}
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

	if (base == newBase && strideBytes == stride * bpp) {
		height = std::max(height, h);
		widthBytes = std::max(widthBytes, w * bpp);
		return;
	}

	// Otherwise whole rows from the lower start to the higher end, which covers both.
	const uint64_t end = std::max((uint64_t)base + (uint64_t)(height - 1) * strideBytes + widthBytes, (uint64_t)newBase + (uint64_t)(h - 1) * stride * bpp + w * bpp);
	base = std::min(base, newBase);
	strideBytes = std::max(strideBytes, stride * bpp);
	widthBytes = strideBytes;
	if (strideBytes != 0)
		height = (uint32_t)((end - base + strideBytes - 1) / strideBytes);
}

void BinManager::UpdateClut(const void *src) {
	PROFILE_THIS_SCOPE("bin_clut");
	if (cluts_.Full())
		Flush("cluts");
	BinClut &clut = cluts_.PeekPush();
	memcpy(clut.readable, src, sizeof(BinClut));
	clutIndex_ = (uint16_t)cluts_.PushPeeked();
}

// Adds a primitive's vertex flags to the current state. The threads may already be drawing with that one, and
// changing its flags would change how it's optimized: then the primitive starts a new, unoptimized state instead.
template <typename F>
void BinManager::AddFlags(F calculate) {
	RasterizerState &state = states_[stateIndex_];
	if (state.liveGen != tileGen_) {
		calculate(&state);
		return;
	}
	// The threads don't read the flags.
	const RasterizerStateFlags old = state.flags;
	calculate(&state);
	if (state.flags == old)
		return;
	state.flags = old;
	const bool selfTexture = state.selfTexture;
	const uint32_t texFlushGen = state.texFlushGen;
	PushState();
	states_[stateIndex_].selfTexture = selfTexture;
	states_[stateIndex_].texFlushGen = texFlushGen;
	calculate(&states_[stateIndex_]);
}

void BinManager::AddTriangle(const VertexData &v0, const VertexData &v1, const VertexData &v2) {
	Vec2<int> d01((int)v0.screenpos.x - (int)v1.screenpos.x, (int)v0.screenpos.y - (int)v1.screenpos.y);
	Vec2<int> d02((int)v0.screenpos.x - (int)v2.screenpos.x, (int)v0.screenpos.y - (int)v2.screenpos.y);
	Vec2<int> d12((int)v1.screenpos.x - (int)v2.screenpos.x, (int)v1.screenpos.y - (int)v2.screenpos.y);

	// Drop primitives which are not in CCW order by checking the cross product, and ones with zero
	// area, which light no pixels even on an edge through pixel centers (gpu/probe exp118).
	static_assert(SCREEN_SCALE_FACTOR <= 16, "Fails if scale factor is too high");
	// In 64 bits: with vertices far apart in the 4096 pixel space the products overflow 32 (gpu/probe exp132).
	if ((int64_t)d01.x * d02.y - (int64_t)d01.y * d02.x <= 0)
		return;

	// Was it fully outside the scissor?
	const BinCoords range = Range(v0, v1, v2);
	if (range.Invalid())
		return;

	if (queue_.Full())
		MakeRoom();
	AddFlags([&](RasterizerState *state) { CalculateRasterStateFlags(state, v0, v1, v2); });
	queue_.Push(BinItem{ BinItemType::TRIANGLE, stateIndex_, range, v0, v1, v2 });
	ItemQueued();
}

void BinManager::AddClearRect(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.Invalid())
		return;

	if (queue_.Full())
		MakeRoom();
	AddFlags([&](RasterizerState *state) { CalculateRasterStateFlags(state, v0, v1, true); });
	queue_.Push(BinItem{ BinItemType::CLEAR_RECT, stateIndex_, range, v0, v1 });
	ItemQueued();
}

void BinManager::AddRect(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.Invalid())
		return;

	if (queue_.Full())
		MakeRoom();
	AddFlags([&](RasterizerState *state) { CalculateRasterStateFlags(state, v0, v1, true); });
	queue_.Push(BinItem{ BinItemType::RECT, stateIndex_, range, v0, v1 });
	ItemQueued();
}

void BinManager::AddSprite(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.Invalid())
		return;

	if (queue_.Full())
		MakeRoom();
	AddFlags([&](RasterizerState *state) { CalculateRasterStateFlags(state, v0, v1, true); });
	queue_.Push(BinItem{ BinItemType::SPRITE, stateIndex_, range, v0, v1 });
	ItemQueued();
}

void BinManager::AddLine(const VertexData &v0, const VertexData &v1) {
	const BinCoords range = Range(v0, v1);
	if (range.Invalid())
		return;

	if (queue_.Full())
		MakeRoom();
	AddFlags([&](RasterizerState *state) { CalculateRasterStateFlags(state, v0, v1, false); });
	queue_.Push(BinItem{ BinItemType::LINE, stateIndex_, range, v0, v1 });
	ItemQueued();
}

void BinManager::AddPoint(const VertexData &v0) {
	const BinCoords range = Range(v0);
	if (range.Invalid())
		return;

	if (queue_.Full())
		MakeRoom();
	AddFlags([&](RasterizerState *state) { CalculateRasterStateFlags(state, v0); });
	queue_.Push(BinItem{ BinItemType::POINT, stateIndex_, range, v0 });
	ItemQueued();
}

void BinManager::Drain() {
	PROFILE_THIS_SCOPE("bin_drain");

	// Also when switching to one thread: what's left goes to the tiles, which hold the earlier items.
	if (maxTasks_ > 1 || activeCount_ != 0) {
		DistributeItems();
		// Waking threads costs more than drawing a little: the flush draws that itself.
		if (entriesSinceWake_ >= WAKE_ENTRIES) {
			WakeTasks();
			entriesSinceWake_ = 0;
		}
		return;
	}

	// Let's try to optimize states, if we can.
	OptimizePendingStates(pendingStateIndex_, stateIndex_);
	pendingStateIndex_ = stateIndex_;

	PROFILE_THIS_SCOPE("bin_drain_single");
	// Anything (transfers, the CPU) may have written memory since the last drain.
	selfTexValid_ = false;
	while (!queue_.Empty()) {
		const BinItem &item = queue_.PeekNext();
		const RasterizerState &state = states_[item.stateIndex];
		if (state.selfTexture) {
			DrawSplit(item, SelfTextureSnapshot(item, state));
		} else {
			selfTexValid_ = false;
			DrawSplit(item, state);
		}
		queue_.SkipNext();
	}
	distributePos_ = queue_.tail_;
}

// One thread draws in order, primitive after primitive, for one that textures from what it draws to (from a
// snapshot taken before it, see SelfTextureSnapshot). Within a primitive each pixel only reads the snapshot
// and its own pixel, so a large one is still drawn by several threads, in bands of rows. Not when pixels
// can wrap past the stride into a row another band draws.
void BinManager::DrawSplit(const BinItem &item, const RasterizerState &state) {
	const BinCoords &range = item.range;
	const int rowPairs = (range.y2 - range.y1 + SCREEN_SCALE_FACTOR * 2) / (SCREEN_SCALE_FACTOR * 2);
	const int width = (range.x2 - range.x1 + SCREEN_SCALE_FACTOR) / SCREEN_SCALE_FACTOR;
	const bool splittable = item.type == BinItemType::TRIANGLE || item.type == BinItemType::RECT || item.type == BinItemType::SPRITE || item.type == BinItemType::CLEAR_RECT;
	if (!splittable || pastStride_ || FORCE_SINGLE_THREAD || rowPairs < 2 * SPLIT_MIN_ROW_PAIRS || width * rowPairs * 2 < SPLIT_MIN_PIXELS) {
		DrawBinItem(item, state);
		return;
	}
	// Bands start an even number of rows from the top, so the 2x2 quads are the same as unsplit.
	ParallelRangeLoop(&g_threadManager, [&](int lower, int upper) {
		BinCoords band = range;
		band.y1 = range.y1 + lower * SCREEN_SCALE_FACTOR * 2;
		band.y2 = std::min(range.y1 + upper * SCREEN_SCALE_FACTOR * 2 - 1, range.y2);
		DrawBinItem(item, band, state);
	}, 0, rowPairs, SPLIT_MIN_ROW_PAIRS, TaskPriority::HIGH);
}

void BinManager::DistributeItems() {
	OptimizePendingStates(pendingStateIndex_, stateIndex_);
	pendingStateIndex_ = stateIndex_;
	DistributeItems(queue_.tail_);
}

void BinManager::DistributeItems(size_t end) {
	undistributed_ = 0;

	while (distributePos_ != end) {
		const size_t index = distributePos_;
		const BinItem &item = queue_[index];
		// From now on the threads may draw with it, so it doesn't change (AddedFlags).
		states_[item.stateIndex].liveGen = tileGen_;
		const int tx1 = std::clamp(item.range.x1 >> tileShiftX_, 0, tilesX_ - 1);
		const int tx2 = std::clamp(item.range.x2 >> tileShiftX_, 0, tilesX_ - 1);
		const int ty1 = std::clamp(item.range.y1 >> tileShiftY_, 0, tilesY_ - 1);
		const int ty2 = std::clamp(item.range.y2 >> tileShiftY_, 0, tilesY_ - 1);
		// Set before any tile can draw it.
		itemRefs_[index].store((tx2 - tx1 + 1) * (ty2 - ty1 + 1), std::memory_order_relaxed);
		for (int ty = ty1; ty <= ty2; ++ty) {
			for (int tx = tx1; tx <= tx2; ++tx) {
				const int t = ty * tilesX_ + tx;
				Tile &tile = tiles_[t];
				const uint32_t tail = tile.tail.load(std::memory_order_relaxed);
				tile.items[tail % QUEUED_PRIMS] = (uint16_t)index;
				entriesSinceWake_++;
				tile.tail.store(tail + 1, std::memory_order_release);
				if (!tileActive_[t]) {
					tileActive_[t] = true;
					const int count = activeCount_.load(std::memory_order_relaxed);
					activeTiles_[count] = (uint16_t)t;
					activeCount_.store(count + 1, std::memory_order_release);
				}
			}
		}
		distributePos_ = index + 1 == QUEUED_PRIMS ? 0 : index + 1;
	}
}

void BinManager::ResetTiles() {
	const int activeCount = activeCount_;
	for (int i = 0; i < activeCount; ++i) {
		const int t = activeTiles_[i];
		tiles_[t].head = 0;
		tiles_[t].tail = 0;
		tileActive_[t] = false;
	}
	activeCount_ = 0;
	entriesSinceWake_ = 0;
	tileGen_++;
}

void BinManager::ReclaimItems() {
	while (!queue_.Empty() && queue_.head_ != distributePos_ && itemRefs_[queue_.head_].load(std::memory_order_acquire) == 0)
		queue_.SkipNext();
}

void BinManager::WakeTasks() {
	// No more threads than tiles with work.
	int pending = 0;
	const int count = activeCount_.load(std::memory_order_relaxed);
	for (int n = 0; n < count && pending < maxTasks_; ++n) {
		const Tile &tile = tiles_[activeTiles_[n]];
		if (tile.head.load(std::memory_order_relaxed) != tile.tail.load(std::memory_order_relaxed))
			pending++;
	}

	int first = -1;
	uint64_t chain = 0;
	for (int i = 0; i < maxTasks_ && pending > 0; ++i) {
		if (taskStatus_[i]) {
			pending--;
			continue;
		}

		waitable_->Fill();
		taskStatus_[i] = true;
		if (first < 0)
			first = i;
		else
			chain |= 1ULL << i;
		pending--;
		enqueues_++;
	}
	if (first < 0)
		return;
	if (chain != 0)
		chainWake_.fetch_or(chain, std::memory_order_release);
	g_threadManager.EnqueueTaskOnThread(first, taskLists_[first].Next());
	mostThreads_ = std::max(mostThreads_, maxTasks_);
}

void BinManager::WakeChained() {
	u64 chain = chainWake_.exchange(0, std::memory_order_acquire);
	while (chain != 0) {
		const int i = LeastSignificantSetBit(chain);
		chain &= chain - 1;
		g_threadManager.EnqueueTaskOnThread(i, taskLists_[i].Next());
	}
}

// The queue is full: with threads, wait for drawn items to free up, helping with the drawing.
void BinManager::MakeRoom() {
	if (maxTasks_ <= 1) {
		Drain();
		return;
	}
	Drain();
	ReclaimItems();
	while (queue_.Full()) {
		if (!ProcessTiles(0))
			std::this_thread::yield();
		ReclaimItems();
	}
}

bool BinManager::ProcessTiles(int start) {
	bool any = false;
	bool found;
	do {
		found = false;
		const int count = activeCount_.load(std::memory_order_acquire);
		for (int n = 0; n < count; ++n) {
			const int t = activeTiles_[(start + n) % count];
			Tile &tile = tiles_[t];
			if (tile.head.load(std::memory_order_relaxed) == tile.tail.load(std::memory_order_acquire))
				continue;
			if (tile.busy.load(std::memory_order_relaxed) || tile.busy.exchange(true, std::memory_order_acquire))
				continue;

			const int tx = t % tilesX_, ty = t / tilesX_;
			const BinCoords tileRange{
				tx << tileShiftX_, ty << tileShiftY_,
				((tx + 1) << tileShiftX_) - 1, ((ty + 1) << tileShiftY_) - 1,
			};
			uint32_t head = tile.head.load(std::memory_order_relaxed);
			uint32_t tail;
			while (head != (tail = tile.tail.load(std::memory_order_acquire))) {
				for (; head != tail; ++head) {
					const uint16_t index = tile.items[head % QUEUED_PRIMS];
					const BinItem &item = queue_[index];
					// Clamping to the grid can put an item in an edge tile it doesn't reach.
					const BinCoords range = tileRange.Intersect(item.range);
					if (!range.Invalid())
						DrawBinItem(item, range, states_[item.stateIndex]);
					itemRefs_[index].fetch_sub(1, std::memory_order_release);
				}
				tile.head.store(head, std::memory_order_release);
			}
			tile.busy.store(false, std::memory_order_release);
			found = true;
			any = true;
		}
	} while (found);
	return any;
}

void BinManager::Flush(const char *reason) {
	if (!queuedSinceFlush_) {
		// Nothing queued, so nothing refers to the older states and CLUTs. Trim them anyway: callers
		// flush because one of these rings is full, and push into it right after.
		while (states_.Size() > 1) {
			states_.SkipNext();
		}
		while (cluts_.Size() > 1) {
			cluts_.SkipNext();
		}
		return;
	}

	double st = 0.0;
	const bool collectDebugStats = g_coreCollectDebugStats;
	if (collectDebugStats) {
		st = time_now_d();
	}
	Drain();
	if (maxTasks_ > 1 || activeCount_ != 0) {
		// Help with the drawing, then wait for what the threads are still on.
		while (ProcessTiles(0)) {
		}
	}
	waitable_->Wait();
	ResetTiles();
	distributePos_ = 0;
	undistributed_ = 0;
	// The CPU and transfers may write memory after a flush.
	selfTexValid_ = false;
	ClearTileMarks();

	queue_.Reset();
	while (states_.Size() > 1)
		states_.SkipNext();
	while (cluts_.Size() > 1)
		cluts_.SkipNext();

	Rasterizer::FlushJit();
	Sampler::FlushJit();

	queuedSinceFlush_ = false;

	for (BinDirtyRange &pending : pendingWrites_) {
		pending.base = 0;
	}
	pendingReads_.clear();

	// We'll need to set the pending writes and reads again, since we just flushed it.
	dirty_ |= SoftDirty::BINNER_RANGE | SoftDirty::BINNER_OVERLAP;

	if (collectDebugStats) {
		const double et = time_now_d();
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

	const size_t capacity = states_.Capacity();
	const int count = (int)((capacity + last - first) % capacity + 1);
	for (int i = 0; i < count; ++i) {
		size_t pos = (first + i) % capacity;
		// The threads may be drawing with it.
		if (states_[pos].liveGen == tileGen_)
			continue;
		OptimizeRasterState(&states_[pos]);
	}
}

bool BinManager::HasPendingWrite(uint32_t start, uint32_t stride, uint32_t w, uint32_t h) {
	// We can only write to VRAM.
	if (!Memory::IsVRAMAddress(start))
		return false;
	// Ignore mirrors for overlap detection.
	start &= 0x041FFFFF;

	for (const auto &range : pendingWrites_) {
		if (PendingWriteIn(range, start, stride, w, h))
			return true;
	}
	return false;
}

bool BinManager::PendingWriteIn(const BinDirtyRange &range, uint32_t start, uint32_t stride, uint32_t w, uint32_t h) {
	start &= 0x041FFFFF;
	if (range.base == 0 || range.strideBytes == 0)
		return false;
	uint32_t size = stride * (h - 1) + w;
	if (start >= range.base + range.height * range.strideBytes || start + size <= range.base)
		return false;

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

void BinManager::GetStats(StringWriter &w) {
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
	w.F("Slowest individual flush: %s (%0.4f)\n"
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

// After queuing an item: draw it now, alone, or later in a batch.
void BinManager::ItemQueued() {
	queuedSinceFlush_ = true;

	if (maxTasks_ == 1) {
		Drain();
	} else if (NeedsOrder(queue_[(queue_.tail_ + QUEUED_PRIMS - 1) % QUEUED_PRIMS])) {
		DrainDependent();
	} else if (++undistributed_ >= DISTRIBUTE_BATCH) {
		Drain();
	}
}

// Whether a primitive has to be drawn after all queued ones and before the next: one whose pixels past the
// stride land in the next row (which another tile may be drawing), one that writes what a queued primitive
// still has to read, and one that textures from what's drawn, reading texels a queued primitive (or itself)
// writes. Otherwise it's queued, and what it writes and reads is noted for the next ones.
bool BinManager::NeedsOrder(const BinItem &item) {
	const RasterizerState &state = states_[item.stateIndex];
	if (pastStride_ && item.range.x2 / SCREEN_SCALE_FACTOR >= (int)state.pixelID.cached.framebufStride)
		return true;

	const int tx1 = std::clamp(item.range.x1 >> tileShiftX_, 0, tilesX_ - 1);
	const int tx2 = std::clamp(item.range.x2 >> tileShiftX_, 0, tilesX_ - 1);
	const int ty1 = std::clamp(item.range.y1 >> tileShiftY_, 0, tilesY_ - 1);
	const int ty2 = std::clamp(item.range.y2 >> tileShiftY_, 0, tilesY_ - 1);
	if (anyTileReads_) {
		for (int ty = ty1; ty <= ty2; ++ty) {
			for (int tx = tx1; tx <= tx2; ++tx) {
				if (tileReads_[ty * tilesX_ + tx])
					return true;
			}
		}
	}
	for (int ty = ty1; ty <= ty2; ++ty)
		memset(tileWrites_ + ty * tilesX_ + tx1, 1, tx2 - tx1 + 1);

	if (!selfRender_)
		return false;
	TexelRegion region;
	if (!SelfReadRegion(item, region))
		return true;
	// The depth buffer isn't in the tile marks.
	if (pendingWrites_[1].base != 0 && PendingWriteIn(pendingWrites_[1], region.start, region.stride, region.widthBytes, region.rows))
		return true;
	bool written = false;
	ForTargetTiles(state, region, [&](int t) {
		written = written || tileWrites_[t] != 0;
	});
	if (written)
		return true;
	ForTargetTiles(state, region, [&](int t) {
		tileReads_[t] = 1;
	});
	anyTileReads_ = true;
	return false;
}

// Each extra tile a triangle touches sets it up again, so tiles are as large as they can be with plenty to
// spare for every thread: at least sixteen per thread over a 480x272 screen. With fewer, a scene whose
// drawing is concentrated on part of the screen leaves threads idle (LocoRoco took a third longer with four
// per thread on eight threads).
// The multiplier trades the repeated setup against the load balance, so the best one depends on what drawing
// a pixel costs. When that gets cheaper, larger tiles can win: with faster span drawing, four per thread was
// as fast in wall time and used less CPU. Measure again (Tools/headless_bench.py, wall and CPU time on
// several games) after changes to the per-pixel cost.
void BinManager::PickTileSize(int threads) {
	static const int sizes[][2] = { { 128, 32 }, { 64, 32 }, { 64, 16 } };
	int w = MIN_TILE_W, h = MIN_TILE_H;
	for (const auto &size : sizes) {
		const int tiles = ((480 + size[0] - 1) / size[0]) * ((272 + size[1] - 1) / size[1]);
		if (tiles >= 16 * threads) {
			w = size[0];
			h = size[1];
			break;
		}
	}
	tileW_ = w;
	tileH_ = h;
	tileShiftX_ = LeastSignificantSetBit((u32)(w * SCREEN_SCALE_FACTOR));
	tileShiftY_ = LeastSignificantSetBit((u32)(h * SCREEN_SCALE_FACTOR));
	tilesX_ = 1024 / w;
	tilesY_ = 1024 / h;
}

void BinManager::ClearTileMarks() {
	memset(tileWrites_, 0, sizeof(tileWrites_));
	if (anyTileReads_)
		memset(tileReads_, 0, sizeof(tileReads_));
	anyTileReads_ = false;
}

// Calls f(tile) for the tiles of the render target that a texel region's memory falls in.
template <typename F>
void BinManager::ForTargetTiles(const RasterizerState &state, const TexelRegion &region, F f) {
	constexpr uint32_t mirrorMask = 0x041FFFFF;
	const uint32_t bpp = state.pixelID.FBFormat() == GE_FORMAT_8888 ? 4 : 2;
	const uint32_t fbStrideBytes = state.pixelID.cached.framebufStride * bpp;
	if (fbStrideBytes == 0)
		return;
	const int64_t fbBase = drawTargetAddr_ & mirrorMask;
	int lastTy = -1, lastTx1 = -1, lastTx2 = -1;
	for (uint32_t r = 0; r < region.rows; ++r) {
		const int64_t a0 = (int64_t)(region.start & mirrorMask) + (int64_t)r * region.stride;
		const int64_t a1 = a0 + region.widthBytes - 1;
		if (a1 < fbBase)
			continue;
		const int64_t off0 = std::max(a0, fbBase) - fbBase;
		const int64_t off1 = a1 - fbBase;
		const int64_t y0 = off0 / fbStrideBytes, y1 = off1 / fbStrideBytes;
		if (y0 >= tilesY_ * tileH_)
			break;
		int x0 = 0, x1 = 1023;
		if (y0 == y1) {
			x0 = (int)((off0 % fbStrideBytes) / bpp);
			x1 = (int)((off1 % fbStrideBytes) / bpp);
		}
		const int tx1 = std::min(x0 / tileW_, tilesX_ - 1), tx2 = std::min(x1 / tileW_, tilesX_ - 1);
		for (int64_t y = y0; y <= y1 && y < tilesY_ * tileH_; ++y) {
			const int ty = (int)(y / tileH_);
			if (ty == lastTy && tx1 == lastTx1 && tx2 == lastTx2)
				continue;
			lastTy = ty;
			lastTx1 = tx1;
			lastTx2 = tx2;
			for (int tx = tx1; tx <= tx2; ++tx)
				f(ty * tilesX_ + tx);
		}
	}
}

// The texels a primitive that textures from what's being drawn reads, as memory rows: within its vertices'
// coordinates (also with perspective), plus the bilinear neighbors. False for anything less simple, which is
// then treated as reading what's drawn.
bool BinManager::SelfReadRegion(const BinItem &item, TexelRegion &region) {

	const RasterizerState &state = states_[item.stateIndex];
	if (!state.enableTextures || state.maxTexLevel != 0 || state.textureProj || state.samplerID.swizzle)
		return false;
	const GETextureFormat fmt = state.samplerID.TexFmt();
	if (fmt == GE_TFMT_DXT1 || fmt == GE_TFMT_DXT3 || fmt == GE_TFMT_DXT5)
		return false;
	// Plain VRAM, not the depth mirrors.
	const uint32_t addr = state.texaddr[0];
	if ((addr & 0x0FE00000) != 0x04000000)
		return false;
	const int w = state.samplerID.cached.sizes[0].w;
	const int h = state.samplerID.cached.sizes[0].h;
	const uint32_t bits = textureBitsPerPixel[fmt];
	// A texture that fits the GE's texture cache can stay cached across draws (SelfTextureSnapshot).
	if (w > 512 || h > 512 || (uint32_t)w * h * bits / 8 <= 8192)
		return false;

	int count;
	switch (item.type) {
	case BinItemType::TRIANGLE: count = 3; break;
	case BinItemType::RECT:
	case BinItemType::SPRITE: count = 2; break;
	default: return false;
	}
	const VertexData *vs[3] = { &item.v0, &item.v1, &item.v2 };
	float umin = vs[0]->texturecoords.s(), umax = umin, vmin = vs[0]->texturecoords.t(), vmax = vmin;
	for (int i = 1; i < count; ++i) {
		umin = std::min(umin, vs[i]->texturecoords.s());
		umax = std::max(umax, vs[i]->texturecoords.s());
		vmin = std::min(vmin, vs[i]->texturecoords.t());
		vmax = std::max(vmax, vs[i]->texturecoords.t());
	}
	if (!state.throughMode) {
		umin *= w;
		umax *= w;
		vmin *= h;
		vmax *= h;
	}
	if (!(umin >= -1024.0f && umax <= 1024.0f && vmin >= -1024.0f && vmax <= 1024.0f))
		return false;
	auto texels = [](float lo, float hi, int size, bool clamp, int &first, int &last) {
		first = (int)floorf(lo) - 1;
		last = (int)floorf(hi) + 1;
		if (clamp) {
			first = std::max(first, 0);
			last = std::min(last, size - 1);
		} else if (first < 0 || last >= size) {
			first = 0;
			last = size - 1;
		}
	};
	int u0, u1, v0, v1;
	texels(umin, umax, w, state.samplerID.clampS, u0, u1);
	texels(vmin, vmax, h, state.samplerID.clampT, v0, v1);
	if (u0 > u1 || v0 > v1)
		return false;

	region.stride = state.texbufw[0] * bits / 8;
	const uint32_t startByte = u0 * bits / 8;
	region.widthBytes = ((u1 + 1) * bits + 7) / 8 - startByte;
	region.start = addr + v0 * region.stride + startByte;
	region.rows = v1 - v0 + 1;
	return true;
}

// The newest queued primitive needs order (NeedsOrder): draw everything before it, then it alone (one that
// textures from what's drawn sees a snapshot of the texture as it was before it).
void BinManager::DrainDependent() {
	const size_t last = (queue_.tail_ + QUEUED_PRIMS - 1) % QUEUED_PRIMS;
	// Also for this primitive's flags.
	OptimizePendingStates(pendingStateIndex_, stateIndex_);
	pendingStateIndex_ = stateIndex_;
	if (distributePos_ != last)
		DistributeItems(last);
	if (activeCount_ != 0) {
		if (entriesSinceWake_ >= WAKE_ENTRIES)
			WakeTasks();
		while (ProcessTiles(0)) {
		}
		waitable_->Wait();
		ResetTiles();
		// The tiles drew what the snapshot may have seen before.
		selfTexValid_ = false;
	}

	const BinItem &item = queue_[last];
	const RasterizerState &state = states_[item.stateIndex];
	if (state.selfTexture) {
		DrawSplit(item, SelfTextureSnapshot(item, state));
	} else {
		selfTexValid_ = false;
		DrawSplit(item, state);
	}
	queue_.Reset();
	distributePos_ = 0;
	undistributed_ = 0;
	ClearTileMarks();
}
