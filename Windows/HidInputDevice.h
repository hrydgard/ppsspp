// This file in particular along with its cpp file is public domain, use it for whatever you want.

#pragma once

#include "Input/InputState.h"
#include "Windows/InputDevice.h"
#include <set>
#include <windows.h>

enum class PSSubType {
	DS4,
	DS5
};

// Supports a few specific HID input devices, namely DualShock and DualSense.
// More may be added later. Just picks the first one available, for now.
class HidInputDevice : public InputDevice {
public:
	void Init() override;
	int UpdateState() override;
	void Shutdown() override;

	static void AddSupportedDevices(std::set<u32> *deviceVIDPIDs);
private:
	struct PSControllerState {
		// Analog sticks
		s8 stickAxes[4];  // LX LY RX RY
		// Analog triggers
		u8 triggerAxes[2];
		// Buttons
		u32 buttons;  // Bitmask, PSButton enum
	};
	bool ReadDualShockInput(HANDLE handle, PSControllerState *state);
	bool ReadDualSenseInput(HANDLE handle, PSControllerState *state);
	void ReleaseAllKeys();
	InputDeviceID DeviceID(int pad);
	PSControllerState prevState_{};

	PSSubType subType_{};
	HANDLE controller_;
	int pad_ = 0;
	int pollCount_ = 0;
	int reportSize_ = 0;
	int outReportSize_ = 0;
	enum {
		POLL_FREQ = 283,  // a prime number.
	};
};
