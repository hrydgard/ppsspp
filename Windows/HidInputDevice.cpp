
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <initguid.h>
#include <vector>

#include "Windows/HidInputDevice.h"
#include "Common/TimeUtil.h"
#include "Common/Log.h"
#include "Common/Input/InputState.h"
#include "Common/System/NativeApp.h"

enum PSButton : u32 {
	PS_DPAD_UP = 1,
	PS_DPAD_DOWN = 2,
	PS_DPAD_LEFT = 4,
	PS_DPAD_RIGHT = 8,
	PS_BTN_SQUARE = 16,
	PS_BTN_CROSS = 32,
	PS_BTN_TRIANGLE = 64,
	PS_BTN_CIRCLE = 128,

	PS_BTN_L1 = (1 << 8),
	PS_BTN_L2 = (1 << 9),
	PS_BTN_R1 = (1 << 10),
	PS_BTN_R2 = (1 << 11),
	PS_BTN_SHARE = (1 << 12),
	PS_BTN_OPTIONS = (1 << 13),
	PS_BTN_L3 = (1 << 14),
	PS_BTN_R3 = (1 << 15),
	PS_BTN_PS_BUTTON = (1 << 16),
	PS_BTN_TOUCHPAD = (1 << 17),
};

constexpr u16 SONY_VID = 0x054C;

constexpr u16 DS4_PID = 0x05C4;
constexpr u16 DS5_PID = 0x0CE6;
constexpr u16 DS4_2_PID = 0x09CC;
constexpr u16 DS4_WIRELESS = 0x0BA0;
constexpr u16 DUALSENSE_WIRELESS = 0x0CE6;
constexpr u16 DUALSENSE_EDGE_WIRELESS = 0x0DF2;
constexpr u16 PS_CLASSIC = 0x0CDA;

struct PSInputMapping {
	PSButton button;
	InputKeyCode keyCode;
};

static const PSInputMapping g_psInputMappings[] = {
	{PS_DPAD_UP, NKCODE_DPAD_UP},
	{PS_DPAD_DOWN, NKCODE_DPAD_DOWN},
	{PS_DPAD_LEFT, NKCODE_DPAD_LEFT},
	{PS_DPAD_RIGHT, NKCODE_DPAD_RIGHT},
	{PS_BTN_SQUARE, NKCODE_BUTTON_X},
	{PS_BTN_TRIANGLE, NKCODE_BUTTON_Y},
	{PS_BTN_CIRCLE, NKCODE_BUTTON_B},
	{PS_BTN_CROSS, NKCODE_BUTTON_A},
	{PS_BTN_PS_BUTTON, NKCODE_HOME},
	{PS_BTN_SHARE, NKCODE_BUTTON_START},
	{PS_BTN_OPTIONS, NKCODE_BUTTON_SELECT},
	{PS_BTN_L1, NKCODE_BUTTON_L1},
	{PS_BTN_R1, NKCODE_BUTTON_R1},
	{PS_BTN_L2, NKCODE_BUTTON_L2},
	{PS_BTN_R2, NKCODE_BUTTON_R2},
	{PS_BTN_L3, NKCODE_BUTTON_THUMBL},
	{PS_BTN_R3, NKCODE_BUTTON_THUMBR},
};

struct PSControllerInfo {
	PSSubType type;
	u16 productId;
};

static const PSControllerInfo g_psInfos[] = {
	{TYPE_DS4, DS4_PID},
	{TYPE_DS5, DS5_PID},
	{TYPE_DS4, DS4_2_PID},
	{TYPE_DS4, DS4_WIRELESS},
	{TYPE_DS5, DUALSENSE_WIRELESS},
	{TYPE_DS5, DUALSENSE_EDGE_WIRELESS},
	{TYPE_DS4, PS_CLASSIC},
};

static bool IsSonyGamepad(HANDLE handle, USHORT *pidOut, PSSubType *subType) {
	HIDD_ATTRIBUTES attr{sizeof(HIDD_ATTRIBUTES)};
	if (!HidD_GetAttributes(handle, &attr)) {
		return false;
	}
	for (const auto &info : g_psInfos) {
		if (attr.VendorID == SONY_VID && attr.ProductID == info.productId) {
			*pidOut = attr.ProductID;
			*subType = info.type;
			return true;
		}
	}
	return false;
}

HANDLE OpenFirstDualShockOrSense(PSSubType *subType) {
	GUID hidGuid;
	HidD_GetHidGuid(&hidGuid);

	HDEVINFO deviceInfoSet = SetupDiGetClassDevs(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (deviceInfoSet == INVALID_HANDLE_VALUE)
		return nullptr;

	SP_DEVICE_INTERFACE_DATA interfaceData;
	interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, nullptr, &hidGuid, i, &interfaceData); ++i) {
		DWORD requiredSize = 0;
		SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &interfaceData, nullptr, 0, &requiredSize, nullptr);

		std::vector<BYTE> buffer(requiredSize);
		auto* detailData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(buffer.data());
		detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

		if (SetupDiGetDeviceInterfaceDetail(deviceInfoSet, &interfaceData, detailData, requiredSize, nullptr, nullptr)) {
			HANDLE handle = CreateFile(detailData->DevicePath, GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (handle != INVALID_HANDLE_VALUE) {
				USHORT pid;
				if (IsSonyGamepad(handle, &pid, subType)) {
					INFO_LOG(Log::UI, "Found Sony gamepad. PID: %04x", pid);
					SetupDiDestroyDeviceInfoList(deviceInfoSet);
					return handle;
				}
				CloseHandle(handle);
			}
		}
	}

	SetupDiDestroyDeviceInfoList(deviceInfoSet);
	return nullptr;
}

void HidInputDevice::AddSupportedDevices(std::set<u32> *deviceVIDPIDs) {
	for (const auto &info : g_psInfos) {
		const u32 vidpid = MAKELONG(SONY_VID, info.productId);
		deviceVIDPIDs->insert(vidpid);
	}
}

struct PSInputReportHeader {
	u8 id; // not sure if useful.
	u8 lx;
	u8 ly;
	u8 rx;
	u8 ry;
	u8 buttons[3];  // note, starts at 5 so not aligned
	u8 l2_analog;
	u8 r2_analog;
	u8 pad[2];
	u8 battery;
	// Then there's motion and all kinds of stuff.
};

bool HidInputDevice::ReadDS4Input(HANDLE handle, PSControllerState *state) {
	BYTE inputReport[64] = {0}; // 64-byte input report for DS4
	DWORD bytesRead = 0;
	if (ReadFile(handle, inputReport, sizeof(inputReport), &bytesRead, nullptr)) {
		PSInputReportHeader hdr{};
		static_assert(sizeof(hdr) < sizeof(inputReport));
		memcpy(&hdr, inputReport, sizeof(hdr));

		// Center the sticks.
		state->lx = hdr.lx - 128;
		state->rx = hdr.rx - 128;
		state->ly = hdr.ly - 128;
		state->ry = hdr.ry - 128;

		u32 buttons{};
		hdr.buttons[2] &= 3;  // Remove noise
		memcpy(&buttons, &hdr.buttons[0], 3);

		// Clear out and re-fill the DPAD, it works differently somehow
		buttons &= ~0xF;

		const u8 dpad = hdr.buttons[0] & 0xF;
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

		state->buttons = buttons;
		return true;
	} else {
		return false;
	}
}

void HidInputDevice::Init() {}
void HidInputDevice::Shutdown() {
	if (controller_) {
		CloseHandle(controller_);
		controller_ = nullptr;
	}
}

void HidInputDevice::ReleaseAllKeys() {
	for (const auto &mapping : g_psInputMappings) {
		KeyInput key;
		key.deviceId = DEVICE_ID_XINPUT_0 + pad_;
		key.flags = KEY_UP;
		key.keyCode = mapping.keyCode;
		NativeKey(key);
	}
}

InputDeviceID HidInputDevice::DeviceID(int pad) {
	return DEVICE_ID_PAD_0 + pad;
}

int HidInputDevice::UpdateState() {
	if (!controller_) {
		// Poll for controllers from time to time.
		if (pollCount_ == 0) {
			pollCount_ = POLL_FREQ;
			HANDLE newController = OpenFirstDualShockOrSense(&subType_);
			if (newController) {
				controller_ = newController;
			}
		} else {
			pollCount_--;
		}
	}

	if (controller_) {
		PSControllerState state{};
		if (ReadDS4Input(controller_, &state)) {
			const InputDeviceID deviceID = DeviceID(pad_);
			// Process the input and generate input events.
			const u32 downMask = state.buttons & (~prevState_.buttons);
			const u32 upMask = (~state.buttons) & prevState_.buttons;

			for (const auto &mapping : g_psInputMappings) {
				if (downMask & mapping.button) {
					KeyInput key;
					key.deviceId = deviceID;
					key.flags = KEY_DOWN;
					key.keyCode = mapping.keyCode;
					NativeKey(key);
				}
				if (upMask & mapping.button) {
					KeyInput key;
					key.deviceId = deviceID;
					key.flags = KEY_UP;
					key.keyCode = mapping.keyCode;
					NativeKey(key);
				}
			}

			prevState_ = state;
		} else {
			// might have been disconnected. retry later.
			ReleaseAllKeys();
			CloseHandle(controller_);
			controller_ = NULL;
			pollCount_ = POLL_FREQ;
		}
	}
	return 0;
}
