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

#ifdef __MINGW32__
#include <unistd.h>
#endif
#include <ctime>

#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceUsbGps.h"
#include "Core/MemMapHelpers.h"

enum GpsStatus {
	GPS_STATE_OFF = 0,
	GPS_STATE_ACTIVATING1 = 1,
	GPS_STATE_ACTIVATING2 = 2,
	GPS_STATE_ON = 3,
};

constexpr int AUTO_UPDATE_GPSTIME = 10;

GpsStatus gpsStatus = GPS_STATE_OFF;
GpsData gpsData;
SatData satData;
time_t lastGPSTime;

void __UsbGpsInit() {
	gpsStatus = GPS_STATE_OFF;
}

void __UsbGpsDoState(PointerWrap &p) {
	auto s = p.Section("sceUsbGps", 0, 1);
	if (!s)
		return;

	Do(p, gpsStatus);
	if (gpsStatus == GPS_STATE_ON) {
		GPS::init();
		System_GPSCommand("open");
	}
}

void __UsbGpsShutdown() {
    gpsStatus = GPS_STATE_OFF;
    System_GPSCommand("close");
};

static int sceUsbGpsGetInitDataLocation(u32 addr) {
    return 0;
}

static int sceUsbGpsGetState(u32 stateAddr) {
	if (Memory::IsValidRange(stateAddr, 4)) {
		Memory::WriteUnchecked_U32(gpsStatus, stateAddr);
	}
	return 0;
}

static int sceUsbGpsOpen() {
	GPS::init();
	gpsStatus = GPS_STATE_ON;
	System_GPSCommand("open");
	return 0;
}

static int sceUsbGpsClose() {
	gpsStatus = GPS_STATE_OFF;
	System_GPSCommand("close");
	return 0;
}

static int sceUsbGpsGetData(u32 gpsDataAddr, u32 satDataAddr) {
	time_t currentTime;
	time(&currentTime);
	if (difftime(currentTime, lastGPSTime) > AUTO_UPDATE_GPSTIME)
	{
		/* Simulate fresh updates to satisfy MAPLUS 1/2 apps
		 * when real GPS data isn't available */
		GPS::setGpsTime(&currentTime);
	}

	auto gpsData = PSPPointer<GpsData>::Create(gpsDataAddr);
	if (gpsData.IsValid()) {
		*gpsData = *GPS::getGpsData();
		gpsData.NotifyWrite("UsbGpsGetData");
	}
	auto satData = PSPPointer<SatData>::Create(satDataAddr);
	if (satData.IsValid()) {
		*satData = *GPS::getSatData();
		gpsData.NotifyWrite("UsbGpsGetData");
	}
	return 0;
}

const HLEFunction sceUsbGps[] =
{
	{0X268F95CA, nullptr,                                 "sceUsbGpsSetInitDataLocation",  '?', "" },
	{0X31F95CDE, nullptr,                                 "sceUsbGpsGetPowerSaveMode",     '?', "" },
	{0X54D26AA4, &WrapI_U<sceUsbGpsGetInitDataLocation>,  "sceUsbGpsGetInitDataLocation",  'i', "x" },
	{0X5881C826, nullptr,                                 "sceUsbGpsGetStaticNavMode",     '?', "" },
	{0X63D1F89D, nullptr,                                 "sceUsbGpsResetInitialPosition", '?', "" },
	{0X69E4AAA8, nullptr,                                 "sceUsbGpsSaveInitData",         '?', "" },
	{0X6EED4811, &WrapI_V<sceUsbGpsClose>,                "sceUsbGpsClose",                'i', "" },
	{0X7C16AC3A, &WrapI_U<sceUsbGpsGetState>,             "sceUsbGpsGetState",             'i', "x" },
	{0X934EC2B2, &WrapI_UU<sceUsbGpsGetData>,             "sceUsbGpsGetData",              'i', "xx" },
	{0X9D8F99E8, nullptr,                                 "sceUsbGpsSetPowerSaveMode",     '?', "" },
	{0X9F267D34, &WrapI_V<sceUsbGpsOpen>,                 "sceUsbGpsOpen",                 'i', "" },
	{0XA259CD67, nullptr,                                 "sceUsbGpsReset",                '?', "" },
	{0XA8ED0BC2, nullptr,                                 "sceUsbGpsSetStaticNavMode",     '?', "" },
};

void Register_sceUsbGps()
{
	RegisterModule("sceUsbGps", ARRAY_SIZE(sceUsbGps), sceUsbGps);
}

void GPS::init() {
	time_t currentTime;
	time(&currentTime);
	setGpsTime(&currentTime);

	gpsData.hdop      = 1.0f;
	gpsData.latitude  = 51.510357f;
	gpsData.longitude = -0.116773f;
	gpsData.altitude  = 19.0f;
	gpsData.speed     = 3.0f;
	gpsData.bearing   = 35.0f;
	gpsData.garbage2  = 513;

	satData.satellites_in_view = 12;
	for (unsigned char i = 0; i < satData.satellites_in_view; i++) {
		satData.satInfo[i].id = i + 1; // 1 .. 32
		satData.satInfo[i].elevation = 20;
		satData.satInfo[i].azimuth = i * (360/satData.satellites_in_view);
		satData.satInfo[i].snr = 45;
		satData.satInfo[i].good = !!(i % 3);
	}
}

void GPS::setGpsTime(time_t *time) {
	struct tm *gpsTime;
	gpsTime = gmtime(time);

	gpsData.year   = (short)(gpsTime->tm_year + 1900);
	gpsData.month  = (short)(gpsTime->tm_mon + 1);
	gpsData.date   = (short)gpsTime->tm_mday;
	gpsData.hour   = (short)gpsTime->tm_hour;
	gpsData.minute = (short)gpsTime->tm_min;
	gpsData.second = (short)gpsTime->tm_sec;
}

void GPS::setGpsData(long long gpsTime, float hdop, float latitude, float longitude, float altitude, float speed, float bearing) {
	lastGPSTime = gpsTime;
	setGpsTime((time_t*)&gpsTime);

	gpsData.hdop      = hdop;
	gpsData.latitude  = latitude;
	gpsData.longitude = longitude;
	gpsData.altitude  = altitude;
	gpsData.speed     = speed;
	gpsData.bearing   = bearing;
}

void GPS::setSatInfo(short index, unsigned char id, unsigned char elevation, short azimuth, unsigned char snr, unsigned char good) {
	satData.satInfo[index].id = id;
	satData.satInfo[index].elevation = elevation;
	satData.satInfo[index].azimuth = azimuth;
	satData.satInfo[index].snr = snr;
	satData.satInfo[index].good = good;
	satData.satellites_in_view = index + 1;
}

GpsData* GPS::getGpsData() {
	return &gpsData;
}

SatData* GPS::getSatData() {
	return &satData;
}
