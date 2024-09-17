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

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
#include <atomic>
#include <algorithm>  // min
#include <array>
#include <cstring>
#include <string> // System: To be able to add strings with "+"
#include <math.h>
#include <process.h>
#include "Common/CommonWindows.h"

#ifndef _MSC_VER
#include <unistd.h>
#endif

#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/CommonTypes.h"
#include "Common/Log/ConsoleListener.h"
#include "Common/StringUtils.h"

const int LOG_PENDING_MAX = 120 * 10000;
const int LOG_LATENCY_DELAY_MS = 20;
const int LOG_SHUTDOWN_DELAY_MS = 250;
const int LOG_MAX_DISPLAY_LINES = 4000;

static bool g_Initialized;

ConsoleListener::ConsoleListener() : hidden_(true) {
	useColor_ = true;

	// useThread_ = false;

	if (useThread_ && !hTriggerEvent) {
		hTriggerEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		InitializeCriticalSection(&criticalSection);
		logPending_ = new char[LOG_PENDING_MAX];
	}

	_dbg_assert_(!g_Initialized);
	g_Initialized = true;

	logPendingReadPos_.store(0);
	logPendingWritePos_.store(0);
}

ConsoleListener::~ConsoleListener() {
	g_Initialized = false;
	Close();
}

// Handle console event
bool WINAPI ConsoleHandler(DWORD msgType) {
    if (msgType == CTRL_C_EVENT) {
		OutputDebugString(L"Ctrl-C!\n");
        return TRUE;
    } else if (msgType == CTRL_CLOSE_EVENT) {
        OutputDebugString(L"Close console window!\n");
		return TRUE;
    }
    /*
        Other messages:
        CTRL_BREAK_EVENT         Ctrl-Break pressed
        CTRL_LOGOFF_EVENT        User log off
        CTRL_SHUTDOWN_EVENT      System shutdown
    */
    return FALSE;
}

// Open console window - width and height is the size of console window
// Name is the window title
void ConsoleListener::Init(bool AutoOpen, int Width, int Height) {
	openWidth_ = Width;
	openHeight_ = Height;
	if (AutoOpen) {
		Open();
		hidden_ = false;
	}
}

void ConsoleListener::Open() {
	if (!GetConsoleWindow()) {
		// Open the console window and create the window handle for GetStdHandle()
		AllocConsole();
		hWnd = GetConsoleWindow();
		ShowWindow(hWnd, SW_SHOWDEFAULT);
		// disable console close button
		HMENU hMenu = GetSystemMenu(hWnd, false);
		EnableMenuItem(hMenu,SC_CLOSE,MF_GRAYED|MF_BYCOMMAND);
		// Save the window handle that AllocConsole() created
		hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		// Set console handler
		if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)ConsoleHandler, TRUE)) {
			OutputDebugStringA("Console handler is installed!\n");
		}
		// Set the console window title
		SetConsoleTitle(L"PPSSPP Debug Console");
		SetConsoleCP(CP_UTF8);
		SetConsoleOutputCP(CP_UTF8);

		// Set letter space
		LetterSpace(openWidth_, LOG_MAX_DISPLAY_LINES);
		//MoveWindow(GetConsoleWindow(), 200,200, 800,800, true);
	} else {
		hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	}

	if (useThread_ && hTriggerEvent != NULL && !thread_.joinable()) {
		thread_ = std::thread([&] {
			SetCurrentThreadName("Console");
			LogWriterThread();
		});
	}
}

void ConsoleListener::Show(bool bShow) {
	if (bShow && hidden_) {
		if (!IsOpen()) {
			Open();
		}
		ShowWindow(GetConsoleWindow(), SW_SHOW);
		hidden_ = false;
	} else if (!bShow && !hidden_) {
		ShowWindow(GetConsoleWindow(), SW_HIDE);
		hidden_ = true;
	}
}

void ConsoleListener::UpdateHandle() {
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
}

// Close the console window and close the eventual file handle
void ConsoleListener::Close() {
	if (thread_.joinable()) {
		logPendingWritePos_.store((u32)-1, std::memory_order_release);
		SetEvent(hTriggerEvent);
		thread_.join();
	}
	if (hTriggerEvent) {
		DeleteCriticalSection(&criticalSection);
		CloseHandle(hTriggerEvent);
		hTriggerEvent = nullptr;
	}
	if (logPending_) {
		delete [] logPending_;
		logPending_ = nullptr;
	}

	if (hConsole) {
		FreeConsole();
		hConsole = nullptr;
	}
}

bool ConsoleListener::IsOpen() {
	return (hConsole != nullptr);
}

// LetterSpace: SetConsoleScreenBufferSize and SetConsoleWindowInfo are
// dependent on each other, that's the reason for the additional checks.  
void ConsoleListener::BufferWidthHeight(int BufferWidth, int BufferHeight, int ScreenWidth, int ScreenHeight, bool BufferFirst) {
	_dbg_assert_msg_(IsOpen(), "Don't call this before opening the console.");
	BOOL SB, SW;
	if (BufferFirst) {
		// Change screen buffer size
		COORD Co = {(SHORT)BufferWidth, (SHORT)BufferHeight};
		SB = SetConsoleScreenBufferSize(hConsole, Co);
		// Change the screen buffer window size
		SMALL_RECT coo = {(SHORT)0, (SHORT)0, (SHORT)ScreenWidth, (SHORT)ScreenHeight}; // top, left, right, bottom
		SW = SetConsoleWindowInfo(hConsole, TRUE, &coo);
	} else {
		// Change the screen buffer window size
		SMALL_RECT coo = {(SHORT)0, (SHORT)0, (SHORT)ScreenWidth, (SHORT)ScreenHeight}; // top, left, right, bottom
		SW = SetConsoleWindowInfo(hConsole, TRUE, &coo);
		// Change screen buffer size
		COORD Co = {(SHORT)BufferWidth, (SHORT)BufferHeight};
		SB = SetConsoleScreenBufferSize(hConsole, Co);
	}
}

void ConsoleListener::LetterSpace(int Width, int Height) {
	_dbg_assert_msg_(IsOpen(), "Don't call this before opening the console.");
	// Get console info
	CONSOLE_SCREEN_BUFFER_INFO ConInfo;
	GetConsoleScreenBufferInfo(hConsole, &ConInfo);

	int OldBufferWidth = ConInfo.dwSize.X;
	int OldBufferHeight = ConInfo.dwSize.Y;
	int OldScreenWidth = (ConInfo.srWindow.Right - ConInfo.srWindow.Left);
	int OldScreenHeight = (ConInfo.srWindow.Bottom - ConInfo.srWindow.Top);

	int NewBufferWidth = Width;
	int NewBufferHeight = Height;
	int NewScreenWidth = NewBufferWidth - 1;
	int NewScreenHeight = OldScreenHeight;

	// Width
	BufferWidthHeight(NewBufferWidth, OldBufferHeight, NewScreenWidth, OldScreenHeight, (NewBufferWidth > OldScreenWidth-1));
	// Height
	BufferWidthHeight(NewBufferWidth, NewBufferHeight, NewScreenWidth, NewScreenHeight, (NewBufferHeight > OldScreenHeight-1));

	// Resize the window too
	// MoveWindow(GetConsoleWindow(), 200,200, (Width*8 + 50),(NewScreenHeight*12 + 200), true);
}

COORD ConsoleListener::GetCoordinates(int BytesRead, int BufferWidth) {
	COORD Ret = {0, 0};
	// Full rows
	int Step = (int)floor((float)BytesRead / (float)BufferWidth);
	Ret.Y += Step;
	// Partial row
	Ret.X = BytesRead - (BufferWidth * Step);
	return Ret;
}

void ConsoleListener::LogWriterThread() {
	char *logLocal = new char[LOG_PENDING_MAX];
	int logLocalSize = 0;

	while (true) {
		WaitForSingleObject(hTriggerEvent, INFINITE);
		Sleep(LOG_LATENCY_DELAY_MS);

		u32 logRemotePos = logPendingWritePos_.load(std::memory_order_acquire);
		if (logRemotePos == (u32)-1) {
			break;
		} else if (logRemotePos == logPendingReadPos_) {
			continue;
		} else {
			EnterCriticalSection(&criticalSection);
			logRemotePos = logPendingWritePos_.load(std::memory_order_acquire);

			int start = 0;
			if (logRemotePos < logPendingReadPos_) {
				const int count = LOG_PENDING_MAX - logPendingReadPos_;
				memcpy(logLocal + start, logPending_ + logPendingReadPos_, count);

				start = count;
				logPendingReadPos_ = 0;
			}

			const int count = logRemotePos - logPendingReadPos_;
			memcpy(logLocal + start, logPending_ + logPendingReadPos_, count);

			logPendingReadPos_ += count;
			LeaveCriticalSection(&criticalSection);

			// Double check.
			if (logPendingWritePos_ == (u32)-1) {
				break;
			}

			logLocalSize = start + count;
		}

		for (char *Text = logLocal, *End = logLocal + logLocalSize; Text < End; ) {
			LogLevel Level = LogLevel::LINFO;

			char *next = (char *) memchr(Text + 1, '\033', End - Text);
			size_t Len = next - Text;
			if (!next) {
				Len = End - Text;
			}

			if (Text[0] == '\033' && Text + 1 < End) {
				Level = (LogLevel)(Text[1] - '0');
				Len -= 2;
				Text += 2;
			}

			// Make sure we didn't start quitting. This is kinda slow.
			if (logPendingWritePos_ == (u32)-1) {
				break;
			}

			WriteToConsole(Level, Text, Len);
			Text += Len;
		}
	}

	delete [] logLocal;
}

void ConsoleListener::SendToThread(LogLevel Level, const char *Text) {
	// Oops, we're already quitting.  Just do nothing.
	if (logPendingWritePos_ == (u32)-1) {
		return;
	}

	int Len = (int)strlen(Text);
	if (Len > LOG_PENDING_MAX)
		Len = LOG_PENDING_MAX - 16;

	char ColorAttr[16] = "";
	int ColorLen = 0;
	if (useColor_) {
		// Not ANSI, since the console doesn't support it, but ANSI-like.
		snprintf(ColorAttr, 16, "\033%d", Level);
		// For now, rather than properly support it.
		_dbg_assert_msg_(strlen(ColorAttr) == 2, "Console logging doesn't support > 9 levels.");
		ColorLen = (int)strlen(ColorAttr);
	}

	EnterCriticalSection(&criticalSection);
	u32 logWritePos = logPendingWritePos_.load();
	u32 prevLogWritePos = logWritePos;
	if (logWritePos + ColorLen + Len >= LOG_PENDING_MAX) {
		for (int i = 0; i < ColorLen; ++i) {
			logPending_[(logWritePos + i) % LOG_PENDING_MAX] = ColorAttr[i];
		}
		logWritePos += ColorLen;
		if (logWritePos >= LOG_PENDING_MAX)
			logWritePos -= LOG_PENDING_MAX;

		int start = 0;
		if (logWritePos < LOG_PENDING_MAX && logWritePos + Len >= LOG_PENDING_MAX) {
			const int count = LOG_PENDING_MAX - logWritePos;
			memcpy(logPending_ + logWritePos, Text, count);
			start = count;
			logWritePos = 0;
		}
		const int count = Len - start;
		if (count > 0) {
			memcpy(logPending_ + logWritePos, Text + start, count);
			logWritePos += count;
		}
	} else {
		memcpy(logPending_ + logWritePos, ColorAttr, ColorLen);
		memcpy(logPending_ + logWritePos + ColorLen, Text, Len);
		logWritePos += ColorLen + Len;
	}

	// Oops, we passed the read pos.
	if (prevLogWritePos < logPendingReadPos_ && logWritePos >= logPendingReadPos_) {
		char *nextNewline = (char *) memchr(logPending_ + logWritePos, '\n', LOG_PENDING_MAX - logWritePos);
		if (nextNewline == NULL && logWritePos > 0)
			nextNewline = (char *) memchr(logPending_, '\n', logWritePos);

		// Okay, have it go right after the next newline.
		if (nextNewline != NULL)
			logPendingReadPos_ = (u32)(nextNewline - logPending_ + 1);
	}

	// Double check we didn't start quitting.
	if (logPendingWritePos_ == (u32) -1) {
		LeaveCriticalSection(&criticalSection);
		return;
	}

	logPendingWritePos_.store(logWritePos, std::memory_order::memory_order_release);
	LeaveCriticalSection(&criticalSection);

	SetEvent(hTriggerEvent);
}

void ConsoleListener::WriteToConsole(LogLevel Level, const char *Text, size_t Len) {
	_dbg_assert_msg_(IsOpen(), "Don't call this before opening the console.");

	/*
	const int MAX_BYTES = 1024*10;
	char Str[MAX_BYTES];
	va_list ArgPtr;
	int Cnt;
	va_start(ArgPtr, Text);
	Cnt = vsnprintf(Str, MAX_BYTES, Text, ArgPtr);
	va_end(ArgPtr);
	*/
	DWORD cCharsWritten;
	WORD Color;
	static wchar_t tempBuf[2048];

	switch (Level) {
	case LogLevel::LNOTICE: // light green
		Color = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
		break;
	case LogLevel::LERROR: // light red
		Color = FOREGROUND_RED | FOREGROUND_INTENSITY;
		break;
	case LogLevel::LWARNING: // light yellow
		Color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
		break;
	case LogLevel::LINFO: // cyan
		Color = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
		break;
	case LogLevel::LDEBUG: // gray
		Color = FOREGROUND_INTENSITY;
		break;
	default: // off-white
		Color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
		break;
	}
	if (Len > 10) {
		// First 10 chars white
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
		int wlen = MultiByteToWideChar(CP_UTF8, 0, Text, (int)Len, NULL, NULL);
		MultiByteToWideChar(CP_UTF8, 0, Text, (int)Len, tempBuf, wlen);
		WriteConsole(hConsole, tempBuf, 10, &cCharsWritten, NULL);
		Text += 10;
		Len -= 10;
	}
	SetConsoleTextAttribute(hConsole, Color);
	int wlen = MultiByteToWideChar(CP_UTF8, 0, Text, (int)Len, NULL, NULL);
	MultiByteToWideChar(CP_UTF8, 0, Text, (int)Len, tempBuf, wlen);
	WriteConsole(hConsole, tempBuf, (DWORD)wlen, &cCharsWritten, NULL);
}

void ConsoleListener::PixelSpace(int Left, int Top, int Width, int Height, bool Resize) {
	_dbg_assert_msg_(IsOpen(), "Don't call this before opening the console.");
	// Check size
	if (Width < 8 || Height < 12) return;

	std::string SLog = "";

	// Get console info
	CONSOLE_SCREEN_BUFFER_INFO ConInfo;
	GetConsoleScreenBufferInfo(hConsole, &ConInfo);
	DWORD BufferSize = ConInfo.dwSize.X * ConInfo.dwSize.Y;

	// ---------------------------------------------------------------------
	//  Save the current text
	// ------------------------
	DWORD cCharsRead = 0;
	COORD coordScreen = { 0, 0 };

	static const int MAX_BYTES = 1024 * 16;

	std::vector<std::array<wchar_t, MAX_BYTES>> Str;
	std::vector<std::array<WORD, MAX_BYTES>> Attr;

	// ReadConsoleOutputAttribute seems to have a limit at this level
	static const int ReadBufferSize = MAX_BYTES - 32;

	DWORD cAttrRead = ReadBufferSize;
	DWORD BytesRead = 0;
	while (BytesRead < BufferSize) {
		Str.resize(Str.size() + 1);
		if (!ReadConsoleOutputCharacter(hConsole, Str.back().data(), ReadBufferSize, coordScreen, &cCharsRead))
			SLog += StringFromFormat("WriteConsoleOutputCharacter error");

		Attr.resize(Attr.size() + 1);
		if (!ReadConsoleOutputAttribute(hConsole, Attr.back().data(), ReadBufferSize, coordScreen, &cAttrRead))
			SLog += StringFromFormat("WriteConsoleOutputAttribute error");

		// Break on error
		if (cAttrRead == 0) break;
		BytesRead += cAttrRead;
		coordScreen = GetCoordinates(BytesRead, ConInfo.dwSize.X);
	}
	// Letter space
	int LWidth = (int)(floor((float)Width / 8.0f) - 1.0f);
	int LHeight = (int)(floor((float)Height / 12.0f) - 1.0f);
	int LBufWidth = LWidth + 1;
	int LBufHeight = (int)floor((float)BufferSize / (float)LBufWidth);
	// Change screen buffer size
	LetterSpace(LBufWidth, LBufHeight);


	ClearScreen(true);	
	coordScreen.Y = 0;
	coordScreen.X = 0;
	DWORD cCharsWritten = 0;

	int BytesWritten = 0;
	DWORD cAttrWritten = 0;
	for (size_t i = 0; i < Attr.size(); i++) {
		if (!WriteConsoleOutputCharacter(hConsole, Str[i].data(), ReadBufferSize, coordScreen, &cCharsWritten))
			SLog += StringFromFormat("WriteConsoleOutputCharacter error");
		if (!WriteConsoleOutputAttribute(hConsole, Attr[i].data(), ReadBufferSize, coordScreen, &cAttrWritten))
			SLog += StringFromFormat("WriteConsoleOutputAttribute error");

		BytesWritten += cAttrWritten;
		coordScreen = GetCoordinates(BytesWritten, LBufWidth);
	}	

	const int OldCursor = ConInfo.dwCursorPosition.Y * ConInfo.dwSize.X + ConInfo.dwCursorPosition.X;
	COORD Coo = GetCoordinates(OldCursor, LBufWidth);
	SetConsoleCursorPosition(hConsole, Coo);

	// if (SLog.length() > 0) Log(LogLevel::LNOTICE, SLog.c_str());

	// Resize the window too
	if (Resize) {
		MoveWindow(GetConsoleWindow(), Left, Top, (Width + 100), Height, true);
	}
}

void ConsoleListener::Log(const LogMessage &msg) {
	char buf[2048];
	snprintf(buf, sizeof(buf), "%s %s %s", msg.timestamp, msg.header, msg.msg.c_str());
	buf[sizeof(buf) - 2] = '\n';
	buf[sizeof(buf) - 1] = '\0';

	if (!useThread_ && IsOpen())
		WriteToConsole(msg.level, buf, strlen(buf));
	else
		SendToThread(msg.level, buf);
}

// Clear console screen
void ConsoleListener::ClearScreen(bool Cursor) { 
	_dbg_assert_msg_(IsOpen(), "Don't call this before opening the console.");

	CONSOLE_SCREEN_BUFFER_INFO csbi; 
	GetConsoleScreenBufferInfo(hConsole, &csbi); 
	DWORD dwConSize = csbi.dwSize.X * csbi.dwSize.Y;
	// Write space to the entire console
	DWORD cCharsWritten;
	COORD coordScreen = { 0, 0 };
	FillConsoleOutputCharacter(hConsole, TEXT(' '), dwConSize, coordScreen, &cCharsWritten);
	GetConsoleScreenBufferInfo(hConsole, &csbi); 
	FillConsoleOutputAttribute(hConsole, csbi.wAttributes, dwConSize, coordScreen, &cCharsWritten);
	// Reset cursor
	if (Cursor) {
		SetConsoleCursorPosition(hConsole, coordScreen);
	}
}

#endif  // PPSSPP_PLATFORM(WINDOWS)
