/*
 * Copyright (c) 2012 Sacha Refshauge
 *
 */
// Blackberry implementation of the framework.

#include <pwd.h>
#include <unistd.h>
#include <string>

#include <AL/al.h>
#include <AL/alc.h>

#include <EGL/egl.h>
#include <screen/screen.h>
#include <sys/platform.h>
#include <GLES2/gl2.h>

#include "base/timeutil.h"
#include "gfx_es2/glsl_program.h"
#include "file/zip_read.h"
#include "input/input_state.h"
#include "base/NativeApp.h"
#include "net/resolve.h"
#include "display.h"

// Blackberry specific
#include <bps/bps.h>            // Blackberry Platform Services
#include <bps/screen.h>	        // Blackberry Window Manager
#include <bps/navigator.h>      // Invoke Service
#include <bps/virtualkeyboard.h>// Keyboard Service
#include <bps/sensor.h>         // Accelerometer
#include <sys/keycodes.h>
#include <bps/dialog.h>         // Dialog Service (Toast=BB10)
#include <bps/vibration.h>      // Vibrate Service (BB10)

EGLDisplay egl_disp;
EGLSurface egl_surf;

static EGLConfig egl_conf;
static EGLContext egl_ctx;

static screen_context_t screen_ctx;
static screen_window_t screen_win;
static screen_display_t screen_disp;

// Simple implementations of System functions

void SystemToast(const char *text) {
	dialog_instance_t dialog = 0;
	dialog_create_toast(&dialog);
	dialog_set_toast_message_text(dialog, text);
	dialog_set_toast_position(dialog, DIALOG_POSITION_TOP_CENTER);
	dialog_show(dialog);
}

void ShowAd(int x, int y, bool center_x) {
	// Ads on Blackberry?
}

void ShowKeyboard() {
	virtualkeyboard_show();
}

void Vibrate(int length_ms) {
	vibration_request(VIBRATION_INTENSITY_LOW, 500 /* intensity (1-100), duration (ms) */);
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

// Video
int init_GLES2(screen_context_t ctx) {
	int usage = SCREEN_USAGE_ROTATION | SCREEN_USAGE_OPENGL_ES2;
	int format = SCREEN_FORMAT_RGBX8888;
	int num_configs;

	EGLint attrib_list[]= {
				EGL_RED_SIZE,        8,
				EGL_GREEN_SIZE,      8,
				EGL_BLUE_SIZE,       8,
				EGL_DEPTH_SIZE,	     24,
				EGL_STENCIL_SIZE,    8,
				EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
				EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
				EGL_NONE};

	const EGLint attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	const EGLint egl_surfaceAttr[] = { EGL_RENDER_BUFFER, EGL_BACK_BUFFER, EGL_NONE };

	screen_ctx = ctx;
	screen_create_window(&screen_win, screen_ctx);
	screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_FORMAT, &format);
	screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_USAGE, &usage);
	screen_get_window_property_pv(screen_win, SCREEN_PROPERTY_DISPLAY, (void **)&screen_disp);

	pixel_xres = atoi(getenv("WIDTH")); pixel_yres = atoi(getenv("HEIGHT"));
	int size[2] = { pixel_xres, pixel_yres };
	screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_BUFFER_SIZE, size);

	screen_create_window_buffers(screen_win, 2); // Double buffered
	egl_disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(egl_disp, NULL, NULL);

	eglChooseConfig(egl_disp, attrib_list, &egl_conf, 1, &num_configs);
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

// Audio
#define SAMPLE_SIZE 44100
class BlackberryAudio
{
public:
	BlackberryAudio()
	{
		alcDevice = alcOpenDevice(NULL);
		if (alContext = alcCreateContext(alcDevice, NULL))
			alcMakeContextCurrent(alContext);
		alGenSources(1, &source);
		alGenBuffers(1, &buffer);
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_create(&thread_handle, &attr, &BlackberryAudio::staticThreadProc, this);
	}
	~BlackberryAudio()
	{
		pthread_cancel(thread_handle);
		alcMakeContextCurrent(NULL);
		if (alContext)
		{
			alcDestroyContext(alContext);
			alContext = NULL;
		}
		if (alcDevice)
		{
			alcCloseDevice(alcDevice);
			alcDevice = NULL;
		}
	}
	static void* staticThreadProc(void* arg)
	{
		return reinterpret_cast<BlackberryAudio*>(arg)->RunAudio();
	}
private:
	void* RunAudio()
	{
		while(true)
		{
			size_t frames_ready;
			alGetSourcei(source, AL_SOURCE_STATE, &state);
			if (state != AL_PLAYING)
				frames_ready = NativeMix(stream, SAMPLE_SIZE / 2);
			else
				frames_ready = 0;
			if (frames_ready > 0)
			{
				const size_t bytes_ready = frames_ready * sizeof(short) * 2;
				alSourcei(source, AL_BUFFER, 0);
				alBufferData(buffer, AL_FORMAT_STEREO16, stream, bytes_ready, SAMPLE_SIZE);
				alSourcei(source, AL_BUFFER, buffer);
				alSourcePlay(source);
				// TODO: Maybe this could get behind?
				usleep((1000000 * frames_ready) / SAMPLE_SIZE);
			}
			else
				usleep(100);
		}
	}
	ALCdevice *alcDevice;
	ALCcontext *alContext;
	ALenum state;
	ALuint buffer;
	ALuint source;
	short stream[SAMPLE_SIZE];
	pthread_t thread_handle;
};

// Entry Point
int main(int argc, char *argv[]) {
	static screen_context_t screen_cxt;
	// Receive events from window manager
	screen_create_context(&screen_cxt, 0);
	// Initialise Blackberry Platform Services
	bps_initialize();
	// TODO: Enable/disable based on setting
	sensor_set_rate(SENSOR_TYPE_ACCELEROMETER, 25000);
	sensor_request_events(SENSOR_TYPE_ACCELEROMETER);

	net::Init();
	init_GLES2(screen_cxt);
	// Z10: 1280x768, 4.2", 356DPI, 0.6f scale
	// Q10:  720x720, 3.1", 328DPI, 0.65f*1.4f=0.91f scale
	int dpi;
	screen_get_display_property_iv(screen_disp, SCREEN_PROPERTY_DPI, &dpi);
	float dpi_scale = 213.6f / dpi;
	if (pixel_xres == pixel_yres) dpi_scale *= 1.4;
	dp_xres = (int)(pixel_xres * dpi_scale); dp_yres = (int)(pixel_yres * dpi_scale);

	NativeInit(argc, (const char **)argv, "/accounts/1000/shared/misc/", "data/", "BADCOFFEE");
	NativeInitGraphics();
	screen_request_events(screen_cxt);
	navigator_request_events(0);
	dialog_request_events(0);
	vibration_request_events(0);
	static int pad_buttons = 0, controller_buttons = 0;
	BlackberryAudio* audio = new BlackberryAudio();
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
					int buttons;
					char device_id[16];
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
					                      (buttons & (SCREEN_DPAD_UP_GAME_BUTTON | SCREEN_DPAD_DOWN_GAME_BUTTON | SCREEN_DPAD_LEFT_GAME_BUTTON | SCREEN_DPAD_RIGHT_GAME_BUTTON)) >> 8 |
					                      (buttons & (SCREEN_DPAD_UP_GAME_BUTTON | SCREEN_DPAD_DOWN_GAME_BUTTON | SCREEN_DPAD_LEFT_GAME_BUTTON | SCREEN_DPAD_RIGHT_GAME_BUTTON)) >> 2;
					break;
				}
			} else if (domain == navigator_get_domain()) {
				switch(bps_event_get_code(event))
				{
				case NAVIGATOR_BACK:
				case NAVIGATOR_SWIPE_DOWN:
					pad_buttons |= PAD_BUTTON_MENU;
					break;
				case NAVIGATOR_EXIT:
					NativeShutdown();
					exit(0);
					break;
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
		NativeRender();
		time_update();
		// On Blackberry, this handles VSync for us
		eglSwapBuffers(egl_disp, egl_surf);
	}

	screen_stop_events(screen_cxt);
	bps_shutdown();

	NativeShutdownGraphics();
	delete audio;
	NativeShutdown();
	kill_GLES2();
	net::Shutdown();
	screen_destroy_context(screen_cxt);
	exit(0);
	return 0;
}
