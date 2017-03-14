#include "input/input_state.h"
#include "input/keycodes.h"
#include <vector>

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
	case DEVICE_ID_X360_0: return "x360";
	case DEVICE_ID_X360_1: return "x360_2";
	case DEVICE_ID_X360_2: return "x360_3";
	case DEVICE_ID_X360_3: return "x360_4";
	case DEVICE_ID_ACCELEROMETER: return "accelerometer";
	case DEVICE_ID_MOUSE: return "mouse";
	default:
		return "unknown";
	}
}

std::vector<KeyDef> dpadKeys;
std::vector<KeyDef> confirmKeys;
std::vector<KeyDef> cancelKeys;
std::vector<KeyDef> tabLeftKeys;
std::vector<KeyDef> tabRightKeys;

static void AppendKeys(std::vector<KeyDef> &keys, const std::vector<KeyDef> &newKeys) {
	for (auto iter = newKeys.begin(); iter != newKeys.end(); ++iter) {
		keys.push_back(*iter);
	}
}

void SetDPadKeys(const std::vector<KeyDef> &leftKey, const std::vector<KeyDef> &rightKey,
		const std::vector<KeyDef> &upKey, const std::vector<KeyDef> &downKey) {
	dpadKeys.clear();

	// Store all directions into one vector for now.  In the future it might be
	// useful to keep track of the different directions separately.
	AppendKeys(dpadKeys, leftKey);
	AppendKeys(dpadKeys, rightKey);
	AppendKeys(dpadKeys, upKey);
	AppendKeys(dpadKeys, downKey);
}

void SetConfirmCancelKeys(const std::vector<KeyDef> &confirm, const std::vector<KeyDef> &cancel) {
	confirmKeys = confirm;
	cancelKeys = cancel;
}

void SetTabLeftRightKeys(const std::vector<KeyDef> &tabLeft, const std::vector<KeyDef> &tabRight) {
	tabLeftKeys = tabLeft;
	tabRightKeys = tabRight;
}
