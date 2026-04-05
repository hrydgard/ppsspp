#pragma once

#include "Common/CommonWindows.h"
#include "Windows/Hid/HidInputDevice.h"

enum PSDPadButton : u32 {
	PS_DPAD_UP = 1, // These dpad ones are not real, we convert from hat switch format.
	PS_DPAD_DOWN = 2,
	PS_DPAD_LEFT = 4,
	PS_DPAD_RIGHT = 8,
};

enum PSHIDButton : u32 {
	PS_BTN_SQUARE = 16,
	PS_BTN_CROSS = 32,
	PS_BTN_TRIANGLE = 64,
	PS_BTN_CIRCLE = 128,

	PS_BTN_L1 = (1 << 8),
	PS_BTN_R1 = (1 << 9),
	PS_BTN_L2 = (1 << 10),
	PS_BTN_R2 = (1 << 11),
	PS_BTN_SHARE = (1 << 12),
	PS_BTN_OPTIONS = (1 << 13),
	PS_BTN_L3 = (1 << 14),
	PS_BTN_R3 = (1 << 15),
	PS_BTN_PS_BUTTON = (1 << 16),
	PS_BTN_TOUCHPAD = (1 << 17),
};

inline u32 DecodePSHatSwitch(u8 dpad) {
	u32 buttons = 0;
	if (dpad == 0 || dpad == 1 || dpad == 7) {
		buttons |= PS_DPAD_UP;
	}
	if (dpad == 1 || dpad == 2 || dpad == 3) {
		buttons |= PS_DPAD_RIGHT;
	}
	if (dpad == 3 || dpad == 4 || dpad == 5) {
		buttons |= PS_DPAD_DOWN;
	}
	if (dpad == 5 || dpad == 6 || dpad == 7) {
		buttons |= PS_DPAD_LEFT;
	}
	return buttons;
}

bool InitializeDualShock(HANDLE handle, int outReportSize);
bool ShutdownDualShock(HANDLE handle, int outReportSize);
bool ReadDualShockInput(HANDLE handle, HIDControllerState *state, int inReportSize);
void GetPSButtonInputMappings(const ButtonInputMapping **mappings, size_t *size);
