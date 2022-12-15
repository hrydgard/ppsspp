#pragma once

#include "Common/Input/InputState.h"
#include "Core/KeyMap.h"

#include <functional>

// Utilities for mapping input events to PSP inputs and virtual keys.
// Main use is of course from EmuScreen.cpp, but also useful from control settings etc.

// At some point I want to refactor this from using callbacks to simply providing lists of events.
// Still it won't be able to be completely stateless due to the 2-D processing of analog sticks.

class ControlMapper {
public:
	void Update();

	bool Key(const KeyInput &key, bool *pauseTrigger);
	void pspKey(int deviceId, int pspKeyCode, int flags);
	bool Axis(const AxisInput &axis);

	// Required callbacks
	void SetCallbacks(
		std::function<void(int)> onVKeyDown,
		std::function<void(int)> onVKeyUp,
		std::function<void(int, float, float)> setPSPAnalog);

	// Optional callback, only used in config
	void SetRawCallback(std::function<void(int, float, float)> setRawAnalog);

private:
	void processAxis(const AxisInput &axis, int direction);
	void setVKeyAnalog(int deviceId, char axis, int stick, int virtualKeyMin, int virtualKeyMax, bool setZero = true);

	void SetPSPAxis(int deviceId, char axis, float value, int stick);
	void ProcessAnalogSpeed(const AxisInput &axis, bool opposite);

	void onVKeyDown(int deviceId, int vkey);
	void onVKeyUp(int deviceId, int vkey);

	// To track mappable virtual keys. We can have as many as we want.
	bool virtKeys[VIRTKEY_COUNT]{};

	// De-noise mapped axis updates
	int axisState_[JOYSTICK_AXIS_MAX]{};

	int lastNonDeadzoneDeviceID_[2]{};

	float history[2][2] = {};

	// Mappable auto-rotation. Useful for keyboard/dpad->analog in a few games.
	bool autoRotatingAnalogCW_ = false;
	bool autoRotatingAnalogCCW_ = false;

	// Callbacks
	std::function<void(int)> onVKeyDown_;
	std::function<void(int)> onVKeyUp_;
	std::function<void(int, float, float)> setPSPAnalog_;
	std::function<void(int, float, float)> setRawAnalog_;
};

void ConvertAnalogStick(float &x, float &y);
