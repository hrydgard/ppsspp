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

#include "LogManager.h"
#include "ConsoleListener.h"
#include "Timer.h"
#include "Thread.h"
#include "FileUtil.h"
#ifdef __SYMBIAN32__
#include <e32debug.h>
#endif

// Don't need to savestate this.
const char *hleCurrentThreadName = NULL;

// Unfortunately this is quite slow.
#define LOG_MSC_OUTPUTDEBUG false
// #define LOG_MSC_OUTPUTDEBUG true

void GenericLog(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type, 
		const char *file, int line, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	if (LogManager::GetInstance())
		LogManager::GetInstance()->Log(level, type,
			file, line, fmt, args);
	va_end(args);
}

LogManager *LogManager::m_logManager = NULL;

LogManager::LogManager()
{
	// create log files
	m_Log[LogTypes::MASTER_LOG] = new LogContainer("*",	      "Master Log");
	m_Log[LogTypes::BOOT]       = new LogContainer("BOOT",    "Boot");
	m_Log[LogTypes::COMMON]     = new LogContainer("COMMON",  "Common");
	m_Log[LogTypes::CPU]        = new LogContainer("CPU",     "CPU");
	m_Log[LogTypes::LOADER]     = new LogContainer("LOAD",    "Loader");
	m_Log[LogTypes::IO]         = new LogContainer("IO",      "IO");
	m_Log[LogTypes::DISCIO]     = new LogContainer("DIO",     "DiscIO");
	m_Log[LogTypes::PAD]        = new LogContainer("PAD",     "Pad");
	m_Log[LogTypes::FILESYS]    = new LogContainer("FileSys", "File System");
	m_Log[LogTypes::G3D]        = new LogContainer("G3D",     "3D Graphics");
	m_Log[LogTypes::DMA]        = new LogContainer("DMA",     "DMA");
	m_Log[LogTypes::INTC]       = new LogContainer("INTC",    "Interrupts");
	m_Log[LogTypes::MEMMAP]     = new LogContainer("MM",      "Memory Map");
	m_Log[LogTypes::SOUND]      = new LogContainer("SND",     "Sound");
	m_Log[LogTypes::SAS]        = new LogContainer("SAS",     "Sound Mixer (Sas)");
	m_Log[LogTypes::HLE]        = new LogContainer("HLE",     "HLE");
	m_Log[LogTypes::TIMER]      = new LogContainer("TMR",     "Timer");
	m_Log[LogTypes::VIDEO]      = new LogContainer("VID",     "Video");
	m_Log[LogTypes::DYNA_REC]   = new LogContainer("Jit",     "JIT compiler");
	m_Log[LogTypes::NETPLAY]    = new LogContainer("NET",     "Net play");
	m_Log[LogTypes::ME]         = new LogContainer("ME",      "Media Engine");

	// Remove file logging on small devices
#if !defined(USING_GLES2) || defined(_DEBUG)
	m_fileLog = new FileLogListener(File::GetUserPath(F_MAINLOG_IDX).c_str());
	m_consoleLog = new ConsoleListener();
	m_debuggerLog = new DebuggerLogListener();
#else
	m_fileLog = NULL;
	m_consoleLog = NULL;
	m_debuggerLog = NULL;
#endif

	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i)
	{
		m_Log[i]->SetEnable(true);
#if !defined(USING_GLES2) || defined(_DEBUG)
		m_Log[i]->AddListener(m_fileLog);
		m_Log[i]->AddListener(m_consoleLog);
#ifdef _MSC_VER
		if (IsDebuggerPresent() && m_debuggerLog != NULL && LOG_MSC_OUTPUTDEBUG)
			m_Log[i]->AddListener(m_debuggerLog);
#endif
#endif
	}
}

LogManager::~LogManager()
{
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i)
	{
#if !defined(USING_GLES2) || defined(_DEBUG)
		if (m_fileLog != NULL)
			m_logManager->RemoveListener((LogTypes::LOG_TYPE)i, m_fileLog);
		m_logManager->RemoveListener((LogTypes::LOG_TYPE)i, m_consoleLog);
#ifdef _MSC_VER
		m_logManager->RemoveListener((LogTypes::LOG_TYPE)i, m_debuggerLog);
#endif
#endif
	}

	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i)
		delete m_Log[i];
	if (m_fileLog != NULL)
		delete m_fileLog;
#if !defined(USING_GLES2) || defined(_DEBUG)
	delete m_consoleLog;
	delete m_debuggerLog;
#endif
}

void LogManager::ChangeFileLog(const char *filename)
{
	if (m_fileLog != NULL)
	{
		for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i)
			m_logManager->RemoveListener((LogTypes::LOG_TYPE)i, m_fileLog);
		delete m_fileLog;
	}

	if (filename != NULL)
	{
		m_fileLog = new FileLogListener(filename);
		for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; ++i)
			m_Log[i]->AddListener(m_fileLog);
	}
}

void LogManager::SaveConfig(IniFile::Section *section)
{
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++)
	{
		section->Set((std::string(m_Log[i]->GetShortName()) + "Enabled").c_str(), m_Log[i]->IsEnabled());
		section->Set((std::string(m_Log[i]->GetShortName()) + "Level").c_str(), (int)m_Log[i]->GetLevel());
	}
}

void LogManager::LoadConfig(IniFile::Section *section)
{
	for (int i = 0; i < LogTypes::NUMBER_OF_LOGS; i++)
	{
		bool enabled;
		int level;
		section->Get((std::string(m_Log[i]->GetShortName()) + "Enabled").c_str(), &enabled, true);
		section->Get((std::string(m_Log[i]->GetShortName()) + "Level").c_str(), &level, 0);
		m_Log[i]->SetEnable(enabled);
		m_Log[i]->SetLevel((LogTypes::LOG_LEVELS)level);
	}
}

void LogManager::Log(LogTypes::LOG_LEVELS level, LogTypes::LOG_TYPE type, const char *file, int line, const char *format, va_list args)
{
	std::lock_guard<std::mutex> lk(m_log_lock);

	char msg[MAX_MSGLEN * 2];
	LogContainer *log = m_Log[type];
	if (!log || !log->IsEnabled() || level > log->GetLevel() || ! log->HasListeners())
		return;

	static const char level_to_char[8] = "-NEWIDV";
	char formattedTime[13];
	Common::Timer::GetTimeFormatted(formattedTime);

#ifdef _DEBUG
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
#endif

	char *msgPos = msg;
	if (hleCurrentThreadName != NULL)
	{
		msgPos += sprintf(msgPos, "%s %-12.12s %c[%s]: %s:%d ",
			formattedTime,
			hleCurrentThreadName, level_to_char[(int)level],
			log->GetShortName(),
			file, line);
	}
	else
	{
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

void LogManager::Init()
{
	m_logManager = new LogManager();
}

void LogManager::Shutdown()
{
	delete m_logManager;
	m_logManager = NULL;
}

LogContainer::LogContainer(const char* shortName, const char* fullName, bool enable)
	: m_enable(enable)
{
	strncpy(m_fullName, fullName, 128);
	strncpy(m_shortName, shortName, 32);
	m_level = LogTypes::LDEBUG;
}

// LogContainer
void LogContainer::AddListener(LogListener *listener)
{
	std::lock_guard<std::mutex> lk(m_listeners_lock);
	m_listeners.insert(listener);
}

void LogContainer::RemoveListener(LogListener *listener)
{
	std::lock_guard<std::mutex> lk(m_listeners_lock);
	m_listeners.erase(listener);
}

void LogContainer::Trigger(LogTypes::LOG_LEVELS level, const char *msg)
{
#ifdef __SYMBIAN32__
	RDebug::Printf("%s",msg);
#else
	std::lock_guard<std::mutex> lk(m_listeners_lock);

	std::set<LogListener*>::const_iterator i;
	for (i = m_listeners.begin(); i != m_listeners.end(); ++i)
	{
		(*i)->Log(level, msg);
	}
#endif
}

FileLogListener::FileLogListener(const char *filename)
{
	m_logfile.open(filename, std::ios::app);
	SetEnable(true);
}

void FileLogListener::Log(LogTypes::LOG_LEVELS, const char *msg)
{
	if (!IsEnabled() || !IsValid())
		return;

	std::lock_guard<std::mutex> lk(m_log_lock);
	m_logfile << msg << std::flush;
}

void DebuggerLogListener::Log(LogTypes::LOG_LEVELS, const char *msg)
{
#if _MSC_VER
	::OutputDebugStringA(msg);
#endif
}
