#include <vector>
#include <cstdio>

#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/StringUtils.h"

const char *GetDeviceName(int deviceId) {
	switch (deviceId) {
	case DEVICE_ID_ANY: return "any";
	case DEVICE_ID_DEFAULT: return "built-in";
	case DEVICE_ID_KEYBOARD: return "kbd";
	case DEVICE_ID_PAD_0: return "pad1";
	case DEVICE_ID_PAD_1: return "pad2";
	case DEVICE_ID_PAD_2: return "pad3";
	case DEVICE_ID_PAD_3: return "pad4";
	case DEVICE_ID_PAD_4: return "pad5";
	case DEVICE_ID_PAD_5: return "pad6";
	case DEVICE_ID_PAD_6: return "pad7";
	case DEVICE_ID_PAD_7: return "pad8";
	case DEVICE_ID_PAD_8: return "pad9";
	case DEVICE_ID_PAD_9: return "pad10";
	case DEVICE_ID_XINPUT_0: return "x360";  // keeping these strings for backward compat. Hm, what would break if we changed them to xbox?
	case DEVICE_ID_XINPUT_1: return "x360_2";
	case DEVICE_ID_XINPUT_2: return "x360_3";
	case DEVICE_ID_XINPUT_3: return "x360_4";
	case DEVICE_ID_ACCELEROMETER: return "accelerometer";
	case DEVICE_ID_MOUSE: return "mouse";
	case DEVICE_ID_XR_CONTROLLER_LEFT: return "xr_l";
	case DEVICE_ID_XR_CONTROLLER_RIGHT: return "xr_r";
	default:
		return "unknown";
	}
}

std::vector<InputMapping> dpadKeys;
std::vector<InputMapping> confirmKeys;
std::vector<InputMapping> cancelKeys;
std::vector<InputMapping> infoKeys;
std::vector<InputMapping> tabLeftKeys;
std::vector<InputMapping> tabRightKeys;
static std::unordered_map<InputDeviceID, int> uiFlipAnalogY;

static void AppendKeys(std::vector<InputMapping> &keys, const std::vector<InputMapping> &newKeys) {
	for (const auto &key : newKeys) {
		keys.push_back(key);
	}
}

void SetDPadKeys(const std::vector<InputMapping> &leftKey, const std::vector<InputMapping> &rightKey,
		const std::vector<InputMapping> &upKey, const std::vector<InputMapping> &downKey) {
	dpadKeys.clear();

	// Store all directions into one vector for now.  In the future it might be
	// useful to keep track of the different directions separately.
	AppendKeys(dpadKeys, leftKey);
	AppendKeys(dpadKeys, rightKey);
	AppendKeys(dpadKeys, upKey);
	AppendKeys(dpadKeys, downKey);
}

void SetConfirmCancelKeys(const std::vector<InputMapping> &confirm, const std::vector<InputMapping> &cancel) {
	confirmKeys = confirm;
	cancelKeys = cancel;
}

void SetTabLeftRightKeys(const std::vector<InputMapping> &tabLeft, const std::vector<InputMapping> &tabRight) {
	tabLeftKeys = tabLeft;
	tabRightKeys = tabRight;
}

void SetInfoKeys(const std::vector<InputMapping> &info) {
	infoKeys = info;
}

void SetAnalogFlipY(const std::unordered_map<InputDeviceID, int> &flipYByDeviceId) {
	uiFlipAnalogY = flipYByDeviceId;
}

int GetAnalogYDirection(InputDeviceID deviceId) {
	auto configured = uiFlipAnalogY.find(deviceId);
	if (configured != uiFlipAnalogY.end())
		return configured->second;
	return 0;
}

// NOTE: Changing the format of FromConfigString/ToConfigString breaks controls.ini backwards compatibility.
InputMapping InputMapping::FromConfigString(const std::string_view str) {
	std::vector<std::string_view> parts;
	SplitString(str, '-', parts);
	// We only convert to std::string here to add null terminators for atoi.
	InputDeviceID deviceId = (InputDeviceID)(atoi(std::string(parts[0]).c_str()));
	InputKeyCode keyCode = (InputKeyCode)atoi(std::string(parts[1]).c_str());

	InputMapping mapping;
	mapping.deviceId = deviceId;
	mapping.keyCode = keyCode;
	return mapping;
}

std::string InputMapping::ToConfigString() const {
	return StringFromFormat("%d-%d", (int)deviceId, keyCode);
}

void InputMapping::FormatDebug(char *buffer, size_t bufSize) const {
	if (IsAxis()) {
		int direction;
		int axis = Axis(&direction);
		snprintf(buffer, bufSize, "Device: %d Axis: %d (%d)", (int)deviceId, axis, direction);
	} else {
		snprintf(buffer, bufSize, "Device: %d Key: %d", (int)deviceId, keyCode);
	}
}
