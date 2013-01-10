/*
 * Copyright (c) 2012 Sacha Refshauge
 *
 */
// Blackberry implementation of the framework.

#include <pwd.h>
#include <unistd.h>
#include <string>

#include <sys/asoundlib.h>
#include <sys/asound.h>

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

// Input
const int buttonMappings[18] = {
	KEYCODE_X,          //A
	KEYCODE_S,          //B
	KEYCODE_Z,          //X
	KEYCODE_A,          //Y
	KEYCODE_W,          //LBUMPER
	KEYCODE_Q,          //RBUMPER
	KEYCODE_ONE,        //START
	KEYCODE_TWO,        //SELECT
	KEYCODE_UP,         //UP
	KEYCODE_DOWN,       //DOWN
	KEYCODE_LEFT,       //LEFT
	KEYCODE_RIGHT,      //RIGHT
	0,                  //MENU (SwipeDown)
	KEYCODE_BACKSPACE,  //BACK
	KEYCODE_I,          //JOY UP
	KEYCODE_K,          //JOY DOWN
	KEYCODE_J,          //JOY LEFT
	KEYCODE_L,          //JOY RIGHT
};

void SimulateGamepad(InputState *input) {
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
	screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_BUFFER_SIZE, buffer_size);
	screen_set_window_property_iv(screen_win, SCREEN_PROPERTY_ROTATION, &angle);

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
#define AUDIO_CHANNELS 2
#define AUDIO_FREQ 44100
#define AUDIO_SAMPLES 1024
class BlackberryAudio
{
public:
	BlackberryAudio()
	{
		paused = false;
		OpenAudio();
	}
	~BlackberryAudio()
	{
		pthread_cancel(thread_handle);
		snd_pcm_plugin_flush(pcm_handle, SND_PCM_CHANNEL_PLAYBACK);
		snd_mixer_close(mixer_handle);
		snd_pcm_close(pcm_handle);
		free(mixer_handle);
		free(pcm_handle);
	}
	void setPaused(bool pause)
	{
		paused = pause;
	}
	static void* staticThreadProc(void* arg)
	{
		return reinterpret_cast<BlackberryAudio*>(arg)->RunAudio();
	}
private:
	void OpenAudio()
	{
		int card = -1, dev = 0;

		snd_pcm_channel_info_t pi;
		snd_mixer_group_t group;
		snd_pcm_channel_params_t pp;
		snd_pcm_channel_setup_t setup;

		mixlen = 2*AUDIO_CHANNELS*AUDIO_SAMPLES;
		mixbuf = (uint8_t*)malloc(mixlen);
		if (mixbuf == NULL)
			return;
		memset(mixbuf, 0, mixlen);

		if ((snd_pcm_open_preferred(&pcm_handle, &card, &dev, SND_PCM_OPEN_PLAYBACK)) < 0)
			return;

		memset(&pi, 0, sizeof (pi));
		pi.channel = SND_PCM_CHANNEL_PLAYBACK;
		if ((snd_pcm_plugin_info (pcm_handle, &pi)) < 0)
			return;

		memset(&pp, 0, sizeof (pp));
		pp.mode = SND_PCM_MODE_BLOCK;
		pp.channel = SND_PCM_CHANNEL_PLAYBACK;
		pp.start_mode = SND_PCM_START_FULL;
		pp.stop_mode = SND_PCM_STOP_STOP;

		pp.buf.block.frag_size = pi.max_fragment_size;
		pp.buf.block.frags_max = -1;
		pp.buf.block.frags_min = 1;

		pp.format.interleave = 1;
		pp.format.rate = AUDIO_FREQ;
		pp.format.voices = AUDIO_CHANNELS;
		pp.format.format = SND_PCM_SFMT_S16_LE;

		if ((snd_pcm_plugin_params (pcm_handle, &pp)) < 0)
			return;

		snd_pcm_plugin_prepare(pcm_handle, SND_PCM_CHANNEL_PLAYBACK);

		memset (&setup, 0, sizeof(setup));
		memset (&group, 0, sizeof(group));
		setup.channel = SND_PCM_CHANNEL_PLAYBACK;
		setup.mixer_gid = &group.gid;
		if ((snd_pcm_plugin_setup (pcm_handle, &setup)) < 0)
			return;
		setup.buf.block.frag_size;
		if ((snd_mixer_open(&mixer_handle, card, setup.mixer_device)) < 0)
			return;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		pthread_create(&thread_handle, &attr, &BlackberryAudio::staticThreadProc, this);
	}
	void* RunAudio()
	{
		while(true)
		{
			if (!paused)
			{
				memset(mixbuf, 0, mixlen);
				NativeMix((short *)mixbuf, mixlen / 4);

				fd_set rfds, wfds;
				int nflds;
				if (FD_ISSET(snd_pcm_file_descriptor(pcm_handle, SND_PCM_CHANNEL_PLAYBACK), &wfds))
				{
					snd_pcm_plugin_write(pcm_handle, mixbuf, mixlen);
				}
				FD_ZERO(&rfds);
				FD_ZERO(&wfds);

				FD_SET(snd_mixer_file_descriptor(mixer_handle), &rfds);
				FD_SET(snd_pcm_file_descriptor(pcm_handle, SND_PCM_CHANNEL_PLAYBACK), &wfds);
				nflds = std::max(snd_mixer_file_descriptor(mixer_handle), snd_pcm_file_descriptor(pcm_handle, SND_PCM_CHANNEL_PLAYBACK));
				select (nflds+1, &rfds, &wfds, NULL, NULL);
			}
			else
				delay((AUDIO_SAMPLES*1000)/AUDIO_FREQ);
		}
	}
	snd_pcm_t* pcm_handle;
	snd_mixer_t* mixer_handle;
	int mixlen;
	uint8_t* mixbuf;
	pthread_t thread_handle;
	bool paused;
};

// Entry Point
int main(int argc, char *argv[]) {
	static screen_context_t screen_cxt;
	// Receive events from window manager
	screen_create_context(&screen_cxt, 0);
	//Initialise Blackberry Platform Services
	bps_initialize();

	net::Init();
	init_GLES2(screen_cxt);
#ifdef BLACKBERRY10
	// Dev Alpha: 1280x768, 4.2", 356DPI, 0.6f scale
	int dpi;
	screen_get_display_property_iv(screen_disp, SCREEN_PROPERTY_DPI, &dpi);
#else
	// Playbook: 1024x600, 7", 170DPI, 1.25f scale
	int screen_phys_size[2];
	screen_get_display_property_iv(screen_disp, SCREEN_PROPERTY_PHYSICAL_SIZE, screen_phys_size);
	int screen_resolution[2];
	screen_get_display_property_iv(screen_disp, SCREEN_PROPERTY_SIZE, screen_resolution);
	double diagonal_pixels = sqrt(screen_resolution[0] * screen_resolution[0] + screen_resolution[1] * screen_resolution[1]);
	double diagonal_inches = 0.0393700787 * sqrt(screen_phys_size[0] * screen_phys_size[0] + screen_phys_size[1] * screen_phys_size[1]);
	int dpi = (int)(diagonal_pixels / diagonal_inches + 0.5);
#endif
	float dpi_scale = 213.6f / dpi;
	dp_xres = (int)(pixel_xres * dpi_scale); dp_yres = (int)(pixel_yres * dpi_scale);

	NativeInit(argc, (const char **)argv, "data/", "/accounts/1000/shared", "BADCOFFEE");
	NativeInitGraphics();
	screen_request_events(screen_cxt);
	navigator_request_events(0);
	dialog_request_events(0);
#ifdef BLACKBERRY10
	vibration_request_events(0);
#endif
	BlackberryAudio* audio = new BlackberryAudio();
	InputState input_state;
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
					else {
						for (int b = 0; b < 14; b++) {
							if (value == buttonMappings[b])
								input_state.pad_buttons &= ~(1<<b);
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
