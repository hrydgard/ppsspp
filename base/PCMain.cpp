// PC implementation of the framework.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <ShellAPI.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

#include <string>

#include "SDL/SDL.h"
#include "SDL/SDL_timer.h"
#include "SDL/SDL_audio.h"
#include "SDL/SDL_video.h"

#include "base/display.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "gfx_es2/glsl_program.h"
#include "file/zip_read.h"
#include "input/input_state.h"
#include "base/NativeApp.h"
#include "net/resolve.h"

// Simple implementations of System functions



void SystemToast(const char *text) {
#ifdef _WIN32
	MessageBox(0, text, "Toast!", MB_ICONINFORMATION);
#else
	puts(text);
#endif
}


void ShowAd(int x, int y, bool center_x) {
	// Ignore ads on PC
}

void ShowKeyboard() {
	// Irrelevant on PC
}

void Vibrate(int length_ms) {
	// Ignore on PC
}

void LaunchBrowser(const char *url)
{
#ifdef _WIN32
	ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
	ILOG("Would have gone to %s but LaunchBrowser is not implemented on this platform", url);
#endif
}

void LaunchMarket(const char *url)
{
#ifdef _WIN32
	ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
	ILOG("Would have gone to %s but LaunchMarket is not implemented on this platform", url);
#endif
}

void LaunchEmail(const char *email_address)
{
#ifdef _WIN32
	ShellExecute(NULL, "open", (std::string("mailto:") + email_address).c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
	ILOG("Would have opened your email client for %s but LaunchEmail is not implemented on this platform", email_address);
#endif
}



const int buttonMappings[14] = {
	SDLK_x,										//A
	SDLK_s,										//B
	SDLK_z,										//X
	SDLK_a,										//Y
	SDLK_w,										//LBUMPER
	SDLK_q,										//RBUMPER
	SDLK_1,										//START
	SDLK_2,										//SELECT
	SDLK_UP,									//UP
	SDLK_DOWN,								//DOWN
	SDLK_LEFT,								//LEFT
	SDLK_RIGHT,								//RIGHT
	SDLK_m,									 //MENU
	SDLK_BACKSPACE,					 //BACK
};

void SimulateGamepad(const uint8 *keys, InputState *input) {
	input->pad_buttons = 0;
	input->pad_lstick_x = 0;
	input->pad_lstick_y = 0;
	input->pad_rstick_x = 0;
	input->pad_rstick_y = 0;
	for (int b = 0; b < 14; b++) {
		if (keys[buttonMappings[b]])
			input->pad_buttons |= (1<<b);
	}

	if (keys[SDLK_i])
		input->pad_lstick_y=1;
	else if (keys[SDLK_k])
		input->pad_lstick_y=-1;
	if (keys[SDLK_j])
		input->pad_lstick_x=-1;
	else if (keys[SDLK_l])
		input->pad_lstick_x=1;
	if (keys[SDLK_KP8])
		input->pad_rstick_y=1;
	else if (keys[SDLK_KP2])
		input->pad_rstick_y=-1;
	if (keys[SDLK_KP4])
		input->pad_rstick_x=-1;
	else if (keys[SDLK_KP6])
		input->pad_rstick_x=1;
}

extern void mixaudio(void *userdata, Uint8 *stream, int len) {
	NativeMix((short *)stream, len / 4);
}

#ifdef _WIN32
#undef main
#endif
int main(int argc, char *argv[]) {
	/* // Xoom/Nexus 7 resolution. Other common tablet resolutions: 1024x600 , 1366x768
	dp_xres = 1280;
	dp_yres = 800;

	*/
	std::string app_name;
	std::string app_name_nice;

	float zoom = 1.0f;
	bool tablet = false;
	const char *zoomenv = getenv("ZOOM");
	const char *tabletenv = getenv("TABLET");
	if (zoomenv) {
		zoom = atof(zoomenv);
	}
	if (tabletenv) {
		tablet = (bool)atoi(tabletenv);
	}

	bool landscape;
	NativeGetAppInfo(&app_name, &app_name_nice, &landscape);

	if (landscape) {
		if (tablet) {
			pixel_xres = 1280 * zoom;
			pixel_yres = 800 * zoom;
		} else {
			pixel_xres = 800 * zoom;
			pixel_yres = 480 * zoom;
		}
	} else {
		// PC development hack for more space
		//pixel_xres = 1580 * zoom;
		//pixel_yres = 1000 * zoom;
		if (tablet) {
			pixel_xres = 800 * zoom;
			pixel_yres = 1280 * zoom;
		} else {
			pixel_xres = 480 * zoom;
			pixel_yres = 800 * zoom;
		}
	}

	net::Init();
#ifdef __APPLE__
	// Make sure to request a somewhat modern GL context at least - the
	// latest supported by MacOSX (really, really sad...)
	// Requires SDL 2.0 (which is even more sad, as that hasn't been released yet)
	//SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	//SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
#endif

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
		return 1;
	}

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, 1);

	if (SDL_SetVideoMode(pixel_xres, pixel_yres, 0, SDL_OPENGL) == NULL) {
		fprintf(stderr, "SDL SetVideoMode failed: Unable to create OpenGL screen: %s\n", SDL_GetError());
		SDL_Quit();
		return(2);
	}

	SDL_WM_SetCaption(app_name_nice.c_str(), NULL);

	if (GLEW_OK != glewInit()) {
		printf("Failed to initialize glew!\n");
		return 1;
	}

	if (GLEW_VERSION_2_0) {
		printf("OpenGL 2.0 or higher.\n");
	} else {
		printf("Sorry, this program requires OpenGL 2.0.\n");
		return 1;
	}

#ifdef _MSC_VER
	// VFSRegister("temp/", new DirectoryAssetReader("E:\\Temp\\"));
	TCHAR path[MAX_PATH];
	SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, path);
	PathAppend(path, (app_name + "\\").c_str());
#else
	// Mac / Linux
	char path[512];
	const char *the_path = getenv("HOME");
	if (!the_path) {
		struct passwd* pwd = getpwuid(getuid());
		if (pwd)
			the_path = pwd->pw_dir;
	}
	strcpy(path, the_path);
	if (path[strlen(path)-1] != '/')
		strcat(path, "/");
#endif

#ifdef _WIN32
	NativeInit(argc, (const char **)argv, path, "D:\\", "BADCOFFEE");
#else
	NativeInit(argc, (const char **)argv, path, "/tmp", "BADCOFFEE");
#endif

	float density = 1.0f;
	dp_xres = (float)pixel_xres * density / zoom;
	dp_yres = (float)pixel_yres * density / zoom;

	NativeInitGraphics();

	float dp_xscale = (float)dp_xres / pixel_xres;
	float dp_yscale = (float)dp_yres / pixel_yres;

	printf("Pixels: %i x %i\n", pixel_xres, pixel_yres);
	printf("Virtual pixels: %i x %i\n", dp_xres, dp_yres);

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
	bool nextFrameMD = 0;
	float t = 0;
	while (true) {
		input_state.accelerometer_valid = false;
		input_state.mouse_valid = true;
		int quitRequested = 0;

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			float mx = event.motion.x * dp_xscale;
			float my = event.motion.y * dp_yscale;

			if (event.type == SDL_QUIT) {
				quitRequested = 1;
			} else if (event.type == SDL_KEYDOWN) {
				if (event.key.keysym.sym == SDLK_ESCAPE) {
					quitRequested = 1;
				}
			} else if (event.type == SDL_MOUSEMOTION) {
				input_state.pointer_x[0] = mx;
				input_state.pointer_y[0] = my;
				NativeTouch(0, mx, my, 0, TOUCH_MOVE);
			} else if (event.type == SDL_MOUSEBUTTONDOWN) {
				if (event.button.button == SDL_BUTTON_LEFT) {
					//input_state.mouse_buttons_down = 1;
					input_state.pointer_down[0] = true;
					nextFrameMD = true;
					NativeTouch(0, mx, my, 0, TOUCH_DOWN);
				}
			} else if (event.type == SDL_MOUSEBUTTONUP) {
				if (event.button.button == SDL_BUTTON_LEFT) {
					input_state.pointer_down[0] = false;
					nextFrameMD = false;
					//input_state.mouse_buttons_up = 1;
					NativeTouch(0, mx, my, 0, TOUCH_UP);
				}
			}
		}

		if (quitRequested)
			break;

		const uint8 *keys = (const uint8 *)SDL_GetKeyState(NULL);
		if (keys[SDLK_ESCAPE])
			break;
		SimulateGamepad(keys, &input_state);
		UpdateInputState(&input_state);
		NativeUpdate(input_state);
		NativeRender();

		EndInputState(&input_state);

		if (framecount % 60 == 0) {
			// glsl_refresh(); // auto-reloads modified GLSL shaders once per second.
		}

		SDL_GL_SwapBuffers();

		// Simple frame rate limiting
		while (time_now() < t + 1.0f/60.0f) {
			sleep_ms(0);
			time_update();
		}
		time_update();
		t = time_now();
		framecount++;
	}
	// Faster exit, thanks to the OS. Remove this if you want to debug shutdown
	// The speed difference is only really noticable on Linux. On Windows you do notice it though
	// exit(0);

	NativeShutdownGraphics();
	SDL_PauseAudio(1);
	NativeShutdown();
	SDL_CloseAudio();
	SDL_Quit();
	net::Shutdown();
	exit(0);
	return 0;
}
