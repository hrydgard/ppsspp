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
#include "Core/System.h"
#include "Core/KeyMap.h"
#include "Core/HLE/sceCtrl.h"

// Utilities to dynamically load XInput. Adapted from SDL.

#if !PPSSPP_PLATFORM(UWP)

#ifndef __MINGW32__
struct XINPUT_CAPABILITIES_EX {
	XINPUT_CAPABILITIES Capabilities;
	WORD VendorId;
	WORD ProductId;
	WORD VersionNumber;
	WORD  unk1;
	DWORD unk2;
};
#endif

typedef DWORD (WINAPI *XInputGetState_t) (DWORD dwUserIndex, XINPUT_STATE* pState);
typedef DWORD (WINAPI *XInputSetState_t) (DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);
typedef DWORD (WINAPI *XInputGetCapabilitiesEx_t) (DWORD unknown, DWORD dwUserIndex, DWORD flags, XINPUT_CAPABILITIES_EX *pCapabilities);

static XInputGetState_t PPSSPP_XInputGetState = nullptr;
static XInputSetState_t PPSSPP_XInputSetState = nullptr;
static XInputGetCapabilitiesEx_t PPSSPP_XInputGetCapabilitiesEx = nullptr;
static DWORD PPSSPP_XInputVersion = 0;
static HMODULE s_pXInputDLL = nullptr;
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

	// 100 is the ordinal for _XInputGetStateEx, which returns the same struct as XinputGetState, but with extra data in wButtons for the guide button, we think...
	// Let's try the name first, though - then fall back to ordinal, then to a non-Ex version (xinput9_1_0.dll doesn't have Ex)
	PPSSPP_XInputGetState = (XInputGetState_t)GetProcAddress( (HMODULE)s_pXInputDLL, "XInputGetStateEx" );
	if (!PPSSPP_XInputGetState) {
		PPSSPP_XInputGetState = (XInputGetState_t)GetProcAddress( (HMODULE)s_pXInputDLL, (LPCSTR)100 );
		if (!PPSSPP_XInputGetState) {
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
			s_pXInputDLL = nullptr;
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
static const struct { int from; InputKeyCode to; } xinput_ctrl_map[] = {
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

XinputDevice::XinputDevice() {
	if (LoadXInputDLL() != 0) {
		WARN_LOG(Log::sceCtrl, "Failed to load XInput! DLL missing");
	}

	for (size_t i = 0; i < ARRAY_SIZE(padData_); ++i) {
		padData_[i].checkDelayUpdates = (int)i;
	}
}

XinputDevice::~XinputDevice() {
	UnloadXInputDLL();
}

int XinputDevice::UpdateState() {
#if !PPSSPP_PLATFORM(UWP)
	if (!s_pXInputDLL)
		return 0;
#endif

	bool anySuccess = false;
	for (int i = 0; i < XUSER_MAX_COUNT; i++) {
		XINPUT_STATE state{};
		if (padData_[i].checkDelayUpdates-- > 0)
			continue;
		DWORD dwResult = PPSSPP_XInputGetState(i, &state);
		if (dwResult == ERROR_SUCCESS) {
			if (!padData_[i].connected) {
				padData_[i].connected = true;
				padData_[i].vendorId = 0;
				padData_[i].productId = 0;
#if !PPSSPP_PLATFORM(UWP)
				XINPUT_CAPABILITIES_EX caps{};
				if (PPSSPP_XInputGetCapabilitiesEx != nullptr && PPSSPP_XInputGetCapabilitiesEx(1, i, 0, &caps) == ERROR_SUCCESS) {
					padData_[i].vendorId = caps.VendorId;
					padData_[i].productId = caps.ProductId;
				}
#endif
				KeyMap::NotifyPadConnected(DEVICE_ID_XINPUT_0 + i, StringFromFormat("Xbox pad %d: %d/%d", (i + 1), padData_[i].vendorId, padData_[i].productId));
			}
			XINPUT_VIBRATION vibration{};
			UpdatePad(i, state, vibration);
			anySuccess = true;
		} else if (dwResult == ERROR_DEVICE_NOT_CONNECTED) {
			if (padData_[i].connected) {
				ReleaseAllKeys(i);
				KeyMap::NotifyPadDisconnected(DEVICE_ID_XINPUT_0 + i);
				padData_[i].connected = false;
			}
			padData_[i].checkDelayUpdates = 30;
		} else {
			padData_[i].checkDelayUpdates = 30;
		}
	}

	// If we get XInput, skip the others. This might not actually be a good idea,
	// and was done to avoid conflicts between DirectInput and XInput.
	return 0; // anySuccess ? UPDATESTATE_SKIP_PAD : 0;
}

void XinputDevice::ReleaseAllKeys(int pad) {
	for (int i = 0; i < ARRAY_SIZE(xinput_ctrl_map); i++) {
		const auto &mapping = xinput_ctrl_map[i];
		KeyInput key;
		key.deviceId = (InputDeviceID)(DEVICE_ID_XINPUT_0 + pad);
		key.flags = KeyInputFlags::UP;
		key.keyCode = mapping.to;
		NativeKey(key);
	}
	static const InputAxis allAxes[6] = {
		JOYSTICK_AXIS_X,
		JOYSTICK_AXIS_Y,
		JOYSTICK_AXIS_Z,
		JOYSTICK_AXIS_RZ,
		JOYSTICK_AXIS_LTRIGGER,
		JOYSTICK_AXIS_RTRIGGER,
	};
	for (const auto axisId : allAxes) {
		AxisInput axis;
		axis.deviceId = (InputDeviceID)(DEVICE_ID_XINPUT_0 + pad);
		axis.axisId = axisId;
		axis.value = 0;
		NativeAxis(&axis, 1);
	}
}

void XinputDevice::UpdatePad(int pad, const XINPUT_STATE &state, XINPUT_VIBRATION &vibration) {
	ApplyButtons(pad, state);
	ApplyVibration(pad, vibration);

	AxisInput axis[6];
	int axisCount = 0;
	for (int i = 0; i < ARRAY_SIZE(axis); i++) {
		axis[i].deviceId = (InputDeviceID)(DEVICE_ID_XINPUT_0 + pad);
	}
	auto sendAxis = [&](InputAxis axisId, float value, int axisIndex) {
		if (value != padData_[pad].prevAxisValue[axisIndex]) {
			padData_[pad].prevAxisValue[axisIndex] = value;
			axis[axisCount].axisId = axisId;
			axis[axisCount].value = value;
			axisCount++;
		}
	};

	sendAxis(JOYSTICK_AXIS_X, (float)state.Gamepad.sThumbLX / 32767.0f, 0);
	sendAxis(JOYSTICK_AXIS_Y, (float)state.Gamepad.sThumbLY / 32767.0f, 1);
	sendAxis(JOYSTICK_AXIS_Z, (float)state.Gamepad.sThumbRX / 32767.0f, 2);
	sendAxis(JOYSTICK_AXIS_RZ, (float)state.Gamepad.sThumbRY / 32767.0f, 3);
	sendAxis(JOYSTICK_AXIS_LTRIGGER, (float)state.Gamepad.bLeftTrigger / 255.0f, 4);
	sendAxis(JOYSTICK_AXIS_RTRIGGER, (float)state.Gamepad.bRightTrigger / 255.0f, 5);

	if (axisCount) {
		NativeAxis(axis, axisCount);
	}

	padData_[pad].prevState = state;
	padData_[pad].checkDelayUpdates = 0;
}

void XinputDevice::ApplyButtons(int pad, const XINPUT_STATE &state) {
	const u32 buttons = state.Gamepad.wButtons;

	const u32 downMask = buttons & (~padData_[pad].prevButtons);
	const u32 upMask = (~buttons) & padData_[pad].prevButtons;
	padData_[pad].prevButtons = buttons;

	for (int i = 0; i < ARRAY_SIZE(xinput_ctrl_map); i++) {
		if (downMask & xinput_ctrl_map[i].from) {
			KeyInput key;
			key.deviceId = DEVICE_ID_XINPUT_0 + pad;
			key.flags = KeyInputFlags::DOWN;
			key.keyCode = xinput_ctrl_map[i].to;
			NativeKey(key);
		}
		if (upMask & xinput_ctrl_map[i].from) {
			KeyInput key;
			key.deviceId = DEVICE_ID_XINPUT_0 + pad;
			key.flags = KeyInputFlags::UP;
			key.keyCode = xinput_ctrl_map[i].to;
			NativeKey(key);
		}
	}
}

void XinputDevice::ApplyVibration(int pad, XINPUT_VIBRATION &vibration) {
	if (PSP_IsInited()) {
		newVibrationTime_ = time_now_d();
		// We have to run PPSSPP_XInputSetState at time intervals
		// since it bugs otherwise with very high fast-forward speeds
		// and freezes at constant vibration or no vibration at all.
		if (newVibrationTime_ - prevVibrationTime_ >= 1.0 / 64.0) {
			if (GetUIState() == UISTATE_INGAME) {
				vibration.wLeftMotorSpeed = sceCtrlGetLeftVibration(); // use any value between 0-65535 here
				vibration.wRightMotorSpeed = sceCtrlGetRightVibration(); // use any value between 0-65535 here
			} else {
				vibration.wLeftMotorSpeed = 0;
				vibration.wRightMotorSpeed = 0;
			}

			if (padData_[pad].prevVibration.wLeftMotorSpeed != vibration.wLeftMotorSpeed || padData_[pad].prevVibration.wRightMotorSpeed != vibration.wRightMotorSpeed) {
				PPSSPP_XInputSetState(pad, &vibration);
				padData_[pad].prevVibration = vibration;
			}
			prevVibrationTime_ = newVibrationTime_;
		}
	} else {
		DWORD dwResult = PPSSPP_XInputSetState(pad, &vibration);
		if (dwResult != ERROR_SUCCESS) {
			padData_[pad].checkDelayUpdates = 30;
		}
	}
}
