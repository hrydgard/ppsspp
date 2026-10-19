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

#include <atomic>
#include <thread>

#include "Core/Debugger/WebSocket/DebuggerEventQueue.h"
#include "unittest/UnitTest.h"

static std::string HitEvent(size_t sequence) {
	return "{\"event\":\"cpu.breakpoint.hit\",\"sequence\":" + std::to_string(sequence) + "}";
}

static const std::string stepping = R"({"event":"cpu.stepping","pc":142606336,"ticks":100})";
static const std::string gameStart = R"({"event":"game.start","game":{"id":"TEST"}})";
static const std::string gameQuit = R"({"event":"game.quit","game":null})";
static constexpr size_t capacity = DebuggerEventQueue::MAX_PENDING_EVENTS;

static bool TestEventQueueOrder() {
	DebuggerEventQueue queue;
	queue.Push("game", gameStart);
	queue.Push("breakpoint", HitEvent(1));
	queue.Push("stepping", stepping);
	queue.Push("game", gameQuit);
	std::vector<DebuggerEventQueue::Event> events;
	EXPECT_TRUE(queue.Take(&events));
	EXPECT_EQ_INT(events.size(), 4);
	EXPECT_EQ_STR(events[0].second, gameStart);
	EXPECT_EQ_STR(events[1].second, HitEvent(1));
	EXPECT_EQ_STR(events[2].second, stepping);
	EXPECT_EQ_STR(events[3].second, gameQuit);
	// Reusing the output must not put already-delivered events back in the queue.
	EXPECT_TRUE(queue.Take(&events));
	EXPECT_TRUE(events.empty());
	queue.Push("breakpoint", HitEvent(2));
	EXPECT_TRUE(queue.Take(&events));
	EXPECT_EQ_INT(events.size(), 1);
	EXPECT_EQ_STR(events[0].second, HitEvent(2));
	return true;
}

static bool TestEventQueueBreakpointPressure() {
	DebuggerEventQueue queue;
	queue.Push("game", gameStart);
	for (size_t sequence = 1; sequence <= capacity * 2; ++sequence) {
		queue.Push("breakpoint", HitEvent(sequence));
	}
	queue.Push("stepping", stepping);
	for (size_t sequence = capacity * 2 + 1; sequence <= capacity * 3; ++sequence) {
		queue.Push("breakpoint", HitEvent(sequence));
	}
	queue.Push("game", gameQuit);
	std::vector<DebuggerEventQueue::Event> events;
	EXPECT_TRUE(queue.Take(&events));
	EXPECT_EQ_INT(events.size(), capacity);
	EXPECT_EQ_STR(events[0].second, gameStart);
	for (size_t sequence = 1; sequence <= capacity - 3; ++sequence) {
		EXPECT_EQ_STR(events[sequence].second, HitEvent(sequence));
	}
	EXPECT_EQ_STR(events[capacity - 2].second, stepping);
	EXPECT_EQ_STR(events[capacity - 1].second, gameQuit);
	// After draining, delivery recovers with the producer's sequence unchanged.
	// The next hit exposes the gap; delivery of a control event proves no such completeness.
	queue.Push("breakpoint", HitEvent(capacity * 3 + 1));
	queue.Push("stepping", stepping);
	EXPECT_TRUE(queue.Take(&events));
	EXPECT_EQ_INT(events.size(), 2);
	EXPECT_EQ_STR(events[0].second, HitEvent(capacity * 3 + 1));
	EXPECT_EQ_STR(events[1].second, stepping);
	return true;
}

static bool TestEventQueueOnlyBreakpointEvicted() {
	DebuggerEventQueue queue;
	for (size_t i = 0; i < capacity; ++i) {
		if (i == capacity / 2) {
			queue.Push("breakpoint", HitEvent(1));
		} else {
			queue.Push("stepping", std::to_string(i));
		}
	}
	queue.Push("game", gameQuit);
	std::vector<DebuggerEventQueue::Event> events;
	EXPECT_TRUE(queue.Take(&events));
	EXPECT_EQ_INT(events.size(), capacity);
	for (size_t i = 0; i < capacity - 1; ++i) {
		const size_t original = i < capacity / 2 ? i : i + 1;
		EXPECT_TRUE(std::strcmp(events[i].first, "stepping") == 0);
		EXPECT_EQ_STR(events[i].second, std::to_string(original));
	}
	EXPECT_EQ_STR(events.back().second, gameQuit);
	return true;
}

static bool TestEventQueueControlCapacity() {
	DebuggerEventQueue queue;
	for (size_t i = 0; i < capacity; ++i) {
		queue.Push("stepping", std::to_string(i));
	}
	// A full control-only queue still tolerates noisy breakpoint hits.
	for (size_t i = 1; i <= capacity * 2; ++i) {
		queue.Push("breakpoint", HitEvent(i));
	}
	std::vector<DebuggerEventQueue::Event> events;
	EXPECT_TRUE(queue.Take(&events));
	EXPECT_EQ_INT(events.size(), capacity);
	for (size_t i = 0; i < capacity; ++i) {
		EXPECT_EQ_STR(events[i].second, std::to_string(i));
	}
	queue.Push("game", gameQuit);
	EXPECT_TRUE(queue.Take(&events));
	EXPECT_EQ_INT(events.size(), 1);
	EXPECT_EQ_STR(events[0].second, gameQuit);
	return true;
}

static bool TestEventQueueControlOverflow() {
	DebuggerEventQueue queue;
	for (size_t i = 0; i < capacity; ++i) {
		queue.Push("stepping", std::to_string(i));
	}
	queue.Push("game", gameQuit);
	std::vector<DebuggerEventQueue::Event> events;
	EXPECT_FALSE(queue.Take(&events));
	EXPECT_TRUE(events.empty());
	// Draining must not recover a connection which has already lost a transition.
	for (size_t i = 0; i < capacity * 2; ++i) {
		queue.Push("game", gameStart);
		queue.Push("breakpoint", HitEvent(i));
	}
	EXPECT_FALSE(queue.Take(&events));
	EXPECT_TRUE(events.empty());
	DebuggerEventQueue reconnected;
	reconnected.Push("stepping", stepping);
	EXPECT_TRUE(reconnected.Take(&events));
	EXPECT_EQ_INT(events.size(), 1);
	EXPECT_EQ_STR(events[0].second, stepping);
	return true;
}

static bool TestEventQueueConcurrentDrain() {
	DebuggerEventQueue queue;
	std::atomic<bool> done{ false };
	constexpr int controls = 32;
	std::thread producer([&]() {
		for (int i = 1; i <= controls * 1000; ++i) {
			// Numeric payloads mark production order; formatting is outside the queue.
			queue.Push("breakpoint", std::to_string(i));
			if (i % 1000 == 0) {
				queue.Push("stepping", std::to_string(i / 1000));
			}
		}
		done.store(true);
	});
	bool healthy = true;
	int lastHit = 0;
	int lastControl = 0;
	std::vector<DebuggerEventQueue::Event> events;
	auto drain = [&]() {
		healthy &= queue.Take(&events);
		healthy &= events.size() <= capacity;
		for (const auto &event : events) {
			const int sequence = std::stoi(event.second);
			if (std::strcmp(event.first, "breakpoint") == 0) {
				healthy &= sequence > lastHit;
				lastHit = sequence;
			} else {
				healthy &= sequence == lastControl + 1;
				lastControl = sequence;
			}
		}
	};
	while (!done.load()) {
		drain();
		std::this_thread::yield();
	}
	producer.join();
	drain();
	EXPECT_TRUE(healthy);
	EXPECT_EQ_INT(lastControl, controls);
	return true;
}

bool TestDebuggerEventQueue() {
	// These exercise the production mailbox with synthetic events, without running a PSP.
	bool passed = TestEventQueueOrder();
	passed &= TestEventQueueBreakpointPressure();
	passed &= TestEventQueueOnlyBreakpointEvicted();
	passed &= TestEventQueueControlCapacity();
	passed &= TestEventQueueControlOverflow();
	passed &= TestEventQueueConcurrentDrain();
	return passed;
}
