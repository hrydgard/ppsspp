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
#include <cstdarg>
#include <cstdio>

#include "Common/Data/Format/IniFile.h"
#include "Common/CommonFuncs.h"
#include "Common/Log.h"

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

// pure virtual interface
class LogListener {
public:
	virtual ~LogListener() {}

	virtual void Log(const LogMessage &msg) = 0;
};

class FileLogListener : public LogListener {
public:
	FileLogListener(const char *filename);
	~FileLogListener();

	void Log(const LogMessage &msg) override;

	bool IsValid() { if (!fp_) return false; else return true; }
	bool IsEnabled() const { return m_enable; }
	void SetEnabled(bool enable) { m_enable = enable; }

	const char *GetName() const { return "file"; }

private:
	std::mutex m_log_lock;
	FILE *fp_ = nullptr;
	bool m_enable;
};

class OutputDebugStringLogListener : public LogListener {
public:
	void Log(const LogMessage &msg) override;
};

class RingbufferLogListener : public LogListener {
public:
	void Log(const LogMessage &msg) override;

	bool IsEnabled() const { return enabled_; }
	void SetEnabled(bool enable) { enabled_ = enable; }

	int GetCount() const { return count_ < MAX_LOGS ? count_ : MAX_LOGS; }
	const char *TextAt(int i) const { return messages_[(curMessage_ - i - 1) & (MAX_LOGS - 1)].msg.c_str(); }
	LogLevel LevelAt(int i) const { return messages_[(curMessage_ - i - 1) & (MAX_LOGS - 1)].level; }

private:
	enum { MAX_LOGS = 128 };
	LogMessage messages_[MAX_LOGS];
	int curMessage_ = 0;
	int count_ = 0;
	bool enabled_ = false;
};

// TODO: A simple buffered log that can be used to display the log in-window
// on Android etc.
// class BufferedLogListener { ... }

struct LogChannel {
	char m_shortName[32]{};
	LogLevel level;
	bool enabled;
};

class ConsoleListener;
class StdioListener;

class LogManager {
private:
	LogManager(bool *enabledSetting);
	~LogManager();

	// Prevent copies.
	LogManager(const LogManager &) = delete;
	void operator=(const LogManager &) = delete;

	LogChannel log_[(size_t)Log::NUMBER_OF_LOGS];
	FileLogListener *fileLog_ = nullptr;
#if PPSSPP_PLATFORM(WINDOWS)
	ConsoleListener *consoleLog_ = nullptr;
#endif
	StdioListener *stdioLog_ = nullptr;
	OutputDebugStringLogListener *debuggerLog_ = nullptr;
	RingbufferLogListener *ringLog_ = nullptr;
	static LogManager *logManager_;  // Singleton. Ugh.

	std::mutex listeners_lock_;
	std::vector<LogListener*> listeners_;

public:
	void AddListener(LogListener *listener);
	void RemoveListener(LogListener *listener);

	static u32 GetMaxLevel() { return (u32)MAX_LOGLEVEL;	}
	static int GetNumChannels() { return (int)Log::NUMBER_OF_LOGS; }

	void LogLine(LogLevel level, Log type,
				 const char *file, int line, const char *fmt, va_list args);
	bool IsEnabled(LogLevel level, Log type);

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

	StdioListener *GetStdioListener() const {
		return stdioLog_;
	}

	OutputDebugStringLogListener *GetDebuggerListener() const {
		return debuggerLog_;
	}

	RingbufferLogListener *GetRingbufferListener() const {
		return ringLog_;
	}

	static inline LogManager* GetInstance() {
		return logManager_;
	}

	static void SetInstance(LogManager *logManager) {
		logManager_ = logManager;
	}

	static void Init(bool *enabledSetting);
	static void Shutdown();

	void ChangeFileLog(const char *filename);

	void SaveConfig(Section *section);
	void LoadConfig(const Section *section, bool debugDefaults);
};
