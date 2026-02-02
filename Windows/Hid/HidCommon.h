#pragma once

#include "Common/CommonWindows.h"
#include "Windows/Hid/HidInputDevice.h"

constexpr u8 LED_R = 0x05;
constexpr u8 LED_G = 0x10;
constexpr u8 LED_B = 0x40;

enum HidStickAxis : u32 {
	HID_STICK_LX = 0,
	HID_STICK_LY = 1,
	HID_STICK_RX = 2,
	HID_STICK_RY = 3,
};

enum HidTriggerAxis : u32 {
	HID_TRIGGER_L2 = 0,
	HID_TRIGGER_R2 = 1,
};

bool WriteReport(HANDLE handle, const u8 *data, size_t size);

template<class T>
inline bool WriteReport(HANDLE handle, const T &report) {
	return WriteReport(handle, (const u8 *)&report, sizeof(T));
}
