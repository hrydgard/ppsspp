// Some info from
// https://controllers.fandom.com/wiki/Sony_DualSense

#include "Windows/Hid/DualShock.h"
#include "Windows/Hid/DualSense.h"
#include "Windows/Hid/HidInputDevice.h"
#include "Windows/Hid/HidCommon.h"

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

// https://github.com/ds4windowsapp/DS4Windows/blob/65609b470f53a4f832fb07ac24085d3e28ec15bd/DS4Windows/DS4Library/InputDevices/DualSenseDevice.cs#L905

// Sends initialization packet to DualSense
bool InitializeDualSense(HANDLE handle, int outReportSize) {
	if (outReportSize != sizeof(DualSenseOutputReport)) {
		return false;
	}

	DualSenseOutputReport report{};
	report.reportId = 2;
	report.flags1 = 0x0C;
	report.flags2 = 0x15;
	report.muteLED = 1;
	report.playerLights = 1;
	report.enableBrightness = 1;
	report.brightness = 0;  // 0 = high, 1 = medium, 2 = low
	report.lightbarRed = LED_R;
	report.lightbarGreen = LED_G;
	report.lightbarBlue = LED_B;
	return WriteReport(handle, report);
}

bool ShutdownDualsense(HANDLE handle, int outReportSize) {
	if (outReportSize != sizeof(DualSenseOutputReport)) {
		return false;
	}

	DualSenseOutputReport report{};
	report.reportId = 2;
	report.flags1 = 0x0C;
	report.flags2 = 0x15;
	report.muteLED = 1;
	report.playerLights = 0;
	report.enableBrightness = 1;
	report.brightness = 2;  // 0 = high, 1 = medium, 2 = low
	report.lightbarRed = 0;
	report.lightbarGreen = 0;
	report.lightbarBlue = 0;
	return WriteReport(handle, report);
}

// So strange that this is different!
struct DualSenseInputReport {
	u8 firstByte;  // must be 1

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
};

bool ReadDualSenseInput(HANDLE handle, HIDControllerState *state) {
	BYTE inputReport[64]{}; // 64-byte input report for DS4
	DWORD bytesRead = 0;
	if (!ReadFile(handle, inputReport, sizeof(inputReport), &bytesRead, nullptr)) {
		const u32 error = GetLastError();
		return false;
	}

	DualSenseInputReport report{};
	static_assert(sizeof(report) < sizeof(inputReport));
	if (bytesRead < 14) {
		return false;
	}

	// OK, check the first byte to figure out what we're dealing with here.
	if (inputReport[0] != 1) {
		// Wrong data
		return false;
	}
	// const bool isBluetooth = (reportId == 0x11 || reportId == 0x31);

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

	u32 buttons{};
	report.buttons[2] &= 3;  // Remove noise
	memcpy(&buttons, &report.buttons[0], 3);

	// Clear out and re-fill the DPAD, it works differently somehow
	buttons &= ~0xF;
	buttons |= DecodePSHatSwitch(report.buttons[0] & 0xF);

	state->buttons = buttons;
	return true;
}
