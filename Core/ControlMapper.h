#pragma once

#include "Common/Input/InputState.h"
#include "Core/KeyMap.h"

#include <functional>
#include <cstring>
#include <mutex>

// Utilities for mapping input events to PSP inputs and virtual keys.
// Main use is of course from EmuScreen.cpp, but also useful from control settings etc.
class ControlMapper {
public:
	void Update();

	// Inputs to the table-based mapping
	// These functions are free-threaded.
	bool Key(const KeyInput &key, bool *pauseTrigger);
	void Axis(const AxisInput &axis);

	// Required callbacks.
	// TODO: These are so many now that a virtual interface might be more appropriate..
	void SetCallbacks(
		std::function<void(int, bool)> onVKey,
		std::function<void(int, float)> onVKeyAnalog,
		std::function<void(uint32_t, uint32_t)> setAllPSPButtonStates_,
		std::function<void(int, bool)> setPSPButtonState,
		std::function<void(int, float, float)> setPSPAnalog,
		std::function<void(int, float, float)> setRawAnalog);

	// Inject raw PSP key input directly, such as from touch screen controls.
	// Combined with the mapped input. Unlike __Ctrl APIs, this supports
	// virtual key codes, though not analog mappings.
	void PSPKey(int deviceId, int pspKeyCode, int flags);

	void GetDebugString(char *buffer, size_t bufSize) const;

private:
	bool UpdatePSPState(const InputMapping &changedMapping);

	void SetPSPAxis(int deviceId, int stick, char axis, float value);

	void onVKey(int vkey, bool down);
	void onVKeyAnalog(int deviceId, int vkey, float value);

	// To track mappable virtual keys. We can have as many as we want.
	float virtKeys_[VIRTKEY_COUNT]{};

	int lastNonDeadzoneDeviceID_[2]{};

	float history_[2][2]{};
	float converted_[2][2]{};  // for debug display

	// Mappable auto-rotation. Useful for keyboard/dpad->analog in a few games.
	bool autoRotatingAnalogCW_ = false;
	bool autoRotatingAnalogCCW_ = false;

	// Protects basically all the state.
	std::mutex mutex_;

	std::map<InputMapping, float> curInput_;

	// Callbacks
	std::function<void(int, bool)> onVKey_;
	std::function<void(int, float)> onVKeyAnalog_;
	std::function<void(uint32_t, uint32_t)> setAllPSPButtonStates_;
	std::function<void(int, bool)> setPSPButtonState_;
	std::function<void(int, float, float)> setPSPAnalog_;
	std::function<void(int, float, float)> setRawAnalog_;
};

void ConvertAnalogStick(float x, float y, float *outX, float *outY);
