#include <limits.h>

#include "base/NativeApp.h"
#include "Core/Config.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "XinputDevice.h"
#include "ControlMapping.h"

// Utilities to dynamically load XInput. Adapted from SDL.

typedef DWORD (WINAPI *XInputGetState_t) (DWORD dwUserIndex, XINPUT_STATE* pState);
typedef DWORD (WINAPI *XInputSetState_t) (DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);
typedef DWORD (WINAPI *XInputGetCapabilities_t) (DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities);

XInputGetState_t PPSSPP_XInputGetState = NULL;
XInputSetState_t PPSSPP_XInputSetState = NULL;
XInputGetCapabilities_t PPSSPP_XInputGetCapabilities = NULL;
static DWORD PPSSPP_XInputVersion = 0;
static HMODULE s_pXInputDLL = 0;
static int s_XInputDLLRefCount = 0;

static void UnloadXInputDLL();

static int LoadXInputDLL() {
	DWORD version = 0;

	if (s_pXInputDLL) {
		s_XInputDLLRefCount++;
		return 0;  /* already loaded */
	}

	version = (1 << 16) | 4;
	s_pXInputDLL = LoadLibrary( L"XInput1_4.dll" );  // 1.4 Ships with Windows 8.
	if (!s_pXInputDLL) {
		version = (1 << 16) | 3;
		s_pXInputDLL = LoadLibrary( L"XInput1_3.dll" );  // 1.3 Ships with Vista and Win7, can be installed as a restributable component.
	}
	if (!s_pXInputDLL) {
		return -1;
	}

	PPSSPP_XInputVersion = version;
	s_XInputDLLRefCount = 1;

	/* 100 is the ordinal for _XInputGetStateEx, which returns the same struct as XinputGetState, but with extra data in wButtons for the guide button, we think... */
	PPSSPP_XInputGetState = (XInputGetState_t)GetProcAddress( (HMODULE)s_pXInputDLL, (LPCSTR)100 );
	PPSSPP_XInputSetState = (XInputSetState_t)GetProcAddress( (HMODULE)s_pXInputDLL, "XInputSetState" );
	PPSSPP_XInputGetCapabilities = (XInputGetCapabilities_t)GetProcAddress( (HMODULE)s_pXInputDLL, "XInputGetCapabilities" );
	if ( !PPSSPP_XInputGetState || !PPSSPP_XInputSetState || !PPSSPP_XInputGetCapabilities ) {
		UnloadXInputDLL();
		return -1;
	}

	return 0;
}

static void UnloadXInputDLL() {
	if ( s_pXInputDLL ) {
		if (--s_XInputDLLRefCount == 0) {
			FreeLibrary( s_pXInputDLL );
			s_pXInputDLL = NULL;
		}
	}
}

#ifndef XUSER_MAX_COUNT
#define XUSER_MAX_COUNT 4
#endif

// Undocumented. Steam annoyingly grabs this button though....
#define XINPUT_GUIDE_BUTTON 0x400


// Permanent map. Actual mapping happens elsewhere.
static const struct {int from, to;} xinput_ctrl_map[] = {
	{XINPUT_GAMEPAD_A,              NKCODE_BUTTON_A},
	{XINPUT_GAMEPAD_B,              NKCODE_BUTTON_B},
	{XINPUT_GAMEPAD_X,              NKCODE_BUTTON_X},
	{XINPUT_GAMEPAD_Y,              NKCODE_BUTTON_Y},
	{XINPUT_GAMEPAD_BACK,           NKCODE_BUTTON_SELECT},
	{XINPUT_GAMEPAD_START,          NKCODE_BUTTON_START},
	{XINPUT_GAMEPAD_LEFT_SHOULDER,  NKCODE_BUTTON_L1},
	{XINPUT_GAMEPAD_RIGHT_SHOULDER, NKCODE_BUTTON_R1},
	{XINPUT_GAMEPAD_LEFT_THUMB,     NKCODE_BUTTON_THUMBL},
	{XINPUT_GAMEPAD_RIGHT_THUMB,    NKCODE_BUTTON_THUMBR},
	{XINPUT_GAMEPAD_DPAD_UP,        NKCODE_DPAD_UP},
	{XINPUT_GAMEPAD_DPAD_DOWN,      NKCODE_DPAD_DOWN},
	{XINPUT_GAMEPAD_DPAD_LEFT,      NKCODE_DPAD_LEFT},
	{XINPUT_GAMEPAD_DPAD_RIGHT,     NKCODE_DPAD_RIGHT},
	{XINPUT_GUIDE_BUTTON,           NKCODE_HOME},
};

static const unsigned int xinput_ctrl_map_size = sizeof(xinput_ctrl_map) / sizeof(xinput_ctrl_map[0]);

XinputDevice::XinputDevice() {
	if (LoadXInputDLL() != 0) {
		ERROR_LOG(SCECTRL, "Failed to load XInput! DLL missing");
	}
	ZeroMemory( &this->prevState, sizeof(this->prevState) );
	this->check_delay = 0;
	this->gamepad_idx = -1;
}

XinputDevice::~XinputDevice() {
	UnloadXInputDLL();
}

struct Stick {
	float x;
	float y;
};
static Stick NormalizedDeadzoneFilter(short x, short y);

int XinputDevice::UpdateState(InputState &input_state) {
	if (!s_pXInputDLL)
		return 0;

	if (this->check_delay-- > 0) return -1;
	XINPUT_STATE state;
	ZeroMemory( &state, sizeof(XINPUT_STATE) );

	DWORD dwResult;
	if (this->gamepad_idx >= 0)
		dwResult = PPSSPP_XInputGetState( this->gamepad_idx, &state );
	else {
		// use the first gamepad that responds
		for (int i = 0; i < XUSER_MAX_COUNT; i++) {
			dwResult = PPSSPP_XInputGetState( i, &state );
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

		if (prevState.Gamepad.bLeftTrigger != state.Gamepad.bLeftTrigger) {
			AxisInput axis;
			axis.deviceId = DEVICE_ID_X360_0;
			axis.axisId = JOYSTICK_AXIS_LTRIGGER;
			axis.value = (float)state.Gamepad.bLeftTrigger / 255.0f;
			NativeAxis(axis);
		}

		if (prevState.Gamepad.bRightTrigger != state.Gamepad.bRightTrigger) {
			AxisInput axis;
			axis.deviceId = DEVICE_ID_X360_0;
			axis.axisId = JOYSTICK_AXIS_RTRIGGER;
			axis.value = (float)state.Gamepad.bRightTrigger / 255.0f;
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
