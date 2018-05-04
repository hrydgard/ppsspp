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

#include "Core/HLE/FunctionWrappers.h"

void Register_sceUsbGps();

void __UsbGpsInit();
void __UsbGpsDoState(PointerWrap &p);
void __UsbGpsShutdown();

#pragma pack(push, 1)

typedef struct {
	s16_le year;
	s16_le month;
	s16_le date;
	s16_le hour;
	s16_le minute;
	s16_le second;
	float_le garbage1;
	float_le hdop;
	float_le garbage2;
	float_le latitude;
	float_le longitude;
	float_le altitude;
	float_le garbage3;
	float_le speed;
	float_le bearing;
}  GpsData;

typedef struct {
	unsigned char   id;
	unsigned char   elevation;
	s16_le          azimuth;
	unsigned char   snr;
	unsigned char   good;
	s16_le          garbage;
} SatInfo;

typedef struct {
	s16_le satellites_in_view;
	s16_le garbage;
	SatInfo satInfo[24];
} SatData;

#pragma pack(pop)

namespace GPS {
	void init();
	void setGpsTime(time_t *time);
	void setGpsData(long long time, float hdop, float latitude, float longitude, float altitude, float speed, float bearing);
	void setSatInfo(short index, unsigned char id, unsigned char elevation, short azimuth, unsigned char snr, unsigned char good);
	GpsData *getGpsData();
	SatData *getSatData();
}
