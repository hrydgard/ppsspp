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

#include "Core/Debugger/MemBlockInfo.h"
#include "Core/Debugger/WebSocket/MemoryInfoSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"

class WebSocketMemoryInfoState : public DebuggerSubscriber {
public:
	WebSocketMemoryInfoState() {
	}
	~WebSocketMemoryInfoState() override {
		UpdateOverride(false);
	}

	void Config(DebuggerRequest &req);

protected:
	void UpdateOverride(bool flag);

	bool detailOverride_ = false;
};

DebuggerSubscriber *WebSocketMemoryInfoInit(DebuggerEventHandlerMap &map) {
	auto p = new WebSocketMemoryInfoState();
	map["memory.info.config"] = std::bind(&WebSocketMemoryInfoState::Config, p, std::placeholders::_1);

	return p;
}

void WebSocketMemoryInfoState::UpdateOverride(bool flag) {
	if (detailOverride_ && !flag)
		MemBlockReleaseDetailed();
	if (!detailOverride_ && flag)
		MemBlockOverrideDetailed();
	detailOverride_ = flag;
}

// Update memory info tracking config (memory.info.config)
//
// Parameters:
//  - detailed: optional, boolean to force enable detailed tracking (perf impact.)
//
// Response (same event name):
//  - detailed: boolean state of tracking before any changes.
//
// Note: Even if you set false, may stay enabled if set by user or another debug session.
void WebSocketMemoryInfoState::Config(DebuggerRequest &req) {
	bool setDetailed = req.HasParam("detailed");
	bool detailed = false;
	if (!req.ParamBool("detailed", &detailed, DebuggerParamType::OPTIONAL))
		return;

	JsonWriter &json = req.Respond();
	json.writeBool("detailed", MemBlockInfoDetailed());

	if (setDetailed)
		UpdateOverride(detailed);
}
