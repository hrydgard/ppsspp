// Copyright (c) 2018- PPSSPP Project.

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

#include "Common/Data/Encoding/Base64.h"
#include "Common/File/FileUtil.h"
#include "Core/Debugger/WebSocket/GPURecordSubscriber.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"
#include "Core/System.h"
#include "GPU/Debugger/Record.h"

struct WebSocketGPURecordState : public DebuggerSubscriber {
	~WebSocketGPURecordState();
	void Dump(DebuggerRequest &req);

	void Broadcast(net::WebSocketServer *ws) override;

protected:
	bool pending_ = false;
	std::string lastTicket_;
	Path lastFilename_;
};

DebuggerSubscriber *WebSocketGPURecordInit(DebuggerEventHandlerMap &map) {
	auto p = new WebSocketGPURecordState();
	map["gpu.record.dump"] = std::bind(&WebSocketGPURecordState::Dump, p, std::placeholders::_1);

	return p;
}

WebSocketGPURecordState::~WebSocketGPURecordState() {
	// Clear the callback to hopefully avoid a crash.
	if (pending_)
		GPURecord::ClearCallback();
}

// Begin recording (gpu.record.dump)
//
// No parameters.
//
// Response (same event name):
//  - uri: data: URI containing debug dump data.
//
// Note: recording may take a moment.
void WebSocketGPURecordState::Dump(DebuggerRequest &req) {
	if (!PSP_IsInited()) {
		return req.Fail("CPU not started");
	}

	bool result = GPURecord::RecordNextFrame([=](const Path &filename) {
		lastFilename_ = filename;
		pending_ = false;
	});

	if (!result) {
		return req.Fail("Recording already in progress");
	}

	pending_ = true;

	const JsonNode *value = req.data.get("ticket");
	lastTicket_ = value ? json_stringify(value) : "";
}

// This handles the asynchronous gpu.record.dump response.
void WebSocketGPURecordState::Broadcast(net::WebSocketServer *ws) {
	if (!lastFilename_.empty()) {
		FILE *fp = File::OpenCFile(lastFilename_, "rb");
		if (!fp) {
			lastFilename_.clear();
			return;
		}

		// We write directly to the stream since this is a large chunk of data.
		ws->AddFragment(false, R"({"event":"gpu.record.dump")");
		if (!lastTicket_.empty()) {
			ws->AddFragment(false, R"(,"ticket":)");
			ws->AddFragment(false, lastTicket_);
		}
		ws->AddFragment(false, R"(,"uri":"data:application/octet-stream;base64,)");

		// Divisible by 3 for base64 reasons.
		const size_t BUF_SIZE = 16383;
		std::vector<uint8_t> buf;
		buf.resize(BUF_SIZE);
		while (!feof(fp)) {
			size_t bytes = fread(&buf[0], 1, BUF_SIZE, fp);
			ws->AddFragment(false, Base64Encode(&buf[0], bytes));
		}
		fclose(fp);

		ws->AddFragment(true, R"("})");

		lastFilename_.clear();
		lastTicket_.clear();
	}
}
