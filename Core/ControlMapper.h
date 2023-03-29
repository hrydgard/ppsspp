#pragma once

#include "Common/Input/InputState.h"
#include "Core/KeyMap.h"

#include <functional>
#include <cstring>

// Utilities for mapping input events to PSP inputs and virtual keys.
// Main use is of course from EmuScreen.cpp, but also useful from control settings etc.

// At some point I want to refactor this from using callbacks to simply providing lists of events.
// Still it won't be able to be completely stateless due to the 2-D processing of analog sticks.

// Unified representation. Might spread across the code base later.
struct ControlInputKey {
	int direction;  // 0 if key, 1 or -1 if axis.
	int deviceId;
	int controlId;  // key or axis.

	bool operator < (const ControlInputKey &other) const {
		return memcmp(this, &other, sizeof(*this)) < 0;
	}
};

class ControlMapper {
public:
	void Update();

	// Inputs to the table-based mapping
	bool Key(const KeyInput &key, bool *pauseTrigger);
	void Axis(const AxisInput &axis);

	// Required callbacks
	void SetCallbacks(
		std::function<void(int, bool)> onVKey,
		std::function<void(uint32_t, uint32_t)> setAllPSPButtonStates_,
		std::function<void(int, bool)> setPSPButtonState,
		std::function<void(int, float, float)> setPSPAnalog);

	// Inject raw PSP key input directly, such as from touch screen controls.
	// Combined with the mapped input.
	void PSPKey(int deviceId, int pspKeyCode, int flags);

	// Optional callback, only used in config
	void SetRawCallback(std::function<void(int, float, float)> setRawAnalog);

private:
	bool UpdatePSPState();

	void ProcessAxis(const AxisInput &axis, int direction);
	void SetVKeyAnalog(int deviceId, char axis, int stick, int virtualKeyMin, int virtualKeyMax, bool setZero = true);

	void SetPSPKey(int deviceId, int pspKeyCode, int flags);
	void SetPSPAxis(int deviceId, char axis, float value, int stick);
	void ProcessAnalogSpeed(const AxisInput &axis, bool opposite);

	void onVKey(int deviceId, int vkey, bool down);

	// To track mappable virtual keys. We can have as many as we want.
	bool lastVirtKeys_[VIRTKEY_COUNT]{};

	// De-noise mapped axis updates
	int axisState_[JOYSTICK_AXIS_MAX]{};

	int lastNonDeadzoneDeviceID_[2]{};

	float history_[2][2]{};

	// Mappable auto-rotation. Useful for keyboard/dpad->analog in a few games.
	bool autoRotatingAnalogCW_ = false;
	bool autoRotatingAnalogCCW_ = false;

	std::map<InputMapping, float> curInput_;

	// Callbacks
	std::function<void(int, bool)> onVKey_;
	std::function<void(uint32_t, uint32_t)> setAllPSPButtonStates_;
	std::function<void(int, bool)> setPSPButtonState_;
	std::function<void(int, float, float)> setPSPAnalog_;
	std::function<void(int, float, float)> setRawAnalog_;
};

void ConvertAnalogStick(float &x, float &y);
