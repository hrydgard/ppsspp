#include <limits.h>
#include "base/NativeApp.h"
#include "Core/Config.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "XinputDevice.h"
#include "ControlMapping.h"


#ifndef XUSER_MAX_COUNT
#define XUSER_MAX_COUNT 4
#endif

// Permanent map. Actual mapping happens elsewhere.
static const struct {int from, to;} xinput_ctrl_map[] = {
	{XBOX_CODE_LEFTTRIGGER,         KEYCODE_BUTTON_L2},
	{XBOX_CODE_RIGHTTRIGGER,        KEYCODE_BUTTON_R2},
	{XINPUT_GAMEPAD_A,              KEYCODE_BUTTON_A},
	{XINPUT_GAMEPAD_B,              KEYCODE_BUTTON_B},
	{XINPUT_GAMEPAD_X,              KEYCODE_BUTTON_X},
	{XINPUT_GAMEPAD_Y,              KEYCODE_BUTTON_Y},
	{XINPUT_GAMEPAD_BACK,           KEYCODE_BUTTON_SELECT},
	{XINPUT_GAMEPAD_START,          KEYCODE_BUTTON_START},
	{XINPUT_GAMEPAD_LEFT_SHOULDER,  KEYCODE_BUTTON_L1},
	{XINPUT_GAMEPAD_RIGHT_SHOULDER, KEYCODE_BUTTON_R1},
	{XINPUT_GAMEPAD_LEFT_THUMB,     KEYCODE_BUTTON_THUMBL},
	{XINPUT_GAMEPAD_RIGHT_THUMB,    KEYCODE_BUTTON_THUMBR},
	{XINPUT_GAMEPAD_DPAD_UP,        KEYCODE_DPAD_UP},
	{XINPUT_GAMEPAD_DPAD_DOWN,      KEYCODE_DPAD_DOWN},
	{XINPUT_GAMEPAD_DPAD_LEFT,      KEYCODE_DPAD_LEFT},
	{XINPUT_GAMEPAD_DPAD_RIGHT,     KEYCODE_DPAD_RIGHT},
};

static const unsigned int xinput_ctrl_map_size = sizeof(xinput_ctrl_map) / sizeof(xinput_ctrl_map[0]);

XinputDevice::XinputDevice() {
	ZeroMemory( &this->prevState, sizeof(this->prevState) );
	this->check_delay = 0;
	this->gamepad_idx = -1;
}

struct Stick {
	float x;
	float y;
};
static Stick NormalizedDeadzoneFilter(short x, short y);

int XinputDevice::UpdateState(InputState &input_state) {
	if (this->check_delay-- > 0) return -1;
	XINPUT_STATE state;
	ZeroMemory( &state, sizeof(XINPUT_STATE) );

	DWORD dwResult;
	if (this->gamepad_idx >= 0)
		dwResult = XInputGetState( this->gamepad_idx, &state );
	else {
		// use the first gamepad that responds
		for (int i = 0; i < XUSER_MAX_COUNT; i++) {
			dwResult = XInputGetState( i, &state );
			if (dwResult == ERROR_SUCCESS) {
				this->gamepad_idx = i;
				break;
			}
		}
	}
	
	if ( dwResult == ERROR_SUCCESS ) {
		ApplyButtons(state, input_state);

		if (prevState.Gamepad.sThumbLX != state.Gamepad.sThumbLX || prevState.Gamepad.sThumbLY != state.Gamepad.sThumbLY) {
			Stick left = NormalizedDeadzoneFilter(state.Gamepad.sThumbLX, state.Gamepad.sThumbLY);

			AxisInput axis;
			axis.deviceId = DEVICE_ID_X360_0;
			axis.axisId = JOYSTICK_AXIS_X;
			axis.value = left.x;
			NativeAxis(axis);
			axis.axisId = JOYSTICK_AXIS_Y;
			axis.value = left.y;
			NativeAxis(axis);
		}

		if (prevState.Gamepad.sThumbRX != state.Gamepad.sThumbRX || prevState.Gamepad.sThumbRY != state.Gamepad.sThumbRY) {
			Stick right = NormalizedDeadzoneFilter(state.Gamepad.sThumbRX, state.Gamepad.sThumbRY);

			AxisInput axis;
			axis.deviceId = DEVICE_ID_X360_0;
			axis.axisId = JOYSTICK_AXIS_Z;
			axis.value = right.x;
			NativeAxis(axis);
			axis.axisId = JOYSTICK_AXIS_RZ;
			axis.value = right.y;
			NativeAxis(axis);
		}

		this->prevState = state;
		this->check_delay = 0;

		// If there's an XInput pad, skip following pads. This prevents DInput and XInput
		// from colliding.
		return UPDATESTATE_SKIP_PAD;
	} else {
		// wait check_delay frames before polling the controller again
		this->gamepad_idx = -1;
		this->check_delay = 100;
		return -1;
	}
}

inline float Clampf(float val, float min, float max) {
	if (val < min) return min;
	if (val > max) return max;
	return val;
}

static Stick NormalizedDeadzoneFilter(short x, short y) {
	static const float DEADZONE = (float)XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.0f;
	Stick s;

	s.x = (float)x / 32767.0f;
	s.y = (float)y / 32767.0f;

	float magnitude = sqrtf(s.x * s.x + s.y * s.y);
	
	if (magnitude > DEADZONE) {
		if (magnitude > 1.0f) {
			s.x *= 1.41421f;
			s.y *= 1.41421f;
		}

		s.x = Clampf(s.x, -1.0f, 1.0f);
		s.y = Clampf(s.y, -1.0f, 1.0f);
	} else {
		s.x = 0.0f;
		s.y = 0.0f;
	}
	return s;
}

void XinputDevice::ApplyButtons(XINPUT_STATE &state, InputState &input_state) {
	u32 buttons = state.Gamepad.wButtons;

	if (state.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
		buttons |= XBOX_CODE_LEFTTRIGGER;
	if (state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
		buttons |= XBOX_CODE_RIGHTTRIGGER;

	u32 downMask = buttons & (~prevButtons);
	u32 upMask = (~buttons) & prevButtons;
	prevButtons = buttons;
	
	for (int i = 0; i < xinput_ctrl_map_size; i++) {
		if (downMask & xinput_ctrl_map[i].from) {
			KeyInput key;
			key.deviceId = DEVICE_ID_X360_0;
			key.flags = KEY_DOWN;
			key.keyCode = xinput_ctrl_map[i].to;
			NativeKey(key);
		}
		if (upMask & xinput_ctrl_map[i].from) {
			KeyInput key;
			key.deviceId = DEVICE_ID_X360_0;
			key.flags = KEY_UP;
			key.keyCode = xinput_ctrl_map[i].to;
			NativeKey(key);
		}
	}
}
