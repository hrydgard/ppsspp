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
#include <string>
#include "json/json_writer.h"
#include "net/websocket_server.h"
#include "Core/Debugger/WebSocket.h"
#include "Common/LogManager.h"

// TODO: Move this to its own file?
class DebuggerLogListener : public LogListener {
public:
	void Log(const LogMessage &msg) override {
		std::lock_guard<std::mutex> guard(lock_);
		messages_[nextMessage_] = msg;
		nextMessage_++;
		if (nextMessage_ >= BUFFER_SIZE)
			nextMessage_ -= BUFFER_SIZE;
		count_++;
	}

	std::vector<LogMessage> GetMessages() {
		std::lock_guard<std::mutex> guard(lock_);
		int splitPoint;
		int readCount;
		if (read_ + BUFFER_SIZE < count_) {
			// We'll start with our oldest then.
			splitPoint = nextMessage_;
			readCount = Count();
		} else {
			splitPoint = read_;
			readCount = count_ - read_;
		}

		read_ = count_;

		std::vector<LogMessage> results;
		int splitEnd = std::min(splitPoint + readCount, (int)BUFFER_SIZE);
		for (int i = splitPoint; i < splitEnd; ++i) {
			results.push_back(messages_[i]);
			readCount--;
		}
		for (int i = 0; i < readCount; ++i) {
			results.push_back(messages_[i]);
		}

		return results;
	}

	int Count() const {
		return count_ < BUFFER_SIZE ? count_ : BUFFER_SIZE;
	}

private:
	enum { BUFFER_SIZE = 128 };
	LogMessage messages_[BUFFER_SIZE];
	std::mutex lock_;
	int nextMessage_ = 0;
	int count_ = 0;
	int read_ = 0;
};

struct DebuggerLogEvent {
	std::string header;
	std::string message;
	int level;
	const char *channel;

	operator std::string() {
		JsonWriter j;
		j.begin();
		j.writeString("event", "log");
		j.writeString("header", header);
		j.writeString("message", message);
		j.writeInt("level", level);
		j.writeString("channel", channel);
		j.end();
		return j.str();
	}
};

void HandleDebuggerRequest(const http::Request &request) {
	net::WebSocketServer *ws = net::WebSocketServer::CreateAsUpgrade(request, "debugger.ppsspp.org");
	if (!ws)
		return;

	DebuggerLogListener *logListener = new DebuggerLogListener();
	if (LogManager::GetInstance())
		LogManager::GetInstance()->AddListener(logListener);

	// TODO: Handle incoming messages.
	ws->SetTextHandler([&](const std::string &t) {
		ws->Send(R"({"event":"error","message":"Bad message","level":2})");
	});
	ws->SetBinaryHandler([&](const std::vector<uint8_t> &d) {
		ws->Send(R"({"event":"error","message":"Bad message","level":2})");
	});

	while (ws->Process(0.1f)) {
		auto messages = logListener->GetMessages();
		// TODO: Check for other conditions?
		for (auto msg : messages) {
			ws->Send(DebuggerLogEvent{msg.header, msg.msg, msg.level, msg.log});
		}
		continue;
	}

	if (LogManager::GetInstance())
		LogManager::GetInstance()->RemoveListener(logListener);
	delete logListener;
	delete ws;
}
