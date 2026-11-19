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

#include <ctime>

#include "Core/HLE/FunctionWrappers.h"
#include "Common/Swap.h"

void Register_sceUsbGps();

void __UsbGpsInit();
void __UsbGpsDoState(PointerWrap &p);
void __UsbGpsShutdown();

#pragma pack(push, 1)

enum GpsState {
	GPS_STATE_NOT_AVAILABLE = 0,
	GPS_STATE_NOT_ACCESSORY = 1,
	GPS_STATE_BUSY = 2,
	GPS_STATE_WORKING = 3,
};

enum GpsPositionQuality {
	GPS_POSITION_QUALITY_NOT_AVAILABLE = 0,
	GPS_POSITION_QUALITY_GPS           = 1,
	GPS_POSITION_QUALITY_DGPS          = 2,
};

enum GpsPositionMode {
	GPS_POSITION_MODE_NOT_AVAILABLE = 0,
	GPS_POSITION_MODE_2D = 1,
	GPS_POSITION_MODE_3D = 2,
};

typedef struct {
	s16_le year;
	s16_le month;
	s16_le day;
	s16_le hour;
	s16_le minute;
	s16_le second;
	u32_le microsecond;
} UsbGpsDateTime;

typedef struct {
	UsbGpsDateTime datetime;
	float          hdop;
	u8             quality;
	u8             mode;
	u16_le         reserve0;  //quality+mode  NA->0 2D->257(0x0000 0101) 3D->513(0x0000 0201)
	float          latitude;
	float          longitude;
	float          altitude;  // m
	u32_le         reserve1;  // geoid separation (m)?
	float          speed;     // km/h
	float          course;    // deg (0-360), called "bearing" on Android
} GpsData;

typedef struct {
	unsigned char   id;
	unsigned char   elevation;
	short           azimuth;
	unsigned char   snr;
	unsigned char   good;
	short           garbage;
} SatInfo;

typedef struct {
	short   satellites_in_view;
	short   garbage;
	SatInfo satInfo[32];
} SatData;

#pragma pack(pop)

namespace GPS {
	void init();
	void setGpsTime(time_t *time);
	void setGpsData(long long time, float hdop, float latitude, float longitude, float altitude, float speed, float course);
	void setSatInfo(short index, unsigned char id, unsigned char elevation, short azimuth, unsigned char snr, unsigned char good);
	GpsData *getGpsData();
	SatData *getSatData();
}
