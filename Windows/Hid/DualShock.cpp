#include "Windows/Hid/DualShock.h"
#include "Windows/Hid/HidInputDevice.h"
#include "Windows/Hid/HidCommon.h"

static const ButtonInputMapping g_psInputMappings[] = {
	{PS_DPAD_UP, NKCODE_DPAD_UP},
	{PS_DPAD_DOWN, NKCODE_DPAD_DOWN},
	{PS_DPAD_LEFT, NKCODE_DPAD_LEFT},
	{PS_DPAD_RIGHT, NKCODE_DPAD_RIGHT},
	{PS_BTN_SQUARE, NKCODE_BUTTON_4},
	{PS_BTN_TRIANGLE, NKCODE_BUTTON_3},
	{PS_BTN_CIRCLE, NKCODE_BUTTON_1},
	{PS_BTN_CROSS, NKCODE_BUTTON_2},
	{PS_BTN_PS_BUTTON, NKCODE_HOME},
	{PS_BTN_SHARE, NKCODE_BUTTON_9},
	{PS_BTN_OPTIONS, NKCODE_BUTTON_10},
	{PS_BTN_L1, NKCODE_BUTTON_7},
	{PS_BTN_R1, NKCODE_BUTTON_8},
	{PS_BTN_TOUCHPAD, NKCODE_BUTTON_11},
	// {PS_BTN_L2, NKCODE_BUTTON_L2},  // These are done by the analog triggers.
	// {PS_BTN_R2, NKCODE_BUTTON_R2},
	{PS_BTN_L3, NKCODE_BUTTON_THUMBL},
	{PS_BTN_R3, NKCODE_BUTTON_THUMBR},
};

void GetPSButtonInputMappings(const ButtonInputMapping **mappings, size_t *size) {
	*mappings = g_psInputMappings;
	*size = ARRAY_SIZE(g_psInputMappings);
}

enum class DS4FeatureBits : u8 {
	VOL_L = 0x10,
	VOL_R = 0x20,
	MIC_VOL = 0x40,
	SPEAKER_VOL = 0x80,
	RUMBLE = 0x1,
	LIGHTBAR = 0x2,
	FLASH = 0x4,
};
ENUM_CLASS_BITOPS(DS4FeatureBits);

struct DualshockOutputReport {
	u8 reportId;
	u8 featureBits;
	u8 two;
	u8 pad;
	u8 rumbleRight;
	u8 rumbleLeft;
	u8 lightbarRed;
	u8 lightbarGreen;
	u8 lightbarBlue;
	u8 padding[23];
};
static_assert(sizeof(DualshockOutputReport) == 32);

struct DualShockInputReport {
	u8 lx;
	u8 ly;
	u8 rx;
	u8 ry;
	u8 buttons[3];  // note, starts at 5 so not aligned
	u8 l2_analog;
	u8 r2_analog;
	u8 pad[2];
	u8 battery;
	// Then there's motion and all kinds of stuff.
};

static void FillDualShockOutputReport(DualshockOutputReport *report, bool lightsOn) {
	report->reportId = 0x05;  // USB default
	report->featureBits = (u8)(DS4FeatureBits::RUMBLE | DS4FeatureBits::LIGHTBAR | DS4FeatureBits::FLASH); // Flags: enable lightbar, rumble, etc.
	report->two = 2;
	report->rumbleRight = 0;
	report->rumbleLeft = 0;

	// Turn on or off the lights.
	report->lightbarRed = lightsOn ? LED_R : 0;
	report->lightbarGreen = lightsOn ? LED_G : 0;
	report->lightbarBlue = lightsOn ? LED_B : 0;
}

bool InitializeDualShock(HANDLE handle, int outReportSize) {
	if (outReportSize != sizeof(DualshockOutputReport)) {
		WARN_LOG(Log::UI, "DS4 unexpected report size %d", outReportSize);
		return false;
	}

	DualshockOutputReport report{};
	FillDualShockOutputReport(&report, true);
	return WriteReport(handle, report);
}

bool ShutdownDualShock(HANDLE handle, int outReportSize) {
	if (outReportSize != sizeof(DualshockOutputReport)) {
		WARN_LOG(Log::UI, "DS4 unexpected report size %d", outReportSize);
		return false;
	}

	DualshockOutputReport report{};
	FillDualShockOutputReport(&report, false);
	return WriteReport(handle, report);
}

bool ReadDualShockInput(HANDLE handle, HIDControllerState *state, int inReportSize) {
	if (inReportSize > 1024) {
		return false;
	}
	BYTE inputReport[1024]{};
	DWORD bytesRead = 0;
	if (!ReadFile(handle, inputReport, inReportSize, &bytesRead, nullptr)) {
		u32 error = GetLastError();
		return false;
	}
	DualShockInputReport report{};
	static_assert(sizeof(report) < sizeof(inputReport));
	if (bytesRead < 14) {
		return false;
	}

	// OK, check the first byte to figure out what we're dealing with here.
	int offset = 1;
	int reportId;
	if (inputReport[0] == 0xA1) {
		// 2-byte bluetooth frame
		offset = 2;
		reportId = inputReport[1];
	} else {
		offset = 1;
		reportId = inputReport[0];
	}
	// const bool isBluetooth = (reportId == 0x11 || reportId == 0x31);

	memcpy(&report, inputReport + offset, sizeof(report));

	// Center the sticks.
	state->stickAxes[HID_STICK_LX] = report.lx - 128;
	state->stickAxes[HID_STICK_LY] = report.ly - 128;
	state->stickAxes[HID_STICK_RX] = report.rx - 128;
	state->stickAxes[HID_STICK_RY] = report.ry - 128;

	// Copy over the triggers.
	state->triggerAxes[HID_TRIGGER_L2] = report.l2_analog;
	state->triggerAxes[HID_TRIGGER_R2] = report.r2_analog;

	state->accValid = false;

	u32 buttons{};
	int frameCounter = report.buttons[2] >> 2;
	report.buttons[2] &= 3;
	memcpy(&buttons, &report.buttons[0], 3);

	// Clear out and re-fill the DPAD, it works differently somehow
	buttons &= ~0xF;
	buttons |= DecodePSHatSwitch(report.buttons[0] & 0xF);

	state->buttons = buttons;
	return true;
}
