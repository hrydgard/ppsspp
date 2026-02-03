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

// Standard CRC32 Update function. Used for DualSense bluetooth messages.
constexpr inline uint32_t UpdateCRC32(uint32_t crc, const uint8_t* data, size_t len) {
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int j = 0; j < 8; j++) {
			crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
		}
	}
	return crc;
}

// The specific "Sony Bluetooth" CRC32
constexpr inline uint32_t ComputeDualSenseBTCRC(const uint8_t* buffer, size_t len) {
	constexpr uint8_t btHeader = 0xA2;

	// Step 1: Initialize with 0xFFFFFFFF and process the "Hidden" BT header
	constexpr uint32_t headerCRC = UpdateCRC32(0xFFFFFFFF, &btHeader, 1);

	// Step 2: Continue the calculation with the actual Report ID and Data
	const uint32_t crc = UpdateCRC32(headerCRC, buffer, len);

	// Step 3: Final XOR (the ~ in C#)
	return ~crc;
}
