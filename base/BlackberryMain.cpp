/*
 * Copyright (c) 2012 Sacha Refshauge
 *
 */
// Blackberry implementation of the framework.

#include <pwd.h>
#include <unistd.h>
#include <string>

// TODO: Replace SDL with asound
#include "SDL/SDL.h"
#include "SDL/SDL_audio.h"

#include <EGL/egl.h>
#include <screen/screen.h>
#include <sys/platform.h>
#include <GLES2/gl2.h>

#include "base/display.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "gfx_es2/glsl_program.h"
#include "file/zip_read.h"
#include "input/input_state.h"
#include "base/NativeApp.h"
#include "net/resolve.h"
#include "display.h"

// Blackberry specific
#include <bps/bps.h>			// Blackberry Platform Services
#include <bps/screen.h>			// Blackberry Window Manager
#include <bps/navigator.h>		// Invoke Service
#include <bps/virtualkeyboard.h>// Keyboard Service
#include <sys/keycodes.h>
#include <bps/dialog.h> 		// Dialog Service (Toast=BB10)
#ifdef BLACKBERRY10
#include <bps/vibration.h>		// Vibrate Service (BB10)
#endif

EGLDisplay egl_disp;
EGLSurface egl_surf;

static EGLConfig egl_conf;
static EGLContext egl_ctx;

static screen_context_t screen_ctx;
static screen_window_t screen_win;
static screen_display_t screen_disp;

// Simple implementations of System functions

void SystemToast(const char *text) {
#ifdef BLACKBERRY10
	dialog_instance_t dialog = 0;
	dialog_create_toast(&dialog);
	dialog_set_toast_message_text(dialog, text);
	dialog_set_toast_position(dialog, DIALOG_POSITION_TOP_CENTER);
	dialog_show(dialog);
#else
	puts(text);
#endif
}

void ShowAd(int x, int y, bool center_x) {
	// Ads on Blackberry?
}

void ShowKeyboard() {
	virtualkeyboard_show();
}

void Vibrate(int length_ms) {
#ifdef BLACKBERRY10
	vibration_request(VIBRATION_INTENSITY_LOW, 500 /* intensity (1-100), duration (ms) */);
#endif
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

const int buttonMappings[18] = {
	KEYCODE_X,			//A
	KEYCODE_S,			//B
	KEYCODE_Z,			//X
	KEYCODE_A,			//Y
	KEYCODE_W,			//LBUMPER
	KEYCODE_Q,			//RBUMPER
	KEYCODE_ONE,		//START
	KEYCODE_TWO,		//SELECT
	KEYCODE_UP,			//UP
	KEYCODE_DOWN,		//DOWN
	KEYCODE_LEFT,		//LEFT
	KEYCODE_RIGHT,		//RIGHT
	NULL,				//MENU (SwipeDown)
	KEYCODE_BACKSPACE,	//BACK
	KEYCODE_I,			//JOY UP
	KEYCODE_K,			//JOY DOWN
	KEYCODE_J,			//JOY LEFT
	KEYCODE_L,			//JOY RIGHT
};

void SimulateGamepad(InputState *input) {
	input->pad_buttons  = 0;
	input->pad_lstick_x = 0;
	input->pad_lstick_y = 0;
	input->pad_rstick_x = 0;
	input->pad_rstick_y = 0;

	if (input->pad_buttons | (1<<14))
		input->pad_lstick_y=1;
	else if (input->pad_buttons | (1<<15))
		input->pad_lstick_y=-1;
	if (input->pad_buttons | (1<<16))
		input->pad_lstick_x=-1;
	else if (input->pad_buttons | (1<<17))
		input->pad_lstick_x=1;
	/*if (keys[SDLK_KP8])
		input->pad_rstick_y=1;
	else if (keys[SDLK_KP2])
		input->pad_rstick_y=-1;
	if (keys[SDLK_KP4])
		input->pad_rstick_x=-1;
	else if (keys[SDLK_KP6])
		input->pad_rstick_x=1;*/
}

int init_GLES2(screen_context_t ctx) {
	int usage = SCREEN_USAGE_DISPLAY | SCREEN_USAGE_OPENGL_ES2;
	int format = SCREEN_FORMAT_RGBA8888;
	int num_configs;
	const int intInterval = 0;

	EGLint attrib_list[]= { EGL_RED_SIZE,        8,
				EGL_GREEN_SIZE,      8,
				EGL_BLUE_SIZE,       8,
				EGL_ALPHA_SIZE,      8,
				EGL_DEPTH_SIZE,	     24,
				EGL_STENCIL_SIZE,    8,
				EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
				EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
				EGL_NONE};

	usage = SCREEN_USAGE_OPENGL_ES2 | SCREEN_USAGE_ROTATION;
	const EGLint attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	const EGLint egl_surfaceAttr[] = { EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE };

	screen_ctx = ctx;
	screen_create_window(&screen_win, screen_ctx);
	screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_FORMAT, &format);
	screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_USAGE, &usage);
	screen_get_window_property_pv(screen_win, SCREEN_PROPERTY_DISPLAY, (void **)&screen_disp);

	// This must be landscape.
	int screen_resolution[2];
	screen_get_display_property_iv(screen_disp, SCREEN_PROPERTY_SIZE, screen_resolution);
	int angle = atoi(getenv("ORIENTATION"));
	pixel_xres = screen_resolution[0]; pixel_yres = screen_resolution[1];

	screen_display_mode_t screen_mode;
	screen_get_display_property_pv(screen_disp, SCREEN_PROPERTY_MODE, (void**)&screen_mode);

	int size[2];
	screen_get_window_property_iv(screen_win, SCREEN_PROPERTY_BUFFER_SIZE, size);

	int buffer_size[2] = {size[0], size[1]};

	if ((angle == 0) || (angle == 180)) {
		if (((screen_mode.width > screen_mode.height) && (size[0] < size[1])) ||
		((screen_mode.width < screen_mode.height) && (size[0] > size[1]))) {
			buffer_size[1] = size[0];
			buffer_size[0] = size[1];
			pixel_yres = screen_resolution[0];
			pixel_xres = screen_resolution[1];
		}
	} else if ((angle == 90) || (angle == 270)){
		if (((screen_mode.width > screen_mode.height) && (size[0] > size[1])) ||
		((screen_mode.width < screen_mode.height && size[0] < size[1]))) {
			buffer_size[1] = size[0];
			buffer_size[0] = size[1];
			pixel_yres = screen_resolution[0];
			pixel_xres = screen_resolution[1];
		}
	}
	screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_SWAP_INTERVAL, &intInterval);
	screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_BUFFER_SIZE, buffer_size);
	screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_ROTATION, &angle);

	screen_create_window_buffers(screen_win, 2); // Double buffered
	egl_disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(egl_disp, NULL, NULL);

	if (eglChooseConfig(egl_disp, attrib_list, &egl_conf, 1, &num_configs) != EGL_TRUE || egl_conf == 0)
	{
		printf("Configs weren't set!\n");
	}
	egl_ctx = eglCreateContext(egl_disp, egl_conf, EGL_NO_CONTEXT, attributes);

	egl_surf = eglCreateWindowSurface(egl_disp, egl_conf, screen_win, egl_surfaceAttr);

	eglMakeCurrent(egl_disp, egl_surf, egl_surf, egl_ctx);
	eglSwapInterval(egl_disp, 1);

	return 0;
}

void kill_GLES2() {
	if (egl_disp != EGL_NO_DISPLAY) {
		eglMakeCurrent(egl_disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (egl_surf != EGL_NO_SURFACE) {
			eglDestroySurface(egl_disp, egl_surf);
			egl_surf = EGL_NO_SURFACE;
		}
		if (egl_ctx != EGL_NO_CONTEXT) {
			eglDestroyContext(egl_disp, egl_ctx);
			egl_ctx = EGL_NO_CONTEXT;
		}
		if (screen_win != NULL) {
			screen_destroy_window(screen_win);
			screen_win = NULL;
		}
		eglTerminate(egl_disp);
		egl_disp = EGL_NO_DISPLAY;
	}
	eglReleaseThread();
}

extern void mixaudio(void *userdata, Uint8 *stream, int len) {
	NativeMix((short *)stream, len / 4);
}

int main(int argc, char *argv[]) {
	static screen_context_t screen_cxt;
	// Receive events from window manager
	screen_create_context(&screen_cxt, 0);
	//Initialise Blackberry Platform Services
	bps_initialize();

	net::Init();
	if (SDL_Init(SDL_INIT_AUDIO) < 0) {
		fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
		return 1;
	}
	init_GLES2(screen_cxt);
	// Playbook: 1024x600@7", Dev Alpha: 1280x768@4.2"
#ifdef BLACKBERRY10
	int dpi;
	screen_get_display_property_iv(screen_disp, SCREEN_PROPERTY_DPI, &dpi);
	float dpi_scale = 222.5f / dpi;
#else
	float dpi_scale = 1.1f;
#endif
	dp_xres = (int)(pixel_xres * dpi_scale); dp_yres = (int)(pixel_yres * dpi_scale);

	NativeInit(argc, (const char **)argv, "data/", "/accounts/1000/shared", "BADCOFFEE");
	NativeInitGraphics();
	screen_request_events(screen_cxt);
	navigator_request_events(0);
	// Yes, we only want landscape.
	navigator_rotation_lock(false);
	dialog_request_events(0);
#ifdef BLACKBERRY10
	vibration_request_events(0);
#endif

	SDL_AudioSpec fmt;
	fmt.freq = 44100;
	fmt.format = AUDIO_S16;
	fmt.channels = 2;
	fmt.samples = 1024;
	fmt.callback = &mixaudio;
	fmt.userdata = (void *)0;

	if (SDL_OpenAudio(&fmt, NULL) < 0) {
		ELOG("Failed to open audio: %s", SDL_GetError());
		return 1;
	}

	// Audio must be unpaused _after_ NativeInit()
	SDL_PauseAudio(0);

	InputState input_state;
	int framecount = 0;
	float t = 0;
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
				int screen_val, buttons, pointerId;
				int pair[2];

				screen_event_t screen_event = screen_event_get_event(event);

				screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_TYPE, &screen_val);
				screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_SOURCE_POSITION, pair);
				screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_TOUCH_ID, &pointerId);

				input_state.mouse_valid = true;
				switch(screen_val)
				{
				// Touchscreen
				case SCREEN_EVENT_MTOUCH_TOUCH:
				case SCREEN_EVENT_MTOUCH_RELEASE: 	// Up, down
					input_state.pointer_down[pointerId] = (screen_val == SCREEN_EVENT_MTOUCH_TOUCH);
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
					if (flags & (KEY_DOWN | KEY_SYM_VALID)) {
						for (int b = 0; b < 14; b++) {
							if (value == buttonMappings[b])
								input_state.pad_buttons |= (1<<b);
						}
					}
					break;
				}
			} else if (domain == navigator_get_domain()) {
				switch(bps_event_get_code(event))
				{
				case NAVIGATOR_BACK:
				case NAVIGATOR_SWIPE_DOWN:
					input_state.pad_buttons |= PAD_BUTTON_MENU;
					break;
				case NAVIGATOR_EXIT:
					running = false;
					break;
				}
			}
		}

		UpdateInputState(&input_state);
		NativeUpdate(input_state);
		EndInputState(&input_state);
		NativeRender();

		eglSwapBuffers(egl_disp, egl_surf);

		// Simple frame rate limiting
		while (time_now() < t + 1.0f/60.0f) {
			sleep_ms(0);
			time_update();
		}
		time_update();
		t = time_now();
		framecount++;
	}

	screen_stop_events(screen_cxt);
	bps_shutdown();

	NativeShutdownGraphics();
	SDL_PauseAudio(1);
	NativeShutdown();
	SDL_CloseAudio();
	SDL_Quit();
	kill_GLES2();
	net::Shutdown();
	screen_destroy_context(screen_cxt);
	exit(0);
	return 0;
}
