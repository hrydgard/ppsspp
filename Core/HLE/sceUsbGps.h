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

void Register_sceUsbGps();

void __UsbGpsInit();
void __UsbGpsDoState(PointerWrap &p);

#pragma pack(push, 1)

typedef struct {
	short year;
	short month;
	short date;
	short hour;
	short minute;
	short second;
	float garbage1;
	float hdop;
	float garbage2;
	float latitude;
	float longitude;
	float altitude;
	float garbage3;
	float speed;
	float bearing;
}  GpsData;

typedef struct {
	unsigned char   id;
	unsigned char   elevation;
	short           azimuth;
	unsigned char   snr;
	unsigned char   good;
	short           garbage;
} SatInfo;

typedef struct {
	short satellites_in_view;
	short garbage;
	SatInfo satInfo[24];
} SatData;

#pragma pack(pop)

namespace GPS {
	void init();
	void setGpsTime(time_t *time);
	void setGpsData(float latitude, float longitude, float altitude, float speed, float bearing, long long time);
	GpsData *getGpsData();
	SatData *getSatData();
}
