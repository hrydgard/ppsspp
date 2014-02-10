// SDL/EGL implementation of the framework.
// This is quite messy due to platform-specific implementations and #ifdef's.
// It is suggested to use the Qt implementation instead.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <ShellAPI.h>
#include "SDL.h"
#include "SDL_timer.h"
#include "SDL_audio.h"
#include "SDL_video.h"
#else
#include <unistd.h>
#include <pwd.h>
#include "SDL.h"
#include "SDL_timer.h"
#include "SDL_audio.h"
#include "SDL_video.h"
#endif

#ifdef RPI
#include <bcm_host.h>
#endif

#include <algorithm>

#include "base/display.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "gfx_es2/gl_state.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "net/resolve.h"
#include "base/NKCodeFromSDL.h"
#include "util/const_map.h"
#include "math/math_util.h"

#ifndef _WIN32
#include "SDL/SDLJoystick.h"
#endif

#ifdef PPSSPP
// Bad: PPSSPP includes from native
#include "Core/Core.h"
#include "Core/Config.h"

GlobalUIState lastUIState = UISTATE_MENU;
#endif

static SDL_Surface*        g_Screen        = NULL;
static bool g_ToggleFullScreenNextFrame = false;
static int g_QuitRequested = 0;

#if defined(MAEMO) || defined(PANDORA)
#define EGL
#include "EGL/egl.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "SDL_syswm.h"
#include "math.h"

static EGLDisplay          g_eglDisplay    = NULL;
static EGLContext          g_eglContext    = NULL;
static EGLSurface          g_eglSurface    = NULL;
static Display*            g_Display       = NULL;
static NativeWindowType    g_Window        = (NativeWindowType)NULL;

int8_t CheckEGLErrors(const std::string& file, uint16_t line) {
	EGLenum error;
	std::string errortext;

	error = eglGetError();
	switch (error)
	{
		case EGL_SUCCESS: case 0:           return 0;
		case EGL_NOT_INITIALIZED:           errortext = "EGL_NOT_INITIALIZED"; break;
		case EGL_BAD_ACCESS:                errortext = "EGL_BAD_ACCESS"; break;
		case EGL_BAD_ALLOC:                 errortext = "EGL_BAD_ALLOC"; break;
		case EGL_BAD_ATTRIBUTE:             errortext = "EGL_BAD_ATTRIBUTE"; break;
		case EGL_BAD_CONTEXT:               errortext = "EGL_BAD_CONTEXT"; break;
		case EGL_BAD_CONFIG:                errortext = "EGL_BAD_CONFIG"; break;
		case EGL_BAD_CURRENT_SURFACE:       errortext = "EGL_BAD_CURRENT_SURFACE"; break;
		case EGL_BAD_DISPLAY:               errortext = "EGL_BAD_DISPLAY"; break;
		case EGL_BAD_SURFACE:               errortext = "EGL_BAD_SURFACE"; break;
		case EGL_BAD_MATCH:                 errortext = "EGL_BAD_MATCH"; break;
		case EGL_BAD_PARAMETER:             errortext = "EGL_BAD_PARAMETER"; break;
		case EGL_BAD_NATIVE_PIXMAP:         errortext = "EGL_BAD_NATIVE_PIXMAP"; break;
		case EGL_BAD_NATIVE_WINDOW:         errortext = "EGL_BAD_NATIVE_WINDOW"; break;
		default:                            errortext = "unknown"; break;
	}
	printf( "ERROR: EGL Error detected in file %s at line %d: %s (0x%X)\n", file.c_str(), line, errortext.c_str(), error );
	return 1;
}
#define EGL_ERROR(str, check) { \
		if (check) CheckEGLErrors( __FILE__, __LINE__ ); \
		printf("EGL ERROR: " str "\n"); \
		return 1; \
	}

int8_t EGL_Open() {
#ifdef PANDORA
	g_Display = EGL_DEFAULT_DISPLAY;
#else
	if ((g_Display = XOpenDisplay(NULL)) == NULL)
		EGL_ERROR("Unable to get display!", false);
#endif
	if ((g_eglDisplay = eglGetDisplay((NativeDisplayType)g_Display)) == EGL_NO_DISPLAY)
		EGL_ERROR("Unable to create EGL display.", true);
	if (eglInitialize(g_eglDisplay, NULL, NULL) != EGL_TRUE)
		EGL_ERROR("Unable to initialize EGL display.", true);
	return 0;
}

int8_t EGL_Init() {
	EGLConfig g_eglConfig;
	EGLint g_numConfigs = 0;
	EGLint attrib_list[]= {
#ifdef PANDORA
		EGL_RED_SIZE,        5,
		EGL_GREEN_SIZE,      6,
		EGL_BLUE_SIZE,       5,
#endif
		EGL_DEPTH_SIZE,      16,
		EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SAMPLE_BUFFERS,  0,
		EGL_SAMPLES,         0,
#ifdef MAEMO
		EGL_BUFFER_SIZE, 16,
#endif
		EGL_NONE};

	const EGLint attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

	EGLBoolean result = eglChooseConfig(g_eglDisplay, attrib_list, &g_eglConfig, 1, &g_numConfigs);
	if (result != EGL_TRUE || g_numConfigs == 0) EGL_ERROR("Unable to query for available configs.", true);

	g_eglContext = eglCreateContext(g_eglDisplay, g_eglConfig, NULL, attributes );
	if (g_eglContext == EGL_NO_CONTEXT) EGL_ERROR("Unable to create GLES context!", true);

	// Get the SDL window handle
	SDL_SysWMinfo sysInfo; //Will hold our Window information
	SDL_VERSION(&sysInfo.version); //Set SDL version
	if(SDL_GetWMInfo(&sysInfo) <= 0) {
		printf("EGL ERROR: Unable to get SDL window handle: %s\n", SDL_GetError());
		return 1;
	}

#ifdef PANDORA
	g_Window = (NativeWindowType)NULL;
#else
	g_Window = (NativeWindowType)sysInfo.info.x11.window;
#endif
	g_eglSurface = eglCreateWindowSurface(g_eglDisplay, g_eglConfig, g_Window, 0);
	if (g_eglSurface == EGL_NO_SURFACE)
		EGL_ERROR("Unable to create EGL surface!", true);

	if (eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext) != EGL_TRUE)
		EGL_ERROR("Unable to make GLES context current.", true);

	return 0;
}

void EGL_Close() {
	if (g_eglDisplay != NULL) {
		eglMakeCurrent(g_eglDisplay, NULL, NULL, EGL_NO_CONTEXT);
		if (g_eglContext != NULL) {
			eglDestroyContext(g_eglDisplay, g_eglContext);
		}
		if (g_eglSurface != NULL) {
			eglDestroySurface(g_eglDisplay, g_eglSurface);
		}
		eglTerminate(g_eglDisplay);
		g_eglDisplay = NULL;
	}
	if (g_Display != NULL) {
		XCloseDisplay(g_Display);
		g_Display = NULL;
	}
	g_eglSurface = NULL;
	g_eglContext = NULL;
}
#else
#endif

#ifdef PANDORA
SDL_Joystick    *ljoy = NULL;
SDL_Joystick    *rjoy = NULL;
#else
#ifndef _WIN32
SDLJoystick *joystick = NULL;
#endif
#endif

// Simple implementations of System functions


void SystemToast(const char *text) {
#ifdef _WIN32
	MessageBox(0, text, "Toast!", MB_ICONINFORMATION);
#else
	puts(text);
#endif
}

void ShowKeyboard() {
	// Irrelevant on PC
}

void Vibrate(int length_ms) {
	// Ignore on PC
}

void System_SendMessage(const char *command, const char *parameter) {
	if (!strcmp(command, "toggle_fullscreen")) {
		g_ToggleFullScreenNextFrame = true;
	} else if (!strcmp(command, "finish")) {
		// Do a clean exit
		g_QuitRequested = true;
	}
}

void LaunchBrowser(const char *url) {
#ifdef _WIN32
	ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif __linux__
	std::string command = std::string("xdg-open ") + url;
	int err = system(command.c_str());
	if (err) {
		ILOG("Would have gone to %s but xdg-utils seems not to be installed", url)
	}
#elif __APPLE__
	std::string command = std::string("open ") + url;
	system(command.c_str());
#else
	ILOG("Would have gone to %s but LaunchBrowser is not implemented on this platform", url);
#endif
}

void LaunchMarket(const char *url) {
#ifdef _WIN32
	ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif __linux__
	std::string command = std::string("xdg-open ") + url;
	int err = system(command.c_str());
	if (err) {
		ILOG("Would have gone to %s but xdg-utils seems not to be installed", url)
	}
#elif __APPLE__
	std::string command = std::string("open ") + url;
	system(command.c_str());
#else
	ILOG("Would have gone to %s but LaunchMarket is not implemented on this platform", url);
#endif
}

void LaunchEmail(const char *email_address) {
#ifdef _WIN32
	ShellExecute(NULL, "open", (std::string("mailto:") + email_address).c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif __linux__
	std::string command = std::string("xdg-email ") + email_address;
	int err = system(command.c_str());
	if (err) {
		ILOG("Would have gone to %s but xdg-utils seems not to be installed", email_address)
	}
#elif __APPLE__
	std::string command = std::string("open mailto:") + email_address;
	system(command.c_str());
#else
	ILOG("Would have opened your email client for %s but LaunchEmail is not implemented on this platform", email_address);
#endif
}

std::string System_GetProperty(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_NAME:
#ifdef _WIN32
		return "SDL:Windows";
#elif __linux__
		return "SDL:Linux";
#elif __APPLE__
		return "SDL:OSX";
#else
		return "SDL:";
#endif
	case SYSPROP_LANGREGION:
		return "en_US";
	default:
		return "";
	}
}

InputState input_state;

static const int legacyKeyMap[] {
	NKCODE_X,          //A
	NKCODE_S,          //B
	NKCODE_Z,          //X
	NKCODE_A,          //Y
	NKCODE_W,          //LBUMPER
	NKCODE_Q,          //RBUMPER
	NKCODE_1,        //START
	NKCODE_2,        //SELECT
	NKCODE_DPAD_UP,         //UP
	NKCODE_DPAD_DOWN,       //DOWN
	NKCODE_DPAD_LEFT,       //LEFT
	NKCODE_DPAD_RIGHT,      //RIGHT
	0,                  //MENU (SwipeDown)
	NKCODE_ESCAPE,  //BACK
	NKCODE_I,          //JOY UP
	NKCODE_K,          //JOY DOWN
	NKCODE_J,          //JOY LEFT
	NKCODE_L,          //JOY RIGHT
};

void SimulateGamepad(const uint8 *keys, InputState *input) {
	// Legacy key mapping.
	input->pad_buttons = 0;
	input->pad_lstick_x = 0;
	input->pad_lstick_y = 0;
	input->pad_rstick_x = 0;
	input->pad_rstick_y = 0;

#ifdef PANDORA
	if (ljoy || rjoy) {
		SDL_JoystickUpdate();
		if (ljoy) {
			input->pad_lstick_x = max(min(SDL_JoystickGetAxis(ljoy, 0) / 32000.0f, 1.0f), -1.0f);
			input->pad_lstick_y = max(min(-SDL_JoystickGetAxis(ljoy, 1) / 32000.0f, 1.0f), -1.0f);
		}
		if (rjoy) {
			input->pad_rstick_x = max(min(SDL_JoystickGetAxis(rjoy, 0) / 32000.0f, 1.0f), -1.0f);
			input->pad_rstick_y = max(min(SDL_JoystickGetAxis(rjoy, 1) / 32000.0f, 1.0f), -1.0f);
		}
	}
#endif
}

extern void mixaudio(void *userdata, Uint8 *stream, int len) {
	NativeMix((short *)stream, len / 4);
}

// returns -1 on failure
static int parseInt(const char *str) {
	int val;
	int retval = sscanf(str, "%d", &val);
	printf("%i = scanf %s\n", retval, str);
	if (retval != 1) {
		return -1;
	} else {
		return val;
	}
}

static float parseFloat(const char *str) {
	float val;
	int retval = sscanf(str, "%f", &val);
	printf("%i = sscanf %s\n", retval, str);
	if (retval != 1) {
		return -1.0f;
	} else {
		return val;
	}
}

void ToggleFullScreenIfFlagSet() {
	if (g_ToggleFullScreenNextFrame) {
		g_ToggleFullScreenNextFrame = false;

#if 1
		int flags = g_Screen->flags; // Save the current flags in case toggling fails
		g_Screen = SDL_SetVideoMode(0, 0, 0, g_Screen->flags ^ SDL_FULLSCREEN); // Toggles FullScreen Mode
		if (g_Screen == NULL) {
			g_Screen = SDL_SetVideoMode(0, 0, 0, flags); // If toggle FullScreen failed, then switch back
		}
		if (g_Screen == NULL) {
			exit(1); // If you can't switch back for some reason, then epic fail
		}
#endif
	}
}

#ifdef _WIN32
#undef main
#endif
int main(int argc, char *argv[]) {
#ifdef RPI
	bcm_host_init();
#endif

	// Avoid warning about conversion from const char * to char *
	char envTemp[256];
	strcpy(envTemp, "SDL_VIDEO_CENTERED=1");
	putenv(envTemp);

	std::string app_name;
	std::string app_name_nice;
	bool landscape;
	NativeGetAppInfo(&app_name, &app_name_nice, &landscape);

	net::Init();
#ifdef __APPLE__
	// Make sure to request a somewhat modern GL context at least - the
	// latest supported by MacOSX (really, really sad...)
	// Requires SDL 2.0
	// We really should upgrade to SDL 2.0 soon.
	//SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	//SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
#endif

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) < 0) {
		fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
		return 1;
	}
#ifdef EGL
	if (EGL_Open())
		return 1;
#endif

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, 1);

	int mode;
	float set_dpi = 1.0f;
	float set_scale = 1.0f;
#ifdef USING_GLES2
	mode = SDL_SWSURFACE | SDL_FULLSCREEN;
	int set_xres = -1;
	int set_yres = -1;
#else
	mode = SDL_OPENGL;
	int set_xres = -1;
	int set_yres = -1;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i],"--fullscreen"))
			mode |= SDL_FULLSCREEN;
		if (set_xres == -2) {
			set_xres = parseInt(argv[i]);
		} else if (set_yres == -2) {
			set_yres = parseInt(argv[i]);
		}
		if (set_dpi == -2)
			set_dpi = parseFloat(argv[i]);
		if (set_scale == -2)
			set_scale = parseFloat(argv[i]);

		if (!strcmp(argv[i],"--xres"))
			set_xres = -2;
		if (!strcmp(argv[i],"--yres"))
			set_yres = -2;
		if (!strcmp(argv[i],"--dpi"))
			set_dpi = -2;
		if (!strcmp(argv[i],"--scale"))
			set_scale = -2;
	}
#endif
	if (mode & SDL_FULLSCREEN) {
		const SDL_VideoInfo* info = SDL_GetVideoInfo();
		pixel_xres = info->current_w;
		pixel_yres = info->current_h;
#ifdef PPSSPP
		g_Config.bFullScreen = true;
#endif
	} else {
		// set a sensible default resolution (2x)
		pixel_xres = 480 * 2 * set_scale;
		pixel_yres = 272 * 2 * set_scale;
#ifdef PPSSPP
		g_Config.bFullScreen = false;
#endif
	}

	set_dpi = 1.0f / set_dpi;

	if (!landscape) {
		std::swap(pixel_xres, pixel_yres);
	}

	if (set_xres > 0) {
		pixel_xres = set_xres;
	}
	if (set_yres > 0) {
		pixel_yres = set_yres;
	}
	float dpi_scale = 1.0f;
	if (set_dpi > 0) {
		dpi_scale = set_dpi;
	}

	dp_xres = (float)pixel_xres * dpi_scale;
	dp_yres = (float)pixel_yres * dpi_scale;

	g_Screen = SDL_SetVideoMode(pixel_xres, pixel_yres, 0, mode);
	if (g_Screen == NULL) {
		fprintf(stderr, "SDL SetVideoMode failed: Unable to create OpenGL screen: %s\n", SDL_GetError());
		SDL_Quit();
		return 2;
	}

#ifdef EGL
	EGL_Init();
#endif

#ifdef PPSSPP
	SDL_WM_SetCaption((app_name_nice + " " + PPSSPP_GIT_VERSION).c_str(), NULL);
#endif

#ifdef MAEMO
	SDL_ShowCursor(SDL_DISABLE);
#endif


#ifndef USING_GLES2
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
#endif

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

	pixel_in_dps = (float)pixel_xres / dp_xres;
	g_dpi_scale = dp_xres / (float)pixel_xres;

	printf("Pixels: %i x %i\n", pixel_xres, pixel_yres);
	printf("Virtual pixels: %i x %i\n", dp_xres, dp_yres);

	NativeInitGraphics();
	NativeResized();

	SDL_AudioSpec fmt;
	fmt.freq = 44100;
	fmt.format = AUDIO_S16;
	fmt.channels = 2;
	fmt.samples = 2048;
	fmt.callback = &mixaudio;
	fmt.userdata = (void *)0;

	if (SDL_OpenAudio(&fmt, NULL) < 0) {
		ELOG("Failed to open audio: %s", SDL_GetError());
	}

	// Audio must be unpaused _after_ NativeInit()
	SDL_PauseAudio(0);
#ifdef PANDORA
	int numjoys = SDL_NumJoysticks();
	// Joysticks init, we the nubs if setup as Joystick
	if (numjoys > 0) {
		ljoy = SDL_JoystickOpen(0);
		if (numjoys > 1)
			rjoy = SDL_JoystickOpen(1);
	}
#else
#ifndef _WIN32
	joystick = new SDLJoystick();
#endif
#endif
	EnableFZ();

	int framecount = 0;
	float t = 0;
	float lastT = 0;
	uint32_t pad_buttons = 0;	 // legacy pad buttons
	while (true) {
		input_state.accelerometer_valid = false;
		input_state.mouse_valid = true;

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			float mx = event.motion.x * g_dpi_scale;
			float my = event.motion.y * g_dpi_scale;

			switch (event.type) {
			case SDL_QUIT:
				g_QuitRequested = 1;
				break;
			case SDL_KEYDOWN:
				{
					int k = event.key.keysym.sym;
					KeyInput key;
					key.flags = KEY_DOWN;
					key.keyCode = KeyMapRawSDLtoNative.find(k)->second;
					key.deviceId = DEVICE_ID_KEYBOARD;
					NativeKey(key);

					for (int i = 0; i < ARRAY_SIZE(legacyKeyMap); i++) {
						if (legacyKeyMap[i] == key.keyCode)
							pad_buttons |= 1 << i;
					}
					break;
				}
			case SDL_KEYUP:
				{
					int k = event.key.keysym.sym;
					KeyInput key;
					key.flags = KEY_UP;
					key.keyCode = KeyMapRawSDLtoNative.find(k)->second;
					key.deviceId = DEVICE_ID_KEYBOARD;
					NativeKey(key);
					for (int i = 0; i < ARRAY_SIZE(legacyKeyMap); i++) {
						if (legacyKeyMap[i] == key.keyCode)
							pad_buttons &= ~(1 << i);
					}
					break;
				}
			case SDL_MOUSEBUTTONDOWN:
				switch (event.button.button) {
				case SDL_BUTTON_LEFT:
					{
						input_state.pointer_x[0] = mx;
						input_state.pointer_y[0] = my;
						input_state.pointer_down[0] = true;
						input_state.mouse_valid = true;
						TouchInput input;
						input.x = mx;
						input.y = my;
						input.flags = TOUCH_DOWN;
						input.id = 0;
						NativeTouch(input);
						KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_1, KEY_DOWN);
						NativeKey(key);
					}
					break;
				case SDL_BUTTON_RIGHT:
					{
						KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_2, KEY_DOWN);
						NativeKey(key);
					}
					break;
				case SDL_BUTTON_WHEELUP:
					{
						KeyInput key;
						key.deviceId = DEVICE_ID_MOUSE;
						key.keyCode = NKCODE_EXT_MOUSEWHEEL_UP;
						key.flags = KEY_DOWN;
						NativeKey(key);
					}
					break;
				case SDL_BUTTON_WHEELDOWN:
					{
						KeyInput key;
						key.deviceId = DEVICE_ID_MOUSE;
						key.keyCode = NKCODE_EXT_MOUSEWHEEL_DOWN;
						key.flags = KEY_DOWN;
						NativeKey(key);
					}
					break;
				}
				break;
			case SDL_MOUSEMOTION:
				if (input_state.pointer_down[0]) {
					input_state.pointer_x[0] = mx;
					input_state.pointer_y[0] = my;
					input_state.mouse_valid = true;
					TouchInput input;
					input.x = mx;
					input.y = my;
					input.flags = TOUCH_MOVE;
					input.id = 0;
					NativeTouch(input);
				}
				break;
			case SDL_MOUSEBUTTONUP:
				switch (event.button.button) {
				case SDL_BUTTON_LEFT:
					{
						input_state.pointer_x[0] = mx;
						input_state.pointer_y[0] = my;
						input_state.pointer_down[0] = false;
						input_state.mouse_valid = true;
						//input_state.mouse_buttons_up = 1;
						TouchInput input;
						input.x = mx;
						input.y = my;
						input.flags = TOUCH_UP;
						input.id = 0;
						NativeTouch(input);
						KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_1, KEY_UP);
						NativeKey(key);
					}
					break;
				case SDL_BUTTON_RIGHT:
					{
						KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_2, KEY_UP);
						NativeKey(key);
					}
					break;
				case SDL_BUTTON_WHEELUP:
					{
						KeyInput key;
						key.deviceId = DEVICE_ID_DEFAULT;
						key.keyCode = NKCODE_EXT_MOUSEWHEEL_UP;
						key.flags = KEY_UP;
						NativeKey(key);
					}
					break;
				case SDL_BUTTON_WHEELDOWN:
					{
						KeyInput key;
						key.deviceId = DEVICE_ID_DEFAULT;
						key.keyCode = NKCODE_EXT_MOUSEWHEEL_DOWN;
						key.flags = KEY_UP;
						NativeKey(key);
					}
					break;
				}
				break;
			default:
#ifndef _WIN32
				joystick->ProcessInput(event);
#endif
				break;
			}
		}

		if (g_QuitRequested)
			break;

		const uint8 *keys = (const uint8 *)SDL_GetKeyState(NULL);
		SimulateGamepad(keys, &input_state);
		input_state.pad_buttons = pad_buttons;
		UpdateInputState(&input_state, true);
		NativeUpdate(input_state);
		NativeRender();
#if defined(PPSSPP) && !defined(MAEMO)
		if (lastUIState != globalUIState) {
			lastUIState = globalUIState;
			if (lastUIState == UISTATE_INGAME && g_Config.bFullScreen && !g_Config.bShowTouchControls)
				SDL_ShowCursor(SDL_DISABLE);
			if (lastUIState != UISTATE_INGAME && g_Config.bFullScreen)
				SDL_ShowCursor(SDL_ENABLE);
		}
#endif

		EndInputState(&input_state);

		if (framecount % 60 == 0) {
			// glsl_refresh(); // auto-reloads modified GLSL shaders once per second.
		}

#ifdef EGL
		eglSwapBuffers(g_eglDisplay, g_eglSurface);
#else
		if (!keys[SDLK_TAB] || t - lastT >= 1.0/60.0)
		{
			SDL_GL_SwapBuffers();
			lastT = t;
		}
#endif

		ToggleFullScreenIfFlagSet();
		time_update();
		t = time_now();
		framecount++;
	}
#ifndef PANDORA
#ifndef _WIN32
	delete joystick;
	joystick = NULL;
#endif
#endif
	// Faster exit, thanks to the OS. Remove this if you want to debug shutdown
	// The speed difference is only really noticable on Linux. On Windows you do notice it though
#ifdef _WIN32
	exit(0);
#endif
	NativeShutdownGraphics();
	SDL_PauseAudio(1);
	SDL_CloseAudio();
	NativeShutdown();
#ifdef EGL
	EGL_Close();
#endif
	SDL_Quit();
	net::Shutdown();
#ifdef RPI
	bcm_host_deinit();
#endif

	exit(0);
	return 0;
}
