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
		std::function<void(uint32_t, uint32_t)> updatePSPButtons,
		std::function<void(int, float, float)> setPSPAnalog,
		std::function<void(int, float, float)> setRawAnalog);

	// Inject raw PSP key input directly, such as from touch screen controls.
	// Combined with the mapped input. Unlike __Ctrl APIs, this supports
	// virtual key codes, including analog mappings.
	void PSPKey(int deviceId, int pspKeyCode, int flags);

	// Toggle swapping DPAD and Analog. Useful on some input devices with few buttons.
	void ToggleSwapAxes();

	// Call this when a Vkey press triggers leaving the screen you're using the controlmapper on. This can cause
	// the loss of key-up events, which will confuse things later when you're back.
	// Might replace this later by allowing through "key-up" and similar events to lower screens.
	void ForceReleaseVKey(int vkey);

	void GetDebugString(char *buffer, size_t bufSize) const;

private:
	bool UpdatePSPState(const InputMapping &changedMapping);
	float MapAxisValue(float value, int vkId, const InputMapping &mapping, const InputMapping &changedMapping, bool *oppositeTouched);
	void SwapMappingIfEnabled(uint32_t *vkey);

	void SetPSPAxis(int deviceId, int stick, char axis, float value);
	void UpdateAnalogOutput(int stick);

	void onVKey(int vkey, bool down);
	void onVKeyAnalog(int deviceId, int vkey, float value);

	// To track mappable virtual keys. We can have as many as we want.
	float virtKeys_[VIRTKEY_COUNT]{};
	bool virtKeyOn_[VIRTKEY_COUNT]{};  // Track boolean output separaately since thresholds may differ.

	int lastNonDeadzoneDeviceID_[2]{};

	float history_[2][2]{};
	float converted_[2][2]{};  // for debug display

	// Mappable auto-rotation. Useful for keyboard/dpad->analog in a few games.
	bool autoRotatingAnalogCW_ = false;
	bool autoRotatingAnalogCCW_ = false;

	bool swapAxes_ = false;

	// Protects basically all the state.
	std::mutex mutex_;

	std::map<InputMapping, float> curInput_;

	// Callbacks
	std::function<void(int, bool)> onVKey_;
	std::function<void(int, float)> onVKeyAnalog_;
	std::function<void(uint32_t, uint32_t)> updatePSPButtons_;
	std::function<void(int, float, float)> setPSPAnalog_;
	std::function<void(int, float, float)> setRawAnalog_;
};

void ConvertAnalogStick(float x, float y, float *outX, float *outY);
