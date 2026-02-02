#include "Common/Math/math_util.h"
#include "Windows/Hid/HidInputDevice.h"
#include "Windows/Hid/SwitchPro.h"
#include "Windows/Hid/HidCommon.h"

enum HIDButton : u32 {
	SWITCH_PRO_BTN_Y = (1 << 0),
	SWITCH_PRO_BTN_X = (1 << 1),
	SWITCH_PRO_BTN_B = (1 << 2),
	SWITCH_PRO_BTN_A = (1 << 3),
	SWITCH_PRO_BTN_R1 = (1 << 6),
	SWITCH_PRO_BTN_R2 = (1 << 7),
	SWITCH_PRO_BTN_L3 = (1 << 11),
	SWITCH_PRO_BTN_R3 = (1 << 10),
	SWITCH_PRO_BTN_SHARE = (1 << 8),
	SWITCH_PRO_BTN_OPTIONS = (1 << 9),
	SWITCH_PRO_BTN_PS_BUTTON = (1 << 12),
	SWITCH_PRO_BTN_CAPTURE = (1 << 13),
	SWITCH_PRO_DPAD_DOWN = (1 << 16),
	SWITCH_PRO_DPAD_UP = (1 << 17),
	SWITCH_PRO_DPAD_RIGHT = (1 << 18),
	SWITCH_PRO_DPAD_LEFT = (1 << 19),
	SWITCH_PRO_BTN_L1 = (1 << 22),
	SWITCH_PRO_BTN_L2 = (1 << 23),
};

static const ButtonInputMapping g_switchProInputMappings[] = {
	{SWITCH_PRO_DPAD_UP, NKCODE_DPAD_UP},
	{SWITCH_PRO_DPAD_DOWN, NKCODE_DPAD_DOWN},
	{SWITCH_PRO_DPAD_LEFT, NKCODE_DPAD_LEFT},
	{SWITCH_PRO_DPAD_RIGHT, NKCODE_DPAD_RIGHT},
	{SWITCH_PRO_BTN_Y, NKCODE_BUTTON_4},
	{SWITCH_PRO_BTN_X, NKCODE_BUTTON_1},
	{SWITCH_PRO_BTN_B, NKCODE_BUTTON_2},
	{SWITCH_PRO_BTN_A, NKCODE_BUTTON_3},
	{SWITCH_PRO_BTN_PS_BUTTON, NKCODE_HOME},
	{SWITCH_PRO_BTN_SHARE, NKCODE_BUTTON_9},
	{SWITCH_PRO_BTN_OPTIONS, NKCODE_BUTTON_10},
	{SWITCH_PRO_BTN_L1, NKCODE_BUTTON_7},
	{SWITCH_PRO_BTN_R1, NKCODE_BUTTON_8},
	{SWITCH_PRO_BTN_L2, NKCODE_BUTTON_L2},  // No analog triggers.
	{SWITCH_PRO_BTN_R2, NKCODE_BUTTON_R2},
	{SWITCH_PRO_BTN_L3, NKCODE_BUTTON_THUMBL},
	{SWITCH_PRO_BTN_R3, NKCODE_BUTTON_THUMBR},
};

void GetSwitchButtonInputMappings(const ButtonInputMapping **mappings, size_t *size) {
	*mappings = g_switchProInputMappings;
	*size = ARRAY_SIZE(g_switchProInputMappings);
}

enum class SwitchProSubCmd {
	SET_INPUT_MODE = 0x03,
	SET_LOW_POWER_STATE = 0x08,
	SPI_FLASH_READ = 0x10,
	SET_LIGHTS = 0x30, // LEDs on controller
	SET_HOME_LIGHT = 0x38,
	ENABLE_IMU = 0x40,
	SET_IMU_SENS = 0x41,
	ENABLE_VIBRATION = 0x48,
};

constexpr int SwitchPro_INPUT_REPORT_LEN = 362;
constexpr int SwitchPro_OUTPUT_REPORT_LEN = 49;
constexpr int SwitchPro_RUMBLE_REPORT_LEN = 64;

static const u8 g_switchProCmdBufHeader[] = {0x0, 0x1, 0x40, 0x40, 0x0, 0x1, 0x40, 0x40};

bool InitializeSwitchPro(HANDLE handle) {
	return true;
}

struct SwitchProInputReport {
	u8 reportId;
	u8 padding;
	u8 battery;
	u8 buttons[3];
	u8 lStick[3]; // 2 12-bit values.
	u8 rStick[3]; // 2 12-bit values.
	// Next up is gyro and all sorts of stuff we don't care about right now.
};

static void DecodeSwitchProStick(const u8 *stickData, s8 *outX, s8 *outY) {
	int x = ((stickData[1] & 0xF) << 8) | (stickData[0]);
	int y = (stickData[1] >> 4) | (stickData[2] << 4);

	// For some reason the values are not really centered. Let's approximate.
	// We probably should add some low level calibration?
	x = (x - 2048) / 12;
	y = (y - 1950) / 12;

	*outX = (s8)clamp_value(x, -128, 127);
	*outY = (s8)clamp_value(y, -128, 127);
	// INFO_LOG(Log::sceCtrl, "Switch Pro input: x=%d, y=%d, cx=%d, cy=%d", x, y, *outX, *outY);
}

bool ReadSwitchProInput(HANDLE handle, HIDControllerState *state) {
	BYTE inputReport[SwitchPro_INPUT_REPORT_LEN]{}; // 64-byte input report for Switch Pro
	DWORD bytesRead = 0;
	if (!ReadFile(handle, inputReport, sizeof(inputReport), &bytesRead, nullptr)) {
		u32 error = GetLastError();
		return false;
	}

	if (inputReport[0] != 0x30) {
		// Not a Switch Pro controller input report.
		return false;
	}

	SwitchProInputReport report{};
	memcpy(&report, inputReport, sizeof(report));

	u32 buttons = 0;
	memcpy(&state->buttons, &report.buttons[0], 3);
	// INFO_LOG(Log::sceCtrl, "Switch Pro input: buttons=%08x", state->buttons);

	DecodeSwitchProStick(report.lStick, &state->stickAxes[HID_STICK_LX], &state->stickAxes[HID_STICK_LY]);
	DecodeSwitchProStick(report.rStick, &state->stickAxes[HID_STICK_RX], &state->stickAxes[HID_STICK_RY]);
	return true;
}
