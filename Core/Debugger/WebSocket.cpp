// Copyright (c) 2017- PPSSPP Project.

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

#include <algorithm>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>

#include "Common/Thread/ThreadUtil.h"
#include "Common/TimeUtil.h"
#include "Core/Core.h"
#include "Core/Debugger/WebSocket.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"

// This WebSocket (connected through the same port as disc sharing) allows API/debugger access to PPSSPP.
// Currently, the only subprotocol "debugger.ppsspp.org" uses a simple JSON based interface.
//
// Messages to and from PPSSPP follow the same basic format:
//    { "event": "NAME", ... }
//
// And are primarily of these types:
//  * Events from the debugger/client (you) to PPSSPP
//    If there's a response, it will generally use the same name.  It may not be immedate - it's an event.
//  * Spontaneous events from PPSSPP
//    Things like logs, breakpoint hits, etc. not directly requested.
//
// Otherwise you may see error events which indicate PPSSPP couldn't understand or failed internally:
//  - "event": "error"
//  - "message": A string describing what happened.
//  - "level": Integer severity level. (1 = NOTICE, 2 = ERROR, 3 = WARN, 4 = INFO, 5 = DEBUG, 6 = VERBOSE)
//  - "ticket": Optional, present if in response to an event with a "ticket" field, simply repeats that value.
//
// At start, please send a "version" event.  See WebSocket/GameSubscriber.cpp for more details.
//
// For other events, look inside Core/Debugger/WebSocket/ for details on each event.

#include "Core/Debugger/WebSocket/GameBroadcaster.h"
#include "Core/Debugger/WebSocket/InputBroadcaster.h"
#include "Core/Debugger/WebSocket/LogBroadcaster.h"
#include "Core/Debugger/WebSocket/SteppingBroadcaster.h"

#include "Core/Debugger/WebSocket/BreakpointSubscriber.h"
#include "Core/Debugger/WebSocket/CPUCoreSubscriber.h"
#include "Core/Debugger/WebSocket/DisasmSubscriber.h"
#include "Core/Debugger/WebSocket/GameSubscriber.h"
#include "Core/Debugger/WebSocket/GPUBufferSubscriber.h"
#include "Core/Debugger/WebSocket/GPUDisasmSubscriber.h"
#include "Core/Debugger/WebSocket/GPURecordSubscriber.h"
#include "Core/Debugger/WebSocket/GPUStatsSubscriber.h"
#include "Core/Debugger/WebSocket/HLEKernelObjectSubscriber.h"
#include "Core/Debugger/WebSocket/HLESubscriber.h"
#include "Core/Debugger/WebSocket/InputSubscriber.h"
#include "Core/Debugger/WebSocket/LogConfigSubscriber.h"
#include "Core/Debugger/WebSocket/MemoryInfoSubscriber.h"
#include "Core/Debugger/WebSocket/MemorySubscriber.h"
#include "Core/Debugger/WebSocket/ReplaySubscriber.h"
#include "Core/Debugger/WebSocket/SteppingSubscriber.h"
#include "Core/Debugger/WebSocket/ClientConfigSubscriber.h"

typedef DebuggerSubscriber *(*SubscriberInit)(DebuggerEventHandlerMap &map);
static const std::vector<SubscriberInit> subscribers({
	&WebSocketBreakpointInit,
	&WebSocketCPUCoreInit,
	&WebSocketDisasmInit,
	&WebSocketGameInit,
	&WebSocketGPUBufferInit,
	&WebSocketGPUDisasmInit,
	&WebSocketGPURecordInit,
	&WebSocketGPUStatsInit,
	&WebSocketHLEKernelObjectInit,
	&WebSocketHLEInit,
	&WebSocketInputInit,
	&WebSocketLogConfigInit,
	&WebSocketMemoryInfoInit,
	&WebSocketMemoryInit,
	&WebSocketReplayInit,
	&WebSocketSteppingInit,
	&WebSocketClientConfigInit,
});

// To handle webserver restart, keep track of how many running.
static volatile int debuggersConnected = 0;
static volatile bool stopRequested = false;
static std::mutex stopLock;
static std::condition_variable stopCond;

// There is deliberately no lock guarding debugger handlers against the core being started or torn
// down under them: every handler either does its emulator-state access inside Core_RunOnCPUThread()
// (so it's serialized with startup/shutdown, which also run on the CPU thread), or only touches
// state that carries its own lock - the log ring buffer, ctrlMutex, GPUStepping's rendezvous.
// The lock that used to be here had to be held across a whole handler, including the blocking wait
// inside Core_RunOnCPUThread(), which deadlocked against the CPU thread taking it on STOPPING.

static void UpdateConnected(int delta) {
	std::lock_guard<std::mutex> guard(stopLock);
	debuggersConnected += delta;
	stopCond.notify_all();
}

// Per-connection mailbox for events the CPU thread produces (cpu.stepping, game.start, ...).
//
// These used to be polled per connection from the WebSocket thread, which meant every connected
// debugger was reading pc, the tick count, the UI state and the param SFO out from under the CPU
// thread on every lap of its loop. Now the CPU thread notices the transition once, formats the
// event, and drops it in here; the connection's own thread just drains and sends.
// A log-only breakpoint in a hot loop can produce events far faster than a connection drains them
// (the drain runs once per lap of ws->Process, so at best a few hundred times a second). Without a
// cap the queue grows without bound and the connection falls further and further behind. Dropping
// is the only sane answer; cpu.breakpoint.hit carries a sequence number so a client can tell
// exactly how many it missed rather than silently believing it saw everything.
static constexpr size_t MAX_PENDING_EVENTS = 4096;

struct DebuggerEventSink {
	std::mutex lock;
	std::vector<std::pair<const char *, std::string>> pending;
	// A debugger that connects while the CPU is already stopped still wants to hear about it.
	bool needsSteppingPrime = true;

	void Push(const char *category, std::string json) {
		std::lock_guard<std::mutex> guard(lock);
		if (pending.size() >= MAX_PENDING_EVENTS)
			return;
		pending.emplace_back(category, std::move(json));
	}

	void Take(std::vector<std::pair<const char *, std::string>> *out) {
		std::lock_guard<std::mutex> guard(lock);
		out->swap(pending);
		pending.clear();
	}
};

static std::mutex g_sinkLock;
static std::vector<DebuggerEventSink *> g_sinks;
// Mirrors g_sinks.size() so the breakpoint path can check "is anyone listening" with one relaxed
// load, instead of taking g_sinkLock on every single breakpoint hit.
static std::atomic<int> g_sinkCount{ 0 };

static void RegisterSink(DebuggerEventSink *sink) {
	std::lock_guard<std::mutex> guard(g_sinkLock);
	g_sinks.push_back(sink);
	g_sinkCount.store((int)g_sinks.size(), std::memory_order_relaxed);
}

static void UnregisterSink(DebuggerEventSink *sink) {
	std::lock_guard<std::mutex> guard(g_sinkLock);
	g_sinks.erase(std::remove(g_sinks.begin(), g_sinks.end(), sink), g_sinks.end());
	g_sinkCount.store((int)g_sinks.size(), std::memory_order_relaxed);
}

bool WebSocketDebuggerHasClients() {
	return g_sinkCount.load(std::memory_order_relaxed) != 0;
}

void WebSocketNotifyBreakpointHit(const BreakpointHit &hit) {
	// Counts hits produced, not hits delivered, so a gap in what a client receives tells it how
	// many were dropped by the cap in Push().
	static uint64_t g_hitSequence = 0;

	std::lock_guard<std::mutex> guard(g_sinkLock);
	if (g_sinks.empty())
		return;

	// Formatted once here on the CPU thread, then shared - same rule as the other pushed events:
	// a connection's own thread must never be the one reading emulator state.
	JsonWriter j;
	j.begin();
	j.writeString("event", "cpu.breakpoint.hit");
	j.writeFloat("sequence", (double)++g_hitSequence);
	WriteBreakpointHit(j, hit);
	j.end();
	const std::string json = j.str();

	for (DebuggerEventSink *sink : g_sinks)
		sink->Push("breakpoint", json);
}

void WebSocketDebuggerTick() {
	// Poll unconditionally, even with nothing connected: these track transitions, and skipping them
	// would let the "previous" state go stale and fire a bogus event at whoever connects next.
	const std::string gameEvent = GameBroadcaster::PollChange();
	const std::string steppingEvent = SteppingBroadcaster::PollChange();

	std::lock_guard<std::mutex> guard(g_sinkLock);
	if (g_sinks.empty())
		return;

	std::string steppingPrime;
	for (DebuggerEventSink *sink : g_sinks) {
		if (sink->needsSteppingPrime) {
			sink->needsSteppingPrime = false;
			// Only format it if somebody actually needs it.
			if (steppingPrime.empty())
				steppingPrime = SteppingBroadcaster::CurrentState();
			if (!steppingPrime.empty())
				sink->Push("stepping", steppingPrime);
			continue;
		}
		if (!gameEvent.empty())
			sink->Push("game", gameEvent);
		if (!steppingEvent.empty())
			sink->Push("stepping", steppingEvent);
	}
}

void HandleDebuggerRequest(const http::ServerRequest &request) {
	SetCurrentThreadName("WebSocketDebugger");

	net::WebSocketServer *ws = net::WebSocketServer::CreateAsUpgrade(request, "debugger.ppsspp.org");
	if (!ws) {
		return;
	}

	UpdateConnected(1);

	WebSocketClientInfo client_info;
	auto& disallowed_config = client_info.disallowed;
	// Seed every broadcaster category. broadcast.config.set only accepts keys that already exist
	// here (so a typo is rejected rather than silently ignored), and these otherwise only appear
	// as a side effect of operator[] the first time each category actually broadcasts - which
	// meant "game" and "stepping" were rejected as unsupported until one happened to fire, even
	// though they're documented and valid. Keep in sync with the Broadcast calls further down.
	for (const char *category : { "logger", "input", "game", "stepping", "breakpoint" })
		disallowed_config[category] = false;

	LogBroadcaster logger;
	InputBroadcaster input;

	DebuggerEventSink sink;
	RegisterSink(&sink);

	DebuggerEventHandlerMap eventHandlers;
	std::vector<DebuggerSubscriber *> subscriberData;
	for (auto init : subscribers) {
		subscriberData.push_back(init(eventHandlers));
	}

	// There's a tradeoff between responsiveness to incoming events, and polling for changes.
	int highActivity = 0;
	ws->SetTextHandler([&](const std::string &t) {
		JsonReader reader(t.c_str(), t.size());
		if (!reader.ok()) {
			ws->Send(DebuggerErrorEvent("Bad message: invalid JSON", LogLevel::LERROR));
			return;
		}

		const JsonGet root = reader.root();
		const char *event = root ? root.getStringOr("event", nullptr) : nullptr;
		if (!event) {
			ws->Send(DebuggerErrorEvent("Bad message: no event property", LogLevel::LERROR, root));
			return;
		}

		DEBUG_LOG(Log::Debugger, "WS: Handling '%s'", event);

		DebuggerRequest req(event, ws, root, &client_info);
		auto eventFunc = eventHandlers.find(event);
		if (eventFunc != eventHandlers.end()) {
			eventFunc->second(req);
			if (!req.Finish()) {
				// The handler arranged something that finishes later - a step, a resume, a stats
				// feed - rather than answering now. A client that asked for it gets told so, so it
				// can tell "accepted, wait for the event" from "dropped on the floor" without
				// carrying a hardcoded list of the events that don't answer. Everyone else sees
				// exactly what they saw before; see client.config.set for why it can't be the
				// default.
				if (client_info.acknowledgeDeferred)
					ws->Send(DebuggerDeferredEvent(event, root));
				// Poll more frequently for a second in case this triggers something.
				highActivity = 1000;
			}
		} else {
			req.Fail("Bad message: unknown event");
		}
	});

	ws->SetBinaryHandler([&](const std::vector<uint8_t> &d) {
		ERROR_LOG(Log::Debugger, "Received binary WebSocket frame, not supported");
		ws->Send(DebuggerErrorEvent("Bad message: binary WebSocket frames are not supported", LogLevel::LERROR));
	});

	// Don't out-line the highActivity check, it needs to recompute on every lap.
	constexpr float lowActivityPollTimeStep = 1.0f / 60.0f;
	constexpr float highActivityPollTimeStep = 1.0f / 1000.0f;
	while (ws->Process(highActivity ? highActivityPollTimeStep : lowActivityPollTimeStep)) {
		// These send events that aren't just responses to requests

		// The client can explicitly ask not to be notified about some events
		// so we check the client settings first
		if (!disallowed_config["logger"])
			logger.Broadcast(ws);
		if (!disallowed_config["input"])
			input.Broadcast(ws);

		// Whatever the CPU thread queued up for us since last lap.
		std::vector<std::pair<const char *, std::string>> events;
		sink.Take(&events);
		for (const auto &ev : events) {
			if (!disallowed_config[ev.first])
				ws->Send(ev.second);
		}

		for (size_t i = 0; i < subscribers.size(); ++i) {
			if (subscriberData[i]) {
				subscriberData[i]->Broadcast(ws);
			}
		}

		if (stopRequested) {
			ws->Close(net::WebSocketClose::GOING_AWAY);
		}

		if (highActivity > 0) {
			highActivity--;
		}
	}

	UnregisterSink(&sink);

	for (size_t i = 0; i < subscribers.size(); ++i) {
		delete subscriberData[i];
	}

	delete ws;
	request.In()->Discard();
	UpdateConnected(-1);
}

void StopAllDebuggers() {
	std::unique_lock<std::mutex> guard(stopLock);
	while (debuggersConnected != 0) {
		stopRequested = true;
		stopCond.wait(guard);
	}

	// Reset it back for next time.
	stopRequested = false;
}
