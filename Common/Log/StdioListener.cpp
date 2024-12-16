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

#include <cstring>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include "ppsspp_config.h"
#include "Common/CommonTypes.h"
#include "Common/Log/LogManager.h"
#include "Common/Log/StdioListener.h"
#include "Common/StringUtils.h"

StdioLog::StdioLog()  {
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

void StdioLog::Log(const LogMessage &msg) {
	char text[2048];
	snprintf(text, sizeof(text), "%s %s %s", msg.timestamp, msg.header, msg.msg.c_str());
	text[sizeof(text) - 2] = '\n';
	text[sizeof(text) - 1] = '\0';

	const char *colorAttr = "";
	const char *resetAttr = "";

	if (bUseColor) {
		resetAttr = "\033[0m";
		switch (msg.level) {
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
