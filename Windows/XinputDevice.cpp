#include <limits.h>
#include "XinputDevice.h"
#include "input/input_state.h"

#ifndef XUSER_MAX_COUNT
#define XUSER_MAX_COUNT 4
#endif

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
		ApplyDiff(state, input_state);
		Stick left = NormalizedDeadzoneFilter(state.Gamepad.sThumbLX, state.Gamepad.sThumbLY);
		Stick right = NormalizedDeadzoneFilter(state.Gamepad.sThumbRX, state.Gamepad.sThumbRY);
		input_state.pad_lstick_x += left.x;
		input_state.pad_lstick_y += left.y;
		input_state.pad_rstick_x += right.x;
		input_state.pad_rstick_y += right.y;

		// Also convert the analog triggers.
		input_state.pad_ltrigger = state.Gamepad.bLeftTrigger / 255.0f;
		input_state.pad_rtrigger = state.Gamepad.bRightTrigger / 255.0f;

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

// We only filter the left stick since PSP has no analog triggers or right stick
static Stick NormalizedDeadzoneFilter(short x, short y) {
	static const short DEADZONE = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
	Stick left;
	left.x = x;
	left.y = y;

	float magnitude = sqrt(left.x*left.x + left.y*left.y);

	Stick norm;
	norm.x = left.x / magnitude;
	norm.y = left.y / magnitude;

	if (magnitude > DEADZONE) {
		if (magnitude > 32767) magnitude = 32767;
		// normalize the magnitude
		magnitude -= DEADZONE;
		magnitude /= (32767 - DEADZONE);

		// normalize the axis
		left.x = norm.x * magnitude;
		left.y = norm.y * magnitude;
	} else {
		left.x = left.y = 0;
	}

	return left;
}

// Yes, this maps more than the PSP has, but that's fine as this lets us
// map buttons to extra functionality like speedup.
static const unsigned int xinput_ctrl_map[] = {
	XINPUT_GAMEPAD_DPAD_UP,        PAD_BUTTON_UP,
	XINPUT_GAMEPAD_DPAD_DOWN,      PAD_BUTTON_DOWN,
	XINPUT_GAMEPAD_DPAD_LEFT,      PAD_BUTTON_LEFT,
	XINPUT_GAMEPAD_DPAD_RIGHT,     PAD_BUTTON_RIGHT,
	XINPUT_GAMEPAD_START,          PAD_BUTTON_START,
	XINPUT_GAMEPAD_BACK,           PAD_BUTTON_SELECT,
	XINPUT_GAMEPAD_LEFT_SHOULDER,  PAD_BUTTON_LBUMPER,
	XINPUT_GAMEPAD_RIGHT_SHOULDER, PAD_BUTTON_RBUMPER,
	XINPUT_GAMEPAD_A,              PAD_BUTTON_A,
	XINPUT_GAMEPAD_B,              PAD_BUTTON_B,
	XINPUT_GAMEPAD_X,              PAD_BUTTON_X,
	XINPUT_GAMEPAD_Y,              PAD_BUTTON_Y,
	XINPUT_GAMEPAD_LEFT_THUMB,     PAD_BUTTON_LEFT_THUMB,
	XINPUT_GAMEPAD_RIGHT_THUMB,    PAD_BUTTON_RIGHT_THUMB,
};

static inline u32 CtrlForXinput(int xinput) {
	for (int i = 0; i < sizeof(xinput_ctrl_map)/sizeof(xinput_ctrl_map[0]); i += 2)
		if (xinput_ctrl_map[i] == xinput) return (u32) xinput_ctrl_map[i+1];
	return 0;
}

void XinputDevice::ApplyDiff(XINPUT_STATE &state, InputState &input_state) {
	for (int i = 1; i < USHRT_MAX; i <<= 1) {
		if (state.Gamepad.wButtons & i)
			input_state.pad_buttons |= CtrlForXinput(i);
	}
}