// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Common/CommonTypes.h"
#include "Common/Log.h"
#include <string>

#define DEBUG_LOG_REPORT(t,...)   { DEBUG_LOG(t, __VA_ARGS__);  Reporting::ReportMessage(__VA_ARGS__); }
#define ERROR_LOG_REPORT(t,...)   { ERROR_LOG(t, __VA_ARGS__);  Reporting::ReportMessage(__VA_ARGS__); }
#define WARN_LOG_REPORT(t,...)    { WARN_LOG(t, __VA_ARGS__);   Reporting::ReportMessage(__VA_ARGS__); }
#define NOTICE_LOG_REPORT(t,...)  { NOTICE_LOG(t, __VA_ARGS__); Reporting::ReportMessage(__VA_ARGS__); }
#define INFO_LOG_REPORT(t,...)    { INFO_LOG(t, __VA_ARGS__);   Reporting::ReportMessage(__VA_ARGS__); }

#define DEBUG_LOG_REPORT_ONCE(n,t,...)   { if (Reporting::ShouldLogOnce(#n)) { DEBUG_LOG_REPORT(t, __VA_ARGS__); } }
#define ERROR_LOG_REPORT_ONCE(n,t,...)   { if (Reporting::ShouldLogOnce(#n)) { ERROR_LOG_REPORT(t, __VA_ARGS__); } }
#define WARN_LOG_REPORT_ONCE(n,t,...)    { if (Reporting::ShouldLogOnce(#n)) { WARN_LOG_REPORT(t, __VA_ARGS__); } }
#define NOTICE_LOG_REPORT_ONCE(n,t,...)  { if (Reporting::ShouldLogOnce(#n)) { NOTICE_LOG_REPORT(t, __VA_ARGS__); } }
#define INFO_LOG_REPORT_ONCE(n,t,...)    { if (Reporting::ShouldLogOnce(#n)) { INFO_LOG_REPORT(t, __VA_ARGS__); } }

#define ERROR_LOG_ONCE(n,t,...)   { if (Reporting::ShouldLogOnce(#n)) { ERROR_LOG(t, __VA_ARGS__); } }
#define WARN_LOG_ONCE(n,t,...)    { if (Reporting::ShouldLogOnce(#n)) { WARN_LOG(t, __VA_ARGS__); } }
#define NOTICE_LOG_ONCE(n,t,...)  { if (Reporting::ShouldLogOnce(#n)) { NOTICE_LOG(t, __VA_ARGS__); } }
#define INFO_LOG_ONCE(n,t,...)    { if (Reporting::ShouldLogOnce(#n)) { INFO_LOG(t, __VA_ARGS__); } }

class PointerWrap;

namespace Reporting
{
	// Should be called whenever a new game is loaded/shutdown to forget things.
	void Init();
	void Shutdown();

	// Check savestate compatibility, mostly needed on load.
	void DoState(PointerWrap &p);

	// Should be called whenever the game configuration changes.
	void UpdateConfig();

	// Returns whether or not the reporting system is currently enabled.
	bool IsEnabled();

	// Returns whether the reporting system can be enabled (based on system or settings.)
	bool IsSupported();

	// Set the current enabled state of the reporting system and desired reporting server host.
	void Enable(bool flag, std::string host);

	// Use the default reporting setting (per compiled settings) of host and enabled state.
	void EnableDefault();

	// Report a message string, using the format string as a key.
	void ReportMessage(const char *message, ...);

	// Returns true if that identifier has not been logged yet.
	bool ShouldLogOnce(const char *identifier);
}