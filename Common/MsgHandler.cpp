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

#include <string>

#include "ppsspp_config.h"

#include "Common.h"
#include "StringUtils.h"
#include "base/logging.h"
#include "util/text/utf8.h"

bool MsgHandler(const char *caption, const char *text, bool yes_no, int Style);

bool MsgAlert(bool yes_no, int Style, const char* format, ...) {
	// Read message and write it to the log
	char buffer[2048];
	static const char *captions[] = {
		"Information",
		"Question",
		"Warning",
		"Critical"
	};

	const char *caption = captions[Style];
	va_list args;
	va_start(args, format);
	CharArrayFromFormatV(buffer, sizeof(buffer)-1, format, args);
	va_end(args);
	// Normal logging (will also log to Android log)
	ERROR_LOG(SYSTEM, "%s: %s", caption, buffer);
	// Don't ignore questions, especially AskYesNo, PanicYesNo could be ignored
	if (Style == QUESTION || Style == CRITICAL)
		return MsgHandler(caption, buffer, yes_no, Style);
	return true;
}

#ifdef _WIN32
#include "CommonWindows.h"
#endif

// Default non library dependent panic alert
bool MsgHandler(const char* caption, const char* text, bool yes_no, int Style) {
#if defined(USING_WIN_UI)
	int msgBoxStyle = MB_ICONINFORMATION;
	if (Style == QUESTION) msgBoxStyle = MB_ICONQUESTION;
	if (Style == WARNING) msgBoxStyle = MB_ICONWARNING;

	std::wstring wtext = ConvertUTF8ToWString(text) + L"\n\nTry to continue?";
	std::wstring wcaption = ConvertUTF8ToWString(caption);
	OutputDebugString(wtext.c_str());
	return IDYES == MessageBox(0, wtext.c_str(), wcaption.c_str(), msgBoxStyle | (yes_no ? MB_YESNO : MB_OK));
#elif PPSSPP_PLATFORM(UWP)
	OutputDebugStringUTF8(text);
	return false;
#else
	// Will use android-log if available, printf if not.
	ELOG("%s", text);
	return false;
#endif
}
