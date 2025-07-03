// This file in particular along with its header is public domain, use it for whatever you want.

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <initguid.h>
#include <vector>

#include "Windows/HidInputDevice.h"
#include "Common/TimeUtil.h"
#include "Common/Log.h"
#include "Common/Input/InputState.h"
#include "Common/Common.h"
#include "Common/System/NativeApp.h"
#include "Common/System/OSD.h"

enum PSButton : u32 {
	PS_DPAD_UP = 1, // These dpad ones are not real, we convert from hat switch format.
	PS_DPAD_DOWN = 2,
	PS_DPAD_LEFT = 4,
	PS_DPAD_RIGHT = 8,
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

struct PSInputMapping {
	PSButton button;
	InputKeyCode keyCode;
};

static const PSInputMapping g_psInputMappings[] = {
	{PS_DPAD_UP, NKCODE_DPAD_UP},
	{PS_DPAD_DOWN, NKCODE_DPAD_DOWN},
	{PS_DPAD_LEFT, NKCODE_DPAD_LEFT},
	{PS_DPAD_RIGHT, NKCODE_DPAD_RIGHT},
	{PS_BTN_SQUARE, NKCODE_BUTTON_4},
	{PS_BTN_TRIANGLE, NKCODE_BUTTON_3},
	{PS_BTN_CIRCLE, NKCODE_BUTTON_1},
	{PS_BTN_CROSS, NKCODE_BUTTON_2},
	{PS_BTN_PS_BUTTON, NKCODE_HOME},
	{PS_BTN_SHARE, NKCODE_BUTTON_9},
	{PS_BTN_OPTIONS, NKCODE_BUTTON_10},
	{PS_BTN_L1, NKCODE_BUTTON_7},
	{PS_BTN_R1, NKCODE_BUTTON_8},
	// {PS_BTN_L2, NKCODE_BUTTON_L2},  // These are done by the analog triggers.
	// {PS_BTN_R2, NKCODE_BUTTON_R2},
	{PS_BTN_L3, NKCODE_BUTTON_THUMBL},
	{PS_BTN_R3, NKCODE_BUTTON_THUMBR},
};

enum PSStickAxis : u32 {
	PS_STICK_LX = 0,
	PS_STICK_LY = 1,
	PS_STICK_RX = 2,
	PS_STICK_RY = 3,
};

struct PSStickMapping {
	PSStickAxis stickAxis;
	InputAxis inputAxis;
};

// This is the same mapping as DInput etc.
static const PSStickMapping g_psStickMappings[] = {
	{PS_STICK_LX, JOYSTICK_AXIS_X},
	{PS_STICK_LY, JOYSTICK_AXIS_Y},
	{PS_STICK_RX, JOYSTICK_AXIS_Z},
	{PS_STICK_RY, JOYSTICK_AXIS_RX},
};

enum PSTriggerAxis : u32 {
	PS_TRIGGER_L2 = 0,
	PS_TRIGGER_R2 = 1,
};

struct PSTriggerMapping {
	PSTriggerAxis triggerAxis;
	InputAxis inputAxis;
};

static const PSTriggerMapping g_psTriggerMappings[] = {
	{PS_TRIGGER_L2, JOYSTICK_AXIS_LTRIGGER},
	{PS_TRIGGER_R2, JOYSTICK_AXIS_RTRIGGER},
};

struct PSControllerInfo {
	u16 vendorId;
	u16 productId;
	PSSubType type;
	const char *name;
};

constexpr u16 SONY_VID = 0x054C;

constexpr u16 DS4_WIRELESS = 0x0BA0;
constexpr u16 PS_CLASSIC = 0x0CDA;

// We pick a few ones from here to support, let's add more later.
// https://github.com/ds4windowsapp/DS4Windows/blob/65609b470f53a4f832fb07ac24085d3e28ec15bd/DS4Windows/DS4Library/DS4Devices.cs#L126
static const PSControllerInfo g_psInfos[] = {
	{SONY_VID, 0x05C4, PSSubType::DS4, "DS4 v.1"},
	{SONY_VID, 0x09CC, PSSubType::DS4, "DS4 v.2"},
	{SONY_VID, 0x0CE6, PSSubType::DS5, "DualSense"},
	{SONY_VID, PS_CLASSIC, PSSubType::DS4, "PS Classic"},
	// {PSSubType::DS4, DS4_WIRELESS},
	// {PSSubType::DS5, DUALSENSE_WIRELESS},
	// {PSSubType::DS5, DUALSENSE_EDGE_WIRELESS},
};

static bool IsSupportedGamepad(HANDLE handle, USHORT *pidOut, PSSubType *subType) {
	HIDD_ATTRIBUTES attr{sizeof(HIDD_ATTRIBUTES)};
	if (!HidD_GetAttributes(handle, &attr)) {
		return false;
	}
	for (const auto &info : g_psInfos) {
		if (attr.VendorID == info.vendorId && attr.ProductID == info.productId) {
			*pidOut = attr.ProductID;
			*subType = info.type;
			return true;
		}
	}
	return false;
}

struct DualSenseOutputReport{
	uint8_t reportId;
	uint8_t flags1;
	uint8_t flags2;
	uint8_t headphone[4];
	uint8_t muteLED;
	uint8_t micMute;
};

// https://github.com/ds4windowsapp/DS4Windows/blob/65609b470f53a4f832fb07ac24085d3e28ec15bd/DS4Windows/DS4Library/InputDevices/DualSenseDevice.cs#L905

// Sends initialization packet to DualSense
static bool InitializeDualSense(HANDLE handle, int outReportSize) {
	if (outReportSize != 48) {
		return false;
	}

	// Output report (0x05) – sets lightbar, enables full report mode
	uint8_t reportData[48] = {0};

	DualSenseOutputReport report;
	report.reportId = 2;
	report.flags1 = 0x0C;
	report.flags2 = 0x15;
	report.muteLED = 1;

	memcpy(reportData, &report, sizeof(report));

	DWORD written;
	bool result = WriteFile(handle, reportData, sizeof(reportData), &written, NULL);
	if (!result) {
		u32 errorCode = GetLastError();

		if (errorCode == ERROR_INVALID_PARAMETER) {
			if (!HidD_SetOutputReport(handle, reportData, outReportSize)) {
				errorCode = GetLastError();
			}
		}

		WARN_LOG(Log::UI, "Failed initializing: %08x", errorCode);
		return false;
	}
	return true;
}

enum class DS4FeatureBits : u8 {
	VOL_L = 0x10,
	VOL_R = 0x20,
	MIC_VOL = 0x40,
	SPEAKER_VOL = 0x80,
	RUMBLE = 0x1,
	LIGHTBAR = 0x2,
	FLASH = 0x4,
};
ENUM_CLASS_BITOPS(DS4FeatureBits);

static bool InitializeDualShock(HANDLE handle, int outReportSize) {
	if (outReportSize != 32) {
		WARN_LOG(Log::UI, "DS4 unexpected report size %d", outReportSize);
		return false;
	}

	// DS4 USB output report (report ID 0x05)
	// Total size: 32 bytes (must match device buffer size)
	uint8_t report[32] = {0};

	report[0] = 0x05; // Report ID (DS4 output)
	report[1] = (u8)(DS4FeatureBits::RUMBLE | DS4FeatureBits::LIGHTBAR | DS4FeatureBits::FLASH); // Flags: enable lightbar, rumble, etc.
	report[2] = 0x02;
	// Rumble
	report[4] = 0x00; // Right (weak)
	report[5] = 0x00; // Left (strong)

	// Lightbar (RGB)
	report[6] = 0x00; // Red
	report[7] = 0x10; // Green
	report[8] = 0x40; // Blue (dim blue)

	DWORD written;
	return WriteFile(handle, report, sizeof(report), &written, NULL);
}

HANDLE OpenFirstDualShockOrSense(PSSubType *subType, int *reportSize) {
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
				FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (handle != INVALID_HANDLE_VALUE) {
				USHORT pid;
				if (IsSupportedGamepad(handle, &pid, subType)) {
					INFO_LOG(Log::UI, "Found supported gamepad. PID: %04x", pid);
					HIDP_CAPS caps;
					PHIDP_PREPARSED_DATA preparsedData;

					HidD_GetPreparsedData(handle, &preparsedData);
					HidP_GetCaps(preparsedData, &caps);
					HidD_FreePreparsedData(preparsedData);

					*reportSize = caps.InputReportByteLength;
					int outReportSize = caps.OutputReportByteLength;

					INFO_LOG(Log::UI, "Initializing gamepad. out report size=%d", outReportSize);
					bool result;
					if (*subType == PSSubType::DS5) {
						result = InitializeDualSense(handle, outReportSize);
					} else {
						result = InitializeDualShock(handle, outReportSize);
					}

					SetupDiDestroyDeviceInfoList(deviceInfoSet);

					return handle;
				} else {
					DEBUG_LOG(Log::UI, "Skipping HID device: pid=%02x", pid);
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

static u32 DecodeHatSwitch(u8 dpad) {
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

struct DualShockInputReport {
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

bool HidInputDevice::ReadDualShockInput(HANDLE handle, PSControllerState *state) {
	BYTE inputReport[64]{}; // 64-byte input report for DS4
	DWORD bytesRead = 0;
	if (ReadFile(handle, inputReport, sizeof(inputReport), &bytesRead, nullptr)) {
		DualShockInputReport report{};
		static_assert(sizeof(report) < sizeof(inputReport));
		if (bytesRead < 14) {
			return false;
		}

		// OK, check the first byte to figure out what we're dealing with here.
		int offset = 1;
		int reportId;
		if (inputReport[0] == 0xA1) {
			// 2-byte bluetooth frame
			offset = 2;
			reportId = inputReport[1];
		} else {
			offset = 1;
			reportId = inputReport[0];
		}
		// const bool isBluetooth = (reportId == 0x11 || reportId == 0x31);

		memcpy(&report, inputReport + offset, sizeof(report));

		// Center the sticks.
		state->stickAxes[PS_STICK_LX] = report.lx - 128;
		state->stickAxes[PS_STICK_LY] = report.ly - 128;
		state->stickAxes[PS_STICK_RX] = report.rx - 128;
		state->stickAxes[PS_STICK_RY] = report.ry - 128;

		// Copy over the triggers.
		state->triggerAxes[PS_TRIGGER_L2] = report.l2_analog;
		state->triggerAxes[PS_TRIGGER_R2] = report.r2_analog;

		u32 buttons{};
		int frameCounter = report.buttons[2] >> 2;
		report.buttons[2] &= 3;
		memcpy(&buttons, &report.buttons[0], 3);

		// Clear out and re-fill the DPAD, it works differently somehow
		buttons &= ~0xF;
		buttons |= DecodeHatSwitch(report.buttons[0] & 0xF);

		state->buttons = buttons;
		return true;
	} else {
		u32 error = GetLastError();
		return false;
	}
}

// So strange that this is different!
struct DualSenseInputReport {
	u8 lx;
	u8 ly;
	u8 rx;
	u8 ry;

	u8 l2_analog;
	u8 r2_analog;

	u8 frameCounter;

	u8 buttons[3];

	// TODO: More stuff (battery, tilt, etc).
};

bool HidInputDevice::ReadDualSenseInput(HANDLE handle, PSControllerState *state) {
	BYTE inputReport[64]{}; // 64-byte input report for DS4
	DWORD bytesRead = 0;
	if (ReadFile(handle, inputReport, sizeof(inputReport), &bytesRead, nullptr)) {
		DualSenseInputReport report{};
		static_assert(sizeof(report) < sizeof(inputReport));
		if (bytesRead < 14) {
			return false;
		}

		// OK, check the first byte to figure out what we're dealing with here.
		int offset = 1;
		if (inputReport[0] != 1) {
			// Wrong data
			return false;
		}
		// const bool isBluetooth = (reportId == 0x11 || reportId == 0x31);

		memcpy(&report, inputReport + offset, sizeof(report));

		// Center the sticks.
		state->stickAxes[PS_STICK_LX] = report.lx - 128;
		state->stickAxes[PS_STICK_LY] = report.ly - 128;
		state->stickAxes[PS_STICK_RX] = report.rx - 128;
		state->stickAxes[PS_STICK_RY] = report.ry - 128;

		// Copy over the triggers.
		state->triggerAxes[PS_TRIGGER_L2] = report.l2_analog;
		state->triggerAxes[PS_TRIGGER_R2] = report.r2_analog;

		u32 buttons{};
		report.buttons[2] &= 3;  // Remove noise
		memcpy(&buttons, &report.buttons[0], 3);

		// Clear out and re-fill the DPAD, it works differently somehow
		buttons &= ~0xF;
		buttons |= DecodeHatSwitch(report.buttons[0] & 0xF);

		state->buttons = buttons;
		return true;
	} else {
		u32 error = GetLastError();
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
			HANDLE newController = OpenFirstDualShockOrSense(&subType_, &reportSize_);
			if (newController) {
				controller_ = newController;
			}
		} else {
			pollCount_--;
		}
	}

	if (controller_) {
		PSControllerState state{};
		bool result;
		if (subType_ == PSSubType::DS4) {
			result = ReadDualShockInput(controller_, &state);
		} else if (subType_ == PSSubType::DS5) {
			result = ReadDualSenseInput(controller_, &state);
		}

		if (result) {
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

			for (const auto &mapping : g_psStickMappings) {
				if (state.stickAxes[mapping.stickAxis] != prevState_.stickAxes[mapping.stickAxis]) {
					AxisInput axis;
					axis.deviceId = deviceID;
					axis.axisId = mapping.inputAxis;
					axis.value = (float)state.stickAxes[mapping.stickAxis] * (1.0f / 128.0f);
					NativeAxis(&axis, 1);
				}
			}

			for (const auto &mapping : g_psTriggerMappings) {
				if (state.triggerAxes[mapping.triggerAxis] != prevState_.triggerAxes[mapping.triggerAxis]) {
					AxisInput axis;
					axis.deviceId = deviceID;
					axis.axisId = mapping.inputAxis;
					axis.value = (float)state.triggerAxes[mapping.triggerAxis] * (1.0f / 255.0f);
					NativeAxis(&axis, 1);
				}
			}

			prevState_ = state;
			return UPDATESTATE_NO_SLEEP;  // The ReadFile sleeps for us, effectively.
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
