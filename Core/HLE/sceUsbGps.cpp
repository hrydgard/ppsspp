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

#include <ctime>
#include <mutex>

#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/Serialize/Serializer.h"
#include "Common/Serialize/SerializeFuncs.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/FunctionWrappers.h"
#include "Core/HLE/sceUsbGps.h"
#include "Core/MemMapHelpers.h"

constexpr int AUTO_UPDATE_GPSTIME = 10;

GpsState gpsState = GPS_STATE_NOT_AVAILABLE;
GpsData gpsData;
SatData satData;
time_t lastGPSTime;
// The platform's location callbacks set these from their own thread.
static std::recursive_mutex gpsLock;

void __UsbGpsInit() {
	gpsState = GPS_STATE_NOT_AVAILABLE;
}

void __UsbGpsDoState(PointerWrap &p) {
	const bool wasWorking = gpsState == GPS_STATE_WORKING;
	auto s = p.Section("sceUsbGps", 0, 1);
	if (!s) {
		// Older states didn't save it, so mark it as not available.
		if (p.mode == p.MODE_READ) {
			if (wasWorking) {
				System_GPSCommand("close");
			}
			gpsState = GPS_STATE_NOT_AVAILABLE;
		}
		return;
	}

	Do(p, gpsState);
	if (p.mode == p.MODE_READ) {
		if (gpsState == GPS_STATE_WORKING) {
			GPS::init();
			System_GPSCommand("open");
		} else if (wasWorking) {
			System_GPSCommand("close");
		}
	}
}

void __UsbGpsShutdown() {
	gpsState = GPS_STATE_NOT_AVAILABLE;
	System_GPSCommand("close");
}

static int sceUsbGpsGetInitDataLocation(u32 addr) {
	return 0;
}

static int sceUsbGpsGetState(u32 stateAddr) {
	if (Memory::IsValidRange(stateAddr, 4)) {
		Memory::WriteUnchecked_U32(gpsState, stateAddr);
	}
	return 0;
}

static int sceUsbGpsOpen() {
	GPS::init();
	gpsState = GPS_STATE_WORKING;
	System_GPSCommand("open");
	return 0;
}

static int sceUsbGpsClose() {
	gpsState = GPS_STATE_NOT_AVAILABLE;
	System_GPSCommand("close");
	return 0;
}

static int sceUsbGpsGetData(u32 gpsDataAddr, u32 satDataAddr) {
	std::lock_guard<std::recursive_mutex> guard(gpsLock);
	time_t currentTime;
	time(&currentTime);
	if (difftime(currentTime, lastGPSTime) > AUTO_UPDATE_GPSTIME)
	{
		static time_t lastFakeUpdate = 0;
		if (difftime(currentTime, lastFakeUpdate) >= 1.0)
		{
			// Simulate fresh updates to satisfy MAPLUS 1/2 apps when real GPS data isn't available.
			// MAPLUS 1 and 3 are tolerant of received data and can ignore quality (GpsPositionQuality),
			// placing more importance on mode (GpsPositionMode), whereas MAPLUS 2 reflects
			// reception quality more accurately in-game.
			GPS::setGpsTime(&currentTime);
			lastFakeUpdate = currentTime;
		}
	}

	auto gpsPtr = PSPPointer<GpsData>::Create(gpsDataAddr);
	if (gpsPtr.IsValid()) {
		*gpsPtr = *GPS::getGpsData();
		gpsPtr.NotifyWrite("UsbGpsGetData");
	}
	auto satPtr = PSPPointer<SatData>::Create(satDataAddr);
	if (satPtr.IsValid()) {
		*satPtr = *GPS::getSatData();
		satPtr.NotifyWrite("UsbGpsGetData");
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
	RegisterHLEModule("sceUsbGps", ARRAY_SIZE(sceUsbGps), sceUsbGps);
}

void GPS::init() {
	time_t currentTime;
	time(&currentTime);
	lastGPSTime = 0;
	setGpsTime(&currentTime);

	// Dummy values with boundary/precision edge cases to check MAPLUS 1/2/3 behavior differences.
	gpsData.hdop      = 1.0f;
	gpsData.latitude  = 35.6811761f;
	gpsData.longitude = 139.7671141f;
	gpsData.altitude  = 3.6f;
	gpsData.speed     = 1000.9999f;
	gpsData.course    = 359.999f;
	gpsData.quality   = GPS_POSITION_QUALITY_GPS;
	gpsData.mode      = GPS_POSITION_MODE_3D;
	gpsData.reserve0  = 0;
	gpsData.reserve1  = 0; // geoid separation (m)?

	satData.satellites_in_view = 32;
	for (unsigned char i = 0; i < satData.satellites_in_view; i++) {
		satData.satInfo[i].id        = i + 1; // 1 .. 32
		satData.satInfo[i].elevation = 20 * ((i / 12) % 3 + 1); // 1-12=20 13-24=40 25-32=60
		satData.satInfo[i].azimuth   = (i % 12) * 30; // 0, 30, .. 330 deg
		satData.satInfo[i].snr       = 45;
		satData.satInfo[i].good      = !!(i % 3);
	}
}

void GPS::setGpsTime(time_t *time) {
	std::lock_guard<std::recursive_mutex> guard(gpsLock);
	struct tm *gpsTime;
	gpsTime = gmtime(time);

	gpsData.datetime.year        = (short)(gpsTime->tm_year + 1900);
	gpsData.datetime.month       = (short)(gpsTime->tm_mon + 1);
	gpsData.datetime.day         = (short)gpsTime->tm_mday;
	gpsData.datetime.hour        = (short)gpsTime->tm_hour;
	gpsData.datetime.minute      = (short)gpsTime->tm_min;
	gpsData.datetime.second      = (short)gpsTime->tm_sec;
	gpsData.datetime.microsecond = 0;
}

void GPS::setGpsData(long long gpsTime, float hdop, float latitude, float longitude, float altitude, float speed, float course) {
	std::lock_guard<std::recursive_mutex> guard(gpsLock);
	lastGPSTime = (time_t)gpsTime;
	setGpsTime(&lastGPSTime);

	gpsData.hdop      = hdop;
	gpsData.latitude  = latitude;
	gpsData.longitude = longitude;
	gpsData.altitude  = altitude;
	gpsData.speed     = speed;
	gpsData.course    = course;
}

void GPS::setSatInfo(short index, unsigned char id, unsigned char elevation, short azimuth, unsigned char snr, unsigned char good) {
	if (index < 0 || index >= (short)ARRAY_SIZE(satData.satInfo))
		return;
	std::lock_guard<std::recursive_mutex> guard(gpsLock);
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
