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

#pragma once

#include "Common/CommonTypes.h"
#include "Common/Log.h"

#define DEBUG_LOG_ONCE(n,t,...) do { if (LocalLog::ShouldLogNTimes(#n, 1)) { DEBUG_LOG(t, __VA_ARGS__); } } while (false)
#define ERROR_LOG_ONCE(n,t,...)   do { if (LocalLog::ShouldLogNTimes(#n, 1)) { ERROR_LOG(t, __VA_ARGS__); } } while (false)
#define WARN_LOG_ONCE(n,t,...)    do { if (LocalLog::ShouldLogNTimes(#n, 1)) { WARN_LOG(t, __VA_ARGS__); } } while (false)
#define NOTICE_LOG_ONCE(n,t,...)  do { if (LocalLog::ShouldLogNTimes(#n, 1)) { NOTICE_LOG(t, __VA_ARGS__); } } while (false)
#define INFO_LOG_ONCE(n,t,...)    do { if (LocalLog::ShouldLogNTimes(#n, 1)) { INFO_LOG(t, __VA_ARGS__); } } while (false)

#define ERROR_LOG_N_TIMES(s,n,t,...)   do { if (LocalLog::ShouldLogNTimes(#s, n)) { ERROR_LOG(t, __VA_ARGS__); } } while (false)
#define WARN_LOG_N_TIMES(s,n,t,...)    do { if (LocalLog::ShouldLogNTimes(#s, n)) { WARN_LOG(t, __VA_ARGS__); } } while (false)
#define NOTICE_LOG_N_TIMES(s,n,t,...)  do { if (LocalLog::ShouldLogNTimes(#s, n)) { NOTICE_LOG(t, __VA_ARGS__); } } while (false)
#define INFO_LOG_N_TIMES(s,n,t,...)    do { if (LocalLog::ShouldLogNTimes(#s, n)) { INFO_LOG(t, __VA_ARGS__); } } while (false)

namespace LocalLog {

// Resets counts on any count-limited logs (see ShouldLogNTimes).
void ResetCounts();

// Returns true if that identifier has not been logged yet.
bool ShouldLogNTimes(const char *identifier, int n);

} // namespace LocalLog
