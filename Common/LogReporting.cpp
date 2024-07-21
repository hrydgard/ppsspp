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

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include "Common/LogReporting.h"

namespace Reporting {

// Keeps track of report-only-once identifiers.  Since they're always constants, a pointer is okay.
static std::unordered_map<const char *, int> logNTimes;
static std::mutex logNTimesLock;

AllowedCallback allowedCallback = nullptr;
MessageCallback messageCallback = nullptr;

bool ShouldLogNTimes(const char *identifier, int count) {
	// True if it wasn't there already -> so yes, log.
	std::lock_guard<std::mutex> lock(logNTimesLock);
	auto iter = logNTimes.find(identifier);
	if (iter == logNTimes.end()) {
		logNTimes.emplace(identifier, 1);
		return true;
	} else {
		if (iter->second >= count) {
			return false;
		} else {
			iter->second++;
			return true;
		}
	}
}

void ResetCounts() {
	std::lock_guard<std::mutex> lock(logNTimesLock);
	logNTimes.clear();
}

void SetupCallbacks(AllowedCallback allowed, MessageCallback message) {
	allowedCallback = allowed;
	messageCallback = message;
}

void ReportMessage(const char *message, ...) {
	if (!allowedCallback || !messageCallback) {
		ERROR_LOG(Log::System, "Reporting not initialized, skipping: %s", message);
		return;
	}

	if (!allowedCallback())
		return;

	const int MESSAGE_BUFFER_SIZE = 65536;
	char temp[MESSAGE_BUFFER_SIZE];

	va_list args;
	va_start(args, message);
	vsnprintf(temp, MESSAGE_BUFFER_SIZE - 1, message, args);
	temp[MESSAGE_BUFFER_SIZE - 1] = '\0';
	va_end(args);

	messageCallback(message, temp);
}

void ReportMessageFormatted(const char *message, const char *formatted) {
	if (!allowedCallback || !messageCallback) {
		ERROR_LOG(Log::System, "Reporting not initialized, skipping: %s", message);
		return;
	}

	if (!allowedCallback())
		return;
	messageCallback(message, formatted);
}

}  // namespace
