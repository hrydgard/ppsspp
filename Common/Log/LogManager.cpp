// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(ANDROID)

#include <android/log.h>

#endif

#include <algorithm>
#include <cstring>

#include "Common/Data/Encoding/Utf8.h"

#include "Common/Log/LogManager.h"

#if PPSSPP_PLATFORM(WINDOWS)
#include "Common/Log/ConsoleListener.h"
#endif

#include "Common/Log/StdioListener.h"
#include "Common/TimeUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"

// Don't need to savestate this.
const char *hleCurrentThreadName = nullptr;

bool *g_bLogEnabledSetting = nullptr;

static const char level_to_char[8] = "-NEWIDV";

#if PPSSPP_PLATFORM(UWP) && defined(_DEBUG)
#define LOG_MSC_OUTPUTDEBUG true
#else
#define LOG_MSC_OUTPUTDEBUG false
#endif

void GenericLog(LogLevel level, Log type, const char *file, int line, const char* fmt, ...) {
	if (g_bLogEnabledSetting && !(*g_bLogEnabledSetting))
		return;
	va_list args;
	va_start(args, fmt);
	LogManager *instance = LogManager::GetInstance();
	if (instance) {
		instance->LogLine(level, type, file, line, fmt, args);
	} else {
		// Fall back to printf or direct android logger with a small buffer if the log manager hasn't been initialized yet.
#if PPSSPP_PLATFORM(ANDROID)
		char temp[512];
		vsnprintf(temp, sizeof(temp), fmt, args);
		__android_log_print(ANDROID_LOG_INFO, "PPSSPP", "EARLY: %s", temp);
#else
		vprintf(fmt, args);
		printf("\n");
#endif
	}
	va_end(args);
}

bool GenericLogEnabled(LogLevel level, Log type) {
	if (LogManager::GetInstance())
		return (*g_bLogEnabledSetting) && LogManager::GetInstance()->IsEnabled(level, type);
	return false;
}

LogManager *LogManager::logManager_ = NULL;

// NOTE: Needs to be kept in sync with the Log enum.
static const char * const g_logTypeNames[] = {
	"SYSTEM",
	"BOOT",
	"COMMON",
	"CPU",
	"FILESYS",
	"G3D",
	"HLE",
	"JIT",
	"LOADER",
	"ME",  // Media Engine
	"MEMMAP",
	"SASMIX",
	"SAVESTATE",
	"FRAMEBUF",
	"AUDIO",
	"IO",
	"ACHIEVEMENTS",
	"HTTP",
	"PRINTF",

	"SCEAUDIO",
	"SCECTRL",
	"SCEDISP",
	"SCEFONT",
	"SCEGE",
	"SCEINTC",
	"SCEIO",
	"SCEKERNEL",
	"SCEMODULE",
	"SCENET",
	"SCERTC",
	"SCESAS",
	"SCEUTIL",
	"SCEMISC",
};

LogManager::LogManager(bool *enabledSetting) {
	g_bLogEnabledSetting = enabledSetting;

	_dbg_assert_(ARRAY_SIZE(g_logTypeNames) == (size_t)Log::NUMBER_OF_LOGS);

	for (size_t i = 0; i < ARRAY_SIZE(g_logTypeNames); i++) {
		truncate_cpy(log_[i].m_shortName, g_logTypeNames[i]);
		log_[i].enabled = true;
#if defined(_DEBUG)
		log_[i].level = LogLevel::LDEBUG;
#else
		log_[i].level = LogLevel::LINFO;
#endif
	}

	// Remove file logging on small devices in Release mode.
#if PPSSPP_PLATFORM(UWP)
	if (IsDebuggerPresent())
		debuggerLog_ = new OutputDebugStringLogListener();
#else
#if !defined(MOBILE_DEVICE) || defined(_DEBUG)
	fileLog_ = new FileLogListener("");
#if PPSSPP_PLATFORM(WINDOWS)
#if !PPSSPP_PLATFORM(UWP)
	consoleLog_ = new ConsoleListener();
#endif
	if (IsDebuggerPresent())
		debuggerLog_ = new OutputDebugStringLogListener();
#else
	stdioLog_ = new StdioListener();
#endif
#endif
	ringLog_ = new RingbufferLogListener();
#endif

#if !defined(MOBILE_DEVICE) || defined(_DEBUG)
	AddListener(fileLog_);
#if PPSSPP_PLATFORM(WINDOWS)
#if !PPSSPP_PLATFORM(UWP)
	AddListener(consoleLog_);
#endif
#else
	AddListener(stdioLog_);
#endif
#if defined(_MSC_VER) && (defined(USING_WIN_UI) || PPSSPP_PLATFORM(UWP))
	if (IsDebuggerPresent() && debuggerLog_ && LOG_MSC_OUTPUTDEBUG)
		AddListener(debuggerLog_);
#endif
	AddListener(ringLog_);
#endif
}

LogManager::~LogManager() {
	for (int i = 0; i < (int)Log::NUMBER_OF_LOGS; ++i) {
#if !defined(MOBILE_DEVICE) || defined(_DEBUG)
		RemoveListener(fileLog_);
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
		RemoveListener(consoleLog_);
#endif
		RemoveListener(stdioLog_);
#if defined(_MSC_VER) && defined(USING_WIN_UI)
		RemoveListener(debuggerLog_);
#endif
#endif
	}

	// Make sure we don't shutdown while logging.  RemoveListener locks too, but there are gaps.
	std::lock_guard<std::mutex> listeners_lock(listeners_lock_);

	delete fileLog_;
#if !defined(MOBILE_DEVICE) || defined(_DEBUG)
#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	delete consoleLog_;
#endif
	delete stdioLog_;
	delete debuggerLog_;
#endif
	delete ringLog_;
}

void LogManager::ChangeFileLog(const char *filename) {
	if (fileLog_) {
		RemoveListener(fileLog_);
		delete fileLog_;
		fileLog_ = nullptr;
	}

	if (filename) {
		fileLog_ = new FileLogListener(filename);
		AddListener(fileLog_);
	}
}

void LogManager::SaveConfig(Section *section) {
	for (int i = 0; i < (int)Log::NUMBER_OF_LOGS; i++) {
		section->Set((std::string(log_[i].m_shortName) + "Enabled").c_str(), log_[i].enabled);
		section->Set((std::string(log_[i].m_shortName) + "Level").c_str(), (int)log_[i].level);
	}
}

void LogManager::LoadConfig(const Section *section, bool debugDefaults) {
	for (int i = 0; i < (int)Log::NUMBER_OF_LOGS; i++) {
		bool enabled = false;
		int level = 0;
		section->Get((std::string(log_[i].m_shortName) + "Enabled").c_str(), &enabled, true);
		section->Get((std::string(log_[i].m_shortName) + "Level").c_str(), &level, (int)(debugDefaults ? LogLevel::LDEBUG : LogLevel::LERROR));
		log_[i].enabled = enabled;
		log_[i].level = (LogLevel)level;
	}
}

void LogManager::LogLine(LogLevel level, Log type, const char *file, int line, const char *format, va_list args) {
	const LogChannel &log = log_[(size_t)type];
	if (level > log.level || !log.enabled)
		return;

	LogMessage message;
	message.level = level;
	message.log = log.m_shortName;

#ifdef _WIN32
	static const char sep = '\\';
#else
	static const char sep = '/';
#endif
	const char *fileshort = strrchr(file, sep);
	if (fileshort != NULL) {
		do
			--fileshort;
		while (fileshort > file && *fileshort != sep);
		if (fileshort != file)
			file = fileshort + 1;
	}

	GetCurrentTimeFormatted(message.timestamp);

	if (hleCurrentThreadName) {
		snprintf(message.header, sizeof(message.header), "%-12.12s %c[%s]: %s:%d",
			hleCurrentThreadName, level_to_char[(int)level],
			log.m_shortName,
			file, line);
	} else {
		snprintf(message.header, sizeof(message.header), "%s:%d %c[%s]:",
			file, line, level_to_char[(int)level],
			log.m_shortName);
	}

	char msgBuf[1024];
	va_list args_copy;

	va_copy(args_copy, args);
	size_t neededBytes = vsnprintf(msgBuf, sizeof(msgBuf), format, args);
	message.msg.resize(neededBytes + 1);
	if (neededBytes > sizeof(msgBuf)) {
		// Needed more space? Re-run vsnprintf.
		vsnprintf(&message.msg[0], neededBytes + 1, format, args_copy);
	} else {
		memcpy(&message.msg[0], msgBuf, neededBytes);
	}
	message.msg[neededBytes] = '\n';
	va_end(args_copy);

	std::lock_guard<std::mutex> listeners_lock(listeners_lock_);
	for (auto &iter : listeners_) {
		iter->Log(message);
	}
}

bool LogManager::IsEnabled(LogLevel level, Log type) {
	LogChannel &log = log_[(size_t)type];
	if (level > log.level || !log.enabled)
		return false;
	return true;
}

void LogManager::Init(bool *enabledSetting) {
	_assert_(logManager_ == nullptr);
	logManager_ = new LogManager(enabledSetting);
}

void LogManager::Shutdown() {
	delete logManager_;
	logManager_ = NULL;
}

void LogManager::AddListener(LogListener *listener) {
	if (!listener)
		return;
	std::lock_guard<std::mutex> lk(listeners_lock_);
	listeners_.push_back(listener);
}

void LogManager::RemoveListener(LogListener *listener) {
	if (!listener)
		return;
	std::lock_guard<std::mutex> lk(listeners_lock_);
	auto iter = std::find(listeners_.begin(), listeners_.end(), listener);
	if (iter != listeners_.end())
		listeners_.erase(iter);
}

FileLogListener::FileLogListener(const char *filename) {
	if (strlen(filename) > 0) {
		fp_ = File::OpenCFile(Path(std::string(filename)), "at");
	}
	SetEnabled(fp_ != nullptr);
}

FileLogListener::~FileLogListener() {
	if (fp_)
		fclose(fp_);
}

void FileLogListener::Log(const LogMessage &message) {
	if (!IsEnabled() || !IsValid())
		return;

	std::lock_guard<std::mutex> lk(m_log_lock);
	fprintf(fp_, "%s %s %s", message.timestamp, message.header, message.msg.c_str());
	fflush(fp_);
}

void OutputDebugStringLogListener::Log(const LogMessage &message) {
	char buffer[4096];
	snprintf(buffer, sizeof(buffer), "%s %s %s", message.timestamp, message.header, message.msg.c_str());
#if _MSC_VER
	OutputDebugStringUTF8(buffer);
#endif
}

void RingbufferLogListener::Log(const LogMessage &message) {
	if (!enabled_)
		return;
	messages_[curMessage_] = message;
	curMessage_++;
	if (curMessage_ >= MAX_LOGS)
		curMessage_ -= MAX_LOGS;
	count_++;
}

#ifdef _WIN32

void OutputDebugStringUTF8(const char *p) {
	wchar_t temp[16384*4];

	int len = std::min(16383*4, (int)strlen(p));
	int size = (int)MultiByteToWideChar(CP_UTF8, 0, p, len, NULL, 0);
	MultiByteToWideChar(CP_UTF8, 0, p, len, temp, size);
	temp[size] = 0;

	OutputDebugString(temp);
}

#else

void OutputDebugStringUTF8(const char *p) {
	INFO_LOG(Log::System, "%s", p);
}

#endif
