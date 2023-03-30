#include <algorithm>

#include "Common/Math/math_util.h"
#include "Common/TimeUtil.h"
#include "Common/Log.h"

#include "Core/HLE/sceCtrl.h"
#include "Core/KeyMap.h"
#include "Core/ControlMapper.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/System.h"

// TODO: Possibly make these configurable?
static float GetDeviceAxisThreshold(int device) {
	return device == DEVICE_ID_MOUSE ? AXIS_BIND_THRESHOLD_MOUSE : AXIS_BIND_THRESHOLD;
}

static int GetOppositeVKey(int vkey) {
	switch (vkey) {
	case VIRTKEY_AXIS_X_MIN: return VIRTKEY_AXIS_X_MAX; break;
	case VIRTKEY_AXIS_X_MAX: return VIRTKEY_AXIS_X_MIN; break;
	case VIRTKEY_AXIS_Y_MIN: return VIRTKEY_AXIS_Y_MAX; break;
	case VIRTKEY_AXIS_Y_MAX: return VIRTKEY_AXIS_Y_MIN; break;
	case VIRTKEY_AXIS_RIGHT_X_MIN: return VIRTKEY_AXIS_RIGHT_X_MAX; break;
	case VIRTKEY_AXIS_RIGHT_X_MAX: return VIRTKEY_AXIS_RIGHT_X_MIN; break;
	case VIRTKEY_AXIS_RIGHT_Y_MIN: return VIRTKEY_AXIS_RIGHT_Y_MAX; break;
	case VIRTKEY_AXIS_RIGHT_Y_MAX: return VIRTKEY_AXIS_RIGHT_Y_MIN; break;
	default:
		return 0;
	}
}

// This is applied on the circular radius, not directly on the axes.
static float MapAxisValue(float v) {
	const float deadzone = g_Config.fAnalogDeadzone;
	const float invDeadzone = g_Config.fAnalogInverseDeadzone;
	const float sensitivity = g_Config.fAnalogSensitivity;
	const float sign = v >= 0.0f ? 1.0f : -1.0f;

	return sign * Clamp(invDeadzone + (abs(v) - deadzone) / (1.0f - deadzone) * (sensitivity - invDeadzone), 0.0f, 1.0f);
}

void ConvertAnalogStick(float x, float y, float *outX, float *outY) {
	const bool isCircular = g_Config.bAnalogIsCircular;

	float norm = std::max(fabsf(x), fabsf(y));
	if (norm == 0.0f) {
		*outX = x;
		*outY = y;
		return;
	}

	if (isCircular) {
		float newNorm = sqrtf(x * x + y * y);
		float factor = newNorm / norm;
		x *= factor;
		y *= factor;
		norm = newNorm;
	}

	float mappedNorm = MapAxisValue(norm);
	*outX = Clamp(x / norm * mappedNorm, -1.0f, 1.0f);
	*outY = Clamp(y / norm * mappedNorm, -1.0f, 1.0f);
}

void ControlMapper::SetCallbacks(
	std::function<void(int, bool)> onVKey,
	std::function<void(int, float)> onVKeyAnalog,
	std::function<void(uint32_t, uint32_t)> setAllPSPButtonStates,
	std::function<void(int, bool)> setPSPButtonState,
	std::function<void(int, float, float)> setPSPAnalog) {
	onVKey_ = onVKey;
	onVKeyAnalog_ = onVKeyAnalog;
	setAllPSPButtonStates_ = setAllPSPButtonStates;
	setPSPButtonState_ = setPSPButtonState;
	setPSPAnalog_ = setPSPAnalog;
}

void ControlMapper::SetRawCallback(std::function<void(int, float, float)> setRawAnalog) {
	setRawAnalog_ = setRawAnalog;
}

void ControlMapper::SetPSPAxis(int device, int stick, char axis, float value) {
	int axisId = axis == 'X' ? 0 : 1;

	float position[2];
	position[0] = history_[stick][0];
	position[1] = history_[stick][1];

	position[axisId] = value;

	float x = position[0];
	float y = position[1];

	if (setRawAnalog_) {
		setRawAnalog_(stick, x, y);
	}

	// NOTE: We need to use single-axis checks, since the other axis might be from another device,
	// so we'll add a little leeway.
	bool inDeadZone = fabsf(value) < g_Config.fAnalogDeadzone * 0.7f;

	bool ignore = false;
	if (inDeadZone && lastNonDeadzoneDeviceID_[stick] != device) {
		// Ignore this event! See issue #15465
		 ignore = true;
	}

	if (!inDeadZone) {
		lastNonDeadzoneDeviceID_[stick] = device;
	}

	if (!ignore) {
		history_[stick][axisId] = value;
		float x, y;
		ConvertAnalogStick(history_[stick][0], history_[stick][1], &x, &y);
		setPSPAnalog_(stick, x, y);
	}
}

static int RotatePSPKeyCode(int x) {
	switch (x) {
	case CTRL_UP: return CTRL_RIGHT;
	case CTRL_RIGHT: return CTRL_DOWN;
	case CTRL_DOWN: return CTRL_LEFT;
	case CTRL_LEFT: return CTRL_UP;
	default:
		return x;
	}
}

bool ControlMapper::UpdatePSPState(const InputMapping &changedMapping) {
	// Instead of taking an input key and finding what it outputs, we loop through the OUTPUTS and
	// see if the input that corresponds to it has a value. That way we can easily implement all sorts
	// of crazy input combos if needed.

	int rotations = 0;
	switch (g_Config.iInternalScreenRotation) {
	case ROTATION_LOCKED_HORIZONTAL180: rotations = 2; break;
	case ROTATION_LOCKED_VERTICAL:      rotations = 1; break;
	case ROTATION_LOCKED_VERTICAL180:   rotations = 3; break;
	}

	// For the PSP's button inputs, we just go through and put the flags together.
	uint32_t buttonMask = 0;
	uint32_t changedButtonMask = 0;
	for (int i = 0; i < 32; i++) {
		uint32_t mask = 1 << i;
		if (!(mask & CTRL_MASK_USER)) {
			// Not a mappable button bit
			continue;
		}

		uint32_t mappingBit = mask;
		for (int i = 0; i < rotations; i++) {
			mappingBit = RotatePSPKeyCode(mappingBit);
		}

		std::vector<InputMapping> inputMappings;
		if (!KeyMap::InputMappingsFromPspButton(mappingBit, &inputMappings, false))
			continue;

		// If a mapping could consist of a combo, we could trivially check it here.
		for (auto &mapping : inputMappings) {
			// Check if the changed mapping was involved in this PSP key.
			if (changedMapping == mapping) {
				changedButtonMask |= mask;
			}

			auto iter = curInput_.find(mapping);
			if (iter != curInput_.end() && iter->second > GetDeviceAxisThreshold(iter->first.deviceId)) {
				buttonMask |= mask;
			}
		}
	}

	// We only request changing the buttons where the mapped input was involved.
	setAllPSPButtonStates_(buttonMask & changedButtonMask, (~buttonMask) & changedButtonMask);

	// OK, handle all the virtual keys next. For these we need to do deltas here and send events.
	for (int i = 0; i < VIRTKEY_COUNT; i++) {
		int vkId = i + VIRTKEY_FIRST;
		std::vector<InputMapping> inputMappings;
		if (!KeyMap::InputMappingsFromPspButton(vkId, &inputMappings, false))
			continue;

		// If a mapping could consist of a combo, we could trivially check it here.
		// Save the first device ID so we can pass it into onVKeyDown, which in turn needs it for the analog
		// mapping which gets a little hacky.
		float threshold = 1.0f;
		bool touchedByMapping = false;
		float value = 0.0f;
		for (auto &mapping : inputMappings) {
			if (mapping == changedMapping) {
				touchedByMapping = true;
			}

			auto iter = curInput_.find(mapping);
			if (iter != curInput_.end()) {
				if (mapping.IsAxis()) {
					threshold = GetDeviceAxisThreshold(iter->first.deviceId);
				}
				value += iter->second;
			}
		}

		if (!touchedByMapping) {
			continue;
		}

		value = clamp_value(value, 0.0f, 1.0f);

		// Derive bools from the floats using the device's threshold.
		bool bPrevValue = virtKeys_[i] >= threshold;
		bool bValue = value >= threshold;
		if (virtKeys_[i] != value) {
			// INFO_LOG(G3D, "vkeyanalog %s : %f", KeyMap::GetVirtKeyName(vkId), value);
			onVKeyAnalog(changedMapping.deviceId, vkId, value);
			virtKeys_[i] = value;
		}
		if (!bPrevValue && bValue) {
			// INFO_LOG(G3D, "vkeyon %s", KeyMap::GetVirtKeyName(vkId));
			onVKey(vkId, true);
		} else if (bPrevValue && !bValue) {
			// INFO_LOG(G3D, "vkeyoff %s", KeyMap::GetVirtKeyName(vkId));
			onVKey(vkId, false);
		}
	}

	return true;
}

bool ControlMapper::Key(const KeyInput &key, bool *pauseTrigger) {
	if (key.flags & KEY_IS_REPEAT) {
		// Claim that we handled this. Prevents volume key repeats from popping up the volume control on Android.
		return true;
	}

	InputMapping mapping(key.deviceId, key.keyCode);

	if (key.flags & KEY_DOWN) {
		curInput_[mapping] = 1.0f;
	} else if (key.flags & KEY_UP) {
		curInput_[mapping] = 0.0f;
	}

	bool mappingFound = KeyMap::InputMappingToPspButton(mapping, nullptr);
	DEBUG_LOG(SYSTEM, "Key: %d DeviceId: %d", key.keyCode, key.deviceId);
	if (!mappingFound || key.deviceId == DEVICE_ID_DEFAULT) {
		if ((key.flags & KEY_DOWN) && key.keyCode == NKCODE_BACK) {
			*pauseTrigger = true;
			return true;
		}
	}

	return UpdatePSPState(mapping);
}

void ControlMapper::Axis(const AxisInput &axis) {
	if (axis.value > 0) {
		InputMapping mapping(axis.deviceId, axis.axisId, 1);
		curInput_[mapping] = axis.value;
		UpdatePSPState(mapping);
	} else if (axis.value < 0) {
		InputMapping mapping(axis.deviceId, axis.axisId, -1);
		curInput_[mapping] = -axis.value;
		UpdatePSPState(mapping);
	} else if (axis.value == 0.0f) {  // Threshold?
		// Both directions! Prevents sticking for digital input devices that are axises (like HAT)
		InputMapping mappingPositive(axis.deviceId, axis.axisId, 1);
		InputMapping mappingNegative(axis.deviceId, axis.axisId, -1);
		curInput_[mappingPositive] = 0.0f;
		curInput_[mappingNegative] = 0.0f;
		UpdatePSPState(mappingPositive);
		UpdatePSPState(mappingNegative);
	}
}

void ControlMapper::Update() {
	if (autoRotatingAnalogCW_) {
		const double now = time_now_d();
		// Clamp to a square
		float x = std::min(1.0f, std::max(-1.0f, 1.42f * (float)cos(now * -g_Config.fAnalogAutoRotSpeed)));
		float y = std::min(1.0f, std::max(-1.0f, 1.42f * (float)sin(now * -g_Config.fAnalogAutoRotSpeed)));

		setPSPAnalog_(0, x, y);
	} else if (autoRotatingAnalogCCW_) {
		const double now = time_now_d();
		float x = std::min(1.0f, std::max(-1.0f, 1.42f * (float)cos(now * g_Config.fAnalogAutoRotSpeed)));
		float y = std::min(1.0f, std::max(-1.0f, 1.42f * (float)sin(now * g_Config.fAnalogAutoRotSpeed)));

		setPSPAnalog_(0, x, y);
	}
}

void ControlMapper::PSPKey(int deviceId, int pspKeyCode, int flags) {
	if (pspKeyCode >= VIRTKEY_FIRST) {
		int vk = pspKeyCode - VIRTKEY_FIRST;
		if (flags & KEY_DOWN) {
			virtKeys_[vk] = 1.0f;
			onVKey(pspKeyCode, true);
		}
		if (flags & KEY_UP) {
			virtKeys_[vk] = 0.0f;
			onVKey(pspKeyCode, false);
		}
	} else {
		// INFO_LOG(SYSTEM, "pspKey %d %d", pspKeyCode, flags);
		if (flags & KEY_DOWN)
			setPSPButtonState_(pspKeyCode, true);
		if (flags & KEY_UP)
			setPSPButtonState_(pspKeyCode, false);
	}
}

void ControlMapper::onVKeyAnalog(int deviceId, int vkey, float value) {
	// Unfortunately, for digital->analog inputs to work sanely, we need to sum up
	// with the opposite value too.
	int stick = 0;
	int axis = 'X';
	int opposite = GetOppositeVKey(vkey);
	float sign = 1.0f;
	switch (vkey) {
	case VIRTKEY_AXIS_X_MIN: sign = -1.0f; break;
	case VIRTKEY_AXIS_X_MAX: break;
	case VIRTKEY_AXIS_Y_MIN: axis = 'Y'; sign = -1.0f; break;
	case VIRTKEY_AXIS_Y_MAX: axis = 'Y'; break;
	case VIRTKEY_AXIS_RIGHT_X_MIN: stick = CTRL_STICK_RIGHT; sign = -1.0f; break;
	case VIRTKEY_AXIS_RIGHT_X_MAX: stick = CTRL_STICK_RIGHT; break;
	case VIRTKEY_AXIS_RIGHT_Y_MIN: stick = CTRL_STICK_RIGHT; axis = 'Y'; sign = -1.0f; break;
	case VIRTKEY_AXIS_RIGHT_Y_MAX: stick = CTRL_STICK_RIGHT; axis = 'Y'; break;
	default:
		if (onVKeyAnalog_)
			onVKeyAnalog_(vkey, value);
		return;
	}
	value -= virtKeys_[opposite - VIRTKEY_FIRST];
	SetPSPAxis(deviceId, stick, axis, sign * value);
}

void ControlMapper::onVKey(int vkey, bool down) {
	switch (vkey) {
	case VIRTKEY_ANALOG_ROTATE_CW:
		if (down) {
			autoRotatingAnalogCW_ = true;
			autoRotatingAnalogCCW_ = false;
		} else {
			autoRotatingAnalogCW_ = false;
			setPSPAnalog_(0, 0.0f, 0.0f);
		}
		break;
	case VIRTKEY_ANALOG_ROTATE_CCW:
		if (down) {
			autoRotatingAnalogCW_ = false;
			autoRotatingAnalogCCW_ = true;
		} else {
			autoRotatingAnalogCCW_ = false;
			setPSPAnalog_(0, 0.0f, 0.0f);
		}
		break;
	default:
		if (onVKey_)
			onVKey_(vkey, down);
		break;
	}
}
