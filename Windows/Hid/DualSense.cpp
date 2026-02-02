// Some info from
// https://controllers.fandom.com/wiki/Sony_DualSense
// https://github.com/ds4windowsapp/DS4Windows/blob/65609b470f53a4f832fb07ac24085d3e28ec15bd/DS4Windows/DS4Library/InputDevices/DualSenseDevice.cs#L905

// Bluetooth information
// When connecting via bluetooth, outReportSize is 547. However, you can send a smaller 78-byte format instead,
// and if you do, you'll get smaller reports back. They are organized more like the USB messages.

#include <cstring>

#include "Windows/Hid/DualShock.h"
#include "Windows/Hid/DualSense.h"
#include "Windows/Hid/HidInputDevice.h"
#include "Windows/Hid/HidCommon.h"

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
	u8 enableLightbar;
	u8 fade;
	u8 brightness;
	u8 playerLights;
	u8 lightbarRed;
	u8 lightbarGreen;
	u8 lightbarBlue;
};
static_assert(sizeof(DualSenseOutputReport) == 48);

static void FillDualSenseOutputReport(DualSenseOutputReport *report, bool lightsOn) {
	report->reportId = 2;
	report->flags1 = 0x0C;
	report->flags2 = 0x15;
	report->muteLED = 1;
	// Turn on the lights.
	report->playerLights = lightsOn ? 1 : 0;
	report->enableLightbar = 1;
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
		_dbg_assert_(outReportSize == 547);
		// BT case. Not as fun! NOTE: We use the small size method.
		std::vector<uint8_t> buffer;
		buffer.resize(78);

		// 1. Report ID 0x31 is required for Extended Features (Rumble/LED) over BT
		buffer[0] = 0x31;
		// 2. Bluetooth Header: 0x02 sets the "tag" for the controller to process the report
		buffer[1] = 0x02;

		// We skip report.reportId because buffer[0] is already 0x31
		// Copy everything after reportId into buffer starting at index 2
		memcpy(&buffer[2], (uint8_t*)&report + 1, sizeof(DualSenseOutputReport) - 1);
		buffer[3] = 0x15;

		// Calculate CRC over the first 74 bytes (0 to 73)
		// This function includes the 0xA2 "hidden" seed internally
		uint32_t crc = ComputeDualSenseBTCRC(buffer.data(), 74);

		// Append CRC in Little Endian
		memcpy(buffer.data() + 74, &crc, 4);
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

// Templated to handle different DualSense struct layouts.
template<class T>
static void ReadReport(HIDControllerState* state, u32 *buttons, const T& report) {
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
	*buttons = 0;
	memcpy(buttons, &report.buttons[0], 3);
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
		ReadReport(state, &buttons, report);
		// Fall through to button processing
	} else if (inputReport[0] == 0x31 && inReportSize >= 547) {
		// A valid bluetooth packet, large format. Probably we can delete this, see the note
		// at the top of thie file. The layout is a bit different!
		// A valid USB packet.
		DualSenseInputReportBT report;
		memcpy(&report, inputReport, sizeof(report));
		ReadReport(state, &buttons, report);

		// Fall through to button processing
	} else if (inputReport[0] == 0x31 && inReportSize == 78) {
		// Bluetooth packet, short and fast format.
		DualSenseInputReportUSB report;
		// Note: These bluetooth packets are offset from the USB packets by 1.
		memcpy(((uint8_t *)&report) + 1, inputReport + 2, sizeof(report) - 1);
		ReadReport(state, &buttons, report);

		// Fall through to button processing
	} else if (inputReport[0] == 0x1 && inReportSize == 78) {
		// Simple BT packet. This shouldn't happen if we correctly initialize the gamepad.
		buttons = 0;
		// Fall through to button processing
	} else {
		// Unknown packet (simple BT?). Ignore.
		WARN_LOG(Log::System, "Unexpected: %02x type, %d size", inputReport[0], (int)inReportSize);
		return true;
	}

	// Shared button handling.

	// Clear out and re-fill the DPAD, it works differently somehow
	const u32 hat = buttons & 0xF;
	buttons &= ~0xF;
	buttons |= DecodePSHatSwitch(hat);

	state->buttons = buttons;
	return true;
}
