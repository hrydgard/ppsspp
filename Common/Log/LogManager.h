// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

#include "ppsspp_config.h"

#include <mutex>
#include <vector>
#include <cstdio>

#include "Common/Common.h"
#include "Common/CommonFuncs.h"
#include "Common/Log.h"
#include "Common/File/Path.h"

#define	MAX_MESSAGES 8000   

extern const char *hleCurrentThreadName;

// Struct that listeners can output how they want. For example, on Android we don't want to add
// timestamp or write the level as a string, those already exist.
struct LogMessage {
	char timestamp[16];
	char header[64];  // Filename/thread/etc. in front.
	LogLevel level;
	const char *log;
	std::string msg;  // The actual log message.
};

enum class LogOutput {
	Stdio = (1 << 0),
	DebugString = (1 << 1),
	RingBuffer = (1 << 2),
	File = (1 << 3),
	WinConsole = (1 << 4),
	Printf = (1 << 5),
	ExternalCallback = (1 << 6),
};
ENUM_CLASS_BITOPS(LogOutput);

class RingbufferLog {
public:
	void Log(const LogMessage &msg);

	int GetCount() const { return count_ < MAX_LOGS ? count_ : MAX_LOGS; }
	const char *TextAt(int i) const { return messages_[(curMessage_ - i - 1) & (MAX_LOGS - 1)].msg.c_str(); }
	LogLevel LevelAt(int i) const { return messages_[(curMessage_ - i - 1) & (MAX_LOGS - 1)].level; }

	void Clear() {
		curMessage_ = 0;
		count_ = 0;
	}

private:
	enum { MAX_LOGS = 128 };
	LogMessage messages_[MAX_LOGS];
	int curMessage_ = 0;
	int count_ = 0;
};

struct LogChannel {
#if defined(_DEBUG)
	LogLevel level = LogLevel::LDEBUG;
#else
	LogLevel level = LogLevel::LDEBUG;
#endif
	bool enabled = true;
};

class Section;
class ConsoleListener;

typedef void (*LogCallback)(const LogMessage &message, void *userdata);

class LogManager {
public:
	LogManager();
	~LogManager();

	void SetOutputsEnabled(LogOutput outputs);
	LogOutput GetOutputsEnabled() const {
		return outputs_;
	}
	void EnableOutput(LogOutput output) {
		SetOutputsEnabled(outputs_ | output);
	}
	void DisableOutput(LogOutput output) {
		LogOutput temp = outputs_;
		temp &= ~output;
		SetOutputsEnabled(temp);
	}

	static u32 GetMaxLevel() { return (u32)MAX_LOGLEVEL;	}
	static int GetNumChannels() { return (int)Log::NUMBER_OF_LOGS; }

	void LogLine(LogLevel level, Log type,
				 const char *file, int line, const char *fmt, va_list args);

	bool IsEnabled(LogLevel level, Log type) const {
		const LogChannel &log = log_[(size_t)type];
		if (level > log.level || !log.enabled)
			return false;
		return true;
	}

	LogChannel *GetLogChannel(Log type) {
		return &log_[(size_t)type];
	}

	void SetLogLevel(Log type, LogLevel level) {
		log_[(size_t)type].level = level;
	}

	void SetAllLogLevels(LogLevel level) {
		for (int i = 0; i < (int)Log::NUMBER_OF_LOGS; ++i) {
			log_[i].level = level;
		}
	}

	void SetEnabled(Log type, bool enable) {
		log_[(size_t)type].enabled = enable;
	}

	LogLevel GetLogLevel(Log type) {
		return log_[(size_t)type].level;
	}

#if PPSSPP_PLATFORM(WINDOWS)
	ConsoleListener *GetConsoleListener() const {
		return consoleLog_;
	}
#endif

	const RingbufferLog *GetRingbuffer() const {
		return &ringLog_;
	}

	void Init(bool *enabledSetting, bool headless = false);
	void Shutdown();

	void SetExternalLogCallback(LogCallback callback, void *userdata) {
		externalCallback_ = callback;
		externalUserData_ = userdata;
	}

	void ChangeFileLog(const Path &filename);

	void SaveConfig(Section *section);
	void LoadConfig(const Section *section, bool debugDefaults);

	static const char *GetLogTypeName(Log type);

private:
	// Prevent copies.
	LogManager(const LogManager &) = delete;
	void operator=(const LogManager &) = delete;

	bool initialized_ = false;

	LogChannel log_[(size_t)Log::NUMBER_OF_LOGS];
#if PPSSPP_PLATFORM(WINDOWS)
	ConsoleListener *consoleLog_ = nullptr;
#endif
	// Stdio logging
	void StdioLog(const LogMessage &message);
	std::mutex stdioLock_;
	bool stdioUseColor_ = true;

	LogOutput outputs_ = (LogOutput)0;

	// File logging
	std::mutex logFileLock_;
	FILE *fp_ = nullptr;
	bool logFileOpenFailed_ = false;
	Path logFilename_;

	// Ring buffer
	RingbufferLog ringLog_;

	// Callback
	LogCallback externalCallback_ = nullptr;
	void *externalUserData_ = nullptr;
};

extern LogManager g_logManager;
