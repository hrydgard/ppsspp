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

#include "Common/TimeUtil.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/StringUtils.h"

LogManager g_logManager;

const char *hleCurrentThreadName = nullptr;

bool *g_bLogEnabledSetting = nullptr;

static const char level_to_char[8] = "-NEWIDV";

#if PPSSPP_PLATFORM(UWP) && defined(_DEBUG)
#define LOG_MSC_OUTPUTDEBUG true
#else
#define LOG_MSC_OUTPUTDEBUG false
#endif

#if PPSSPP_PLATFORM(ANDROID)
void AndroidLog(const LogMessage &message);
#endif

void GenericLog(LogLevel level, Log type, const char *file, int line, const char* fmt, ...) {
	if (g_bLogEnabledSetting && !(*g_bLogEnabledSetting))
		return;
	va_list args;
	va_start(args, fmt);
	g_logManager.LogLine(level, type, file, line, fmt, args);
	va_end(args);
}

bool GenericLogEnabled(LogLevel level, Log type) {
	return (*g_bLogEnabledSetting) && g_logManager.IsEnabled(level, type);
}

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
	"TEXREPLACE",
	"DEBUGGER",
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

const char *LogManager::GetLogTypeName(Log type) {
	return g_logTypeNames[(size_t)type];
}

// Ultra plain output, for CI and stuff.
void PrintfLog(const LogMessage &message);

void LogManager::Init(bool *enabledSetting, bool headless) {
	g_bLogEnabledSetting = enabledSetting;
	if (initialized_) {
		// Just update the pointer, already done above.
		return;
	}
	initialized_ = true;

	_dbg_assert_(ARRAY_SIZE(g_logTypeNames) == (size_t)Log::NUMBER_OF_LOGS);
	_dbg_assert_(ARRAY_SIZE(g_logTypeNames) == ARRAY_SIZE(log_));

	for (size_t i = 0; i < ARRAY_SIZE(log_); i++) {
		log_[i].enabled = true;
#if defined(_DEBUG)
		log_[i].level = LogLevel::LDEBUG;
#else
		log_[i].level = LogLevel::LINFO;
#endif
	}
}

void LogManager::Shutdown() {
	if (!initialized_) {
		// already done
		return;
	}

	if (fp_) {
		fclose(fp_);
		fp_ = nullptr;
	}

	outputs_ = (LogOutput)0;

	ringLog_.Clear();
	initialized_ = false;

	for (size_t i = 0; i < ARRAY_SIZE(log_); i++) {
		log_[i].enabled = true;
#if defined(_DEBUG)
		log_[i].level = LogLevel::LDEBUG;
#else
		log_[i].level = LogLevel::LINFO;
#endif
	}
}

LogManager::LogManager() {
#if PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(UWP) || PPSSPP_PLATFORM(SWITCH)
	stdioUseColor_ = false;
#elif defined(_MSC_VER)
	stdioUseColor_ = false;
#elif defined(__APPLE__)
	// Xcode builtin terminal used for debugging does not support colours.
	// Fortunately it can be detected with a TERM env variable.
	stdioUseColor_ = isatty(fileno(stdout)) && getenv("TERM") != NULL;
#else
	stdioUseColor_ = isatty(fileno(stdout));
#endif

#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	if (IsDebuggerPresent()) {
		outputs_ |= LogOutput::DebugString;
	}

	if (!consoleLog_) {
		consoleLog_ = new ConsoleListener();
	}
	outputs_ |= LogOutput::WinConsole;
#endif
}

LogManager::~LogManager() {
	Shutdown();

#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	delete consoleLog_;
	consoleLog_ = nullptr;
#endif
}

void LogManager::ChangeFileLog(const Path &filename) {
	if (fp_ && filename == logFilename_) {
		// All good
		return;
	}

	if (fp_) {
		fclose(fp_);
	}

	if (!filename.empty()) {
		logFilename_ = Path(filename);
		fp_ = File::OpenCFile(logFilename_, "at");
		logFileOpenFailed_ = fp_ == nullptr;
		if (logFileOpenFailed_) {
			printf("Failed to open log file %s", filename.c_str());
		}
	}
}

void LogManager::SaveConfig(Section *section) {
	for (int i = 0; i < (int)Log::NUMBER_OF_LOGS; i++) {
		section->Set((std::string(g_logTypeNames[i]) + "Enabled"), log_[i].enabled);
		section->Set((std::string(g_logTypeNames[i]) + "Level"), (int)log_[i].level);
	}
}

void LogManager::LoadConfig(const Section *section, bool debugDefaults) {
	for (int i = 0; i < (int)Log::NUMBER_OF_LOGS; i++) {
		bool enabled = false;
		int level = 0;
		section->Get((std::string(g_logTypeNames[i]) + "Enabled"), &enabled, true);
		section->Get((std::string(g_logTypeNames[i]) + "Level"), &level, (int)(debugDefaults ? LogLevel::LDEBUG : LogLevel::LERROR));
		log_[i].enabled = enabled;
		log_[i].level = (LogLevel)level;
	}
}

void LogManager::SetOutputsEnabled(LogOutput outputs) {
	outputs_ = outputs; 
	if (outputs & LogOutput::File) {
		ChangeFileLog(logFilename_);
	}
}

void LogManager::LogLine(LogLevel level, Log type, const char *file, int line, const char *format, va_list args) {
	char msgBuf[1024];

	const LogChannel &log = log_[(size_t)type];
	if (level > log.level || !log.enabled || outputs_ == (LogOutput)0)
		return;

	LogMessage message;
	message.level = level;
	message.log = g_logTypeNames[(size_t)type];

#ifdef _WIN32
	static const char sep = '\\';
#else
	static const char sep = '/';
#endif
	const char *fileshort = strrchr(file, sep);
	if (fileshort) {
		do
			--fileshort;
		while (fileshort > file && *fileshort != sep);
		if (fileshort != file)
			file = fileshort + 1;
	}

	const char *threadName;
#if PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(MAC)
	const char *hostThreadName = GetCurrentThreadName();
	if ((hostThreadName && strcmp(hostThreadName, "EmuThread") != 0) || !hleCurrentThreadName) {
		// Use the host thread name.
		threadName = hostThreadName ? hostThreadName : "unknown";
	} else {
		// Use the PSP HLE thread name.
		threadName = hleCurrentThreadName;
	}
#else
	threadName = hleCurrentThreadName;
#endif

	if (threadName) {
		snprintf(message.header, sizeof(message.header), "%-12.12s %c[%s]: %s:%d",
			threadName, level_to_char[(int)level],
			message.log,
			file, line);
	} else {
		snprintf(message.header, sizeof(message.header), "%s:%d %c[%s]:",
			file, line, level_to_char[(int)level],
			message.log);
	}

	GetCurrentTimeFormatted(message.timestamp);

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

	if (outputs_ & LogOutput::Stdio) {
		// This has its own mutex.
		StdioLog(message);
	}

	// OK, now go through the possible listeners in order.
	if (outputs_ & LogOutput::File) {
		if (fp_) {
			std::lock_guard<std::mutex> lk(logFileLock_);
			fprintf(fp_, "%s %s %s", message.timestamp, message.header, message.msg.c_str());
			// Is this really necessary to do every time? I guess to catch the last message before a crash..
			fflush(fp_);
		}
	}

#if PPSSPP_PLATFORM(WINDOWS)
	if (outputs_ & LogOutput::DebugString) {
		// No mutex needed
		char buffer[4096];
		// We omit the timestamp for easy copy-paste-diffing.
		snprintf(buffer, sizeof(buffer), "%s %s", message.header, message.msg.c_str());
		OutputDebugStringUTF8(buffer);
	}
#endif

	if (outputs_ & LogOutput::RingBuffer) {
		ringLog_.Log(message);
	}

	if (outputs_ & LogOutput::Printf) {
		PrintfLog(message);
	}

#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	if (outputs_ & LogOutput::WinConsole) {
		if (consoleLog_) {
			consoleLog_->Log(message);
		}
	}
#endif

	if (outputs_ & LogOutput::ExternalCallback) {
		if (externalCallback_) {
			externalCallback_(message, externalUserData_);
		}
	}
}

void RingbufferLog::Log(const LogMessage &message) {
	messages_[curMessage_] = message;
	curMessage_++;
	if (curMessage_ >= MAX_LOGS)
		curMessage_ -= MAX_LOGS;
	count_++;
}

#ifdef _WIN32

void OutputDebugStringUTF8(const char *p) {
	wchar_t *temp = new wchar_t[65536];

	int len = std::min(16383*4, (int)strlen(p));
	int size = (int)MultiByteToWideChar(CP_UTF8, 0, p, len, NULL, 0);
	MultiByteToWideChar(CP_UTF8, 0, p, len, temp, size);
	temp[size] = 0;

	OutputDebugString(temp);
	delete[] temp;
}

#else

void OutputDebugStringUTF8(const char *p) {
	INFO_LOG(Log::System, "%s", p);
}

#endif

void LogManager::StdioLog(const LogMessage &message) {
#if PPSSPP_PLATFORM(ANDROID)
#ifndef LOG_APP_NAME
#define LOG_APP_NAME "PPSSPP"
#endif
	int mode;
	switch (message.level) {
	case LogLevel::LWARNING:
		mode = ANDROID_LOG_WARN;
		break;
	case LogLevel::LERROR:
		mode = ANDROID_LOG_ERROR;
		break;
	default:
		mode = ANDROID_LOG_INFO;
		break;
	}

	// Long log messages need splitting up.
	// Not sure what the actual limit is (seems to vary), but let's be conservative.
	const size_t maxLogLength = 512;
	if (message.msg.length() < maxLogLength) {
		// Log with simplified headers as Android already provides timestamp etc.
		__android_log_print(mode, LOG_APP_NAME, "[%s] %s", message.log, message.msg.c_str());
	} else {
		std::string_view msg = message.msg;

		// Ideally we should split at line breaks, but it's at least fairly usable anyway.
		std::string_view first_part = msg.substr(0, maxLogLength);
		__android_log_print(mode, LOG_APP_NAME, "[%s] %.*s", message.log, (int)first_part.size(), first_part.data());
		msg = msg.substr(maxLogLength);

		while (msg.length() > maxLogLength) {
			std::string_view next_part = msg.substr(0, maxLogLength);
			__android_log_print(mode, LOG_APP_NAME, "%.*s", (int)next_part.size(), next_part.data());
			msg = msg.substr(maxLogLength);
		}
		// Print the final part.
		__android_log_print(mode, LOG_APP_NAME, "%.*s", (int)msg.size(), msg.data());
	}
#else
	std::lock_guard<std::mutex> lock(stdioLock_);
	char text[2048];
	snprintf(text, sizeof(text), "%s %s %s", message.timestamp, message.header, message.msg.c_str());
	text[sizeof(text) - 2] = '\n';
	text[sizeof(text) - 1] = '\0';

	const char *colorAttr = "";
	const char *resetAttr = "";

	if (stdioUseColor_) {
		resetAttr = "\033[0m";
		switch (message.level) {
		case LogLevel::LNOTICE: // light green
			colorAttr = "\033[92m";
			break;
		case LogLevel::LERROR: // light red
			colorAttr = "\033[91m";
			break;
		case LogLevel::LWARNING: // light yellow
			colorAttr = "\033[93m";
			break;
		case LogLevel::LINFO: // cyan
			colorAttr = "\033[96m";
			break;
		case LogLevel::LDEBUG: // gray
			colorAttr = "\033[90m";
			break;
		default:
			break;
		}
	}
	fprintf(stderr, "%s%s%s", colorAttr, text, resetAttr);
#endif
}

void PrintfLog(const LogMessage &message) {
	switch (message.level) {
	case LogLevel::LVERBOSE:
		fprintf(stderr, "V %s", message.msg.c_str());
		break;
	case LogLevel::LDEBUG:
		fprintf(stderr, "D %s", message.msg.c_str());
		break;
	case LogLevel::LINFO:
		fprintf(stderr, "I %s", message.msg.c_str());
		break;
	case LogLevel::LERROR:
		fprintf(stderr, "E %s", message.msg.c_str());
		break;
	case LogLevel::LWARNING:
		fprintf(stderr, "W %s", message.msg.c_str());
		break;
	case LogLevel::LNOTICE:
	default:
		fprintf(stderr, "N %s", message.msg.c_str());
		break;
	}
}
