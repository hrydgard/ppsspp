// Some info from
// https://controllers.fandom.com/wiki/Sony_DualSense

#include <cstring>
#include "Windows/Hid/DualShock.h"
#include "Windows/Hid/DualSense.h"
#include "Windows/Hid/HidInputDevice.h"
#include "Windows/Hid/HidCommon.h"

#pragma pack(push,1)
struct DualSenseOutputReport {
	u8 reportId;
	u8 flags1;
	u8 flags2;
	u8 rumbleRight;
	u8 rumbleLeft;
	u8 pad[2];
	u8 muteLED;
	u8 micMute;  // 10
	u8 other[32];
	u8 enableBrightness;
	u8 fade;
	u8 brightness;
	u8 playerLights;
	u8 lightbarRed;
	u8 lightbarGreen;
	u8 lightbarBlue;
};
static_assert(sizeof(DualSenseOutputReport) == 48);

struct DualSenseInputReportUSB {
	u8 reportId;  // Must be 1

	u8 lx;
	u8 ly;
	u8 rx;
	u8 ry;

	u8 l2_analog;
	u8 r2_analog;

	u8 frameCounter;  // 7

	u8 buttons[3];  // 8-10
	u8 pad[5];  // 11,12,13,14,15

	s16 gyroscope[3];
	s16 accelerometer[3];

	// There's more stuff after here.
};

#pragma pack(push, 1)

struct DualSenseInputReportBT {
	u8 reportId;  // Must be 0x31
	u8 seq_tag;   // Sequence tag / Transaction header

	u8 lx;
	u8 ly;
	u8 rx;
	u8 ry;
	u8 l2_analog;
	u8 r2_analog;

	u8 frameCounter; // Offset 8

	// The BT struct differs from the USB one here:
	// USB has buttons immediately after frameCounter. BT has 6 bytes of status/battery data first.
	u8 unknown_status[6];

	u8 buttons[3];   // Offset 15, 16, 17
	u8 device_extra; // Offset 18 (often contains power/battery status)

	u8 pad[3];       // Offset 19, 20, 21
	s16 gyroscope[3];
	s16 accelerometer[3];

	// There's more stuff after here.
};
#pragma pack(pop)

// https://github.com/ds4windowsapp/DS4Windows/blob/65609b470f53a4f832fb07ac24085d3e28ec15bd/DS4Windows/DS4Library/InputDevices/DualSenseDevice.cs#L905

static void FillDualSenseOutputReport(DualSenseOutputReport *report, bool lightsOn) {
	report->reportId = 2;
	report->flags1 = 0x0C;
	report->flags2 = 0x15;
	report->muteLED = 1;
	// Turn on the lights.
	report->playerLights = 1;
	report->enableBrightness = 1;
	report->brightness = lightsOn ? 0 : 2;  // 0 = high, 1 = medium, 2 = low
	report->lightbarRed = lightsOn ? LED_R : 0;
	report->lightbarGreen = lightsOn ? LED_G : 0;
	report->lightbarBlue = lightsOn ? LED_B : 0;
}

static bool SendReport(HANDLE handle, const DualSenseOutputReport &report, int outReportSize) {
	if (outReportSize == sizeof(DualSenseOutputReport)) {
		// USB case: Just write the plain struct.
		return WriteReport(handle, report);
	} else if (outReportSize >= 547) {
		// BT case. Not as fun!
		std::vector<uint8_t> buffer(outReportSize, 0);

		// 1. Report ID 0x31 is required for Extended Features (Rumble/LED) over BT
		buffer[0] = 0x31;
		// 2. Bluetooth Header: 0x02 sets the "tag" for the controller to process the report
		buffer[1] = 0x02;

		// We skip report.reportId because buffer[0] is already 0x31
		// Copy everything after reportId into buffer starting at index 2
		memcpy(&buffer[2], (uint8_t*)&report + 1, sizeof(DualSenseOutputReport) - 1);

		// 4. Calculate CRC32 
		// The DualSense expects a CRC of the Report ID (0x31) + the entire data payload.
		// For a 547 byte report, the CRC is placed at index 543 (the last 4 bytes).
		uint32_t crc = ComputePSControllerCRC(buffer.data(), 543);

		// Append CRC in Little Endian
		// memcpy(buffer.data() + 543, &crc, 4);
		buffer[543] = (uint8_t)(crc & 0xFF);
		buffer[544] = (uint8_t)((crc >> 8) & 0xFF);
		buffer[545] = (uint8_t)((crc >> 16) & 0xFF);
		buffer[546] = (uint8_t)((crc >> 24) & 0xFF);

		return WriteReport(handle, buffer.data(), buffer.size());
	} else {
		ERROR_LOG(Log::System, "SendReport: Unexpected outReportSize: %d", outReportSize);
		return false;
	}
}

// Sends initialization packet to DualSense
bool InitializeDualSense(HANDLE handle, int outReportSize) {
	DualSenseOutputReport report{};
	FillDualSenseOutputReport(&report, true);
	return SendReport(handle, report, outReportSize);
}

bool ShutdownDualsense(HANDLE handle, int outReportSize) {
	if (outReportSize != sizeof(DualSenseOutputReport)) {
		return false;
	}
	DualSenseOutputReport report{};
	FillDualSenseOutputReport(&report, false);
	return SendReport(handle, report, outReportSize);
}

bool ReadDualSenseInput(HANDLE handle, HIDControllerState *state, int inReportSize) {
	if (inReportSize > 1024) {
		return false;
	}
	BYTE inputReport[1024]{};

	DWORD bytesRead = 0;
	if (!ReadFile(handle, inputReport, inReportSize, &bytesRead, nullptr)) {
		const u32 error = GetLastError();
		return false;
	}

	if (bytesRead < 14) {
		return false;
	}

	u32 buttons{};

	// OK, check the first byte to figure out what we're dealing with here.
	if (inputReport[0] == 0x1 && inReportSize == 64) {
		// A valid USB packet.
		DualSenseInputReportUSB report;
		memcpy(&report, inputReport, sizeof(report));

		// Center the sticks.
		state->stickAxes[HID_STICK_LX] = report.lx - 128;
		state->stickAxes[HID_STICK_LY] = report.ly - 128;
		state->stickAxes[HID_STICK_RX] = report.rx - 128;
		state->stickAxes[HID_STICK_RY] = report.ry - 128;

		// Copy over the triggers.
		state->triggerAxes[HID_TRIGGER_L2] = report.l2_analog;
		state->triggerAxes[HID_TRIGGER_R2] = report.r2_analog;

		const float accelScale = (1.0f / 8192.0f) * 9.81f;
		// We need to remap the axes a bit.
		state->accValid = true;
		state->accelerometer[0] = -report.accelerometer[2] * accelScale;
		state->accelerometer[1] = -report.accelerometer[0] * accelScale;
		state->accelerometer[2] = report.accelerometer[1] * accelScale;
		memcpy(&buttons, &report.buttons[0], 3);

		// Fall through to button processing
	} else if (inputReport[0] == 0x31 && inReportSize >= 547) {
		// A valid bluetooth packet. The layout is a bit different!
		// A valid USB packet.
		DualSenseInputReportUSB report;
		memcpy(&report, inputReport, sizeof(report));

		// Center the sticks.
		state->stickAxes[HID_STICK_LX] = report.lx - 128;
		state->stickAxes[HID_STICK_LY] = report.ly - 128;
		state->stickAxes[HID_STICK_RX] = report.rx - 128;
		state->stickAxes[HID_STICK_RY] = report.ry - 128;

		// Copy over the triggers.
		state->triggerAxes[HID_TRIGGER_L2] = report.l2_analog;
		state->triggerAxes[HID_TRIGGER_R2] = report.r2_analog;

		const float accelScale = (1.0f / 8192.0f) * 9.81f;
		// We need to remap the axes a bit.
		state->accValid = true;
		state->accelerometer[0] = -report.accelerometer[2] * accelScale;
		state->accelerometer[1] = -report.accelerometer[0] * accelScale;
		state->accelerometer[2] = report.accelerometer[1] * accelScale;
		memcpy(&buttons, &report.buttons[0], 3);

		// Fall through to button processing
	} else {
		// Unknown packet (simple BT?). Ignore.
		return false;
	}

	// Shared button handling.

	// Clear out and re-fill the DPAD, it works differently somehow
	const u32 hat = buttons & 0xF;
	buttons &= ~0xF;
	buttons |= DecodePSHatSwitch(hat);

	state->buttons = buttons;
	return true;
}
