#include "input/input_state.h"
#include "input/keycodes.h"
#include <vector>

const char *GetDeviceName(int deviceId) {
	switch (deviceId) {
	case DEVICE_ID_DEFAULT: return "built-in";
	case DEVICE_ID_KEYBOARD: return "kbd";
	case DEVICE_ID_PAD_0: return "pad";
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

std::vector<keycode_t> confirmKeys;
std::vector<keycode_t> cancelKeys;

void SetConfirmCancelKeys(std::vector<keycode_t> confirm, std::vector<keycode_t> cancel) {
	confirmKeys = confirm;
	cancelKeys = cancel;
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
