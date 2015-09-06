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
	case DEVICE_ID_ACCELEROMETER: return "accelerometer";
	case DEVICE_ID_MOUSE: return "mouse";
	default:
		return "unknown";
	}
}

// Default pad mapping to button bits. Used for UI and stuff that doesn't use PPSSPP's key mapping system.
int MapPadButtonFixed(int keycode) {
	switch (keycode) {
	case NKCODE_BACK: return PAD_BUTTON_BACK;  // Back
	case NKCODE_MENU: return PAD_BUTTON_MENU;  // Menu

	case NKCODE_Z:
	case NKCODE_SPACE:
	case NKCODE_BUTTON_1:
	case NKCODE_BUTTON_A: // same as NKCODE_OUYA_BUTTON_O and NKCODE_BUTTON_CROSS_PS3
		return PAD_BUTTON_A;

	case NKCODE_ESCAPE:
	case NKCODE_BUTTON_2:
	case NKCODE_BUTTON_B: // same as NKCODE_OUYA_BUTTON_A and NKCODE_BUTTON_CIRCLE_PS3:
		return PAD_BUTTON_B;

	case NKCODE_DPAD_LEFT: return PAD_BUTTON_LEFT;
	case NKCODE_DPAD_RIGHT: return PAD_BUTTON_RIGHT;
	case NKCODE_DPAD_UP: return PAD_BUTTON_UP;
	case NKCODE_DPAD_DOWN: return PAD_BUTTON_DOWN;

	default:
		return 0;
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

uint32_t ButtonTracker::Update() {
	pad_buttons_ |= pad_buttons_async_set;
	pad_buttons_ &= ~pad_buttons_async_clear;
	return pad_buttons_;
}

void ButtonTracker::Process(const KeyInput &input) {
	int btn = MapPadButtonFixed(input.keyCode);
	if (btn == 0)
		return;

	// For now, use a fixed mapping. Good enough for the basics on most platforms.
	if (input.flags & KEY_DOWN) {
		pad_buttons_async_set |= btn;
		pad_buttons_async_clear &= ~btn;
	}
	if (input.flags & KEY_UP) {
		pad_buttons_async_set &= ~btn;
		pad_buttons_async_clear |= btn;
	}
}

ButtonTracker g_buttonTracker;

void UpdateInputState(InputState *input, bool merge) {
	uint32_t btns = g_buttonTracker.Update();
	input->pad_buttons = merge ? (input->pad_buttons | btns) : btns;
	input->pad_buttons_down = (input->pad_last_buttons ^ input->pad_buttons) & input->pad_buttons;
	input->pad_buttons_up = (input->pad_last_buttons ^ input->pad_buttons) & input->pad_last_buttons;
}

void EndInputState(InputState *input) {
	input->pad_last_buttons = input->pad_buttons;
}
