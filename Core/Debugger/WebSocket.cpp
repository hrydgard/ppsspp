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

#include <mutex>
#include <condition_variable>
#include "Common/Thread/ThreadUtil.h"
#include "Core/Debugger/WebSocket.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/MemMap.h"

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
#include "Core/Debugger/WebSocket/GPURecordSubscriber.h"
#include "Core/Debugger/WebSocket/GPUStatsSubscriber.h"
#include "Core/Debugger/WebSocket/HLESubscriber.h"
#include "Core/Debugger/WebSocket/InputSubscriber.h"
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
	&WebSocketGPURecordInit,
	&WebSocketGPUStatsInit,
	&WebSocketHLEInit,
	&WebSocketInputInit,
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

// Prevent threading surprises and obscure crashes by locking startup/shutdown.
static bool lifecycleLockSetup = false;
static std::mutex lifecycleLock;

static void UpdateConnected(int delta) {
	std::lock_guard<std::mutex> guard(stopLock);
	debuggersConnected += delta;
	stopCond.notify_all();
}

static void WebSocketNotifyLifecycle(CoreLifecycle stage) {
	// We'll likely already be locked during the reboot.
	if (PSP_IsRebooting())
		return;

	switch (stage) {
	case CoreLifecycle::STARTING:
	case CoreLifecycle::STOPPING:
	case CoreLifecycle::MEMORY_REINITING:
		if (debuggersConnected > 0) {
			DEBUG_LOG(Log::System, "Waiting for debugger to complete on shutdown");
		}
		lifecycleLock.lock();
		break;

	case CoreLifecycle::START_COMPLETE:
	case CoreLifecycle::STOPPED:
	case CoreLifecycle::MEMORY_REINITED:
		lifecycleLock.unlock();
		if (debuggersConnected > 0) {
			DEBUG_LOG(Log::System, "Debugger ready for shutdown");
		}
		break;
	}
}

static void SetupDebuggerLock() {
	if (!lifecycleLockSetup) {
		Core_ListenLifecycle(&WebSocketNotifyLifecycle);
		lifecycleLockSetup = true;
	}
}

void HandleDebuggerRequest(const http::ServerRequest &request) {
	net::WebSocketServer *ws = net::WebSocketServer::CreateAsUpgrade(request, "debugger.ppsspp.org");
	if (!ws)
		return;

	SetCurrentThreadName("Debugger");
	UpdateConnected(1);
	SetupDebuggerLock();

	WebSocketClientInfo client_info;
	auto& disallowed_config = client_info.disallowed;

	GameBroadcaster game;
	LogBroadcaster logger;
	InputBroadcaster input;
	SteppingBroadcaster stepping;

	std::unordered_map<std::string, DebuggerEventHandler> eventHandlers;
	std::vector<DebuggerSubscriber *> subscriberData;
	for (auto init : subscribers) {
		std::lock_guard<std::mutex> guard(lifecycleLock);
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

		DebuggerRequest req(event, ws, root, &client_info);
		auto eventFunc = eventHandlers.find(event);
		if (eventFunc != eventHandlers.end()) {
			std::lock_guard<std::mutex> guard(lifecycleLock);
			eventFunc->second(req);
			if (!req.Finish()) {
				// Poll more frequently for a second in case this triggers something.
				highActivity = 1000;
			}
		} else {
			req.Fail("Bad message: unknown event");
		}
	});
	ws->SetBinaryHandler([&](const std::vector<uint8_t> &d) {
		ws->Send(DebuggerErrorEvent("Bad message", LogLevel::LERROR));
	});

	while (ws->Process(highActivity ? 1.0f / 1000.0f : 1.0f / 60.0f)) {
		std::lock_guard<std::mutex> guard(lifecycleLock);
		// These send events that aren't just responses to requests

		// The client can explicitly ask not to be notified about some events
		// so we check the client settings first
		if (!disallowed_config["logger"])
			logger.Broadcast(ws);
		if (!disallowed_config["game"])
			game.Broadcast(ws);
		if (!disallowed_config["stepping"])
			stepping.Broadcast(ws);
		if (!disallowed_config["input"])
			input.Broadcast(ws);

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

	std::lock_guard<std::mutex> guard(lifecycleLock);
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
