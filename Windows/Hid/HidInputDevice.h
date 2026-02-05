// This file in particular along with its cpp file is public domain, use it for whatever you want.

// Internal common header for Hid input stuff.

#pragma once

#include <set>

#include "Common/CommonTypes.h"
#include "Common/Input/InputState.h"
#include "Windows/InputDevice.h"
#include "Common/CommonWindows.h"

enum class HIDControllerType {
	DualShock,
	DualSense,
	SwitchPro,
};

struct HIDControllerState {
	// Analog sticks
	s8 stickAxes[4];  // LX LY RX RY
	// Analog triggers
	u8 triggerAxes[2];
	// Buttons. Here the mapping is specific to the controller type and resolved
	// later.
	u32 buttons;  // Bitmask, PSButton enum

	bool accValid = false;
	bool gyroValid = false;
	float accelerometer[3];  // X, Y, Z
	float gyro[3];
};

struct ButtonInputMapping {
	u32 button;
	InputKeyCode keyCode;
};

// Supports a few specific HID input devices, namely DualShock and DualSense.
// More may be added later. Just picks the first one available, for now.
class HidInputDevice : public InputDevice {
public:
	void Init() override;
	int UpdateState() override;
	void Shutdown() override;

	static void AddSupportedDevices(std::set<u32> *deviceVIDPIDs);
	bool HasAccelerometer() const override {
		switch (subType_) {
		case HIDControllerType::DualSense:
		case HIDControllerType::SwitchPro:
			return true;
		default:
			break;
		}
		return false;
	}
private:
	void ReleaseAllKeys(const ButtonInputMapping *buttonMappings, int count);
	InputDeviceID DeviceID(int pad);
	HIDControllerState prevState_{};
	HIDControllerType subType_{};
	HANDLE controller_;
	std::string name_;
	int pad_ = 0;
	int pollCount_ = 0;
	int inReportSize_ = 0;
	int outReportSize_ = 0;
	enum {
		POLL_FREQ = 283,  // a prime number.
	};
};
