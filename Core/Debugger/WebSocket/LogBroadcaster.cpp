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
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Core/Debugger/WebSocket/LogBroadcaster.h"
#include "Core/Debugger/WebSocket/WebSocketUtils.h"

class DebuggerLogListener {
public:
	void Log(const LogMessage &msg) {
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
		// A source that logs faster than this listener gets polled (WebSocket.cpp's event loop,
		// up to 1000Hz under high activity) - a log-only breakpoint hit thousands of times in a
		// tight loop is a real example, see docs/VSHBootInvestigation.md - can wrap the ring
		// buffer before GetMessages() ever reads the oldest entries, silently losing them. That
		// used to just look like "my breakpoint only logged a few hits" from the client's side,
		// indistinguishable from the breakpoint genuinely not firing - synthesize a warning
		// message reporting exactly how many were lost instead of staying silent about it.
		int droppedCount = 0;
		if (read_ + BUFFER_SIZE < count_) {
			// We'll start with our oldest then.
			droppedCount = (count_ - BUFFER_SIZE) - read_;
			splitPoint = nextMessage_;
			readCount = Count();
		} else {
			// read_ counts messages ever read, so it has to be wrapped to index the ring - the
			// overflow branch above starts from nextMessage_, which already is an index. Without
			// the modulo this went wrong the moment a session logged BUFFER_SIZE messages: with
			// splitPoint >= BUFFER_SIZE the first copy loop below is empty, so the second one
			// handed back messages_[0..readCount-1] - the oldest entries in the buffer, not the
			// new ones - and every later poll stayed that far out of step. High-volume sources
			// (a log-only breakpoint in a hot loop) hit it within seconds, and the result looked
			// like the tail of the log going missing rather than being wrong.
			splitPoint = read_ % BUFFER_SIZE;
			readCount = count_ - read_;
		}

		read_ = count_;

		std::vector<LogMessage> results;
		if (droppedCount > 0) {
			LogMessage dropped;
			dropped.level = LogLevel::LWARNING;
			dropped.log = "Debugger";
			GetCurrentTimeFormatted(dropped.timestamp);
			truncate_cpy(dropped.header, "LogBroadcaster: ring buffer overflow");
			dropped.msg = StringFromFormat("%d log message(s) dropped - loop polling too slow for this volume\n", droppedCount);
			results.push_back(dropped);
		}
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

static void BroadcastCallback(const LogMessage &message, void *userdata) {
	DebuggerLogListener *listener = (DebuggerLogListener *)userdata;
	listener->Log(message);
}

LogBroadcaster::LogBroadcaster() {
	listener_ = new DebuggerLogListener();
	// One of these exists per open connection, so it registers alongside any other client's
	// rather than replacing it - see AddExternalLogCallback().
	callbackHandle_ = g_logManager.AddExternalLogCallback(&BroadcastCallback, (void *)listener_);
}

LogBroadcaster::~LogBroadcaster() {
	// Returns only once no log call is inside our callback, so the listener is safe to delete.
	g_logManager.RemoveExternalLogCallback(callbackHandle_);
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
