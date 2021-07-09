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
	bool Key(const KeyInput &key, bool *pauseTrigger);
	bool Axis(const AxisInput &axis);

	void SetCallbacks(
		std::function<void(int)> onVKeyDown,
		std::function<void(int)> onVKeyUp,
		std::function<void(int, float, float)> setPSPAnalog);

private:
	void processAxis(const AxisInput &axis, int direction);
	void pspKey(int pspKeyCode, int flags);
	void setVKeyAnalog(char axis, int stick, int virtualKeyMin, int virtualKeyMax, bool setZero = true);

	void SetPSPAxis(char axis, float value, int stick);

	void onVKeyDown(int vkey);
	void onVKeyUp(int vkey);

	// To track mappable virtual keys. We can have as many as we want.
	bool virtKeys[VIRTKEY_COUNT]{};

	// De-noise mapped axis updates
	int axisState_[JOYSTICK_AXIS_MAX]{};

	// Callbacks
	std::function<void(int)> onVKeyDown_;
	std::function<void(int)> onVKeyUp_;
	std::function<void(int, float, float)> setPSPAnalog_;
};
