/*
 * Copyright (c) 2013 Sacha Refshauge
 *
 */
// Blackberry implementation of the framework.

#include <pwd.h>
#include <unistd.h>
#include <string>

#include "BlackberryMain.h"

// Simple implementations of System functions

void SystemToast(const char *text) {
	dialog_instance_t dialog = 0;
	dialog_create_toast(&dialog);
	dialog_set_toast_message_text(dialog, text);
	dialog_set_toast_position(dialog, DIALOG_POSITION_TOP_CENTER);
	dialog_show(dialog);
}

void ShowAd(int x, int y, bool center_x) {
}

void ShowKeyboard() {
	virtualkeyboard_show();
}

void Vibrate(int length_ms) {
	vibration_request(VIBRATION_INTENSITY_LOW, 200 /* intensity (1-100), duration (ms) */);
}

void LaunchBrowser(const char *url)
{
	char* error;
	navigator_invoke(url, &error);
}

void LaunchMarket(const char *url)
{
	char* error;
	navigator_invoke(url, &error);
}

void LaunchEmail(const char *email_address)
{
	char* error;
	navigator_invoke((std::string("mailto:") + email_address).c_str(), &error);
}

InputState input_state;

// Input
const unsigned int buttonMappings[18] = {
	KEYCODE_K,          //Cross
	KEYCODE_L,          //Circle
	KEYCODE_J,          //Square
	KEYCODE_I,          //Triangle
	KEYCODE_Q,          //LBUMPER
	KEYCODE_P,          //RBUMPER
	KEYCODE_SPACE,      //START
	KEYCODE_ZERO,       //SELECT
	KEYCODE_W,          //UP
	KEYCODE_S,          //DOWN
	KEYCODE_A,          //LEFT
	KEYCODE_D,          //RIGHT
	0,                  //MENU (SwipeDown)
	KEYCODE_BACKSPACE,  //BACK
	KEYCODE_W,          //JOY UP
	KEYCODE_S,          //JOY DOWN
	KEYCODE_A,          //JOY LEFT
	KEYCODE_D,          //JOY RIGHT
};

void SimulateGamepad(InputState *input) {
	input->pad_lstick_x = 0;
	input->pad_lstick_y = 0;
	input->pad_rstick_x = 0;
	input->pad_rstick_y = 0;

	if (input->pad_buttons & PAD_BUTTON_JOY_UP)
		input->pad_lstick_y=1;
	else if (input->pad_buttons & PAD_BUTTON_JOY_DOWN)
		input->pad_lstick_y=-1;
	if (input->pad_buttons & PAD_BUTTON_JOY_LEFT)
		input->pad_lstick_x=-1;
	else if (input->pad_buttons & PAD_BUTTON_JOY_RIGHT)
		input->pad_lstick_x=1;
}

void BlackberryMain::handleInput(screen_event_t screen_event)
{
	int val, buttons, pointerId;
	int pair[2];
	screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_TYPE, &val);
	screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_SOURCE_POSITION, pair);
	screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_TOUCH_ID, &pointerId);

	input_state.mouse_valid = true;
	switch(val)
	{
	// Touchscreen
	case SCREEN_EVENT_MTOUCH_TOUCH:
	case SCREEN_EVENT_MTOUCH_RELEASE: 	// Up, down
		input_state.pointer_down[pointerId] = (val == SCREEN_EVENT_MTOUCH_TOUCH);
	case SCREEN_EVENT_MTOUCH_MOVE:
		input_state.pointer_x[pointerId] = pair[0] * dpi_scale;
		input_state.pointer_y[pointerId] = pair[1] * dpi_scale;
		break;
	// Mouse, Simulator
    case SCREEN_EVENT_POINTER:
		screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_BUTTONS,
			&buttons);
		if (buttons == SCREEN_LEFT_MOUSE_BUTTON) { 			// Down
			input_state.pointer_x[pointerId] = pair[0] * dpi_scale;
			input_state.pointer_y[pointerId] = pair[1] * dpi_scale;
			input_state.pointer_down[pointerId] = true;
		} else if (input_state.pointer_down[pointerId]) {	// Up
			input_state.pointer_x[pointerId] = pair[0] * dpi_scale;
			input_state.pointer_y[pointerId] = pair[1] * dpi_scale;
			input_state.pointer_down[pointerId] = false;
		}
		break;
	// Keyboard
	case SCREEN_EVENT_KEYBOARD:
		int flags, value;
		screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_KEY_FLAGS, &flags);
		screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_KEY_SYM, &value);
		for (int b = 0; b < 14; b++) {
			if (value == buttonMappings[b] & 0xFF) {
				if (flags & KEY_DOWN)
					pad_buttons |= (1<<b);
				else
					pad_buttons &= ~(1<<b);
			}
		}
		break;
	// Gamepad
	case SCREEN_EVENT_GAMEPAD:
	case SCREEN_EVENT_JOYSTICK:
		char device_id[16];
#define DIR_KEYS SCREEN_DPAD_UP_GAME_BUTTON | SCREEN_DPAD_DOWN_GAME_BUTTON | SCREEN_DPAD_LEFT_GAME_BUTTON | SCREEN_DPAD_RIGHT_GAME_BUTTON
		screen_device_t device;
		screen_get_event_property_pv(screen_event, SCREEN_PROPERTY_DEVICE, (void**)&device);
		screen_get_device_property_cv(device, SCREEN_PROPERTY_ID_STRING, sizeof(device_id), device_id);
		screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_BUTTONS, &buttons);
		// Map the buttons integer to our mappings
		if (strstr(device_id, "057E-0306")) // Wiimote
			controller_buttons = (buttons & (SCREEN_A_GAME_BUTTON | SCREEN_B_GAME_BUTTON)) << 2 |
			                     (buttons & (SCREEN_X_GAME_BUTTON | SCREEN_Y_GAME_BUTTON)) >> 3;
		else
			controller_buttons = (buttons & (SCREEN_A_GAME_BUTTON | SCREEN_B_GAME_BUTTON)) |
			                     (buttons & (SCREEN_X_GAME_BUTTON | SCREEN_Y_GAME_BUTTON)) >> 1;
		controller_buttons |= (buttons & (SCREEN_MENU1_GAME_BUTTON | SCREEN_MENU2_GAME_BUTTON)) |
		                      (buttons & SCREEN_L1_GAME_BUTTON) >> 6 | (buttons & SCREEN_R1_GAME_BUTTON) >> 8 |
		                      (buttons & DIR_KEYS) >> 8 |
		                      (buttons & DIR_KEYS) >> 2;
		break;
	case SCREEN_EVENT_DISPLAY:
		screen_display_t new_dpy = NULL;
		screen_get_event_property_pv(screen_event, SCREEN_PROPERTY_DISPLAY, (void **)&new_dpy);
		for (int i = 0; i < ndisplays; i++) {
			if (new_dpy != screen_dpy[i])
				continue;
			int active = 0;
			screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_ATTACHED, &active);
			if (active) {
				int size[2];
				screen_get_display_property_iv(screen_dpy[i], SCREEN_PROPERTY_SIZE, size);
				if (size[0] == 0 || size[1] == 0)
					active = 0;
			}
			if (active && !displays[i].attached)
				realiseDisplay(i);
			else if (!active && displays[i].attached && displays[i].realised)
				unrealiseDisplay(i);
			displays[i].attached = active;
		}
		break;
	}
}

void BlackberryMain::startMain(int argc, char *argv[]) {
	// Receive events from window manager
	screen_create_context(&screen_cxt, 0);
	// Initialise Blackberry Platform Services
	bps_initialize();
	// TODO: Enable/disable based on setting
	sensor_set_rate(SENSOR_TYPE_ACCELEROMETER, 25000);
	sensor_request_events(SENSOR_TYPE_ACCELEROMETER);

	net::Init();
	startDisplays();
	NativeInit(argc, (const char **)argv, "/accounts/1000/shared/misc/", "/accounts/1000/shared/misc/", "BADCOFFEE");
	NativeInitGraphics();
	screen_request_events(screen_cxt);
	navigator_request_events(0);
	dialog_request_events(0);
	vibration_request_events(0);
	audio = new BlackberryAudio();
	runMain();
}

void BlackberryMain::runMain() {
	bool running = true;
	while (running) {
		input_state.mouse_valid = false;
		input_state.accelerometer_valid = false;
		SimulateGamepad(&input_state);
		while (true) {
			// Handle Blackberry events
			bps_event_t *event = NULL;
			bps_get_event(&event, 0);
			if (event == NULL)
				break; // Ran out of events
			int domain = bps_event_get_domain(event);
			if (domain == screen_get_domain()) {
				handleInput(screen_event_get_event(event));
			} else if (domain == navigator_get_domain()) {
				switch(bps_event_get_code(event))
				{
				case NAVIGATOR_BACK:
				case NAVIGATOR_SWIPE_DOWN:
					pad_buttons |= PAD_BUTTON_MENU;
					break;
				case NAVIGATOR_EXIT:
					return;
				}
			} else if (domain == sensor_get_domain()) {
				if (SENSOR_ACCELEROMETER_READING == bps_event_get_code(event)) {
					float x, y, z;
					sensor_event_get_xyz(event, &x, &y, &z);
					if (pixel_xres == 1024 || pixel_xres == 720) // Q10 has this negative and reversed
					{
						input_state.acc.x = -y;
						input_state.acc.y = -x;
					} else {
						input_state.acc.x = x;
						input_state.acc.y = y;
					}
					input_state.acc.z = z;
				}
			}
		}
		input_state.pad_buttons = pad_buttons | controller_buttons;
		pad_buttons &= ~PAD_BUTTON_MENU;
		UpdateInputState(&input_state);
		NativeUpdate(input_state);
		EndInputState(&input_state);
		// Work in Progress
		// Currently: Render to HDMI port (eg. 1080p) when in game. Render to device when in menu.
		// Idea: Render to all displays. Controls go to internal, game goes to external(s).
		if (globalUIState == UISTATE_INGAME && !emulating)
		{
			emulating = true;
			switchDisplay(screen_emu);
		} else if (globalUIState != UISTATE_INGAME && emulating) {
			emulating = false;
			switchDisplay(screen_ui);
		}
		NativeRender();
		time_update();
		// This handles VSync
		if (emulating)
			eglSwapBuffers(egl_disp[screen_emu], egl_surf[screen_emu]);
		else
			eglSwapBuffers(egl_disp[screen_ui], egl_surf[screen_ui]);
	}
}

void BlackberryMain::endMain() {
	screen_stop_events(screen_cxt);
	bps_shutdown();
	NativeShutdownGraphics();
	delete audio;
	NativeShutdown();
	killDisplays();
	net::Shutdown();
	screen_destroy_context(screen_cxt);
}

// Entry Point
int main(int argc, char *argv[]) {
	delete new BlackberryMain(argc, argv);
	return 0;
}
