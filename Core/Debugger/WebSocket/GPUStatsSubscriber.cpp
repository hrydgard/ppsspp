// Copyright (c) 2021- PPSSPP Project.

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


#include <retro_atomic.h>

#include "Common/Data/Text/StringWriter.h"
#include "Common/Thread/MpscQueue.h"
#include "Core/Debugger/WebSocket/GPUStatsSubscriber.h"
#include "Core/Core.h"
#include "Core/HW/Display.h"
#include "Core/System.h"

struct CollectedStats {
	char statbuf[4096];
};

struct DebuggerGPUStatsEvent {
	const CollectedStats &s;
	const std::string &ticket;

	operator std::string() {
		JsonWriter j;
		j.begin();
		j.writeString("event", "gpu.stats.get");
		if (!ticket.empty())
			j.writeRaw("ticket", ticket);
		j.writeString("info", s.statbuf);
		j.end();
		return j.str();
	}
};

struct WebSocketGPUStatsState : public DebuggerSubscriber {
	WebSocketGPUStatsState();
	~WebSocketGPUStatsState();
	void Get(DebuggerRequest &req);
	void Feed(DebuggerRequest &req);

	void Broadcast(net::WebSocketServer *ws) override;

	static void FlipForwarder(void *thiz);
	void FlipListener();

protected:
	// The WebSocket thread's.
	bool forced_ = false;
	std::string lastTicket_;
	// Set by the WebSocket thread, read on the emu thread at each flip.
	retro_atomic_int_t sendNext_{ 0 };
	retro_atomic_int_t sendFeed_{ 0 };
	// Collected on the emu thread, sent from the WebSocket thread.
	MpscQueue<CollectedStats> pendingStats_;
};

DebuggerSubscriber *WebSocketGPUStatsInit(DebuggerEventHandlerMap &map) {
	auto p = new WebSocketGPUStatsState();
	map["gpu.stats.get"] = [p](DebuggerRequest &req) { p->Get(req); };
	map["gpu.stats.feed"] = [p](DebuggerRequest &req) { p->Feed(req); };

	return p;
}

WebSocketGPUStatsState::WebSocketGPUStatsState() {
	// The flip listeners belong to the CPU thread.
	Core_RunOnCPUThread([this] { __DisplayListenFlip(&WebSocketGPUStatsState::FlipForwarder, this); });
}

WebSocketGPUStatsState::~WebSocketGPUStatsState() {
	// PSP_ForceDebugStats bumps a plain counter, so do it on the CPU thread that owns it - see
	// Core_RunOnCPUThread() in Core.h.
	const bool forced = forced_;
	Core_RunOnCPUThread([this, forced] {
		if (forced) {
			PSP_ForceDebugStats(false);
		}
		__DisplayForgetFlip(&WebSocketGPUStatsState::FlipForwarder, this);
	});
}

void WebSocketGPUStatsState::FlipForwarder(void *thiz) {
	WebSocketGPUStatsState *p = (WebSocketGPUStatsState *)thiz;
	p->FlipListener();
}

void WebSocketGPUStatsState::FlipListener() {
	if (!retro_atomic_load_acquire_int(&sendNext_) && !retro_atomic_load_acquire_int(&sendFeed_))
		return;

	// Okay, collect the data (we'll actually send at next Broadcast.)
	CollectedStats stats;

	StringWriter w(stats.statbuf);
	__DisplayGetDebugStats(w);

	pendingStats_.Push(std::move(stats));
	retro_atomic_store_release_int(&sendNext_, 0);
	mailbox->Wake();
}

// Get next GPU stats (gpu.stats.get)
//
// No parameters.
//
// Response (same event name):
//  - info: string, representation of backend-dependent statistics.
// Host FPS and sleep histories are owned by the frontend.
//
// Note: stats are returned after the next flip completes (paused if CPU or GPU in break.)
// Note: info may not be accurate if certain settings are disabled.
// Note: sending this event with no ticket will not trigger a response! (TODO: maybe fix this?)
void WebSocketGPUStatsState::Get(DebuggerRequest &req) {
	if (PSP_GetBootState() != BootState::Complete)
		return req.Fail("CPU not started");

	const JsonNode *value = req.data.get("ticket");
	lastTicket_ = value ? json_stringify(value) : "";
	retro_atomic_store_release_int(&sendNext_, 1);
}

// Setup GPU stats feed (gpu.stats.feed)
//
// Parameters:
//  - enable: optional boolean, pass false to stop the feed.
//
// No immediate response (only a "deferred" event, if the client asked for those via
// client.config.set).  Events sent each frame (as gpu.stats.get.)
//
// Note: info will be accurate after the first frame.
void WebSocketGPUStatsState::Feed(DebuggerRequest &req) {
	if (PSP_GetBootState() != BootState::Complete)
		return req.Fail("CPU not started");
	bool enable = true;
	if (!req.ParamBool("enable", &enable, DebuggerParamType::OPTIONAL))
		return;

	retro_atomic_store_release_int(&sendFeed_, enable ? 1 : 0);
	if (forced_ != enable) {
		Core_RunOnCPUThread([enable] { PSP_ForceDebugStats(enable); });
		forced_ = enable;
	}
}

void WebSocketGPUStatsState::Broadcast(net::WebSocketServer *ws) {
	if (pendingStats_.Empty()) {
		return;
	}
	std::vector<CollectedStats> pending;
	pendingStats_.Drain([&](CollectedStats &&stats) {
		pending.push_back(std::move(stats));
	});

	const bool sendFeed = retro_atomic_load_acquire_int(&sendFeed_) != 0;
	if (lastTicket_.empty() && !sendFeed) {
		return;
	}

	// To be safe, make sure we only send one if we're doing a get.
	if (!sendFeed && pending.size() > 1)
		pending.resize(1);

	for (size_t i = 0; i < pending.size(); ++i) {
		ws->Send(DebuggerGPUStatsEvent{ pending[i], lastTicket_ });
		lastTicket_.clear();
	}
}
