// Copyright (C) 2014- PPSSPP Project.

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

#include <atomic>
#include <algorithm>  // min
#include <cstring>
#include <string> // System: To be able to add strings with "+"
#include <math.h>
#include <stdarg.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include "ppsspp_config.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/CommonTypes.h"
#include "Common/Log/StdioListener.h"
#include "Common/StringUtils.h"

StdioListener::StdioListener()  {
#if PPSSPP_PLATFORM(IOS) || PPSSPP_PLATFORM(UWP) || PPSSPP_PLATFORM(SWITCH)
	bUseColor = false;
#elif defined(_MSC_VER)
	bUseColor = false;
#elif defined(__APPLE__)
    // Xcode builtin terminal used for debugging does not support colours.
    // Fortunately it can be detected with a TERM env variable.
    bUseColor = isatty(fileno(stdout)) && getenv("TERM") != NULL;
#else
	bUseColor = isatty(fileno(stdout));
#endif
}

void StdioListener::Log(const LogMessage &msg) {
	char Text[2048];
	snprintf(Text, sizeof(Text), "%s %s %s", msg.timestamp, msg.header, msg.msg.c_str());
	Text[sizeof(Text) - 2] = '\n';
	Text[sizeof(Text) - 1] = '\0';

	char ColorAttr[16] = "";
	char ResetAttr[16] = "";

	if (bUseColor) {
		strcpy(ResetAttr, "\033[0m");
		switch (msg.level) {
		case LogLevel::LNOTICE: // light green
			strcpy(ColorAttr, "\033[92m");
			break;
		case LogLevel::LERROR: // light red
			strcpy(ColorAttr, "\033[91m");
			break;
		case LogLevel::LWARNING: // light yellow
			strcpy(ColorAttr, "\033[93m");
			break;
		case LogLevel::LINFO: // cyan
			strcpy(ColorAttr, "\033[96m");
			break;
		case LogLevel::LDEBUG: // gray
			strcpy(ColorAttr, "\033[90m");
			break;
		default:
			break;
		}
	}
	fprintf(stderr, "%s%s%s", ColorAttr, Text, ResetAttr);
}
