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

#include <algorithm>
#include <mutex>
#include "Common/Log/LogManager.h"
#include "Core/Debugger/WebSocket/LogBroadcaster.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"

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
	enum { BUFFER_SIZE = 1024 };
	LogMessage messages_[BUFFER_SIZE];
	std::mutex lock_;
	int nextMessage_ = 0;
	int count_ = 0;
	int read_ = 0;
};

LogBroadcaster::LogBroadcaster() {
	listener_ = new DebuggerLogListener();
	if (LogManager::GetInstance())
		LogManager::GetInstance()->AddListener(listener_);
}

LogBroadcaster::~LogBroadcaster() {
	if (LogManager::GetInstance())
		LogManager::GetInstance()->RemoveListener(listener_);
	delete listener_;
}

struct DebuggerLogEvent {
	const LogMessage &l;

	operator std::string() {
		JsonWriter j;
		j.begin();
		j.writeString("event", "log");
		j.writeString("timestamp", l.timestamp);
		j.writeString("header", l.header);
		j.writeString("message", l.msg);
		j.writeInt("level", (int)l.level);
		j.writeString("channel", l.log);
		j.end();
		return j.str();
	}
};

// Log message (log)
//
// Sent unexpectedly with these properties:
//  - timestamp: string timestamp of event.
//  - header: string header information about the event (including file/line.)
//  - message: actual log message as a string.
//  - level: number severity level (1 = highest.)
//  - channel: string describing log channel / grouping.
void LogBroadcaster::Broadcast(net::WebSocketServer *ws) {
	auto messages = listener_->GetMessages();
	for (auto msg : messages) {
		ws->Send(DebuggerLogEvent{msg});
	}
}
