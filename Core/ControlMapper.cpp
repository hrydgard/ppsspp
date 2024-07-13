#include <algorithm>
#include <sstream>

#include "Common/Math/math_util.h"
#include "Common/TimeUtil.h"
#include "Common/StringUtils.h"
#include "Common/Log.h"

#include "Core/HLE/sceCtrl.h"
#include "Core/KeyMap.h"
#include "Core/ControlMapper.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/System.h"

using KeyMap::MultiInputMapping;

const float AXIS_BIND_THRESHOLD = 0.75f;
const float AXIS_BIND_THRESHOLD_MOUSE = 0.01f;


// We reduce the threshold of some axes when another axis on the same stick is active.
// This makes it easier to hit diagonals if you bind an analog stick to four face buttons or D-Pad.
static InputAxis GetCoAxis(InputAxis axis) {
	switch (axis) {
	case JOYSTICK_AXIS_X: return JOYSTICK_AXIS_Y;
	case JOYSTICK_AXIS_Y: return JOYSTICK_AXIS_X;

		// This looks weird, but it's simply how XInput axes are mapped.
	case JOYSTICK_AXIS_Z: return JOYSTICK_AXIS_RZ;
	case JOYSTICK_AXIS_RZ: return JOYSTICK_AXIS_Z;

		// Not sure if these two are used.
	case JOYSTICK_AXIS_RX: return JOYSTICK_AXIS_RY;
	case JOYSTICK_AXIS_RY: return JOYSTICK_AXIS_RX;

	default:
		return JOYSTICK_AXIS_MAX; // invalid
	}
}

float ControlMapper::GetDeviceAxisThreshold(int device, const InputMapping &mapping) {
	if (device == DEVICE_ID_MOUSE) {
		return AXIS_BIND_THRESHOLD_MOUSE;
	}
	if (mapping.IsAxis()) {
		switch (KeyMap::GetAxisType((InputAxis)mapping.Axis(nullptr))) {
		case KeyMap::AxisType::TRIGGER:
			return g_Config.fAnalogTriggerThreshold;
		case KeyMap::AxisType::STICK:
		{
			// Co-axis processing, see GetCoAxes comment.
			InputAxis axis = (InputAxis)mapping.Axis(nullptr);
			InputAxis coAxis = GetCoAxis(axis);
			if (coAxis != JOYSTICK_AXIS_MAX) {
				float absCoValue = fabsf(rawAxisValue_[(int)coAxis]);
				if (absCoValue > 0.0f) {
					// Bias down the threshold if the other axis is active.
					float biasedThreshold = AXIS_BIND_THRESHOLD * (1.0f - absCoValue * 0.35f);
					// INFO_LOG(Log::System, "coValue: %f  threshold: %f", absCoValue, biasedThreshold);
					return biasedThreshold;
				}
			}
			break;
		}
		default:
			break;
		}
	}
	return AXIS_BIND_THRESHOLD;
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

static bool IsAxisVKey(int vkey) {
	// Little hacky but works, of course.
	return GetOppositeVKey(vkey) != 0;
}

static bool IsUnsignedMapping(int vkey) {
	return vkey == VIRTKEY_SPEED_ANALOG;
}

static bool IsSignedAxis(int axis) {
	switch (axis) {
	case JOYSTICK_AXIS_X:
	case JOYSTICK_AXIS_Y:
	case JOYSTICK_AXIS_Z:
	case JOYSTICK_AXIS_RX:
	case JOYSTICK_AXIS_RY:
	case JOYSTICK_AXIS_RZ:
		return true;
	default:
		return false;
	}
}

// This is applied on the circular radius, not directly on the axes.
// TODO: Share logic with tilt?

static float MapAxisValue(float v) {
	const float deadzone = g_Config.fAnalogDeadzone;
	const float invDeadzone = g_Config.fAnalogInverseDeadzone;
	const float sensitivity = g_Config.fAnalogSensitivity;
	const float sign = v >= 0.0f ? 1.0f : -1.0f;

	// Apply deadzone.
	v = Clamp((fabsf(v) - deadzone) / (1.0f - deadzone), 0.0f, 1.0f);

	// Apply sensitivity and inverse deadzone.
	if (v != 0.0f) {
		v = Clamp(invDeadzone + v * (sensitivity - invDeadzone), 0.0f, 1.0f);
	}

	return sign * v;
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
	std::function<void(uint32_t, uint32_t)> updatePSPButtons,
	std::function<void(int, float, float)> setPSPAnalog,
	std::function<void(int, float, float)> setRawAnalog) {
	onVKey_ = onVKey;
	onVKeyAnalog_ = onVKeyAnalog;
	updatePSPButtons_ = updatePSPButtons;
	setPSPAnalog_ = setPSPAnalog;
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

		UpdateAnalogOutput(stick);
	}
}

void ControlMapper::UpdateAnalogOutput(int stick) {
	float x, y;
	ConvertAnalogStick(history_[stick][0], history_[stick][1], &x, &y);
	if (virtKeyOn_[VIRTKEY_ANALOG_LIGHTLY - VIRTKEY_FIRST]) {
		x *= g_Config.fAnalogLimiterDeadzone;
		y *= g_Config.fAnalogLimiterDeadzone;
	}
	converted_[stick][0] = x;
	converted_[stick][1] = y;
	setPSPAnalog_(stick, x, y);
}

void ControlMapper::ForceReleaseVKey(int vkey) {
	// Note: This one is called from an onVKey_ handler, which already holds mutex_.

	KeyMap::LockMappings();
	std::vector<KeyMap::MultiInputMapping> multiMappings;
	if (KeyMap::InputMappingsFromPspButtonNoLock(vkey, &multiMappings, true)) {
		double now = time_now_d();
		for (const auto &entry : multiMappings) {
			for (const auto &mapping : entry.mappings) {
				curInput_[mapping] = { 0.0f, now };
				// Different logic for signed axes?
				UpdatePSPState(mapping, now);
			}
		}
	}
	KeyMap::UnlockMappings();
}

void ControlMapper::ReleaseAll() {
	std::vector<AxisInput> axes;
	std::vector<KeyInput> keys;

	{
		std::lock_guard<std::mutex> guard(mutex_);

		for (const auto &input : curInput_) {
			if (input.first.IsAxis()) {
				if (input.second.value != 0.0f) {
					AxisInput axis;
					axis.deviceId = input.first.deviceId;
					int dir;
					axis.axisId = (InputAxis)input.first.Axis(&dir);
					axis.value = 0.0;
					axes.push_back(axis);
				}
			} else {
				if (input.second.value != 0.0) {
					KeyInput key;
					key.deviceId = input.first.deviceId;
					key.flags = KEY_UP;
					key.keyCode = (InputKeyCode)input.first.keyCode;
					keys.push_back(key);
				}
			}
		}
	}

	Axis(axes.data(), axes.size());;
	for (const auto &key : keys) {
		Key(key, nullptr);
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

// Used to decay analog values when clashing with digital ones.
static ControlMapper::InputSample ReduceMagnitude(ControlMapper::InputSample sample, double now) {
	float reduction = std::min(std::max(0.0f, (float)(now - sample.timestamp) - 2.0f), 1.0f);
	if (reduction > 0.0f) {
		sample.value *= (1.0f - reduction);
	}
	if ((sample.value > 0.0f && sample.value < 0.05f) || (sample.value < 0.0f && sample.value > -0.05f)) {
		sample.value = 0.0f;
	}
	return sample;
}

float ControlMapper::MapAxisValue(float value, int vkId, const InputMapping &mapping, const InputMapping &changedMapping, bool *oppositeTouched) {
	if (IsUnsignedMapping(vkId)) {
		// If a signed axis is mapped to an unsigned mapping,
		// convert it. This happens when mapping DirectInput triggers to analog speed,
		// for example.
		int direction;
		if (IsSignedAxis(mapping.Axis(&direction))) {
			// The value has been split up into two curInput values, so we need to go fetch the other
			// and put them back together again. Kind of awkward, but at least makes the regular case simple...
			InputMapping other = mapping.FlipDirection();
			if (other == changedMapping) {
				*oppositeTouched = true;
			}
			float valueOther = curInput_[other].value;
			float signedValue = value - valueOther;
			float ranged = (signedValue + 1.0f) * 0.5f;
			if (direction == -1) {
				ranged = 1.0f - ranged;
			}
			// NOTICE_LOG(Log::System, "rawValue: %f other: %f signed: %f ranged: %f", iter->second, valueOther, signedValue, ranged);
			return ranged;
		} else {
			return value;
		}
	} else {
		return value;
	}
}

static bool IsSwappableVKey(uint32_t vkey) {
	switch (vkey) {
	case CTRL_UP:
	case CTRL_LEFT:
	case CTRL_DOWN:
	case CTRL_RIGHT:
	case VIRTKEY_AXIS_X_MIN:
	case VIRTKEY_AXIS_X_MAX:
	case VIRTKEY_AXIS_Y_MIN:
	case VIRTKEY_AXIS_Y_MAX:
		return true;
	default:
		return false;
	}
}

void ControlMapper::SwapMappingIfEnabled(uint32_t *vkey) {
	if (swapAxes_) {
		switch (*vkey) {
		case CTRL_UP: *vkey = VIRTKEY_AXIS_Y_MAX; break;
		case VIRTKEY_AXIS_Y_MAX: *vkey = CTRL_UP; break;
		case CTRL_DOWN: *vkey = VIRTKEY_AXIS_Y_MIN; break;
		case VIRTKEY_AXIS_Y_MIN: *vkey = CTRL_DOWN; break;
		case CTRL_LEFT: *vkey = VIRTKEY_AXIS_X_MIN; break;
		case VIRTKEY_AXIS_X_MIN: *vkey = CTRL_LEFT; break;
		case CTRL_RIGHT: *vkey = VIRTKEY_AXIS_X_MAX; break;
		case VIRTKEY_AXIS_X_MAX: *vkey = CTRL_RIGHT; break;
		}
	}
}

// Can only be called from Key or Axis.
// mutex_ should be locked, and also KeyMap::LockMappings().
// TODO: We should probably make a batched version of this.
bool ControlMapper::UpdatePSPState(const InputMapping &changedMapping, double now) {
	// Instead of taking an input key and finding what it outputs, we loop through the OUTPUTS and
	// see if the input that corresponds to it has a value. That way we can easily implement all sorts
	// of crazy input combos if needed.

	int rotations = 0;
	switch (g_Config.iInternalScreenRotation) {
	case ROTATION_LOCKED_HORIZONTAL180: rotations = 2; break;
	case ROTATION_LOCKED_VERTICAL:      rotations = 1; break;
	case ROTATION_LOCKED_VERTICAL180:   rotations = 3; break;
	}

	// For the PSP's digital button inputs, we just go through and put the flags together.
	uint32_t buttonMask = 0;
	uint32_t changedButtonMask = 0;
	std::vector<MultiInputMapping> inputMappings;
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

		SwapMappingIfEnabled(&mappingBit);
		if (!KeyMap::InputMappingsFromPspButtonNoLock(mappingBit, &inputMappings, false))
			continue;

		// If a mapping could consist of a combo, we could trivially check it here.
		for (auto &multiMapping : inputMappings) {
			// Check if the changed mapping was involved in this PSP key.
			if (multiMapping.mappings.contains(changedMapping)) {
				changedButtonMask |= mask;
			}
			// Check if all inputs are "on".
			bool all = true;
			double curTime = 0.0;
			for (auto mapping : multiMapping.mappings) {
				auto iter = curInput_.find(mapping);
				if (iter == curInput_.end()) {
					all = false;
					continue;
				}
				// Stop reverse ordering from triggering.
				if (g_Config.bStrictComboOrder && iter->second.timestamp < curTime) {
					all = false;
					break;
				} else {
					curTime = iter->second.timestamp;
				}
				bool down = iter->second.value > 0.0f && iter->second.value > GetDeviceAxisThreshold(iter->first.deviceId, mapping);
				if (!down)
					all = false;
			}
			if (all) {
				buttonMask |= mask;
			}
		}
	}

	// We only request changing the buttons where the mapped input was involved.
	updatePSPButtons_(buttonMask & changedButtonMask, (~buttonMask) & changedButtonMask);

	bool keyInputUsed = changedButtonMask != 0;
	bool updateAnalogSticks = false;

	// OK, handle all the virtual keys next. For these we need to do deltas here and send events.
	// Note that virtual keys include the analog directions, as they are driven by them.
	for (int i = 0; i < VIRTKEY_COUNT; i++) {
		int vkId = i + VIRTKEY_FIRST;

		uint32_t idForMapping = vkId;
		SwapMappingIfEnabled(&idForMapping);

		if (!KeyMap::InputMappingsFromPspButtonNoLock(idForMapping, &inputMappings, false))
			continue;

		// If a mapping could consist of a combo, we could trivially check it here.
		// Save the first device ID so we can pass it into onVKeyDown, which in turn needs it for the analog
		// mapping which gets a little hacky.
		float threshold = 1.0f;
		bool touchedByMapping = false;
		float value = 0.0f;
		for (auto &multiMapping : inputMappings) {
			if (multiMapping.mappings.contains(changedMapping)) {
				touchedByMapping = true;
			}

			float product = 1.0f;  // We multiply the various inputs in a combo mapping with each other.
			double curTime = 0.0;
			for (auto mapping : multiMapping.mappings) {
				auto iter = curInput_.find(mapping);

				if (iter != curInput_.end()) {
					// Stop reverse ordering from triggering.
					if (g_Config.bStrictComboOrder && iter->second.timestamp < curTime) {
						product = 0.0f;
						break;
					} else {
						curTime = iter->second.timestamp;
					}

					if (mapping.IsAxis()) {
						threshold = GetDeviceAxisThreshold(iter->first.deviceId, mapping);
						float value = MapAxisValue(iter->second.value, idForMapping, mapping, changedMapping, &touchedByMapping);
						product *= value;
					} else {
						product *= iter->second.value;
					}
				} else {
					product = 0.0f;
				}
			}

			value += product;
		}

		if (!touchedByMapping) {
			continue;
		}

		keyInputUsed = true;

		// Small values from analog inputs like gamepad sticks can linger around, which is bad here because we sum
		// up before applying deadzone etc. This means that it can be impossible to reach the min/max values with digital input!
		// So if non-analog events clash with analog ones mapped to the same input, decay the analog input,
		// which will quickly get things back to normal, while if it's intentional to use both at the same time for some reason,
		// that still works, though a bit weaker. We could also zero here, but you never know who relies on such strange tricks..
		// Note: This is an old problem, it didn't appear with the refactoring.
		if (!changedMapping.IsAxis()) {
			for (auto &multiMapping : inputMappings) {
				for (auto &mapping : multiMapping.mappings) {
					if (mapping != changedMapping && curInput_[mapping].value > 0.0f) {
						// Note that this takes the time into account now - values will
						// decay after a while, not immediately.
						curInput_[mapping] = ReduceMagnitude(curInput_[mapping], now);
					}
				}
			}
		}

		value = clamp_value(value, 0.0f, 1.0f);

		// Derive bools from the floats using the device's threshold.
		// NOTE: This must be before the equality check below.
		bool bPrevValue = virtKeys_[i] >= threshold;
		bool bValue = value >= threshold;

		if (virtKeys_[i] != value) {
			// INFO_LOG(Log::G3D, "vkeyanalog %s : %f", KeyMap::GetVirtKeyName(vkId), value);
			onVKeyAnalog(changedMapping.deviceId, vkId, value);
			virtKeys_[i] = value;
		}

		if (!bPrevValue && bValue) {
			// INFO_LOG(Log::G3D, "vkeyon %s", KeyMap::GetVirtKeyName(vkId));
			onVKey(vkId, true);
			virtKeyOn_[vkId - VIRTKEY_FIRST] = true;

			if (vkId == VIRTKEY_ANALOG_LIGHTLY) {
				updateAnalogSticks = true;
			}
		} else if (bPrevValue && !bValue) {
			// INFO_LOG(Log::G3D, "vkeyoff %s", KeyMap::GetVirtKeyName(vkId));
			onVKey(vkId, false);
			virtKeyOn_[vkId - VIRTKEY_FIRST] = false;

			if (vkId == VIRTKEY_ANALOG_LIGHTLY) {
				updateAnalogSticks = true;
			}
		}
	}

	if (updateAnalogSticks) {
		// If "lightly" (analog limiter) was toggled, we need to update both computed stick outputs.
		UpdateAnalogOutput(0);
		UpdateAnalogOutput(1);
	}

	return keyInputUsed;
}

bool ControlMapper::Key(const KeyInput &key, bool *pauseTrigger) {
	if (key.flags & KEY_IS_REPEAT) {
		// Claim that we handled this. Prevents volume key repeats from popping up the volume control on Android.
		return true;
	}

	double now = time_now_d();
	InputMapping mapping(key.deviceId, key.keyCode);

	std::lock_guard<std::mutex> guard(mutex_);

	if (key.deviceId < DEVICE_ID_COUNT) {
		deviceTimestamps_[(int)key.deviceId] = now;
	}

	if (key.flags & KEY_DOWN) {
		curInput_[mapping] = { 1.0f, now };
	} else if (key.flags & KEY_UP) {
		curInput_[mapping] = { 0.0f, now};
	}

	// TODO: See if this can be simplified further somehow.
	if ((key.flags & KEY_DOWN) && key.keyCode == NKCODE_BACK) {
		bool mappingFound = KeyMap::InputMappingToPspButton(mapping, nullptr);
		DEBUG_LOG(Log::System, "Key: %d DeviceId: %d", key.keyCode, key.deviceId);
		if (!mappingFound || key.deviceId == DEVICE_ID_DEFAULT) {
			*pauseTrigger = true;
			return true;
		}
	}

	KeyMap::LockMappings();
	bool retval = UpdatePSPState(mapping, now);
	KeyMap::UnlockMappings();
	return retval;
}

void ControlMapper::ToggleSwapAxes() {
	// Note: The lock is already locked here.

	swapAxes_ = !swapAxes_;

	updatePSPButtons_(0, CTRL_LEFT | CTRL_RIGHT | CTRL_UP | CTRL_DOWN);

	for (uint32_t vkey = VIRTKEY_FIRST; vkey < VIRTKEY_LAST; vkey++) {
		if (IsSwappableVKey(vkey)) {
			if (virtKeyOn_[vkey - VIRTKEY_FIRST]) {
				onVKey_(vkey, false);
				virtKeyOn_[vkey - VIRTKEY_FIRST] = false;
			}
			if (virtKeys_[vkey - VIRTKEY_FIRST] > 0.0f) {
				onVKeyAnalog_(vkey, 0.0f);
				virtKeys_[vkey - VIRTKEY_FIRST] = 0.0f;
			}
		}
	}

	history_[0][0] = 0.0f;
	history_[0][1] = 0.0f;

	UpdateAnalogOutput(0);
	UpdateAnalogOutput(1);
}

void ControlMapper::UpdateCurInputAxis(const InputMapping &mapping, float value, double timestamp) {
	InputSample &input = curInput_[mapping];
	input.value = value;
	if (value >= GetDeviceAxisThreshold(mapping.deviceId, mapping)) {
		if (input.timestamp == 0.0) {
			input.timestamp = time_now_d();
		}
	} else {
		input.timestamp = 0.0;
	}
}

void ControlMapper::Axis(const AxisInput *axes, size_t count) {
	double now = time_now_d();

	std::lock_guard<std::mutex> guard(mutex_);

	KeyMap::LockMappings();
	for (size_t i = 0; i < count; i++) {
		const AxisInput &axis = axes[i];

		if (axis.deviceId == DEVICE_ID_MOUSE && !g_Config.bMouseControl) {
			continue;
		}

		size_t deviceIndex = (size_t)axis.deviceId;  // this wraps -1 up high, so will get rejected on the next line.
		if (deviceIndex < (size_t)DEVICE_ID_COUNT) {
			deviceTimestamps_[deviceIndex] = now;
		}
		rawAxisValue_[axis.axisId] = axis.value;  // these are only used for co-axis mapping
		if (axis.value >= 0.0f) {
			InputMapping mapping(axis.deviceId, axis.axisId, 1);
			InputMapping opposite(axis.deviceId, axis.axisId, -1);
			UpdateCurInputAxis(mapping, axis.value, now);
			UpdateCurInputAxis(opposite, 0.0f, now);
			UpdatePSPState(mapping, now);
			UpdatePSPState(opposite, now);
		} else if (axis.value < 0.0f) {
			InputMapping mapping(axis.deviceId, axis.axisId, -1);
			InputMapping opposite(axis.deviceId, axis.axisId, 1);
			UpdateCurInputAxis(mapping, -axis.value, now);
			UpdateCurInputAxis(opposite, 0.0f, now);
			UpdatePSPState(mapping, now);
			UpdatePSPState(opposite, now);
		}
	}
	KeyMap::UnlockMappings();
}

void ControlMapper::Update(double now) {
	if (autoRotatingAnalogCW_) {
		// Clamp to a square
		float x = std::min(1.0f, std::max(-1.0f, 1.42f * (float)cos(now * -g_Config.fAnalogAutoRotSpeed)));
		float y = std::min(1.0f, std::max(-1.0f, 1.42f * (float)sin(now * -g_Config.fAnalogAutoRotSpeed)));

		setPSPAnalog_(0, x, y);
	} else if (autoRotatingAnalogCCW_) {
		float x = std::min(1.0f, std::max(-1.0f, 1.42f * (float)cos(now * g_Config.fAnalogAutoRotSpeed)));
		float y = std::min(1.0f, std::max(-1.0f, 1.42f * (float)sin(now * g_Config.fAnalogAutoRotSpeed)));

		setPSPAnalog_(0, x, y);
	}
}

void ControlMapper::PSPKey(int deviceId, int pspKeyCode, int flags) {
	std::lock_guard<std::mutex> guard(mutex_);
	if (pspKeyCode >= VIRTKEY_FIRST) {
		int vk = pspKeyCode - VIRTKEY_FIRST;
		if (flags & KEY_DOWN) {
			virtKeys_[vk] = 1.0f;
			onVKey(pspKeyCode, true);
			onVKeyAnalog(deviceId, pspKeyCode, 1.0f);
		}
		if (flags & KEY_UP) {
			virtKeys_[vk] = 0.0f;
			onVKey(pspKeyCode, false);
			onVKeyAnalog(deviceId, pspKeyCode, 0.0f);
		}
	} else {
		// INFO_LOG(Log::System, "pspKey %d %d", pspKeyCode, flags);
		if (flags & KEY_DOWN)
			updatePSPButtons_(pspKeyCode, 0);
		if (flags & KEY_UP)
			updatePSPButtons_(0, pspKeyCode);
	}
}

void ControlMapper::onVKeyAnalog(int deviceId, int vkey, float value) {
	// Unfortunately, for digital->analog inputs to work sanely, we need to sum up
	// with the opposite value too.
	int stick = 0;
	int axis = 'X';
	int oppositeVKey = GetOppositeVKey(vkey);
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
	if (oppositeVKey != 0) {
		float oppVal = virtKeys_[oppositeVKey - VIRTKEY_FIRST];
		if (oppVal != 0.0f) {
			value -= oppVal;
			// NOTICE_LOG(Log::sceCtrl, "Reducing %f by %f (from %08x : %s)", value, oppVal, oppositeVKey, KeyMap::GetPspButtonName(oppositeVKey).c_str());
		}
	}
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

void ControlMapper::GetDebugString(char *buffer, size_t bufSize) const {
	std::stringstream str;
	for (auto iter : curInput_) {
		char temp[256];
		iter.first.FormatDebug(temp, sizeof(temp));
		str << temp << ": " << iter.second.value << std::endl;
	}
	for (int i = 0; i < ARRAY_SIZE(virtKeys_); i++) {
		int vkId = VIRTKEY_FIRST + i;
		if ((vkId >= VIRTKEY_AXIS_X_MIN && vkId <= VIRTKEY_AXIS_Y_MAX) || vkId == VIRTKEY_ANALOG_LIGHTLY || vkId == VIRTKEY_SPEED_ANALOG) {
			str << KeyMap::GetPspButtonName(vkId) << ": " << virtKeys_[i] << std::endl;
		}
	}
	str << "Lstick: " << converted_[0][0] << ", " << converted_[0][1] << std::endl;
	truncate_cpy(buffer, bufSize, str.str().c_str());
}
