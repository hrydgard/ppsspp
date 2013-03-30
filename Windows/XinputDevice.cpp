#include <limits.h>

#include "XinputDevice.h"

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
static Stick NormalizedDeadzoneFilter(XINPUT_STATE &state);

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
		this->ApplyDiff(state);
		Stick left = NormalizedDeadzoneFilter(state);
		__CtrlSetAnalog(left.x, left.y);
		this->prevState = state;
		this->check_delay = 0;
		return 0;
	} else {
		// wait check_delay frames before polling the controller again
		this->gamepad_idx = -1;
		this->check_delay = 100;
		return -1;
	}
}

// We only filter the left stick since PSP has no analog triggers or right stick
static Stick NormalizedDeadzoneFilter(XINPUT_STATE &state) {
	static const short DEADZONE = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
	Stick left;
	left.x = state.Gamepad.sThumbLX;
	left.y = state.Gamepad.sThumbLY;

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

static const unsigned short xinput_ctrl_map[] = {
	XINPUT_GAMEPAD_DPAD_UP,        CTRL_UP,
	XINPUT_GAMEPAD_DPAD_DOWN,      CTRL_DOWN,
	XINPUT_GAMEPAD_DPAD_LEFT,      CTRL_LEFT,
	XINPUT_GAMEPAD_DPAD_RIGHT,     CTRL_RIGHT,
	XINPUT_GAMEPAD_START,          CTRL_START,
	XINPUT_GAMEPAD_BACK,           CTRL_SELECT,
	XINPUT_GAMEPAD_LEFT_SHOULDER,  CTRL_LTRIGGER,
	XINPUT_GAMEPAD_RIGHT_SHOULDER, CTRL_RTRIGGER,
	XINPUT_GAMEPAD_A,              CTRL_CROSS,
	XINPUT_GAMEPAD_B,              CTRL_CIRCLE,
	XINPUT_GAMEPAD_X,              CTRL_SQUARE,
	XINPUT_GAMEPAD_Y,              CTRL_TRIANGLE,
};
static inline u32 CtrlForXinput(int xinput) {
	for (int i = 0; i < sizeof(xinput_ctrl_map)/sizeof(xinput_ctrl_map[0]); i += 2)
		if (xinput_ctrl_map[i] == xinput) return (u32) xinput_ctrl_map[i+1];
	return 0;
}

void XinputDevice::ApplyDiff(XINPUT_STATE &state) {
	unsigned short pressed  =  state.Gamepad.wButtons & ~this->prevState.Gamepad.wButtons;
	unsigned short released = ~state.Gamepad.wButtons &  this->prevState.Gamepad.wButtons;

	for (int i = 1; i < USHRT_MAX; i <<= 1) {
		if (pressed & i)
			__CtrlButtonDown(CtrlForXinput(i));
		if (released & i)
			__CtrlButtonUp(CtrlForXinput(i));
	}
}