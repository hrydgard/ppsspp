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

#include <cstdint>
#include <ctime>

#include "Common/CommonTypes.h"
#include "Common/Swap.h"

class PointerWrap;
struct FrontendCalendar;

struct PSPTimeval {
	s32_le tv_sec;
	s32_le tv_usec;
};

void __RtcTimeOfDay(PSPTimeval *tv);
int32_t RtcBaseTime(int32_t *micro = nullptr);
void RtcSetBaseTime(int32_t seconds, int32_t micro = 0);
u64 __RtcGetCurrentTick();

// Seconds east of UTC that local time conversions use.
int RtcLocalOffsetSeconds();
// In minutes, what INT_TIMEZONE and /CONFIG/DATE/time_zone_offset read.
int RtcTimeZoneParam();
// PSP_SYSTEMPARAM_DAYLIGHTSAVINGS_SAVING or _STD.
int RtcDaylightSavingsParam();
// Sets utcSeconds to the emulated calendar time now (whole seconds), and when the RTC follows a
// frontend calendar also utcOffset and standardOffset, so a reboot can carry them on.
void RtcRunningCalendar(FrontendCalendar *calendar);
// timegm: a UTC struct tm to seconds, independent of the host time zone.
time_t RtcTimeGm(struct tm *t);

void Register_sceRtc();
void Register_sceRtc_driver();
void __RtcInit();
void __RtcDoState(PointerWrap &p);

struct ScePspDateTime {
	s16_le year;
	s16_le month;
	s16_le day;
	s16_le hour;
	s16_le minute;
	s16_le second;
	u32_le microsecond;
};

// The emulated PSP's clock, in local time - what sceRtcGetCurrentClockLocalTime gives the game.
// For host UI that shows the PSP's time. False if the date is out of range.
bool __RtcGetCurrentLocalTime(ScePspDateTime *t);
