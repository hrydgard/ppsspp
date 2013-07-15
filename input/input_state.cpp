#include "input/input_state.h"
#include "input/keycodes.h"

const char *GetDeviceName(int deviceId) {
	switch (deviceId) {
	case DEVICE_ID_DEFAULT: return "built-in";
	case DEVICE_ID_KEYBOARD: return "kbd";
	case DEVICE_ID_PAD_0: return "pad";
	case DEVICE_ID_X360_0: return "x360";
	case DEVICE_ID_ACCELEROMETER: return "accelerometer";
	default:
		return "unknown";
	}
}

// Default pad mapping to button bits. Used for UI and stuff that doesn't use PPSSPP's key mapping system.
int MapPadButtonFixed(int keycode) {
	switch (keycode) {
	case KEYCODE_BACK: return PAD_BUTTON_BACK;  // Back
	case KEYCODE_MENU: return PAD_BUTTON_MENU;  // Menu

	case KEYCODE_Z:
	case KEYCODE_SPACE:
	case KEYCODE_BUTTON_1:
	case KEYCODE_BUTTON_A: // same as KEYCODE_OUYA_BUTTON_O and KEYCODE_BUTTON_CROSS_PS3
		return PAD_BUTTON_A;

	case KEYCODE_ESCAPE:
	case KEYCODE_BUTTON_2:   
	case KEYCODE_BUTTON_B: // same as KEYCODE_OUYA_BUTTON_A and KEYCODE_BUTTON_CIRCLE_PS3:
		return PAD_BUTTON_B;

	case KEYCODE_DPAD_LEFT: return PAD_BUTTON_LEFT;
	case KEYCODE_DPAD_RIGHT: return PAD_BUTTON_RIGHT;
	case KEYCODE_DPAD_UP: return PAD_BUTTON_UP;
	case KEYCODE_DPAD_DOWN: return PAD_BUTTON_DOWN;

	default:
		return 0;
	}
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