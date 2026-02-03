// This file in particular along with its header is public domain, use it for whatever you want.

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <initguid.h>
#include <vector>

#include "Windows/Hid/HidInputDevice.h"
#include "Windows/Hid/SwitchPro.h"
#include "Windows/Hid/DualSense.h"
#include "Windows/Hid/DualShock.h"
#include "Windows/Hid/HidCommon.h"
#include "Common/CommonTypes.h"
#include "Common/TimeUtil.h"
#include "Common/Math/math_util.h"
#include "Common/Log.h"
#include "Common/Input/InputState.h"
#include "Common/Common.h"
#include "Common/System/NativeApp.h"
#include "Common/System/OSD.h"
#include "Core/KeyMap.h"

struct HidStickMapping {
	HidStickAxis stickAxis;
	InputAxis inputAxis;
};

// This is the same mapping as DInput etc.
static const HidStickMapping g_psStickMappings[] = {
	{HID_STICK_LX, JOYSTICK_AXIS_X},
	{HID_STICK_LY, JOYSTICK_AXIS_Y},
	{HID_STICK_RX, JOYSTICK_AXIS_Z},
	{HID_STICK_RY, JOYSTICK_AXIS_RX},
};

struct HidTriggerMapping {
	HidTriggerAxis triggerAxis;
	InputAxis inputAxis;
};

static const HidTriggerMapping g_psTriggerMappings[] = {
	{HID_TRIGGER_L2, JOYSTICK_AXIS_LTRIGGER},
	{HID_TRIGGER_R2, JOYSTICK_AXIS_RTRIGGER},
};

struct HIDControllerInfo {
	u16 vendorId;
	u16 productId;
	HIDControllerType type;
	const char *name;
};

constexpr u16 SONY_VID = 0x054C;
constexpr u16 NINTENDO_VID = 0x57e;
constexpr u16 SWITCH_PRO_PID = 0x2009;
constexpr u16 DS4_WIRELESS = 0x0BA0;
constexpr u16 PS_CLASSIC = 0x0CDA;

// We pick a few ones from here to support, let's add more later.
// https://github.com/ds4windowsapp/DS4Windows/blob/65609b470f53a4f832fb07ac24085d3e28ec15bd/DS4Windows/DS4Library/DS4Devices.cs#L126
static const HIDControllerInfo g_psInfos[] = {
	{SONY_VID, 0x05C4, HIDControllerType::DualShock, "DS4 v.1"},
	{SONY_VID, 0x09CC, HIDControllerType::DualShock, "DS4 v.2"},
	{SONY_VID, 0x0CE6, HIDControllerType::DualSense, "DualSense"},
	{SONY_VID, PS_CLASSIC, HIDControllerType::DualShock, "PS Classic"},
	{NINTENDO_VID, SWITCH_PRO_PID, HIDControllerType::SwitchPro, "Switch Pro"},
	// {PSSubType::DS4, DS4_WIRELESS},
	// {PSSubType::DS5, DUALSENSE_WIRELESS},
	// {PSSubType::DS5, DUALSENSE_EDGE_WIRELESS},
};

static const HIDControllerInfo *GetGamepadInfo(HANDLE handle) {
	HIDD_ATTRIBUTES attr{sizeof(HIDD_ATTRIBUTES)};
	if (!HidD_GetAttributes(handle, &attr)) {
		return nullptr;
	}
	for (const auto &info : g_psInfos) {
		if (attr.VendorID == info.vendorId && attr.ProductID == info.productId) {
			return &info;
		}
	}
	return nullptr;
}

static HANDLE OpenFirstHIDController(HIDControllerType *subType, int *reportSize, int *outReportSize, const HIDControllerInfo **outInfo) {
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
				const HIDControllerInfo *info = GetGamepadInfo(handle);
				*outInfo = info;
				if (info) {
					*subType = info->type;
					INFO_LOG(Log::UI, "Found supported gamepad. PID: %04x", info->productId);
					HIDP_CAPS caps;
					PHIDP_PREPARSED_DATA preparsedData;

					HidD_GetPreparsedData(handle, &preparsedData);
					HidP_GetCaps(preparsedData, &caps);
					HidD_FreePreparsedData(preparsedData);

					*reportSize = caps.InputReportByteLength;
					*outReportSize = caps.OutputReportByteLength;

					INFO_LOG(Log::UI, "Initializing gamepad. out report size=%d", *outReportSize);
					bool result;
					switch (*subType) {
					case HIDControllerType::DualSense:
						result = InitializeDualSense(handle, *outReportSize);
						break;
					case HIDControllerType::DualShock:
						result = InitializeDualShock(handle, *outReportSize);
						break;
					case HIDControllerType::SwitchPro:
						result = InitializeSwitchPro(handle);
						break;
					}

					if (!result) {
						ERROR_LOG(Log::UI, "Controller initialization failed");
					}

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
		const u32 vidpid = MAKELONG(info.vendorId, info.productId);
		deviceVIDPIDs->insert(vidpid);
	}
}

void HidInputDevice::Init() {}
void HidInputDevice::Shutdown() {
	if (controller_) {
		switch (subType_) {
		case HIDControllerType::DualShock:
			ShutdownDualShock(controller_, outReportSize_);
			break;
		case HIDControllerType::DualSense:
			ShutdownDualsense(controller_, outReportSize_);
			break;
		}
		CloseHandle(controller_);
		controller_ = nullptr;
	}
}

void HidInputDevice::ReleaseAllKeys(const ButtonInputMapping *buttonMappings, int count) {
	for (int i = 0; i < count; i++) {
		const auto &mapping = buttonMappings[i];
		KeyInput key;
		key.deviceId = DEVICE_ID_XINPUT_0 + pad_;
		key.flags = KeyInputFlags::UP;
		key.keyCode = mapping.keyCode;
		NativeKey(key);
	}

	static const InputAxis allAxes[6] = {
		JOYSTICK_AXIS_X,
		JOYSTICK_AXIS_Y,
		JOYSTICK_AXIS_Z,
		JOYSTICK_AXIS_RX,
		JOYSTICK_AXIS_LTRIGGER,
		JOYSTICK_AXIS_RTRIGGER,
	};

	for (const auto axisId : allAxes) {
		AxisInput axis;
		axis.deviceId = DEVICE_ID_XINPUT_0 + pad_;
		axis.axisId = axisId;
		axis.value = 0;
		NativeAxis(&axis, 1);
	}
}

InputDeviceID HidInputDevice::DeviceID(int pad) {
	return DEVICE_ID_PAD_0 + pad;
}

int HidInputDevice::UpdateState() {
	const InputDeviceID deviceID = DeviceID(pad_);

	if (!controller_) {
		// Poll for controllers from time to time.
		if (pollCount_ == 0) {
			pollCount_ = POLL_FREQ;
			const HIDControllerInfo *info{};
			HANDLE newController = OpenFirstHIDController(&subType_, &inReportSize_, &outReportSize_, &info);
			if (newController) {
				controller_ = newController;
				if (info) {
					name_ = info->name;
				}
				KeyMap::NotifyPadConnected(deviceID, name_);
			}
		} else {
			pollCount_--;
		}
	}

	if (controller_) {
		HIDControllerState state{};
		bool result = false;
		const ButtonInputMapping *buttonMappings = nullptr;
		size_t buttonMappingsSize = 0;
		if (subType_ == HIDControllerType::DualShock) {
			result = ReadDualShockInput(controller_, &state, inReportSize_);
			GetPSButtonInputMappings(&buttonMappings, &buttonMappingsSize);
		} else if (subType_ == HIDControllerType::DualSense) {
			result = ReadDualSenseInput(controller_, &state, inReportSize_);
			GetPSButtonInputMappings(&buttonMappings, &buttonMappingsSize);
		} else if (subType_ == HIDControllerType::SwitchPro) {
			result = ReadSwitchProInput(controller_, &state);
			GetSwitchButtonInputMappings(&buttonMappings, &buttonMappingsSize);
		}

		if (result) {
			// Process the input and generate input events.
			const u32 downMask = state.buttons & (~prevState_.buttons);
			const u32 upMask = (~state.buttons) & prevState_.buttons;

			for (u32 i = 0; i < buttonMappingsSize; i++) {
				const ButtonInputMapping &mapping = buttonMappings[i];
				if (downMask & mapping.button) {
					KeyInput key;
					key.deviceId = deviceID;
					key.flags = KeyInputFlags::DOWN;
					key.keyCode = mapping.keyCode;
					NativeKey(key);
				}
				if (upMask & mapping.button) {
					KeyInput key;
					key.deviceId = deviceID;
					key.flags = KeyInputFlags::UP;
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

			if (state.accValid) {
				NativeAccelerometer(state.accelerometer[0], state.accelerometer[1], state.accelerometer[2]);
			}

			prevState_ = state;
			return UPDATESTATE_NO_SLEEP;  // The ReadFile sleeps for us, effectively.
		} else {
			WARN_LOG(Log::System, "Failed to read controller - assuming disconnected.");
			// might have been disconnected. retry later.
			KeyMap::NotifyPadDisconnected(deviceID);
			ReleaseAllKeys(buttonMappings, (int)buttonMappingsSize);
			CloseHandle(controller_);
			controller_ = NULL;
			pollCount_ = POLL_FREQ;
		}
	}
	return 0;
}
