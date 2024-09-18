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

#include "Common/CommonTypes.h"
#include "Common/Swap.h"

class PointerWrap;

struct PSPTimeval {
	s32_le tv_sec;
	s32_le tv_usec;
};

void __RtcTimeOfDay(PSPTimeval *tv);
int32_t RtcBaseTime(int32_t *micro = nullptr);
void RtcSetBaseTime(int32_t seconds, int32_t micro = 0);

void Register_sceRtc();
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
