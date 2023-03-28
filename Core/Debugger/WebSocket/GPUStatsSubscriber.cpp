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

#include <mutex>
#include <vector>
#include "Core/Debugger/WebSocket/GPUStatsSubscriber.h"
#include "Core/HW/Display.h"
#include "Core/System.h"

struct CollectedStats {
	float vps;
	float fps;
	float actual_fps;
	char statbuf[4096];
	std::vector<double> frameTimes;
	std::vector<double> sleepTimes;
	int frameTimePos;
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
		j.pushDict("fps");
		j.writeFloat("actual", s.actual_fps);
		j.writeFloat("target", s.fps);
		j.pop();
		j.pushDict("vblanksPerSecond");
		j.writeFloat("actual", s.vps);
		j.writeFloat("target", 60.0 / 1.001);
		j.pop();
		j.writeString("info", s.statbuf);
		j.pushDict("timing");
		j.pushArray("frames");
		for (double t : s.frameTimes)
			j.writeFloat(t);
		j.pop();
		j.pushArray("sleep");
		for (double t : s.sleepTimes)
			j.writeFloat(t);
		j.pop();
		j.writeInt("pos", s.frameTimePos);
		j.pop();
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
	bool forced_ = false;
	bool sendNext_ = false;
	bool sendFeed_ = false;

	std::string lastTicket_;
	std::mutex pendingLock_;
	std::vector<CollectedStats> pendingStats_;
};

DebuggerSubscriber *WebSocketGPUStatsInit(DebuggerEventHandlerMap &map) {
	auto p = new WebSocketGPUStatsState();
	map["gpu.stats.get"] = std::bind(&WebSocketGPUStatsState::Get, p, std::placeholders::_1);
	map["gpu.stats.feed"] = std::bind(&WebSocketGPUStatsState::Feed, p, std::placeholders::_1);

	return p;
}

WebSocketGPUStatsState::WebSocketGPUStatsState() {
	__DisplayListenFlip(&WebSocketGPUStatsState::FlipForwarder, this);
}

WebSocketGPUStatsState::~WebSocketGPUStatsState() {
	if (forced_)
		Core_ForceDebugStats(false);
	__DisplayForgetFlip(&WebSocketGPUStatsState::FlipForwarder, this);
}

void WebSocketGPUStatsState::FlipForwarder(void *thiz) {
	WebSocketGPUStatsState *p = (WebSocketGPUStatsState *)thiz;
	p->FlipListener();
}

void WebSocketGPUStatsState::FlipListener() {
	if (!sendNext_ && !sendFeed_)
		return;

	// Okay, collect the data (we'll actually send at next Broadcast.)
	std::lock_guard<std::mutex> guard(pendingLock_);
	pendingStats_.resize(pendingStats_.size() + 1);
	CollectedStats &stats = pendingStats_[pendingStats_.size() - 1];

	__DisplayGetFPS(&stats.vps, &stats.fps, &stats.actual_fps);
	__DisplayGetDebugStats(stats.statbuf, sizeof(stats.statbuf));

	int valid;
	double *sleepHistory;
	double *history = __DisplayGetFrameTimes(&valid, &stats.frameTimePos, &sleepHistory);

	stats.frameTimes.resize(valid);
	stats.sleepTimes.resize(valid);
	memcpy(&stats.frameTimes[0], history, sizeof(double) * valid);
	memcpy(&stats.sleepTimes[0], sleepHistory, sizeof(double) * valid);

	sendNext_ = false;
}

// Get next GPU stats (gpu.stats.get)
//
// No parameters.
//
// Response (same event name):
//  - fps: object with "actual" and "target" properties, representing frames per second.
//  - vblanksPerSecond: object with "actual" and "target" properties, for vblank cycles.
//  - info: string, representation of backend-dependent statistics.
//  - timing: object with properties:
//     - frames: array of numbers, each representing the time taken for a frame.
//     - sleep: array of numbers, each representing the delay time waiting for next frame.
//     - pos: number, index of the current frame (not always last.)
//
// Note: stats are returned after the next flip completes (paused if CPU or GPU in break.)
// Note: info and timing may not be accurate if certain settings are disabled.
void WebSocketGPUStatsState::Get(DebuggerRequest &req) {
	if (!PSP_IsInited())
		return req.Fail("CPU not started");

	std::lock_guard<std::mutex> guard(pendingLock_);
	sendNext_ = true;

	const JsonNode *value = req.data.get("ticket");
	lastTicket_ = value ? json_stringify(value) : "";
}

// Setup GPU stats feed (gpu.stats.feed)
//
// Parameters:
//  - enable: optional boolean, pass false to stop the feed.
//
// No immediate response.  Events sent each frame (as gpu.stats.get.)
//
// Note: info and timing will be accurate after the first frame.
void WebSocketGPUStatsState::Feed(DebuggerRequest &req) {
	if (!PSP_IsInited())
		return req.Fail("CPU not started");
	bool enable = true;
	if (!req.ParamBool("enable", &enable, DebuggerParamType::OPTIONAL))
		return;

	std::lock_guard<std::mutex> guard(pendingLock_);
	sendFeed_ = enable;
	if (forced_ != enable) {
		Core_ForceDebugStats(enable);
		forced_ = enable;
	}
}

void WebSocketGPUStatsState::Broadcast(net::WebSocketServer *ws) {
	std::lock_guard<std::mutex> guard(pendingLock_);
	if (lastTicket_.empty() && !sendFeed_) {
		pendingStats_.clear();
		return;
	}

	// To be safe, make sure we only send one if we're doing a get.
	if (!sendFeed_ && pendingStats_.size() > 1)
		pendingStats_.resize(1);

	for (size_t i = 0; i < pendingStats_.size(); ++i) {
		ws->Send(DebuggerGPUStatsEvent{ pendingStats_[i], lastTicket_ });
		lastTicket_.clear();
	}
	pendingStats_.clear();
}
