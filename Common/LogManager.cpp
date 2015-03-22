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

#include <algorithm>
#include "base/logging.h"
#include "util/text/utf8.h"
#include "LogManager.h"
#include "ConsoleListener.h"
#include "Timer.h"
#include "FileUtil.h"
#include "../Core/Config.h"
#ifdef __SYMBIAN32__
#include <e32debug.h>
#endif

// Don't need to savestate this.
const char *hleCurrentThreadName = NULL;

// Unfortunately this is quite slow.
#define LOG_MSC_OUTPUTDEBUG false
// #define LOG_MSC_OUTPUTDEBUG true

void GenericLog(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type, 
		const char *file, int line, const char* fmt, ...) {
	if (!g_Config.bEnableLogging) return;

	va_list args;
	va_start(args, fmt);
	if (LogManager::GetInstance())
		LogManager::GetInstance()->Log(level, type,
			file, line, fmt, args);
	va_end(args);
}

bool GenericLogEnabled(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type) {
	if (LogManager::GetInstance())
		return g_Config.bEnableLogging && LogManager::GetInstance()->IsEnabled(level, type);
	return false;
}

LogManager *LogManager::logManager_ = NULL;

struct LogNameTableEntry {
	LogTypes::LOG_TYPE logType;
	const char *name;
	const char *longName;
};

static const LogNameTableEntry logTable[] = {
	{LogTypes::MASTER_LOG, "*",       "Master Log"},

	{LogTypes::SCEAUDIO   ,"AUDIO",   "sceAudio"},
	{LogTypes::SCECTRL    ,"CTRL",    "sceCtrl"},
	{LogTypes::SCEDISPLAY ,"DISP",    "sceDisplay"},
	{LogTypes::SCEFONT    ,"FONT",    "sceFont"},
	{LogTypes::SCEGE      ,"SCEGE",   "sceGe"},
	{LogTypes::SCEINTC    ,"INTC",    "sceKernelInterrupt"},
	{LogTypes::SCEIO      ,"IO",      "sceIo"},
	{LogTypes::SCEKERNEL  ,"KERNEL",  "sceKernel*"},
	{LogTypes::SCEMODULE  ,"MODULE",  "sceKernelModule"},
	{LogTypes::SCENET     ,"NET",     "sceNet*"},
	{LogTypes::SCERTC     ,"SCERTC",  "sceRtc"},
	{LogTypes::SCESAS     ,"SCESAS",  "sceSas"},
	{LogTypes::SCEUTILITY ,"UTIL",    "sceUtility"},

	{LogTypes::BOOT       ,"BOOT",    "Boot"},
	{LogTypes::COMMON     ,"COMMON",  "Common"},
	{LogTypes::CPU        ,"CPU",     "CPU"},
	{LogTypes::FILESYS    ,"FileSys", "File System"},
	{LogTypes::G3D        ,"G3D",     "3D Graphics"},
	{LogTypes::HLE        ,"HLE",     "HLE"},
	{LogTypes::JIT        ,"JIT",     "JIT compiler"},
	{LogTypes::LOADER     ,"LOAD",    "Loader"},
	{LogTypes::ME         ,"ME",      "Media Engine"},
	{LogTypes::MEMMAP     ,"MM",      "Memory Map"},
	{LogTypes::TIME       ,"TIME",    "CoreTiming"},
	{LogTypes::SASMIX     ,"SASMIX",  "Sound Mixer (Sas)"},
};

LogManager::LogManager() {
	for (size_t i = 0; i < ARRAY_SIZE(logTable); i++) {
		if (i != logTable[i].logType) {
			FLOG("Bad logtable at %i", (int)i);
		}
		log_[logTable[i].logType] = new LogChannel(logTable[i].name, logTable[i].longName);
	}

	// Remove file logging on small devices
#if !(defined(MOBILE_DEVICE) || defined(_XBOX)) || defined(_DEBUG)
	fileLog_ = new FileLogListener("");
	consoleLog_ = new ConsoleListener();
	debuggerLog_ = new DebuggerLogListener();
#else
	fileLog_ = NULL;
	consoleLog_ = NULL;
	debuggerLog_ = NULL;
#endif
	ringLog_ = new RingbufferLogListener();

	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i) {
		log_[i]->SetEnable(true);
#if !(defined(MOBILE_DEVICE) || defined(_XBOX)) || defined(_DEBUG)
		log_[i]->AddListener(fileLog_);
		log_[i]->AddListener(consoleLog_);
#if defined(_MSC_VER) && !defined(_XBOX)
		if (IsDebuggerPresent() && debuggerLog_ != NULL && LOG_MSC_OUTPUTDEBUG)
			log_[i]->AddListener(debuggerLog_);
#endif
		log_[i]->AddListener(ringLog_);
#endif
	}
}

LogManager::~LogManager() {
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i) {
#if !defined(MOBILE_DEVICE) || defined(_DEBUG)
		if (fileLog_ != NULL)
			logManager_->RemoveListener((LogTypes::LOG_TYPE)i, fileLog_);
		logManager_->RemoveListener((LogTypes::LOG_TYPE)i, consoleLog_);
#ifdef _MSC_VER
		logManager_->RemoveListener((LogTypes::LOG_TYPE)i, debuggerLog_);
#endif
#endif
	}

	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i)
		delete log_[i];
	if (fileLog_ != NULL)
		delete fileLog_;
#if !defined(MOBILE_DEVICE) || defined(_DEBUG)
	delete consoleLog_;
	delete debuggerLog_;
#endif
}

void LogManager::ChangeFileLog(const char *filename) {
	if (fileLog_ != NULL) {
		for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i)
			logManager_->RemoveListener((LogTypes::LOG_TYPE)i, fileLog_);
		delete fileLog_;
	}

	if (filename != NULL) {
		fileLog_ = new FileLogListener(filename);
		for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i)
			log_[i]->AddListener(fileLog_);
	}
}

void LogManager::SaveConfig(IniFile::Section *section) {
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++) {
		section->Set((std::string(log_[i]->GetShortName()) + "Enabled").c_str(), log_[i]->IsEnabled());
		section->Set((std::string(log_[i]->GetShortName()) + "Level").c_str(), (int)log_[i]->GetLevel());
	}
}

void LogManager::LoadConfig(IniFile::Section *section) {
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++) {
		bool enabled;
		int level;
		section->Get((std::string(log_[i]->GetShortName()) + "Enabled").c_str(), &enabled, true);
		section->Get((std::string(log_[i]->GetShortName()) + "Level").c_str(), &level, 0);
		log_[i]->SetEnable(enabled);
		log_[i]->SetLevel((LogTypes::LOG_LEVELS)level);
	}
}

void LogManager::Log(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type, const char *file, int line, const char *format, va_list args) {
	LogChannel *log = log_[type];
	if (level > log->GetLevel() || !log->IsEnabled() || !log->HasListeners())
		return;

	std::lock_guard<std::mutex> lk(log_lock_);
	static const char level_to_char[8] = "-NEWIDV";
	char formattedTime[13];
	Common::Timer::GetTimeFormatted(formattedTime);

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
	
	char msg[MAX_MSGLEN * 2];
	char *msgPos = msg;
	if (hleCurrentThreadName != NULL) {
		msgPos += sprintf(msgPos, "%s %-12.12s %c[%s]: %s:%d ",
			formattedTime,
			hleCurrentThreadName, level_to_char[(int)level],
			log->GetShortName(),
			file, line);
	} else {
		msgPos += sprintf(msgPos, "%s %s:%d %c[%s]: ",
			formattedTime,
			file, line, level_to_char[(int)level],
			log->GetShortName());
	}

	msgPos += vsnprintf(msgPos, MAX_MSGLEN, format, args);
	// This will include the null terminator.
	memcpy(msgPos, "\n", sizeof("\n"));

	log->Trigger(level, msg);
}

bool LogManager::IsEnabled(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type) {
	LogChannel *log = log_[type];
	if (level > log->GetLevel() || !log->IsEnabled() || !log->HasListeners())
		return false;
	return true;
}

void LogManager::Init() {
	logManager_ = new LogManager();
}

void LogManager::Shutdown() {
	delete logManager_;
	logManager_ = NULL;
}

LogChannel::LogChannel(const char* shortName, const char* fullName, bool enable)
	: enable_(enable), m_hasListeners(false) {
	strncpy(m_fullName, fullName, 128);
	strncpy(m_shortName, shortName, 32);
#if defined(_DEBUG)
	level_ = LogTypes::LDEBUG;
#else
	level_ = LogTypes::LINFO;
#endif
}

// LogContainer
void LogChannel::AddListener(LogListener *listener) {
	std::lock_guard<std::mutex> lk(m_listeners_lock);
	m_listeners.insert(listener);
	m_hasListeners = true;
}

void LogChannel::RemoveListener(LogListener *listener) {
	std::lock_guard<std::mutex> lk(m_listeners_lock);
	m_listeners.erase(listener);
	m_hasListeners = !m_listeners.empty();
}

void LogChannel::Trigger(LogTypes::LOG_LEVELS level, const char *msg) {
#ifdef __SYMBIAN32__
	RDebug::Printf("%s",msg);
#else
	std::lock_guard<std::mutex> lk(m_listeners_lock);

	std::set<LogListener*>::const_iterator i;
	for (i = m_listeners.begin(); i != m_listeners.end(); ++i) {
		(*i)->Log(level, msg);
	}
#endif
}

FileLogListener::FileLogListener(const char *filename) {
#ifdef _WIN32
	m_logfile.open(ConvertUTF8ToWString(filename).c_str(), std::ios::app);
#else
	m_logfile.open(filename, std::ios::app);
#endif
	SetEnable(true);
}

void FileLogListener::Log(LogTypes::LOG_LEVELS, const char *msg) {
	if (!IsEnabled() || !IsValid())
		return;

	std::lock_guard<std::mutex> lk(m_log_lock);
	m_logfile << msg << std::flush;
}

void DebuggerLogListener::Log(LogTypes::LOG_LEVELS, const char *msg) {
#if _MSC_VER
	OutputDebugStringUTF8(msg);
#endif
}

void RingbufferLogListener::Log(LogTypes::LOG_LEVELS level, const char *msg) {
	if (!enabled_)
		return;
	levels_[curMessage_] = (u8)level;
	size_t len = (int)strlen(msg);
	if (len >= sizeof(messages_[0]))
		len = sizeof(messages_[0]) - 1;
	memcpy(messages_[curMessage_], msg, len);
	messages_[curMessage_][len] = 0;
	curMessage_++;
	if (curMessage_ >= MAX_LOGS)
		curMessage_ -= MAX_LOGS;
	count_++;
}
