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

constexpr int SwitchPro_IMU_DATA_OFFSET = 13; // Where IMU starts in 0x30 report
constexpr float ACCEL_SCALE = 0.000244f;      // Raw to G (range ±8G)
constexpr float GYRO_SCALE = 0.070f;          // Raw to deg/s (range ±2000dps)

struct SwitchProInputReport {
	u8 reportId;
	u8 padding;
	u8 batteryLevel;
	u8 buttons[3];
	u8 lStick[3]; // 2 12-bit values.
	u8 rStick[3]; // 2 12-bit values.

	u8 vibrator_report;
	u8 imu_data[36]; // 3 samples of 6-axis data (12 bytes each)
};

static void ProcessIMU(const u8 *data, HIDControllerState *state) {
	// Data contains 3 samples to account for Bluetooth jitter.
	// We just take the latest one (index 2).
	const u8* s = &data[24];

	// Raw values are little endian int16
	short ax = (short)(s[0] | (s[1] << 8));
	short ay = (short)(s[2] | (s[3] << 8));
	short az = (short)(s[4] | (s[5] << 8));

	short gx = (short)(s[6] | (s[7] << 8));
	short gy = (short)(s[8] | (s[9] << 8));
	short gz = (short)(s[10] | (s[11] << 8));

	// Convert to physical units
	state->accelerometer[0] = ax * ACCEL_SCALE;
	state->accelerometer[1] = ay * ACCEL_SCALE;
	state->accelerometer[2] = az * ACCEL_SCALE;

	// TODO: Consider if these should be radians or degrees. Check what it is...
	state->gyro[0] = gx * GYRO_SCALE;
	state->gyro[1] = gy * GYRO_SCALE;
	state->gyro[2] = gz * GYRO_SCALE;
}

struct StickCal {
	u16 x_center, y_center;
	u16 x_min, y_min;
	u16 x_max, y_max;
};

// Assuming 'data' is the 9-byte payload starting from byte 20 of the input report
void DecodeCalibration(const u8* data, StickCal* cal) {
	// These are 12-bit values packed into 9 bytes
	cal->x_max = ((data[1] << 8) & 0xF00) | data[0];
	cal->y_max = (data[2] << 4) | (data[1] >> 4);
	cal->x_center = ((data[4] << 8) & 0xF00) | data[3];
	cal->y_center = (data[5] << 4) | (data[4] >> 4);
	cal->x_min = ((data[7] << 8) & 0xF00) | data[6];
	cal->y_min = (data[8] << 4) | (data[7] >> 4);
}

static void DecodeSwitchProStick(const u8 *stickData, s8 *outX, s8 *outY) {
	int x = ((stickData[1] & 0xF) << 8) | (stickData[0]);
	int y = (stickData[1] >> 4) | (stickData[2] << 4);

	// For some reason the values are not really centered. Let's approximate.
	// We probably should add some low level calibration?
	x = (x - 2048) / 12;
	y = -(y - 1950) / 12;

	*outX = (s8)clamp_value(x, -128, 127);
	*outY = (s8)clamp_value(y, -128, 127);
}

// Subcommand helper
static bool SendSwitchSubcommand(HANDLE handle, u8 subcommand, const u8 *data, u8 len) {
	u8 buf[64] = {0x01}; // Report ID 0x01 for subcommands
	static u8 global_packet_num = 0;
	buf[1] = global_packet_num++;
	// ... Fill in rumble data (required even if zero)
	buf[10] = subcommand;
	if (data && len > 0) memcpy(&buf[11], data, len);

	DWORD written;
	return WriteFile(handle, buf, 64, &written, nullptr);
}

bool InitializeSwitchPro(HANDLE handle) {
	// 1. USB Handshake (only needed for wired, safe for BT)
	u8 cmd_usb_enable = 0x01;
	DWORD w;
	u8 handshake[2] = {0x80, 0x01};
	WriteFile(handle, handshake, 2, &w, nullptr);
	handshake[1] = 0x02; // Handshake 2
	WriteFile(handle, handshake, 2, &w, nullptr);

	// 2. Set Full Input Mode (0x30)
	u8 mode = 0x30;
	SendSwitchSubcommand(handle, 0x03, &mode, 1);

	// 3. Enable IMU
	u8 enable = 0x01;
	SendSwitchSubcommand(handle, 0x40, &enable, 1);

	return true;
}

bool ReadSwitchProInput(HANDLE handle, HIDControllerState *state) {
	BYTE inputReport[SwitchPro_INPUT_REPORT_LEN]{};
	DWORD bytesRead = 0;
	if (!ReadFile(handle, inputReport, sizeof(inputReport), &bytesRead, nullptr)) {
		u32 error = GetLastError();
		return false;
	}

	// Bluetooth often uses 0x21 for sub-command responses, 0x30 for standard data
	if (inputReport[0] != 0x30 && inputReport[0] != 0x21) return false;

	const SwitchProInputReport* report = (const SwitchProInputReport*)inputReport;
	u32 buttons = 0;
	memcpy(&state->buttons, &report->buttons[0], 3);
	// Decode Sticks (Keep your existing logic)
	DecodeSwitchProStick(report->lStick, &state->stickAxes[HID_STICK_LX], &state->stickAxes[HID_STICK_LY]);
	DecodeSwitchProStick(report->rStick, &state->stickAxes[HID_STICK_RX], &state->stickAxes[HID_STICK_RY]);

	// Decode IMU if report type is 0x30
	if (inputReport[0] == 0x30) {
		state->accValid = true;
		ProcessIMU(report->imu_data, state);
	}

	return true;
}
