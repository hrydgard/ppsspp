// SDL/EGL implementation of the framework.
// This is quite messy due to platform-specific implementations and #ifdef's.
// Note: SDL1.2 implementation is deprecated and will soon be replaced by SDL2.0.
// If your platform is not supported, it is suggested to use Qt instead.

#include <unistd.h>
#include <pwd.h>

#include "SDL.h"
#include "SDL/SDLJoystick.h"
SDLJoystick *joystick = NULL;

#if PPSSPP_PLATFORM(RPI)
#include <bcm_host.h>
#endif

#include <atomic>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <thread>

#include "base/display.h"
#include "base/logging.h"
#include "base/timeutil.h"
#include "ext/glslang/glslang/Public/ShaderLang.h"
#include "input/input_state.h"
#include "input/keycodes.h"
#include "net/resolve.h"
#include "base/NKCodeFromSDL.h"
#include "util/const_map.h"
#include "util/text/utf8.h"
#include "math/math_util.h"
#include "thin3d/GLRenderManager.h"
#include "thread/threadutil.h"
#include "math.h"

#if !defined(__APPLE__)
#include "SDL_syswm.h"
#endif

#if defined(VK_USE_PLATFORM_XLIB_KHR)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#elif defined(VK_USE_PLATFORM_XCB_KHR)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlib-xcb.h>
#endif

#if defined(USING_EGL)
#include "EGL/egl.h"
#endif

#include "Core/System.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Common/GraphicsContext.h"
#include "SDLGLGraphicsContext.h"
#include "SDLVulkanGraphicsContext.h"


GlobalUIState lastUIState = UISTATE_MENU;
GlobalUIState GetUIState();

static bool g_ToggleFullScreenNextFrame = false;
static int g_ToggleFullScreenType;
static int g_QuitRequested = 0;

static int g_DesktopWidth = 0;
static int g_DesktopHeight = 0;

#if defined(USING_EGL)

static EGLDisplay               g_eglDisplay    = NULL;
static EGLContext               g_eglContext    = NULL;
static EGLSurface               g_eglSurface    = NULL;
#ifdef USING_FBDEV
static EGLNativeDisplayType     g_Display       = NULL;
#else
static Display*                 g_Display       = NULL;
#endif
static NativeWindowType         g_Window        = (NativeWindowType)NULL;

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
#ifdef USING_FBDEV
	g_Display = ((EGLNativeDisplayType)0);
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
	// TODO: Should cycle through fallbacks, like on Android
#ifdef USING_FBDEV
		EGL_RED_SIZE,        5,
		EGL_GREEN_SIZE,      6,
		EGL_BLUE_SIZE,       5,
#endif
		EGL_DEPTH_SIZE,      16,
		EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
#ifdef USING_GLES2
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
#else
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
#endif
		EGL_SAMPLE_BUFFERS,  0,
		EGL_SAMPLES,         0,
		EGL_NONE};

	const EGLint attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

	EGLBoolean result = eglChooseConfig(g_eglDisplay, attrib_list, &g_eglConfig, 1, &g_numConfigs);
	if (result != EGL_TRUE || g_numConfigs == 0) EGL_ERROR("Unable to query for available configs.", true);

	g_eglContext = eglCreateContext(g_eglDisplay, g_eglConfig, NULL, attributes );
	if (g_eglContext == EGL_NO_CONTEXT) EGL_ERROR("Unable to create GLES context!", true);

#if !defined(USING_FBDEV) && !defined(__APPLE__)
	//Get the SDL window handle
	SDL_SysWMinfo sysInfo; //Will hold our Window information
	SDL_VERSION(&sysInfo.version); //Set SDL version
	g_Window = (NativeWindowType)sysInfo.info.x11.window;
#else
	g_Window = (NativeWindowType)NULL;
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
#if !defined(USING_FBDEV)
		XCloseDisplay(g_Display);
#endif
		g_Display = NULL;
	}
	g_eglSurface = NULL;
	g_eglContext = NULL;
}
#endif


int getDisplayNumber(void) {
	int displayNumber = 0;
	char * displayNumberStr;

	//get environment
	displayNumberStr=getenv("SDL_VIDEO_FULLSCREEN_HEAD");

	if (displayNumberStr) {
		displayNumber = atoi(displayNumberStr);
	}

	return displayNumber;
}

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
		if (strcmp(parameter, "1") == 0) {
			g_ToggleFullScreenType = 1;
		} else if (strcmp(parameter, "0") == 0) {
			g_ToggleFullScreenType = 0;
		} else {
			// Just toggle.
			g_ToggleFullScreenType = -1;
		}
	} else if (!strcmp(command, "finish")) {
		// Do a clean exit
		g_QuitRequested = true;
	}
}

void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

void LaunchBrowser(const char *url) {
#if defined(MOBILE_DEVICE)
	ILOG("Would have gone to %s but LaunchBrowser is not implemented on this platform", url);
#elif defined(_WIN32)
	ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
	std::string command = std::string("open ") + url;
	system(command.c_str());
#else
	std::string command = std::string("xdg-open ") + url;
	int err = system(command.c_str());
	if (err) {
		ILOG("Would have gone to %s but xdg-utils seems not to be installed", url)
	}
#endif
}

void LaunchMarket(const char *url) {
#if defined(MOBILE_DEVICE)
	ILOG("Would have gone to %s but LaunchMarket is not implemented on this platform", url);
#elif defined(_WIN32)
	ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
	std::string command = std::string("open ") + url;
	system(command.c_str());
#else
	std::string command = std::string("xdg-open ") + url;
	int err = system(command.c_str());
	if (err) {
		ILOG("Would have gone to %s but xdg-utils seems not to be installed", url)
	}
#endif
}

void LaunchEmail(const char *email_address) {
#if defined(MOBILE_DEVICE)
	ILOG("Would have opened your email client for %s but LaunchEmail is not implemented on this platform", email_address);
#elif defined(_WIN32)
	ShellExecute(NULL, "open", (std::string("mailto:") + email_address).c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
	std::string command = std::string("open mailto:") + email_address;
	system(command.c_str());
#else
	std::string command = std::string("xdg-email ") + email_address;
	int err = system(command.c_str());
	if (err) {
		ILOG("Would have gone to %s but xdg-utils seems not to be installed", email_address)
	}
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

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return 44100;
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return 60000;
	case SYSPROP_DEVICE_TYPE:
#if defined(MOBILE_DEVICE)
		return DEVICE_TYPE_MOBILE;
#else
		return DEVICE_TYPE_DESKTOP;
#endif
	default:
		return -1;
	}
}

bool System_GetPropertyBool(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_HAS_BACK_BUTTON:
		return true;
	case SYSPROP_APP_GOLD:
#ifdef GOLD
		return true;
#else
		return false;
#endif
	default:
		return false;
	}
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

void ToggleFullScreenIfFlagSet(SDL_Window *window) {
	if (g_ToggleFullScreenNextFrame) {
		g_ToggleFullScreenNextFrame = false;

		Uint32 window_flags = SDL_GetWindowFlags(window);
		if (g_ToggleFullScreenType == -1) {
			window_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
		} else if (g_ToggleFullScreenType == 1) {
			window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
		} else {
			window_flags &= ~SDL_WINDOW_FULLSCREEN_DESKTOP;
		}
		SDL_SetWindowFullscreen(window, window_flags);
	}
}

enum class EmuThreadState {
	DISABLED,
	START_REQUESTED,
	RUNNING,
	QUIT_REQUESTED,
	STOPPED,
};

static std::thread emuThread;
static std::atomic<int> emuThreadState((int)EmuThreadState::DISABLED);

static void EmuThreadFunc(GraphicsContext *graphicsContext) {
	setCurrentThreadName("Emu");

	// There's no real requirement that NativeInit happen on this thread.
	// We just call the update/render loop here.
	emuThreadState = (int)EmuThreadState::RUNNING;

	NativeInitGraphics(graphicsContext);

	while (emuThreadState != (int)EmuThreadState::QUIT_REQUESTED) {
		UpdateRunLoop();
	}
	emuThreadState = (int)EmuThreadState::STOPPED;

	NativeShutdownGraphics();
}

static void EmuThreadStart(GraphicsContext *context) {
	emuThreadState = (int)EmuThreadState::START_REQUESTED;
	emuThread = std::thread(&EmuThreadFunc, context);
}

static void EmuThreadStop() {
	emuThreadState = (int)EmuThreadState::QUIT_REQUESTED;
}

static void EmuThreadJoin() {
	emuThread.join();
	emuThread = std::thread();
}

#ifdef _WIN32
#undef main
#endif
int main(int argc, char *argv[]) {
	glslang::InitializeProcess();

#if PPSSPP_PLATFORM(RPI)
	bcm_host_init();
#endif
	putenv((char*)"SDL_VIDEO_CENTERED=1");
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

	if (VulkanMayBeAvailable()) {
		printf("Vulkan might be available.\n");
	} else {
		printf("Vulkan is not available.\n");
	}

	int set_xres = -1;
	int set_yres = -1;
	bool portrait = false;
	bool set_ipad = false;
	float set_dpi = 1.0f;
	float set_scale = 1.0f;

	// Produce a new set of arguments with the ones we skip.
	int remain_argc = 1;
	const char *remain_argv[256] = { argv[0] };

	Uint32 mode = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i],"--fullscreen"))
			mode |= SDL_WINDOW_FULLSCREEN_DESKTOP;
		else if (set_xres == -2)
			set_xres = parseInt(argv[i]);
		else if (set_yres == -2)
			set_yres = parseInt(argv[i]);
		else if (set_dpi == -2)
			set_dpi = parseFloat(argv[i]);
		else if (set_scale == -2)
			set_scale = parseFloat(argv[i]);
		else if (!strcmp(argv[i],"--xres"))
			set_xres = -2;
		else if (!strcmp(argv[i],"--yres"))
			set_yres = -2;
		else if (!strcmp(argv[i],"--dpi"))
			set_dpi = -2;
		else if (!strcmp(argv[i],"--scale"))
			set_scale = -2;
		else if (!strcmp(argv[i],"--ipad"))
			set_ipad = true;
		else if (!strcmp(argv[i],"--portrait"))
			portrait = true;
		else {
			remain_argv[remain_argc++] = argv[i];
		}
	}

	std::string app_name;
	std::string app_name_nice;
	std::string version;
	bool landscape;
	NativeGetAppInfo(&app_name, &app_name_nice, &landscape, &version);

	bool joystick_enabled = true;
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) < 0) {
		fprintf(stderr, "Failed to initialize SDL with joystick support. Retrying without.\n");
		joystick_enabled = false;
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
			fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
			return 1;
		}
	}

	// TODO: How do we get this into the GraphicsContext?
#ifdef USING_EGL
	if (EGL_Open())
		return 1;
#endif

	// Get the video info before doing anything else, so we don't get skewed resolution results.
	// TODO: support multiple displays correctly
	SDL_DisplayMode displayMode;
	int should_be_zero = SDL_GetCurrentDisplayMode(0, &displayMode);
	if (should_be_zero != 0) {
		fprintf(stderr, "Could not get display mode: %s\n", SDL_GetError());
		return 1;
	}
	g_DesktopWidth = displayMode.w;
	g_DesktopHeight = displayMode.h;

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetSwapInterval(1);

	// Is resolution is too low to run windowed
	if (g_DesktopWidth < 480 * 2 && g_DesktopHeight < 272 * 2) {
		mode |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	if (mode & SDL_WINDOW_FULLSCREEN_DESKTOP) {
		pixel_xres = g_DesktopWidth;
		pixel_yres = g_DesktopHeight;
		g_Config.bFullScreen = true;
	} else {
		// set a sensible default resolution (2x)
		pixel_xres = 480 * 2 * set_scale;
		pixel_yres = 272 * 2 * set_scale;
		if (portrait) {
			std::swap(pixel_xres, pixel_yres);
		}
		g_Config.bFullScreen = false;
	}

	set_dpi = 1.0f / set_dpi;

	if (set_ipad) {
		pixel_xres = 1024;
		pixel_yres = 768;
	}
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

	// Mac / Linux
	char path[2048];
	const char *the_path = getenv("HOME");
	if (!the_path) {
		struct passwd* pwd = getpwuid(getuid());
		if (pwd)
			the_path = pwd->pw_dir;
	}
	strcpy(path, the_path);
	if (path[strlen(path)-1] != '/')
		strcat(path, "/");

	NativeInit(remain_argc, (const char **)remain_argv, path, "/tmp", nullptr);

	// Use the setting from the config when initing the window.
	if (g_Config.bFullScreen)
		mode |= SDL_WINDOW_FULLSCREEN_DESKTOP;

	int x = SDL_WINDOWPOS_UNDEFINED_DISPLAY(getDisplayNumber());
	int y = SDL_WINDOWPOS_UNDEFINED;

	pixel_in_dps_x = (float)pixel_xres / dp_xres;
	pixel_in_dps_y = (float)pixel_yres / dp_yres;
	g_dpi_scale_x = dp_xres / (float)pixel_xres;
	g_dpi_scale_y = dp_yres / (float)pixel_yres;
	g_dpi_scale_real_x = g_dpi_scale_x;
	g_dpi_scale_real_y = g_dpi_scale_y;

	printf("Pixels: %i x %i\n", pixel_xres, pixel_yres);
	printf("Virtual pixels: %i x %i\n", dp_xres, dp_yres);

	GraphicsContext *graphicsContext = nullptr;
	SDL_Window *window = nullptr;

	std::string error_message;
	if (g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
		SDLGLGraphicsContext *ctx = new SDLGLGraphicsContext();
		if (ctx->Init(window, x, y, mode, &error_message) != 0) {
			printf("GL init error '%s'\n", error_message.c_str());
		}
		graphicsContext = ctx;
	} else if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN) {
		SDLVulkanGraphicsContext *ctx = new SDLVulkanGraphicsContext();
		if (!ctx->Init(window, x, y, mode, &error_message)) {
			printf("Vulkan init error '%s' - falling back to GL\n", error_message.c_str());
			g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
			SetGPUBackend((GPUBackend)g_Config.iGPUBackend);
			delete ctx;
			SDLGLGraphicsContext *glctx = new SDLGLGraphicsContext();
			glctx->Init(window, x, y, mode, &error_message);
			graphicsContext = glctx;
		} else {
			graphicsContext = ctx;
		}
	}

	bool useEmuThread = g_Config.iGPUBackend == (int)GPUBackend::OPENGL;

	SDL_SetWindowTitle(window, (app_name_nice + " " + PPSSPP_GIT_VERSION).c_str());

	// Since we render from the main thread, there's nothing done here, but we call it to avoid confusion.
	if (!graphicsContext->InitFromRenderThread(&error_message)) {
		printf("Init from thread error: '%s'\n", error_message.c_str());
	}

#ifdef MOBILE_DEVICE
	SDL_ShowCursor(SDL_DISABLE);
#endif

	if (!useEmuThread) {
		NativeInitGraphics(graphicsContext);
		NativeResized();
	}

	SDL_AudioSpec fmt, ret_fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.freq = 44100;
	fmt.format = AUDIO_S16;
	fmt.channels = 2;
	fmt.samples = 2048;
	fmt.callback = &mixaudio;
	fmt.userdata = (void *)0;

	if (SDL_OpenAudio(&fmt, &ret_fmt) < 0) {
		ELOG("Failed to open audio: %s", SDL_GetError());
	} else {
		if (ret_fmt.samples != fmt.samples) // Notify, but still use it
			ELOG("Output audio samples: %d (requested: %d)", ret_fmt.samples, fmt.samples);
		if (ret_fmt.freq != fmt.freq || ret_fmt.format != fmt.format || ret_fmt.channels != fmt.channels) {
			ELOG("Sound buffer format does not match requested format.");
			ELOG("Output audio freq: %d (requested: %d)", ret_fmt.freq, fmt.freq);
			ELOG("Output audio format: %d (requested: %d)", ret_fmt.format, fmt.format);
			ELOG("Output audio channels: %d (requested: %d)", ret_fmt.channels, fmt.channels);
			ELOG("Provided output format does not match requirement, turning audio off");
			SDL_CloseAudio();
		}
	}

	// Audio must be unpaused _after_ NativeInit()
	SDL_PauseAudio(0);
	if (joystick_enabled) {
		joystick = new SDLJoystick();
	} else {
		joystick = nullptr;
	}
	EnableFZ();

	int framecount = 0;
	bool mouseDown = false;

	if (useEmuThread) {
		EmuThreadStart(graphicsContext);
	}
	graphicsContext->ThreadStart();

	while (true) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			float mx = event.motion.x * g_dpi_scale_x;
			float my = event.motion.y * g_dpi_scale_y;

			switch (event.type) {
			case SDL_QUIT:
				g_QuitRequested = 1;
				break;

#if !defined(MOBILE_DEVICE)
			case SDL_WINDOWEVENT:
				switch (event.window.event) {
				case SDL_WINDOWEVENT_RESIZED:
				{
					Uint32 window_flags = SDL_GetWindowFlags(window);
					bool fullscreen = (window_flags & SDL_WINDOW_FULLSCREEN);

					pixel_xres = event.window.data1;
					pixel_yres = event.window.data2;
					dp_xres = (float)pixel_xres * dpi_scale;
					dp_yres = (float)pixel_yres * dpi_scale;
					NativeResized();

					// Set variable here in case fullscreen was toggled by hotkey
					g_Config.bFullScreen = fullscreen;

					// Hide/Show cursor correctly toggling fullscreen
					if (lastUIState == UISTATE_INGAME && fullscreen && !g_Config.bShowTouchControls) {
						SDL_ShowCursor(SDL_DISABLE);
					} else if (lastUIState != UISTATE_INGAME || !fullscreen) {
						SDL_ShowCursor(SDL_ENABLE);
					}
					break;
				}

				default:
					break;
				}
				break;
#endif
			case SDL_KEYDOWN:
				{
					if (event.key.repeat > 0) { break;}
					int k = event.key.keysym.sym;
					KeyInput key;
					key.flags = KEY_DOWN;
					auto mapped = KeyMapRawSDLtoNative.find(k);
					if (mapped == KeyMapRawSDLtoNative.end() || mapped->second == NKCODE_UNKNOWN) {
						break;
					}
					key.keyCode = mapped->second;
					key.deviceId = DEVICE_ID_KEYBOARD;
					NativeKey(key);
					break;
				}
			case SDL_KEYUP:
				{
					if (event.key.repeat > 0) { break;}
					int k = event.key.keysym.sym;
					KeyInput key;
					key.flags = KEY_UP;
					auto mapped = KeyMapRawSDLtoNative.find(k);
					if (mapped == KeyMapRawSDLtoNative.end() || mapped->second == NKCODE_UNKNOWN) {
						break;
					}
					key.keyCode = mapped->second;
					key.deviceId = DEVICE_ID_KEYBOARD;
					NativeKey(key);
					break;
				}
			case SDL_TEXTINPUT:
				{
					int pos = 0;
					int c = u8_nextchar(event.text.text, &pos);
					KeyInput key;
					key.flags = KEY_CHAR;
					key.keyCode = c;
					key.deviceId = DEVICE_ID_KEYBOARD;
					NativeKey(key);
					break;
				}
			case SDL_MOUSEBUTTONDOWN:
				switch (event.button.button) {
				case SDL_BUTTON_LEFT:
					{
						mouseDown = true;
						TouchInput input;
						input.x = mx;
						input.y = my;
						input.flags = TOUCH_DOWN | TOUCH_MOUSE;
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
				}
				break;
			case SDL_MOUSEWHEEL:
				{
					KeyInput key;
					key.deviceId = DEVICE_ID_MOUSE;
					if (event.wheel.y > 0) {
						key.keyCode = NKCODE_EXT_MOUSEWHEEL_UP;
					} else {
						key.keyCode = NKCODE_EXT_MOUSEWHEEL_DOWN;
					}
					key.flags = KEY_DOWN;
					NativeKey(key);

					// SDL2 doesn't consider the mousewheel a button anymore
					// so let's send the KEY_UP right away.
					// Maybe KEY_UP alone will suffice?
					key.flags = KEY_UP;
					NativeKey(key);
				}
			case SDL_MOUSEMOTION:
				if (mouseDown) {
					TouchInput input;
					input.x = mx;
					input.y = my;
					input.flags = TOUCH_MOVE | TOUCH_MOUSE;
					input.id = 0;
					NativeTouch(input);
				}
				break;
			case SDL_MOUSEBUTTONUP:
				switch (event.button.button) {
				case SDL_BUTTON_LEFT:
					{
						mouseDown = false;
						TouchInput input;
						input.x = mx;
						input.y = my;
						input.flags = TOUCH_UP | TOUCH_MOUSE;
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
				}
				break;
			default:
				if (joystick) {
					joystick->ProcessInput(event);
				}
				break;
			}
		}
		if (g_QuitRequested)
			break;
		const uint8_t *keys = SDL_GetKeyboardState(NULL);
		if (emuThreadState == (int)EmuThreadState::DISABLED) {
			UpdateRunLoop();
		}
		if (g_QuitRequested)
			break;
#if !defined(MOBILE_DEVICE)
		if (lastUIState != GetUIState()) {
			lastUIState = GetUIState();
			if (lastUIState == UISTATE_INGAME && g_Config.bFullScreen && !g_Config.bShowTouchControls)
				SDL_ShowCursor(SDL_DISABLE);
			if (lastUIState != UISTATE_INGAME && g_Config.bFullScreen)
				SDL_ShowCursor(SDL_ENABLE);
		}
#endif

		if (framecount % 60 == 0) {
			// glsl_refresh(); // auto-reloads modified GLSL shaders once per second.
		}

		if (emuThreadState != (int)EmuThreadState::DISABLED) {
			if (!graphicsContext->ThreadFrame())
				break;
		}
		graphicsContext->SwapBuffers();

		ToggleFullScreenIfFlagSet(window);
		time_update();
		framecount++;
	}

	if (useEmuThread) {
		EmuThreadStop();
		while (emuThreadState != (int)EmuThreadState::STOPPED) {
			// Need to keep eating frames to allow the EmuThread to exit correctly.
			graphicsContext->ThreadFrame();
		}
		EmuThreadJoin();
	}

	delete joystick;

	if (!useEmuThread) {
		NativeShutdownGraphics();
	}
	graphicsContext->Shutdown();
	graphicsContext->ThreadEnd();
	graphicsContext->ShutdownFromRenderThread();

	NativeShutdown();
	delete graphicsContext;

	SDL_PauseAudio(1);
	SDL_CloseAudio();
	SDL_Quit();
#if PPSSPP_PLATFORM(RPI)
	bcm_host_deinit();
#endif

	glslang::FinalizeProcess();
	ILOG("Leaving main");
	return 0;
}
