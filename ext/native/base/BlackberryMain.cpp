/*
 * Copyright (c) 2013 Sacha Refshauge
 *
 */
// Blackberry implementation of the framework.

#include <pwd.h>
#include <unistd.h>
#include <string>
#include <string.h>

#include <bps/locale.h>           // Get locale
#include <bps/navigator_invoke.h> // Receive invocation messages
#include "BlackberryMain.h"
#include "base/NKCodeFromBlackberry.h"
#include "gfx_es2/gpu_features.h"
#include "thin3d/thin3d.h"

#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Core.h"
#include "UI/MiscScreens.h"
#include "Common/GraphicsContext.h"

static GraphicsContext *graphicsContext;

class GLDummyGraphicsContext : public DummyGraphicsContext {
public:
	Thin3DContext *CreateThin3DContext() override {
		CheckGLExtensions();
		return T3DCreateGLContext();
	}
};

static bool g_quitRequested = false;

// Simple implementations of System functions

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME: {
		std::string name = "Blackberry:";
#ifdef ARM
		return name + ((pixel_xres != pixel_yres) ? "Touch" : "QWERTY");
#else
		return name + "Simulator";
#endif
	}
	case SYSPROP_LANGREGION: {
		char *locale = 0;
		locale_get_locale(&locale);
		return std::string(locale);
	}
	default:
		return "";
	}
}

int System_GetPropertyInt(SystemProperty prop) {
  switch (prop) {
  case SYSPROP_AUDIO_SAMPLE_RATE:
    return 44100;
  case SYSPROP_DISPLAY_REFRESH_RATE:
    return 60000;
  case SYSPROP_DEVICE_TYPE:
    return DEVICE_TYPE_MOBILE;
  default:
    return -1;
  }
}

void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "finish")) {
		g_quitRequested = true;
	}
}

void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

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
	// Vibration: intensity strength(1-100), duration ms(0-5000)
	// Intensity: LOW = 1, MEDIUM = 10, HIGH = 100
	switch (length_ms) {
	case -1: // Keyboard Tap
		vibration_request(VIBRATION_INTENSITY_LOW, 50);
		break;
	case -2: // Virtual Key
		vibration_request(VIBRATION_INTENSITY_LOW, 25);
		break;
	case -3: // Long Press
		vibration_request(VIBRATION_INTENSITY_LOW, 50);
		break;
	default:
		vibration_request(VIBRATION_INTENSITY_LOW, length_ms);
		break;
	}
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

void BlackberryMain::handleInput(screen_event_t screen_event)
{
	TouchInput input;
	KeyInput key;
	int val, buttons, pointerId;
	int pair[2];
	screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_TYPE, &val);
	screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_SOURCE_POSITION, pair);

	input_state.mouse_valid = true;
	switch(val)
	{
	// Touchscreen
	case SCREEN_EVENT_MTOUCH_TOUCH:
	case SCREEN_EVENT_MTOUCH_RELEASE: 	// Up, down
		screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_TOUCH_ID, &pointerId);
		input_state.pointer_down[pointerId] = (val == SCREEN_EVENT_MTOUCH_TOUCH);
		input_state.pointer_x[pointerId] = pair[0] * g_dpi_scale;
		input_state.pointer_y[pointerId] = pair[1] * g_dpi_scale;

		input.x = pair[0] * g_dpi_scale;
		input.y = pair[1] * g_dpi_scale;
		input.flags = (val == SCREEN_EVENT_MTOUCH_TOUCH) ? TOUCH_DOWN : TOUCH_UP;
		input.id = pointerId;
		NativeTouch(input);
		break;
	case SCREEN_EVENT_MTOUCH_MOVE:
		screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_TOUCH_ID, &pointerId);
		input_state.pointer_x[pointerId] = pair[0] * g_dpi_scale;
		input_state.pointer_y[pointerId] = pair[1] * g_dpi_scale;

		input.x = pair[0] * g_dpi_scale;
		input.y = pair[1] * g_dpi_scale;
		input.flags = TOUCH_MOVE;
		input.id = pointerId;
		NativeTouch(input);
		break;
	// Mouse, Simulator
	case SCREEN_EVENT_POINTER:
		screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_BUTTONS, &buttons);
		if (buttons == SCREEN_LEFT_MOUSE_BUTTON) { // Down
			input_state.pointer_x[0] = pair[0] * g_dpi_scale;
			input_state.pointer_y[0] = pair[1] * g_dpi_scale;
			input_state.pointer_down[0] = true;

			input.x = pair[0] * g_dpi_scale;
			input.y = pair[1] * g_dpi_scale;
			input.flags = TOUCH_DOWN;
			input.id = 0;
			NativeTouch(input);
		} else if (input_state.pointer_down[0]) {	// Up
			input_state.pointer_x[0] = pair[0] * g_dpi_scale;
			input_state.pointer_y[0] = pair[1] * g_dpi_scale;
			input_state.pointer_down[0] = false;

			input.x = pair[0] * g_dpi_scale;
			input.y = pair[1] * g_dpi_scale;
			input.flags = TOUCH_UP;
			input.id = 0;
			NativeTouch(input);
		}
		break;
	// Keyboard
	case SCREEN_EVENT_KEYBOARD:
		int flags, value;
		screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_KEY_FLAGS, &flags);
		screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_KEY_SYM, &value);
		NativeKey(KeyInput(DEVICE_ID_KEYBOARD, KeyMapRawBlackberrytoNative.find(value)->second, (flags & KEY_DOWN) ? KEY_DOWN : KEY_UP));
		break;
	// Gamepad
	case SCREEN_EVENT_GAMEPAD:
	case SCREEN_EVENT_JOYSTICK:
		int analog0[3];
		screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_BUTTONS, &buttons);
		for (int i = 0; i < 32; i++) {
			int mask = 1 << i;
			if ((old_buttons & mask) != (buttons & mask))
				NativeKey(KeyInput(DEVICE_ID_PAD_0, KeyMapPadBlackberrytoNative.find(mask)->second, (buttons & mask) ? KEY_DOWN : KEY_UP));
		}
		if (!screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_ANALOG0, analog0)) {
			for (int i = 0; i < 2; i++) {
				AxisInput axis;
				axis.axisId = JOYSTICK_AXIS_X + i;
				// 1.2 to try to approximate the PSP's clamped rectangular range.
				axis.value = 1.2 * analog0[i] / 128.0f;
				if (axis.value > 1.0f) axis.value = 1.0f;
				if (axis.value < -1.0f) axis.value = -1.0f;
				axis.deviceId = DEVICE_ID_PAD_0;
				axis.flags = 0;
				NativeAxis(axis);
			}
		}
		old_buttons = buttons;
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
	g_quitRequested = false;
	// Receive events from window manager
	screen_create_context(&screen_cxt, 0);
	// Initialise Blackberry Platform Services
	bps_initialize();
	// TODO: Enable/disable based on setting
	sensor_set_rate(SENSOR_TYPE_ACCELEROMETER, 25000);
	sensor_request_events(SENSOR_TYPE_ACCELEROMETER);

	net::Init();
	startDisplays();
	screen_request_events(screen_cxt);
	navigator_request_events(0);
	dialog_request_events(0);
	vibration_request_events(0);
	NativeInit(argc, (const char **)argv, "/accounts/1000/shared/misc/", "app/native/assets/", nullptr);
	graphicsContext = new GLDummyGraphicsContext();
	NativeInitGraphics(graphicsContext);
	audio = new BlackberryAudio();
	runMain();
}

void BlackberryMain::runMain() {
	bool running = true;
	while (running && !g_quitRequested) {
		input_state.mouse_valid = false;
		input_state.accelerometer_valid = false;
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
				case NAVIGATOR_INVOKE_TARGET:
					{
						const navigator_invoke_invocation_t *invoke = navigator_invoke_event_get_invocation(event);
						if(invoke) {
							boot_filename = navigator_invoke_invocation_get_uri(invoke)+7; // Remove file://
						}
					}
					break;
				case NAVIGATOR_ORIENTATION:
					sensor_remap_coordinates(navigator_event_get_orientation_angle(event));
					break;
				case NAVIGATOR_WINDOW_STATE:
					Core_NotifyWindowHidden(navigator_event_get_window_state(event) != NAVIGATOR_WINDOW_FULLSCREEN);
					break;
				case NAVIGATOR_BACK:
				case NAVIGATOR_SWIPE_DOWN:
					NativeKey(KeyInput(DEVICE_ID_KEYBOARD, NKCODE_BACK, KEY_DOWN));
					break;
				case NAVIGATOR_EXIT:
					return;
				}
			} else if (domain == sensor_get_domain()) {
				if (SENSOR_ACCELEROMETER_READING == bps_event_get_code(event)) {
					sensor_event_get_xyz(event, &(input_state.acc.y), &(input_state.acc.x), &(input_state.acc.z));
					AxisInput axis;
					axis.deviceId = DEVICE_ID_ACCELEROMETER;
					axis.flags = 0;

					axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_X;
					axis.value = input_state.acc.x;
					NativeAxis(axis);

					axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_Y;
					axis.value = input_state.acc.y;
					NativeAxis(axis);

					axis.axisId = JOYSTICK_AXIS_ACCELEROMETER_Z;
					axis.value = input_state.acc.z;
					NativeAxis(axis);
				}
			}
		}
		UpdateInputState(&input_state);
		// Work in Progress
		// Currently: Render to HDMI port (eg. 1080p) when in game. Render to device when in menu.
		// Idea: Render to all displays. Controls go to internal, game goes to external(s).
		if (GetUIState() == UISTATE_INGAME && !emulating) {
			emulating = true;
			switchDisplay(screen_emu);
			if (g_Config.iShowFPSCounter == 4) {
				int options = SCREEN_DEBUG_STATISTICS;
				screen_set_window_property_iv(screen_win[0], SCREEN_PROPERTY_DEBUG, &options);
			}
		} else if (GetUIState() != UISTATE_INGAME && emulating) {
			emulating = false;
			switchDisplay(screen_ui);
		}
		time_update();
		UpdateRunLoop(&input_state);
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
	graphicsContext->Shutdown();
	delete graphicsContext;
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
