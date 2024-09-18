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

#include "Core/HLE/sceKernel.h"
#include "Common/Serialize/Serializer.h"

struct ThreadQueueList {
	// Number of queues (number of priority levels starting at 0.)
	static const int NUM_QUEUES = 128;
	// Initial number of threads a single queue can handle.
	static const int INITIAL_CAPACITY = 32;

	struct Queue {
		// Next ever-been-used queue (worse priority.)
		Queue *next;
		// First valid item in data.
		int first;
		// One after last valid item in data.
		int end;
		// A too-large array with room on the front and end.
		SceUID *data;
		// Size of data array.
		int capacity;

		inline int size() const {
			return end - first;
		}
		inline bool empty() const {
			return first == end;
		}
		inline int full() const {
			return end == capacity;
		}
	};

	ThreadQueueList() {
		memset(queues, 0, sizeof(queues));
		first = invalid();
	}

	~ThreadQueueList() {
		clear();
	}

	// Only for debugging, returns priority level.
	int contains(const SceUID uid) {
		for (int i = 0; i < NUM_QUEUES; ++i) {
			if (queues[i].data == nullptr)
				continue;

			Queue *cur = &queues[i];
			for (int j = cur->first; j < cur->end; ++j) {
				if (cur->data[j] == uid)
					return i;
			}
		}

		return -1;
	}

	inline SceUID pop_first() {
		Queue *cur = first;
		while (cur != invalid()) {
			if (cur->size() > 0)
				return cur->data[cur->first++];
			cur = cur->next;
		}

		_dbg_assert_msg_(false, "ThreadQueueList should not be empty.");
		return 0;
	}

	inline SceUID pop_first_better(u32 priority) {
		Queue *cur = first;
		// Don't bother looking past (worse than) this priority.
		Queue *stop = &queues[priority];
		while (cur < stop) {
			if (cur->size() > 0)
				return cur->data[cur->first++];
			cur = cur->next;
		}

		return 0;
	}

	inline SceUID peek_first() {
		Queue *cur = first;
		while (cur != invalid()) {
			if (cur->size() > 0)
				return cur->data[cur->first];
			cur = cur->next;
		}

		return 0;
	}

	inline void push_front(u32 priority, const SceUID threadID) {
		Queue *cur = &queues[priority];
		cur->data[--cur->first] = threadID;
		// If we ran out of room toward the front, add more room for next time.
		if (cur->first == 0)
			rebalance(priority);
	}

	inline void push_back(u32 priority, const SceUID threadID) {
		Queue *cur = &queues[priority];
		cur->data[cur->end++] = threadID;
		if (cur->full())
			rebalance(priority);
	}

	inline void remove(u32 priority, const SceUID threadID) {
		Queue *cur = &queues[priority];
		_dbg_assert_msg_(cur->next != nullptr, "ThreadQueueList::Queue should already be linked up.");

		for (int i = cur->first; i < cur->end; ++i) {
			if (cur->data[i] == threadID) {
				// How many more after this one?
				int remaining = cur->end - i;
				// If there are more, move them into place.
				if (remaining > 0)
					memmove(&cur->data[i], &cur->data[i + 1], remaining * sizeof(SceUID));

				// Now we're one shorter.
				--cur->end;
				return;
			}
		}

		// Wasn't there.
	}

	inline void rotate(u32 priority) {
		Queue *cur = &queues[priority];
		_dbg_assert_msg_(cur->next != nullptr, "ThreadQueueList::Queue should already be linked up.");

		if (cur->size() > 1) {
			// Grab the front and push it on the end.
			cur->data[cur->end++] = cur->data[cur->first++];
			if (cur->full())
				rebalance(priority);
		}
	}

	inline void clear() {
		for (int i = 0; i < NUM_QUEUES; ++i) {
			free(queues[i].data);
		}
		memset(queues, 0, sizeof(queues));
		first = invalid();
	}

	inline bool empty(u32 priority) const {
		const Queue *cur = &queues[priority];
		return cur->empty();
	}

	inline void prepare(u32 priority) {
		Queue *cur = &queues[priority];
		if (cur->next == nullptr)
			link(priority, INITIAL_CAPACITY);
	}

	void DoState(PointerWrap &p) {
		auto s = p.Section("ThreadQueueList", 1);
		if (!s)
			return;

		int numQueues = NUM_QUEUES;
		Do(p, numQueues);
		if (numQueues != NUM_QUEUES) {
			p.SetError(p.ERROR_FAILURE);
			ERROR_LOG(Log::sceKernel, "Savestate loading error: invalid data");
			return;
		}

		if (p.mode == p.MODE_READ)
			clear();

		for (int i = 0; i < NUM_QUEUES; ++i) {
			Queue *cur = &queues[i];
			int size = cur->size();
			Do(p, size);
			int capacity = cur->capacity;
			Do(p, capacity);

			if (capacity == 0)
				continue;

			if (p.mode == p.MODE_READ) {
				link(i, capacity);
				cur->first = (cur->capacity - size) / 2;
				cur->end = cur->first + size;
			}

			if (size != 0)
				DoArray(p, &cur->data[cur->first], size);
		}
	}

private:
	Queue *invalid() const {
		return (Queue *)-1;
	}

	// Initialize a priority level and link to other queues.
	void link(u32 priority, int size) {
		_dbg_assert_msg_(queues[priority].data == nullptr, "ThreadQueueList::Queue should only be initialized once.");

		// Make sure we stay a multiple of INITIAL_CAPACITY.
		if (size <= INITIAL_CAPACITY)
			size = INITIAL_CAPACITY;
		else {
			int goal = size;
			size = INITIAL_CAPACITY;
			while (size < goal)
				size *= 2;
		}

		// Allocate the queue.
		Queue *cur = &queues[priority];
		cur->data = (SceUID *)malloc(sizeof(SceUID) * size);
		cur->capacity = size;
		// Start smack in the middle so it can move both directions.
		cur->first = size / 2;
		cur->end = size / 2;

		for (int i = (int)priority - 1; i >= 0; --i) {
			// This queue is before ours, and points past us.
			// We'll have it point to our new queue, inserting into the chain.
			if (queues[i].next != nullptr) {
				cur->next = queues[i].next;
				queues[i].next = cur;
				return;
			}
		}

		// Never found above - that means there's no better queue yet.
		// The new one is now first, and whoever was first is after it.
		cur->next = first;
		first = cur;
	}

	// Move or allocate as necessary to maintain free space on both sides.
	void rebalance(u32 priority) {
		Queue *cur = &queues[priority];
		int size = cur->size();
		// Basically full.  Time for a larger queue?
		if (size >= cur->capacity - 2) {
			int new_capacity = cur->capacity * 2;
			SceUID *new_data = (SceUID *)realloc(cur->data, new_capacity * sizeof(SceUID));
			if (new_data != nullptr) {
				// Success, it's bigger now.
				cur->capacity = new_capacity;
				cur->data = new_data;
			}
		}

		// If we center all the items, it should start here.
		int newFirst = (cur->capacity - size) / 2;
		if (newFirst != cur->first) {
			memmove(&cur->data[newFirst], &cur->data[cur->first], size * sizeof(SceUID));
			cur->first = newFirst;
			cur->end = newFirst + size;
		}
	}

	// The first queue that's ever been used.
	Queue *first;
	// The priority level queues of thread ids.
	Queue queues[NUM_QUEUES];
};
