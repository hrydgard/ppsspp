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

// Windows-only.

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)

#include <atomic>
#include <thread>

#include "Common/Log/LogManager.h"
#include "Common/CommonWindows.h"

class ConsoleListener : public LogListener {
public:
	ConsoleListener();
	~ConsoleListener();

	void Init(bool AutoOpen = true, int Width = 200, int Height = 100);
	void Open();
	void UpdateHandle();
	void Close();
	bool IsOpen();
	void LetterSpace(int Width, int Height);
	void BufferWidthHeight(int BufferWidth, int BufferHeight, int ScreenWidth, int ScreenHeight, bool BufferFirst);
	void PixelSpace(int Left, int Top, int Width, int Height, bool);
	COORD GetCoordinates(int BytesRead, int BufferWidth);
	void Log(const LogMessage &message) override;
	void ClearScreen(bool Cursor = true);

	void Show(bool bShow);
	bool Hidden() const { return hidden_; }

private:
	HWND hWnd = nullptr;
	HANDLE hConsole = nullptr;

	void LogWriterThread();
	void SendToThread(LogLevel Level, const char *Text);
	void WriteToConsole(LogLevel Level, const char *Text, size_t Len);

	std::thread thread_;

	HANDLE hTriggerEvent = nullptr;
	CRITICAL_SECTION criticalSection{};

	char *logPending_ = nullptr;
	std::atomic<uint32_t> logPendingReadPos_;
	std::atomic<uint32_t> logPendingWritePos_;

	int openWidth_ = 0;
	int openHeight_ = 0;
	bool hidden_ = false;
	bool useColor_ = true;
	bool useThread_ = true;
};

#endif
