#include "ppsspp_config.h"

#include <climits>
#include <algorithm>

#include "Common/System/NativeApp.h"
#include "Common/CommonWindows.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "XinputDevice.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "Core/KeyMap.h"
#include "Core/HLE/sceCtrl.h"

static double newVibrationTime = 0.0;

// Utilities to dynamically load XInput. Adapted from SDL.

#if !PPSSPP_PLATFORM(UWP)

struct XINPUT_CAPABILITIES_EX {
	XINPUT_CAPABILITIES Capabilities;
	WORD vendorId;
	WORD productId;
	WORD revisionId;
	DWORD a4; //unknown
};

typedef DWORD (WINAPI *XInputGetState_t) (DWORD dwUserIndex, XINPUT_STATE* pState);
typedef DWORD (WINAPI *XInputSetState_t) (DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);
typedef DWORD (WINAPI *XInputGetCapabilitiesEx_t) (DWORD unknown, DWORD dwUserIndex, DWORD flags, XINPUT_CAPABILITIES_EX *pCapabilities);

static XInputGetState_t PPSSPP_XInputGetState = nullptr;
static XInputSetState_t PPSSPP_XInputSetState = nullptr;
static XInputGetCapabilitiesEx_t PPSSPP_XInputGetCapabilitiesEx = nullptr;
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
		if (!s_pXInputDLL) {
			version = (1 << 16) | 0;
			s_pXInputDLL = LoadLibrary( L"XInput9_1_0.dll" );  // 1.0 ships with any Windows since WinXP
		}
	}
	if (!s_pXInputDLL) {
		return -1;
	}

	PPSSPP_XInputVersion = version;
	s_XInputDLLRefCount = 1;

	/* 100 is the ordinal for _XInputGetStateEx, which returns the same struct as XinputGetState, but with extra data in wButtons for the guide button, we think...
	   Let's try the name first, though - then fall back to ordinal, then to a non-Ex version (xinput9_1_0.dll doesn't have Ex) */
	PPSSPP_XInputGetState = (XInputGetState_t)GetProcAddress( (HMODULE)s_pXInputDLL, "XInputGetStateEx" );
	if ( !PPSSPP_XInputGetState ) {
		PPSSPP_XInputGetState = (XInputGetState_t)GetProcAddress( (HMODULE)s_pXInputDLL, (LPCSTR)100 );
		if ( !PPSSPP_XInputGetState ) {
			PPSSPP_XInputGetState = (XInputGetState_t)GetProcAddress( (HMODULE)s_pXInputDLL, "XInputGetState" );
		}
	}

	if ( !PPSSPP_XInputGetState ) {
		UnloadXInputDLL();
		return -1;
	}

	/* Let's try the name first, then fall back to a non-Ex version (xinput9_1_0.dll doesn't have Ex) */
	PPSSPP_XInputSetState = (XInputSetState_t)GetProcAddress((HMODULE)s_pXInputDLL, "XInputSetStateEx");
	if (!PPSSPP_XInputSetState) {
		PPSSPP_XInputSetState = (XInputSetState_t)GetProcAddress((HMODULE)s_pXInputDLL, "XInputSetState");
	}

	if (!PPSSPP_XInputSetState) {
		UnloadXInputDLL();
		return -1;
	}

	if (PPSSPP_XInputVersion >= ((1 << 16) | 4)) {
		PPSSPP_XInputGetCapabilitiesEx = (XInputGetCapabilitiesEx_t)GetProcAddress((HMODULE)s_pXInputDLL, (LPCSTR)108);
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

#else
static int LoadXInputDLL() { return 0; }
static void UnloadXInputDLL() {}
#define PPSSPP_XInputGetState XInputGetState
#define PPSSPP_XInputSetState XInputSetState
#endif

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
		WARN_LOG(SCECTRL, "Failed to load XInput! DLL missing");
	}

	for (size_t i = 0; i < ARRAY_SIZE(check_delay); ++i) {
		check_delay[i] = (int)i;
	}
}

XinputDevice::~XinputDevice() {
	UnloadXInputDLL();
}

struct Stick {
	Stick (float x_, float y_, float scale) : x(x_ * scale), y(y_ * scale) {}
	float x;
	float y;
};

bool NormalizedDeadzoneDiffers(u8 x1, u8 x2, const u8 thresh) {
	if (x1 > thresh || x2 > thresh) {
		return x1 != x2;
	}
	return false;
}

int XinputDevice::UpdateState() {
#if !PPSSPP_PLATFORM(UWP)
	if (!s_pXInputDLL)
		return 0;
#endif

	bool anySuccess = false;
	for (int i = 0; i < XUSER_MAX_COUNT; i++) {
		XINPUT_STATE state;
		ZeroMemory(&state, sizeof(XINPUT_STATE));
		XINPUT_VIBRATION vibration;
		ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
		if (check_delay[i]-- > 0)
			continue;
		DWORD dwResult = PPSSPP_XInputGetState(i, &state);
		if (dwResult == ERROR_SUCCESS) {
			UpdatePad(i, state, vibration);
			anySuccess = true;
		} else {
			check_delay[i] = 30;
		}
	}

	// If we get XInput, skip the others. This might not actually be a good idea,
	// and was done to avoid conflicts between DirectInput and XInput.
	return anySuccess ? UPDATESTATE_SKIP_PAD : 0;
}

void XinputDevice::UpdatePad(int pad, const XINPUT_STATE &state, XINPUT_VIBRATION &vibration) {
	static bool notified[XUSER_MAX_COUNT]{};
	if (!notified[pad]) {
		notified[pad] = true;
#if !PPSSPP_PLATFORM(UWP)
		XINPUT_CAPABILITIES_EX caps;
		if (PPSSPP_XInputGetCapabilitiesEx != nullptr && PPSSPP_XInputGetCapabilitiesEx(1, pad, 0, &caps) == ERROR_SUCCESS) {
			KeyMap::NotifyPadConnected(DEVICE_ID_XINPUT_0 + pad, StringFromFormat("Xbox 360 Pad: %d/%d", caps.vendorId, caps.productId));
		} else {
#else
		{
#endif
			KeyMap::NotifyPadConnected(DEVICE_ID_XINPUT_0 + pad, "Xbox 360 Pad");
		}
	}
	ApplyButtons(pad, state);
	ApplyVibration(pad, vibration);

	AxisInput axis;
	axis.deviceId = DEVICE_ID_XINPUT_0 + pad;
	auto sendAxis = [&](AndroidJoystickAxis axisId, float value) {
		axis.axisId = axisId;
		axis.value = value;
		NativeAxis(axis);
	};

	sendAxis(JOYSTICK_AXIS_X, (float)state.Gamepad.sThumbLX / 32767.0f);
	sendAxis(JOYSTICK_AXIS_Y, (float)state.Gamepad.sThumbLY / 32767.0f);
	sendAxis(JOYSTICK_AXIS_Z, (float)state.Gamepad.sThumbRX / 32767.0f);
	sendAxis(JOYSTICK_AXIS_RZ, (float)state.Gamepad.sThumbRY / 32767.0f);

	if (NormalizedDeadzoneDiffers(prevState[pad].Gamepad.bLeftTrigger, state.Gamepad.bLeftTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD)) {
		sendAxis(JOYSTICK_AXIS_LTRIGGER, (float)state.Gamepad.bLeftTrigger / 255.0f);
	}

	if (NormalizedDeadzoneDiffers(prevState[pad].Gamepad.bRightTrigger, state.Gamepad.bRightTrigger, XINPUT_GAMEPAD_TRIGGER_THRESHOLD)) {
		sendAxis(JOYSTICK_AXIS_RTRIGGER, (float)state.Gamepad.bRightTrigger / 255.0f);
	}

	prevState[pad] = state;
	check_delay[pad] = 0;
}

void XinputDevice::ApplyButtons(int pad, const XINPUT_STATE &state) {
	u32 buttons = state.Gamepad.wButtons;

	u32 downMask = buttons & (~prevButtons[pad]);
	u32 upMask = (~buttons) & prevButtons[pad];
	prevButtons[pad] = buttons;
	
	for (int i = 0; i < xinput_ctrl_map_size; i++) {
		if (downMask & xinput_ctrl_map[i].from) {
			KeyInput key;
			key.deviceId = DEVICE_ID_XINPUT_0 + pad;
			key.flags = KEY_DOWN;
			key.keyCode = xinput_ctrl_map[i].to;
			NativeKey(key);
		}
		if (upMask & xinput_ctrl_map[i].from) {
			KeyInput key;
			key.deviceId = DEVICE_ID_XINPUT_0 + pad;
			key.flags = KEY_UP;
			key.keyCode = xinput_ctrl_map[i].to;
			NativeKey(key);
		}
	}
}


void XinputDevice::ApplyVibration(int pad, XINPUT_VIBRATION &vibration) {
	if (PSP_IsInited()) {
		newVibrationTime = time_now_d();
		// We have to run PPSSPP_XInputSetState at time intervals
		// since it bugs otherwise with very high fast-forward speeds
		// and freezes at constant vibration or no vibration at all.
		if (newVibrationTime - prevVibrationTime >= 1.0 / 64.0) {
			if (GetUIState() == UISTATE_INGAME) {
				vibration.wLeftMotorSpeed = sceCtrlGetLeftVibration(); // use any value between 0-65535 here
				vibration.wRightMotorSpeed = sceCtrlGetRightVibration(); // use any value between 0-65535 here
			} else {
				vibration.wLeftMotorSpeed = 0;
				vibration.wRightMotorSpeed = 0;
			}

			if (prevVibration[pad].wLeftMotorSpeed != vibration.wLeftMotorSpeed || prevVibration[pad].wRightMotorSpeed != vibration.wRightMotorSpeed) {
				PPSSPP_XInputSetState(pad, &vibration);
				prevVibration[pad] = vibration;
			}
			prevVibrationTime = newVibrationTime;
		}
	} else {
		DWORD dwResult = PPSSPP_XInputSetState(pad, &vibration);
		if (dwResult != ERROR_SUCCESS) {
			check_delay[pad] = 30;
		}
	}
}

