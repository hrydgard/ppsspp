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
#include "Common/Log.h"
#include "GPU/Software/Rasterizer.h"

struct BinWaitable;

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

	bool Invalid() const {
		return x2 < x1 || y2 < y1;
	}

	BinCoords Intersect(const BinCoords &range) const;
};

struct BinItem {
	BinItemType type;
	int stateIndex;
	BinCoords range;
	VertexData v0;
	VertexData v1;
	VertexData v2;
};

template <typename T, size_t N>
struct BinQueue {
	BinQueue() {
		items_ = new T[N];
		Reset();
	}
	~BinQueue() {
		delete [] items_;
	}

	void Reset() {
		head_ = 0;
		tail_ = 0;
		size_ = 0;
	}

	size_t Push(const T &item) {
		_dbg_assert_(size_ < N - 1);
		size_t i = tail_++;
		if (i + 1 == N)
			tail_ -= N;
		items_[i] = item;
		size_++;
		return i;
	}

	T Pop() {
		_dbg_assert_(!Empty());
		size_t i = head_++;
		if (i + 1 == N)
			head_ -= N;
		T item = items_[i];
		size_--;
		return item;
	}

	// Only safe if you're the only one reading.
	T &PeekNext() {
		_dbg_assert_(!Empty());
		return items_[head_];
	}

	void SkipNext() {
		_dbg_assert_(!Empty());
		size_t i = head_++;
		if (i + 1 == N)
			head_ -= N;
		size_--;
	}

	// Only safe if you're the only one writing.
	T &PeekPush() {
		_dbg_assert_(size_ < N - 1);
		return items_[tail_];
	}

	void PushPeeked() {
		_dbg_assert_(size_ < N - 1);
		size_t i = tail_++;
		if (i + 1 == N)
			tail_ -= N;
		size_++;
	}

	size_t Size() const {
		return size_;
	}

	bool Full() const {
		return size_ == N - 1;
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
};

union BinClut {
	uint8_t readable[1024];
};

class BinManager {
public:
	BinManager();
	~BinManager();

	void UpdateState();
	void UpdateClut(const void *src);

	const Rasterizer::RasterizerState &State() {
		return states_[stateIndex_];
	}

	void AddTriangle(const VertexData &v0, const VertexData &v1, const VertexData &v2);
	void AddClearRect(const VertexData &v0, const VertexData &v1);
	void AddSprite(const VertexData &v0, const VertexData &v1);
	void AddLine(const VertexData &v0, const VertexData &v1);
	void AddPoint(const VertexData &v0);

	void Drain();
	void Flush();

private:
	static constexpr int MAX_POSSIBLE_TASKS = 64;

	BinQueue<Rasterizer::RasterizerState, 64> states_;
	int stateIndex_;
	BinQueue<BinClut, 64> cluts_;
	int clutIndex_;
	BinCoords scissor_;
	BinQueue<BinItem, 1024> queue_;
	BinCoords queueRange_;
	int queueOffsetX_ = -1;
	int queueOffsetY_ = -1;

	int maxTasks_ = 1;
	bool tasksSplit_ = false;
	std::vector<BinCoords> taskRanges_;
	BinQueue<BinItem, 1024> taskQueues_[MAX_POSSIBLE_TASKS];
	std::atomic<bool> taskStatus_[MAX_POSSIBLE_TASKS];
	BinWaitable *waitable_ = nullptr;

	BinCoords Scissor(BinCoords range);
	BinCoords Range(const VertexData &v0, const VertexData &v1, const VertexData &v2);
	BinCoords Range(const VertexData &v0, const VertexData &v1);
	BinCoords Range(const VertexData &v0);
	void Expand(const BinCoords &range);
};
