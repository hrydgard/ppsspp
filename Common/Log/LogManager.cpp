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
#include <io.h>
#include "Common/CommonWindows.h"
#endif

#include "Common/TimeUtil.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/StringUtils.h"

LogChannel g_log[(size_t)Log::NUMBER_OF_LOGS];
LogManager g_logManager;

thread_local const char *hleCurrentThreadName = nullptr;

bool g_bDummySetting = true;
bool *g_bLogEnabledSetting = &g_bDummySetting;

static const char level_to_char[8] = "-NEWIDV";

#if PPSSPP_PLATFORM(UWP) && defined(_DEBUG)
#define LOG_MSC_OUTPUTDEBUG true
#else
#define LOG_MSC_OUTPUTDEBUG false
#endif

#if PPSSPP_PLATFORM(ANDROID)
void AndroidLog(const LogMessage &message);
#endif

// TODO: Get rid of this wrapper, not much point.
void GenericLog(Log type, LogLevel level, const char *file, int line, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	g_logManager.LogLine(level, type, file, line, fmt, args);
	va_end(args);
}

// NOTE: Needs to be kept in sync with the Log enum.
static const char * const g_logTypeNames[] = {
	"System",
	"Config",
	"Boot",
	"Common",
	"CPU",
	"FileSystem",
	"G3D",
	"TexCache",
	"HLE",
	"JIT",
	"Loader",
	"Mpeg",
	"Atrac",
	"ME",
	"MemMap",
	"SasMix",
	"SaveState",
	"FrameBuf",
	"Audio",
	"IO",
	"Achievements",
	"HTTP",
	"Printf",
	"TexReplacement",
	"Debugger",
	"GeDebugger",
	"UI",
	"IAP",
	"CwCheats",
	"Net",
	"sceAudio",
	"sceCtrl",
	"sceDisplay",
	"sceFont",
	"sceGe",
	"sceIntc",
	"sceIo",
	"sceKernel",
	"sceModule",
	"sceNet",
	"sceRtc",
	"sceSas",
	"sceUtility",
	"sceMisc",
	"sceReg",
};

const char *LogManager::GetLogTypeName(Log type) {
	return g_logTypeNames[(size_t)type];
}

// Ultra plain output, for CI and stuff.
void PrintfLog(const LogMessage &message);

void LogManager::Init(bool *enabledSetting) {
	g_bLogEnabledSetting = enabledSetting;
	if (initialized_) {
		// Just update the pointer, already done above.
		return;
	}
	initialized_ = true;

	_dbg_assert_(ARRAY_SIZE(g_logTypeNames) == (size_t)Log::NUMBER_OF_LOGS);
	_dbg_assert_(ARRAY_SIZE(g_logTypeNames) == ARRAY_SIZE(g_log));

	for (size_t i = 0; i < ARRAY_SIZE(g_log); i++) {
		g_log[i].Set(LogLevel::LINFO, true);
	}
}

void LogManager::Shutdown() {
	if (!initialized_) {
		// already done
		return;
	}

	retro_atomic_store_release_int(&outputs_, 0);
	CloseLogFile();

	ringLog_.Clear();
	initialized_ = false;
}

LogManager::LogManager() {
	retro_atomic_int_init(&outputs_, 0);
	retro_atomic_ptr_init(&fp_, nullptr);
	retro_atomic_int_init(&externalCount_, 0);
	for (int i = 0; i < MAX_EXTERNAL_CALLBACKS; i++)
		retro_atomic_ptr_init(&externalCallbacks_[i], nullptr);
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

#if PPSSPP_PLATFORM(WINDOWS)
	if (IsDebuggerPresent()) {
		retro_atomic_fetch_or_int(&outputs_, (int)LogOutput::DebugString);
	}
#endif
}

LogManager::~LogManager() {
	Shutdown();
	for (int i = 0; i < MAX_EXTERNAL_CALLBACKS; i++)
		delete (ExternalCallbackEntry *)retro_atomic_exchange_ptr(&externalCallbacks_[i], nullptr);
}

void LogManager::CloseLogFile() {
	FILE *fp = (FILE *)retro_atomic_exchange_ptr(&fp_, nullptr);
	if (fp) {
		// Loggers that loaded fp before the exchange are inside the gate.
		gate_.Drain();
		fclose(fp);
	}
}

void LogManager::SetFileLogPath(const Path &filename) {
	if (retro_atomic_load_acquire_ptr(&fp_) && filename == logFilename_) {
		// All good
		return;
	}

	CloseLogFile();

	if (!filename.empty()) {
		logFilename_ = Path(filename);

		if (GetOutputsEnabled() & LogOutput::File) {
			File::CreateFullPath(logFilename_.NavigateUp());
			FILE *fp = File::OpenCFile(logFilename_, "at");
			logFileOpenFailed_ = fp == nullptr;
			if (logFileOpenFailed_) {
				printf("Failed to open log file %s\n", logFilename_.c_str());
			}
			retro_atomic_store_release_ptr(&fp_, fp);
		}
	}
}

void LogManager::SaveConfig(Section *section) {
	if (channelsChangedByDebugger_) {
		// Leave the section as whatever was already on disk - see the doc comment on
		// NotifyChannelsChangedByDebugger().
		return;
	}
	for (int i = 0; i < (int)Log::NUMBER_OF_LOGS; i++) {
		section->Set((std::string(g_logTypeNames[i]) + "Enabled"), g_log[i].Enabled());
		section->Set((std::string(g_logTypeNames[i]) + "Level"), (int)g_log[i].Level());
	}
}

void LogManager::LoadConfig(const Section *section) {
	for (int i = 0; i < (int)Log::NUMBER_OF_LOGS; i++) {
		// Defaults. Get now doesn't write the output if it fails.
		bool enabled = true;
		int level = (int)LogLevel::LERROR;
		section->Get((std::string(g_logTypeNames[i]) + "Enabled"), &enabled);
		section->Get((std::string(g_logTypeNames[i]) + "Level"), &level);
		g_log[i].Set((LogLevel)level, enabled);
	}
}

void LogManager::SetOutputsEnabled(LogOutput outputs) {
	retro_atomic_store_release_int(&outputs_, (int)outputs);
	if (outputs & LogOutput::File) {
		SetFileLogPath(logFilename_);
	}
}

void LogManager::LogLine(LogLevel level, Log type, const char *file, int line, const char *format, va_list args) {
	char msgBuf[1024];

	const LogOutput outputs = GetOutputsEnabled();
	if (!g_log[(size_t)type].IsEnabled(level) || outputs == (LogOutput)0) {
		// If we get here, it should have been caught earlier.
		return;
	}

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

	if (outputs & LogOutput::Stdio) {
		StdioLog(message);
	}

	// OK, now go through the possible listeners in order.
	const bool wantFile = (outputs & LogOutput::File) != 0;
	const bool wantExternal = (outputs & LogOutput::ExternalCallback) != 0;
	if (wantFile || wantExternal) {
		gate_.Enter();
		FILE *fp = wantFile ? (FILE *)retro_atomic_load_acquire_ptr(&fp_) : nullptr;
		if (fp) {
			// One write per line so concurrent loggers don't interleave mid-line.
			std::string line;
			line.reserve(strlen(message.timestamp) + strlen(message.header) + message.msg.size() + 2);
			line.append(message.timestamp).append(" ").append(message.header).append(" ").append(message.msg);
			fwrite(line.data(), 1, line.size(), fp);
			// Is this really necessary to do every time? I guess to catch the last message before a crash..
			fflush(fp);
		}
		if (wantExternal) {
			for (int i = 0; i < MAX_EXTERNAL_CALLBACKS; i++) {
				const ExternalCallbackEntry *entry = (const ExternalCallbackEntry *)retro_atomic_load_acquire_ptr(&externalCallbacks_[i]);
				if (entry)
					entry->callback(message, entry->userdata);
			}
		}
		gate_.Exit();
	}

#if PPSSPP_PLATFORM(WINDOWS)
	if (outputs & LogOutput::DebugString) {
		char buffer[4096];
		// We omit the timestamp for easy copy-paste-diffing.
		snprintf(buffer, sizeof(buffer), "%s %s", message.header, message.msg.c_str());
		OutputDebugStringUTF8(buffer);
	}
#endif

	if (outputs & LogOutput::RingBuffer) {
		ringLog_.Log(message);
	}

	if (outputs & LogOutput::Printf) {
		PrintfLog(message);
	}
}

int LogManager::AddExternalLogCallback(LogCallback callback, void *userdata) {
	if (!callback) {
		return -1;
	}
	ExternalCallbackEntry *entry = new ExternalCallbackEntry{ callback, userdata };
	for (int i = 0; i < MAX_EXTERNAL_CALLBACKS; i++) {
		if (retro_atomic_cas_ptr(&externalCallbacks_[i], nullptr, entry)) {
			retro_atomic_fetch_add_int(&externalCount_, 1);
			EnableOutput(LogOutput::ExternalCallback);
			return i;
		}
	}
	delete entry;
	return -1;
}

void LogManager::RemoveExternalLogCallback(int handle) {
	if (handle < 0 || handle >= MAX_EXTERNAL_CALLBACKS) {
		return;
	}
	ExternalCallbackEntry *entry = (ExternalCallbackEntry *)retro_atomic_exchange_ptr(&externalCallbacks_[handle], nullptr);
	if (!entry)
		return;
	// Only when the last one goes away - otherwise removing one connection's callback would stop
	// delivery to the ones still attached.
	if (retro_atomic_fetch_sub_int(&externalCount_, 1) == 1) {
		DisableOutput(LogOutput::ExternalCallback);
	}
	// Anyone still calling it is inside the gate; after this, userdata may be freed.
	gate_.Drain();
	delete entry;
}

RingbufferLog::RingbufferLog() {
	retro_atomic_int_init(&head_, 0);
	retro_atomic_int_init(&count_, 0);
	for (int i = 0; i < MAX_LOGS; i++) {
		retro_atomic_int_init(&slots_[i].seq, 0);
		retro_atomic_int_init(&slots_[i].level, 0);
		for (int j = 0; j < TEXT_WORDS; ++j)
			retro_atomic_int_init(&slots_[i].text[j], 0);
	}
}

void RingbufferLog::Log(const LogMessage &message) {
	const int idx = retro_atomic_fetch_add_int(&head_, 1) & (MAX_LOGS - 1);
	Slot &slot = slots_[idx];
	const int seq = retro_atomic_load_relaxed_int(&slot.seq);
	// Odd: another writer lapped the ring onto this slot. Drop ours.
	if ((seq & 1) || !retro_atomic_cas_int(&slot.seq, seq, (int)((unsigned)seq + 1)))
		return;
	retro_atomic_thread_fence_seq_cst();
	const size_t len = std::min(message.msg.size(), (size_t)MAX_TEXT - 1);
	char text[MAX_TEXT]{};
	memcpy(text, message.msg.data(), len);
	for (int j = 0; j < TEXT_WORDS; ++j) {
		int word;
		memcpy(&word, text + j * sizeof(int), sizeof(word));
		retro_atomic_store_relaxed_int(&slot.text[j], word);
	}
	retro_atomic_store_relaxed_int(&slot.level, (int)message.level);
	retro_atomic_store_release_int(&slot.seq, (int)((unsigned)seq + 2));
	if (retro_atomic_load_relaxed_int(&count_) < MAX_LOGS)
		retro_atomic_fetch_add_int(&count_, 1);
}

int RingbufferLog::GetCount() const {
	const int count = retro_atomic_load_acquire_int(const_cast<retro_atomic_int_t *>(&count_));
	return count < MAX_LOGS ? count : MAX_LOGS;
}

bool RingbufferLog::Read(int i, std::string *text, LogLevel *level) const {
	const int head = retro_atomic_load_acquire_int(const_cast<retro_atomic_int_t *>(&head_));
	const Slot &slot = slots_[(head - i - 1) & (MAX_LOGS - 1)];
	retro_atomic_int_t *seqp = const_cast<retro_atomic_int_t *>(&slot.seq);
	const int seq = retro_atomic_load_acquire_int(seqp);
	if (seq & 1)
		return false;
	char buf[MAX_TEXT];
	for (int j = 0; j < TEXT_WORDS; ++j) {
		int word = retro_atomic_load_relaxed_int(const_cast<retro_atomic_int_t *>(&slot.text[j]));
		memcpy(buf + j * sizeof(int), &word, sizeof(word));
	}
	buf[MAX_TEXT - 1] = '\0';
	const int lvl = retro_atomic_load_relaxed_int(const_cast<retro_atomic_int_t *>(&slot.level));
	retro_atomic_thread_fence_seq_cst();
	if (retro_atomic_load_relaxed_int(seqp) != seq)
		return false;
	if (text)
		*text = buf;
	if (level)
		*level = (LogLevel)lvl;
	return true;
}

std::string RingbufferLog::TextAt(int i) const {
	std::string text;
	Read(i, &text, nullptr);
	return text;
}

LogLevel RingbufferLog::LevelAt(int i) const {
	LogLevel level = LogLevel::LINFO;
	Read(i, nullptr, &level);
	return level;
}

void RingbufferLog::Clear() {
	retro_atomic_store_release_int(&count_, 0);
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

#ifdef HAVE_LIBRETRO_VFS
#undef fprintf
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

	// One call per line; stdio keeps it whole.
	fprintf(stderr, "%s%s%s", colorAttr, text, resetAttr);
#endif
}

void PrintfLog(const LogMessage &message) {
	// Same shape as the stdio and file outputs the other builds use. It used to be its own
	// shorter format, which meant a pattern that matched a log from the app quietly matched
	// nothing in one from headless.
	fprintf(stderr, "%s %s %s", message.timestamp, message.header, message.msg.c_str());
}
