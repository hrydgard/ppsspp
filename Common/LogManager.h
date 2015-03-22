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

#include "Log.h"
#include "StringUtils.h"
#include "FileUtil.h"
#include "file/ini_file.h" 

#include <set>
#include "StdMutex.h"

#define	MAX_MESSAGES 8000   
#define MAX_MSGLEN  1024

extern const char *hleCurrentThreadName;

// pure virtual interface
class LogListener {
public:
	virtual ~LogListener() {}

	virtual void Log(LogTypes::LOG_LEVELS, const char *msg) = 0;
};

class FileLogListener : public LogListener {
public:
	FileLogListener(const char *filename);

	void Log(LogTypes::LOG_LEVELS, const char *msg);

	bool IsValid() { if (!m_logfile) return false; else return true; }
	bool IsEnabled() const { return m_enable; }
	void SetEnable(bool enable) { m_enable = enable; }

	const char* GetName() const { return "file"; }

private:
	std::mutex m_log_lock;
	std::ofstream m_logfile;
	bool m_enable;
};

class DebuggerLogListener : public LogListener {
public:
	void Log(LogTypes::LOG_LEVELS, const char *msg);
};

class RingbufferLogListener : public LogListener {
public:
	RingbufferLogListener() : curMessage_(0), count_(0), enabled_(false) {}
	void Log(LogTypes::LOG_LEVELS, const char *msg);

	bool IsEnabled() const { return enabled_; }
	void SetEnable(bool enable) { enabled_ = enable; }

	int GetCount() const { return count_ < MAX_LOGS ? count_ : MAX_LOGS; }
	const char *TextAt(int i) { return messages_[(curMessage_ - i - 1) & (MAX_LOGS - 1)]; }
	LogTypes::LOG_LEVELS LevelAt(int i) { return (LogTypes::LOG_LEVELS)levels_[(curMessage_ - i - 1) & (MAX_LOGS - 1)]; }

private:
	enum { MAX_LOGS = 128 };
	char messages_[MAX_LOGS][1024];
	u8 levels_[MAX_LOGS];
	int curMessage_;
	int count_;
	bool enabled_;
};

// TODO: A simple buffered log that can be used to display the log in-window
// on Android etc.
// class BufferedLogListener { ... }

class LogChannel {
public:
	LogChannel(const char* shortName, const char* fullName, bool enable = false);
	
	const char* GetShortName() const { return m_shortName; }
	const char* GetFullName() const { return m_fullName; }

	void AddListener(LogListener* listener);
	void RemoveListener(LogListener* listener);

	void Trigger(LogTypes::LOG_LEVELS, const char *msg);

	bool IsEnabled() const { return enable_; }
	void SetEnable(bool enable) { enable_ = enable; }

	LogTypes::LOG_LEVELS GetLevel() const { return (LogTypes::LOG_LEVELS)level_; }

	void SetLevel(LogTypes::LOG_LEVELS level) {	level_ = level; }
	bool HasListeners() const { return m_hasListeners; }

	// Although not elegant, easy to set with a PopupMultiChoice...
	int level_;
	bool enable_;

private:
	char m_fullName[128];
	char m_shortName[32];
	std::mutex m_listeners_lock;
	std::set<LogListener*> m_listeners;
	bool m_hasListeners;
};

class ConsoleListener;

class LogManager : NonCopyable {
private:
	LogChannel* log_[LogTypes::NUMBER_OF_LOGS];
	FileLogListener *fileLog_;
	ConsoleListener *consoleLog_;
	DebuggerLogListener *debuggerLog_;
	RingbufferLogListener *ringLog_;
	static LogManager *logManager_;  // Singleton. Ugh.
	std::mutex log_lock_;

	LogManager();
	~LogManager();

public:

	static u32 GetMaxLevel() { return MAX_LOGLEVEL;	}
	static int GetNumChannels() { return LogTypes::NUMBER_OF_LOGS; }

	void Log(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type, 
			 const char *file, int line, const char *fmt, va_list args);
	bool IsEnabled(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type);

	LogChannel *GetLogChannel(LogTypes::LOG_TYPE type) {
		return log_[type];
	}

	void SetLogLevel(LogTypes::LOG_TYPE type, LogTypes::LOG_LEVELS level) {
		log_[type]->SetLevel(level);
	}

	void SetAllLogLevels(LogTypes::LOG_LEVELS level) {
		for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i) {
			log_[i]->SetLevel(level);
		}
	}

	void SetEnable(LogTypes::LOG_TYPE type, bool enable) {
		log_[type]->SetEnable(enable);
	}

	LogTypes::LOG_LEVELS GetLogLevel(LogTypes::LOG_TYPE type) {
		return log_[type]->GetLevel();
	}

	void AddListener(LogTypes::LOG_TYPE type, LogListener *listener) {
		log_[type]->AddListener(listener);
	}

	void RemoveListener(LogTypes::LOG_TYPE type, LogListener *listener) {
		log_[type]->RemoveListener(listener);
	}

	ConsoleListener *GetConsoleListener() const {
		return consoleLog_;
	}

	DebuggerLogListener *GetDebuggerListener() const {
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

	static void Init();
	static void Shutdown();

	void ChangeFileLog(const char *filename);

  void SaveConfig(IniFile::Section *section);
  void LoadConfig(IniFile::Section *section);
};
