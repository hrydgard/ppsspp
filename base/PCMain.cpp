// SDL/EGL implementation of the framework.
// This is quite messy due to platform-specific implementations and #ifdef's.
// It is suggested to use the Qt implementation instead.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <ShellAPI.h>
#include "SDL/SDL.h"
#include "SDL/SDL_timer.h"
#include "SDL/SDL_audio.h"
#include "SDL/SDL_video.h"
#else
#include <pwd.h>
#include "SDL.h"
#include "SDL_timer.h"
#include "SDL_audio.h"
#include "SDL_video.h"
#endif
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
#include "../SDL/SDLJoystick.h"
// Bad: PPSSPP includes from native
#include "Core/Core.h"
#include "Core/Config.h"


GlobalUIState lastUIState = UISTATE_MENU;

#if defined(MAEMO) || defined(PANDORA)
#define EGL
#include "EGL/egl.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "SDL_syswm.h"
#include "math.h"

EGLDisplay          g_eglDisplay    = NULL;
EGLContext          g_eglContext    = NULL;
EGLSurface          g_eglSurface    = NULL;
Display*            g_Display       = NULL;
NativeWindowType    g_Window        = (NativeWindowType)NULL;

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
	EGLConfig g_eglConfig; //[1] = {NULL};
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
	if(SDL_GetWMInfo(&sysInfo) <= 0)
	{
		printf("EGL ERROR: Unable to get SDL window handle: %s\n", SDL_GetError());
		return 1;
	}

#ifdef PANDORA
	g_Window = (NativeWindowType)NULL;
#else
	g_Window = (NativeWindowType)sysInfo.info.x11.window;
#endif
	g_eglSurface = eglCreateWindowSurface(g_eglDisplay, g_eglConfig, g_Window, 0);
	if (g_eglSurface == EGL_NO_SURFACE) EGL_ERROR("Unable to create EGL surface!", true);

	if (eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext) != EGL_TRUE)
		EGL_ERROR("Unable to make GLES context current.", true);

	return 0;
}

void EGL_Close() {
	if (g_eglDisplay != NULL)
	{
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
SDLJoystick *joystick = NULL;
#endif

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

void System_InputBox(const char *title, const char *defaultValue) {
	// Stub
	NativeMessageReceived((std::string("INPUTBOX:") + title).c_str(), "TestFile");
}

void LaunchBrowser(const char *url)
{
#ifdef _WIN32
	ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif __linux__
	std::string command = std::string("xdg-open ") + url;
	system(command.c_str());
#elif __APPLE__
	std::string command = std::string("open ") + url;
	system(command.c_str());
#else
	ILOG("Would have gone to %s but LaunchBrowser is not implemented on this platform", url);
#endif
}

void LaunchMarket(const char *url)
{
#ifdef _WIN32
	ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif __linux__
	std::string command = std::string("xdg-open ") + url;
	system(command.c_str());
#elif __APPLE__
	std::string command = std::string("open ") + url;
	system(command.c_str());
#else
	ILOG("Would have gone to %s but LaunchMarket is not implemented on this platform", url);
#endif
}

void LaunchEmail(const char *email_address)
{
#ifdef _WIN32
	ShellExecute(NULL, "open", (std::string("mailto:") + email_address).c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif __linux__
	std::string command = std::string("xdg-email ") + email_address;
	system(command.c_str());
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

void SimulateGamepad(const uint8 *keys, InputState *input) {
	input->pad_buttons = 0;
	input->pad_lstick_x = 0;
	input->pad_lstick_y = 0;
	input->pad_rstick_x = 0;
	input->pad_rstick_y = 0;
// TODO: Use NativeAxis for joy instead.
#ifdef PANDORA
	if ((ljoy)||(rjoy)) {
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

#ifdef _WIN32
#undef main
#endif
int main(int argc, char *argv[]) {
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
#ifdef USING_GLES2
	mode = SDL_SWSURFACE | SDL_FULLSCREEN;
#else
	mode = SDL_OPENGL;
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i],"--fullscreen"))
			mode |= SDL_FULLSCREEN;
#endif
	if (mode & SDL_FULLSCREEN) {
		const SDL_VideoInfo* info = SDL_GetVideoInfo();
		pixel_xres = info->current_w;
		pixel_yres = info->current_h;
		g_Config.bFullScreen = true;
	} else {
		// set a sensible default resolution (2x)
		pixel_xres = 480 * 2;
		pixel_yres = 272 * 2;
		g_Config.bFullScreen = false;
	}
	dp_xres = (float)pixel_xres;
	dp_yres = (float)pixel_yres;

	if (SDL_SetVideoMode(pixel_xres, pixel_yres, 0, mode) == NULL) {
		fprintf(stderr, "SDL SetVideoMode failed: Unable to create OpenGL screen: %s\n", SDL_GetError());
		SDL_Quit();
		return(2);
	}
#ifdef EGL
	EGL_Init();
#endif

	SDL_WM_SetCaption((app_name_nice + " " + PPSSPP_GIT_VERSION).c_str(), NULL);
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

	float dp_xscale = (float)dp_xres / pixel_xres;
	float dp_yscale = (float)dp_yres / pixel_yres;

	g_dpi_scale = dp_xres / (float)pixel_xres;

	printf("Pixels: %i x %i\n", pixel_xres, pixel_yres);
	printf("Virtual pixels: %i x %i\n", dp_xres, dp_yres);
	NativeInitGraphics();

	SDL_AudioSpec fmt;
	fmt.freq = 44100;
	fmt.format = AUDIO_S16;
	fmt.channels = 2;
	fmt.samples = 2048;
	fmt.callback = &mixaudio;
	fmt.userdata = (void *)0;

	if (SDL_OpenAudio(&fmt, NULL) < 0)
		ELOG("Failed to open audio: %s", SDL_GetError());

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
	joystick = new SDLJoystick();
#endif
	EnableFZ();

	int framecount = 0;
	float t = 0;
	float lastT = 0;
	while (true) {
		input_state.accelerometer_valid = false;
		input_state.mouse_valid = true;
		int quitRequested = 0;

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			float mx = event.motion.x * dp_xscale;
			float my = event.motion.y * dp_yscale;

			switch (event.type) {
			case SDL_QUIT:
				quitRequested = 1;
				break;
			case SDL_KEYDOWN:
				{
					int k = event.key.keysym.sym;
					KeyInput key;
					key.flags = KEY_DOWN;
					key.keyCode = KeyMapRawSDLtoNative.find(k)->second;
					key.deviceId = DEVICE_ID_KEYBOARD;
					NativeKey(key);
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
					break;
				}
			case SDL_MOUSEBUTTONDOWN:
				switch (event.button.button) {
				case SDL_BUTTON_LEFT:
					{
						input_state.pointer_x[0] = mx;
						input_state.pointer_y[0] = my;
						//input_state.mouse_buttons_down = 1;
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
				joystick->ProcessInput(event);
				break;
			}
		}

		if (quitRequested)
			break;

		const uint8 *keys = (const uint8 *)SDL_GetKeyState(NULL);
		SimulateGamepad(keys, &input_state);
		UpdateInputState(&input_state);
		NativeUpdate(input_state);
		NativeRender();
#ifndef MAEMO
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
		time_update();
		t = time_now();
		framecount++;
	}
#ifndef PANDORA
	delete joystick;
	joystick = NULL;
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
	exit(0);
	return 0;
}
