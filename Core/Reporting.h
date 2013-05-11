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

#define ERROR_LOG_REPORT(t,...)   { ERROR_LOG(t, __VA_ARGS__);  Reporting::ReportMessage(__VA_ARGS__); }
#define WARN_LOG_REPORT(t,...)    { WARN_LOG(t, __VA_ARGS__);   Reporting::ReportMessage(__VA_ARGS__); }
#define NOTICE_LOG_REPORT(t,...)  { NOTICE_LOG(t, __VA_ARGS__); Reporting::ReportMessage(__VA_ARGS__); }
#define INFO_LOG_REPORT(t,...)    { INFO_LOG(t, __VA_ARGS__);   Reporting::ReportMessage(__VA_ARGS__); }

#define ERROR_LOG_REPORT_ONCE(n,t,...)   { static bool n = false; if (!n) { n = true; ERROR_LOG(t, __VA_ARGS__);  Reporting::ReportMessage(__VA_ARGS__); } }
#define WARN_LOG_REPORT_ONCE(n,t,...)    { static bool n = false; if (!n) { n = true; WARN_LOG(t, __VA_ARGS__);   Reporting::ReportMessage(__VA_ARGS__); } }
#define NOTICE_LOG_REPORT_ONCE(n,t,...)  { static bool n = false; if (!n) { n = true; NOTICE_LOG(t, __VA_ARGS__); Reporting::ReportMessage(__VA_ARGS__); } }
#define INFO_LOG_REPORT_ONCE(n,t,...)    { static bool n = false; if (!n) { n = true; INFO_LOG(t, __VA_ARGS__);   Reporting::ReportMessage(__VA_ARGS__); } }

namespace Reporting
{
	bool IsEnabled();
	void ReportMessage(const char *message, ...);
}