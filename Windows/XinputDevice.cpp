#include <limits.h>
#include "Core/Config.h"
#include "input/input_state.h"
#include "XinputDevice.h"
#include "ControlMapping.h"


#ifndef XUSER_MAX_COUNT
#define XUSER_MAX_COUNT 4
#endif

// Yes, this maps more than the PSP has, but that's fine as this lets us
// map buttons to extra functionality like speedup.
unsigned int xinput_ctrl_map[] = {
	XBOX_CODE_LEFTTRIGER,          PAD_BUTTON_MENU,
	XBOX_CODE_RIGHTTRIGER,         PAD_BUTTON_BACK,
	XINPUT_GAMEPAD_A,              PAD_BUTTON_A,
	XINPUT_GAMEPAD_B,              PAD_BUTTON_B,
	XINPUT_GAMEPAD_X,              PAD_BUTTON_X,
	XINPUT_GAMEPAD_Y,              PAD_BUTTON_Y,
	XINPUT_GAMEPAD_BACK,           PAD_BUTTON_SELECT,
	XINPUT_GAMEPAD_START,          PAD_BUTTON_START,
	XINPUT_GAMEPAD_LEFT_SHOULDER,  PAD_BUTTON_LBUMPER,
	XINPUT_GAMEPAD_RIGHT_SHOULDER, PAD_BUTTON_RBUMPER,
	XINPUT_GAMEPAD_LEFT_THUMB,     PAD_BUTTON_LEFT_THUMB,  // Turbo
	XINPUT_GAMEPAD_RIGHT_THUMB,    PAD_BUTTON_RIGHT_THUMB, // Open PauseScreen
	XINPUT_GAMEPAD_DPAD_UP,        PAD_BUTTON_UP,
	XINPUT_GAMEPAD_DPAD_DOWN,      PAD_BUTTON_DOWN,
	XINPUT_GAMEPAD_DPAD_LEFT,      PAD_BUTTON_LEFT,
	XINPUT_GAMEPAD_DPAD_RIGHT,     PAD_BUTTON_RIGHT,
};
static const unsigned int xinput_ctrl_map_size = sizeof(xinput_ctrl_map);

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
	if (g_Config.iForceInputDevice > 0) return -1;
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



struct xinput_button_name {
	unsigned int button;
	char name[10];
};

const xinput_button_name xinput_name_map[] = {
	{XBOX_CODE_LEFTTRIGER,          "LT"},
	{XBOX_CODE_RIGHTTRIGER,         "RT"},
	{XINPUT_GAMEPAD_A,              "A"},
	{XINPUT_GAMEPAD_B,              "B"},
	{XINPUT_GAMEPAD_X,              "X"},
	{XINPUT_GAMEPAD_Y,              "Y"},
	{XINPUT_GAMEPAD_BACK,           "Back"},
	{XINPUT_GAMEPAD_START,          "Start"},
	{XINPUT_GAMEPAD_LEFT_SHOULDER,  "LB"},
	{XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB"},
	{XINPUT_GAMEPAD_LEFT_THUMB,     "LThumb"},
	{XINPUT_GAMEPAD_RIGHT_THUMB,    "RThumb"},
	{XINPUT_GAMEPAD_DPAD_UP,        "Up"},
	{XINPUT_GAMEPAD_DPAD_DOWN,      "Down"},
	{XINPUT_GAMEPAD_DPAD_LEFT,      "Left"},
	{XINPUT_GAMEPAD_DPAD_RIGHT,     "Right"},
};
static const int xbutton_name_map_size = sizeof(xinput_name_map) / sizeof(xinput_button_name);

const char * getXinputButtonName(unsigned int button) {
	for (int i = 0; i < xbutton_name_map_size; i++) {
		if (xinput_name_map[i].button == button)
			return xinput_name_map[i].name;
	}
	return 0;
}

void XinputDevice::ApplyDiff(XINPUT_STATE &state, InputState &input_state) {
	// Skip XBOX_CODE_LIFT/RIGHT TRIGER. it's virtual code for Mapping.
	for (int i = 0; i < sizeof(xinput_ctrl_map)/sizeof(xinput_ctrl_map[0]); i += 2) {
		if (state.Gamepad.wButtons & (WORD)(xinput_ctrl_map[i])) {
			input_state.pad_buttons |= (int)xinput_ctrl_map[i + 1];
		}
		// Effective use of triggers that are not used.
		if (xinput_ctrl_map[i] == XBOX_CODE_LEFTTRIGER &&
			state.Gamepad.bLeftTrigger >  XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
			input_state.pad_buttons |= xinput_ctrl_map[i + 1];
		}
		if (xinput_ctrl_map[i] == XBOX_CODE_RIGHTTRIGER &&
			state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
			input_state.pad_buttons |= xinput_ctrl_map[i + 1];
		}

	}
}
int XinputDevice::UpdateRawStateSingle(RawInputState &rawState)
{
	if (g_Config.iForceInputDevice > 0) return -1;

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
		for (UINT bit = XINPUT_GAMEPAD_DPAD_UP; bit <= XINPUT_GAMEPAD_Y; bit <<= 1) {
			if (state.Gamepad.wButtons & bit) {
				rawState.button = bit;
				break;
			}
		}
		if (state.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
			rawState.button = XBOX_CODE_LEFTTRIGER;
		} else if (state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
			rawState.button = XBOX_CODE_RIGHTTRIGER;
		}
		return TRUE;
	}
	return FALSE;
}
