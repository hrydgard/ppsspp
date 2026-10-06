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

#include <atomic>
#include <unordered_map>
#include "GPU/Software/Rasterizer.h"

struct BinWaitable;
class DrawBinItemsTask;

enum class BinItemType : uint8_t {
	TRIANGLE,
	CLEAR_RECT,
	RECT,
	SPRITE,
	LINE,
	POINT,
};

struct BinCoords {
	int x1;
	int y1;
	int x2;
	int y2;

	bool Invalid() const {
		return x2 < x1 || y2 < y1;
	}

	BinCoords Intersect(const BinCoords &range) const;
};

struct BinItem {
	BinItemType type;
	uint16_t stateIndex;
	BinCoords range;
	VertexData v0;
	VertexData v1;
	VertexData v2;
};

template <typename T, size_t N>
struct BinQueue {
	BinQueue(const BinQueue &) = delete;
	BinQueue &operator=(const BinQueue &) = delete;
	BinQueue() {
		Reset();
	}
	~BinQueue() {
		FreeAlignedMemory(items_);
	}

	void Setup() {
		items_ = (T *)AllocateAlignedMemory(sizeof(T) * capacity_, 16);
	}

	// Only when no other thread uses it: room for newCapacity, the items in order from index 0.
	void Grow(size_t newCapacity) {
		T *items = (T *)AllocateAlignedMemory(sizeof(T) * newCapacity, 16);
		const size_t size = size_;
		for (size_t i = 0; i < size; ++i)
			items[i] = Peek(i);
		FreeAlignedMemory(items_);
		items_ = items;
		capacity_ = newCapacity;
		head_ = 0;
		tail_ = size;
	}

	void Reset() {
		head_ = 0;
		tail_ = 0;
		size_ = 0;
	}

	size_t Push(const T &item) {
		size_t i = tail_++;
		if (i + 1 == capacity_)
			tail_ -= capacity_;
		items_[i] = item;
		size_++;
		return i;
	}

	T Pop() {
		size_t i = head_++;
		if (i + 1 == capacity_)
			head_ -= capacity_;
		T item = items_[i];
		size_--;
		return item;
	}

	// Only safe if you're the only one reading.
	T &PeekNext() {
		return items_[head_];
	}

	void SkipNext() {
		size_t i = head_++;
		if (i + 1 == capacity_)
			head_ -= capacity_;
		size_--;
	}

	// Only safe if you're the only one reading.
	const T &Peek(size_t offset) const {
		size_t i = head_ + offset;
		if (i >= capacity_)
			i -= capacity_;
		return items_[i];
	}

	// Only safe if you're the only one writing.
	T &PeekPush() {
		return items_[tail_];
	}

	size_t PushPeeked() {
		size_t i = tail_++;
		if (i + 1 == capacity_)
			tail_ -= capacity_;
		size_++;
		return i;
	}

	size_t Size() const {
		return size_;
	}

	bool Full() const {
		return size_ >= capacity_ - 1;
	}

	bool NearFull() const {
		return size_ >= capacity_ - 2;
	}

	size_t Capacity() const {
		return capacity_;
	}

	bool Empty() const {
		return size_ == 0;
	}

	T &operator[](size_t index) {
		return items_[index];
	}

	const T &operator[](size_t index) const {
		return items_[index];
	}

	T *items_ = nullptr;
	std::atomic<size_t> head_;
	std::atomic<size_t> tail_ ;
	std::atomic<size_t> size_;
	size_t capacity_ = N;
};

union BinClut {
	uint8_t readable[1024];
};

struct BinTaskList {
	// We shouldn't ever need more than two at once, since we use an atomic to run one at a time.
	// A second could run due to overlap during teardown.
	static constexpr int N = 2;

	DrawBinItemsTask *tasks[N]{};
	int count = 0;

	DrawBinItemsTask *Next() {
		return tasks[count % N];
	}
};

struct BinDirtyRange {
	uint32_t base;
	uint32_t strideBytes;
	uint32_t widthBytes;
	uint32_t height;

	void Expand(uint32_t newBase, uint32_t bpp, uint32_t stride, const DrawingCoords &tl, const DrawingCoords &br);
};

class StringWriter;
class BinManager {
public:
	BinManager(const BinManager &) = delete;
	BinManager &operator=(const BinManager &) = delete;
	BinManager();
	~BinManager();

	void UpdateState();
	void UpdateClut(const void *src);
	// TEXFLUSH empties the GE's texture cache, which self-texturing can see.
	void NotifyTexFlush() {
		texFlushGen_++;
		dirty_ |= SoftDirty::SAMPLER_TEXLIST;
	}

	const Rasterizer::RasterizerState &State() {
		return states_[stateIndex_];
	}

	void AddTriangle(const VertexData &v0, const VertexData &v1, const VertexData &v2);
	void AddClearRect(const VertexData &v0, const VertexData &v1);
	void AddRect(const VertexData &v0, const VertexData &v1);
	void AddSprite(const VertexData &v0, const VertexData &v1);
	void AddLine(const VertexData &v0, const VertexData &v1);
	void AddPoint(const VertexData &v0);

	void Drain();
	void Flush(const char *reason);
	bool HasPendingWrite(uint32_t start, uint32_t stride, uint32_t w, uint32_t h);
	// Assumes you've also checked for a write (writes are partial so are automatically reads.)
	bool HasPendingRead(uint32_t start, uint32_t stride, uint32_t w, uint32_t h);

	void GetStats(StringWriter &w);
	void ResetStats();

	void SetDirty(SoftDirty flags) {
		dirty_ |= flags;
	}
	void ClearDirty(SoftDirty flags) {
		dirty_ &= ~flags;
	}
	SoftDirty GetDirty() {
		return dirty_;
	}
	bool HasDirty(SoftDirty flags) {
		return dirty_ & flags;
	}

protected:
#if PPSSPP_ARCH(32BIT)
	// Use less memory and less address space.  We're unlikely to have 32 cores on a 32-bit CPU.
	static constexpr int MAX_POSSIBLE_TASKS = 16;
#else
	static constexpr int MAX_POSSIBLE_TASKS = 64;
#endif
	// States to start with, about 1 MB. A full ring flushes, then doubles, up to MAX_QUEUED_STATES (stateIndex is
	// 16 bits).
	static constexpr int QUEUED_STATES = 4096;
	static constexpr int MAX_QUEUED_STATES = 32768;
	// These are 1KB each, so half an MB.
	static constexpr int QUEUED_CLUTS = 512;
	// About 360 KB, but we have usually 16 or less of them, so 5 MB - 22 MB.
	static constexpr int QUEUED_PRIMS = 2048;

	typedef BinQueue<Rasterizer::RasterizerState, QUEUED_STATES> BinStateQueue;
	typedef BinQueue<BinClut, QUEUED_CLUTS> BinClutQueue;
	typedef BinQueue<BinItem, QUEUED_PRIMS> BinItemQueue;

private:
	BinStateQueue states_;
	BinClutQueue cluts_;
	uint16_t stateIndex_;
	uint16_t clutIndex_;
	BinCoords scissor_;
	BinItemQueue queue_;
	// Anything was queued since the last flush (drawn or not).
	bool queuedSinceFlush_ = false;
	SoftDirty dirty_ = SoftDirty::NONE;

	int maxTasks_ = 1;
	BinTaskList taskLists_[MAX_POSSIBLE_TASKS];
	std::atomic<bool> taskStatus_[MAX_POSSIBLE_TASKS];
	// Threads whose tasks the first one woken enqueues: waking a thread is a system call, kept off this one.
	std::atomic<uint64_t> chainWake_{ 0 };

	// With threads, queued items are binned into screen tiles. Any thread can take a tile with work and
	// draws its items in order; only one at a time, so each pixel still sees the primitives in order.
	// Larger tiles set up fewer triangles more than once, smaller ones spread the work over more threads.
	// The size is picked at startup for the thread count (PickTileSize); the arrays fit the smallest.
	static constexpr int MIN_TILE_W = 64;
	static constexpr int MIN_TILE_H = 16;
	static constexpr int TILES_X = 1024 / MIN_TILE_W;
	static constexpr int TILES_Y = 1024 / MIN_TILE_H;
	// In subpixels.
	int tileShiftX_ = 0;
	int tileShiftY_ = 0;
	// Pixels.
	int tileW_ = MIN_TILE_W;
	int tileH_ = MIN_TILE_H;
	int tilesX_ = TILES_X;
	int tilesY_ = TILES_Y;
	struct Tile {
		// Indices into queue_, as a ring: head_ is how many have been drawn, tail_ how many were pushed.
		std::atomic<uint32_t> head;
		std::atomic<uint32_t> tail;
		std::atomic<bool> busy;
		uint16_t items[QUEUED_PRIMS];
	};
	Tile *tiles_ = nullptr;
	// For each queued item, how many tiles still have to draw it. It's reclaimed at zero.
	std::atomic<int> itemRefs_[QUEUED_PRIMS];
	// The tiles given work since the last flush, for the threads to look through.
	uint16_t activeTiles_[TILES_X * TILES_Y];
	std::atomic<int> activeCount_{ 0 };
	bool tileActive_[TILES_X * TILES_Y]{};
	// The queue_ index of the first item not yet put in tiles, and how many have been added since.
	size_t distributePos_ = 0;
	int undistributed_ = 0;
	int entriesSinceWake_ = 0;
	// The tiles queued primitives write, and that queued primitives texturing from the target read
	// (NeedsOrder). Cleared when the queue is empty.
	uint8_t tileWrites_[TILES_X * TILES_Y]{};
	uint8_t tileReads_[TILES_X * TILES_Y]{};
	bool anyTileReads_ = false;
	BinWaitable *waitable_ = nullptr;

	BinDirtyRange pendingWrites_[2]{};
	std::unordered_map<uint32_t, BinDirtyRange> pendingReads_;

	// Whether the current state textures from what it draws to, and the texture as it was before the
	// primitive being drawn for one that does.
	bool selfRender_ = false;
	// The scissor reaches past the framebuffer's stride.
	bool pastStride_ = false;
	Rasterizer::RasterizerState selfTexState_;
	std::vector<u8> selfTexBuf_[8];
	uint32_t selfTexAddr_[8]{};
	bool selfTexValid_ = false;
	uint32_t texFlushGen_ = 0;
	uint32_t selfTexFlushGen_ = 0;
	// The snapshot is of a texture small enough to stay in the GE's 8 KB texture cache.
	bool selfTexCached_ = false;
	BinCoords selfTexLastRange_{};
	bool creatingState_ = false;
	// JIT clear generations when the current state was computed.
	int jitGen_ = -1;
	uint16_t pendingStateIndex_ = 0;
	// Advances when every tile has been drawn and reset: a state whose liveGen is this one can be in use by
	// the threads, so it isn't changed (AddFlags).
	uint32_t tileGen_ = 1;

	std::unordered_map<const char *, double> flushReasonTimes_;
	std::unordered_map<const char *, double> lastFlushReasonTimes_;
	const char *slowestFlushReason_ = nullptr;
	double slowestFlushTime_ = 0.0;
	int lastFlipstats_ = 0;
	// The framebuffer the queued draws render to. A framebuffer change flushes first, so it's one for all
	// of them, and during that flush gstate already has the new one.
	u32 drawTargetAddr_ = 0;
	int enqueues_ = 0;
	int mostThreads_ = 0;

	void MarkPendingReads(const Rasterizer::RasterizerState &state);
	void MarkPendingWrites(const Rasterizer::RasterizerState &state);
	bool HasTextureWrite(const Rasterizer::RasterizerState &state);
	const Rasterizer::RasterizerState &SelfTextureSnapshot(const BinItem &item, const Rasterizer::RasterizerState &state);
	void OptimizePendingStates(uint16_t first, uint16_t last);
	void PushState();
	template <typename F>
	void AddFlags(F calculate);
	BinCoords Scissor(BinCoords range);
	BinCoords Range(const VertexData &v0, const VertexData &v1, const VertexData &v2);
	BinCoords Range(const VertexData &v0, const VertexData &v1);
	BinCoords Range(const VertexData &v0);
	void ItemQueued();
	void MakeRoom();
	void DrawSplit(const BinItem &item, const Rasterizer::RasterizerState &state);
	void DistributeItems();
	void DistributeItems(size_t end);
	void ResetTiles();
	void DrainDependent();
	struct TexelRegion {
		uint32_t start;
		uint32_t stride;
		uint32_t widthBytes;
		uint32_t rows;
	};
	bool SelfReadRegion(const BinItem &item, TexelRegion &region);
	template <typename F>
	void ForTargetTiles(const Rasterizer::RasterizerState &state, const TexelRegion &region, F f);
	void PickTileSize(int threads);
	void ClearTileMarks();
	bool PendingWriteIn(const BinDirtyRange &range, uint32_t start, uint32_t stride, uint32_t w, uint32_t h);
	bool NeedsOrder(const BinItem &item);
	void ReclaimItems();
	void WakeTasks();
	void WakeChained();
	bool ProcessTiles(int start);

	friend class DrawBinItemsTask;
};
