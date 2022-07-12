// SDL/EGL implementation of the framework.
// This is quite messy due to platform-specific implementations and #ifdef's.
// If your platform is not supported, it is suggested to use Qt instead.

#include <cstdlib>
#include <unistd.h>
#include <pwd.h>

#include "ppsspp_config.h"
#include "SDL.h"
#include "SDL/SDLJoystick.h"
SDLJoystick *joystick = NULL;

#if PPSSPP_PLATFORM(RPI)
#include <bcm_host.h>
#endif

#include <atomic>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <thread>
#include <locale>

#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "ext/glslang/glslang/Public/ShaderLang.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/Net/Resolve.h"
#include "NKCodeFromSDL.h"
#include "Common/Math/math_util.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/Profiler/Profiler.h"

#include "SDL_syswm.h"

#if defined(VK_USE_PLATFORM_XLIB_KHR)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#elif defined(VK_USE_PLATFORM_XCB_KHR)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlib-xcb.h>
#endif

#include "Common/GraphicsContext.h"
#include "Common/TimeUtil.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Data/Collections/ConstMap.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/System.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "SDLGLGraphicsContext.h"
#include "SDLVulkanGraphicsContext.h"


GlobalUIState lastUIState = UISTATE_MENU;
GlobalUIState GetUIState();

static bool g_ToggleFullScreenNextFrame = false;
static int g_ToggleFullScreenType;
static int g_QuitRequested = 0;

static int g_DesktopWidth = 0;
static int g_DesktopHeight = 0;
static float g_RefreshRate = 60.f;

static SDL_AudioSpec g_retFmt;

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

void sdl_mixaudio_callback(void *userdata, Uint8 *stream, int len) {
	NativeMix((short *)stream, len / (2 * 2));
}

static SDL_AudioDeviceID audioDev = 0;

// Must be called after NativeInit().
static void InitSDLAudioDevice(const std::string &name = "") {
	SDL_AudioSpec fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.freq = 44100;
	fmt.format = AUDIO_S16;
	fmt.channels = 2;
	fmt.samples = 1024;
	fmt.callback = &sdl_mixaudio_callback;
	fmt.userdata = nullptr;

	std::string startDevice = name;
	if (startDevice.empty()) {
		startDevice = g_Config.sAudioDevice;
	}

	audioDev = 0;
	if (!startDevice.empty()) {
		audioDev = SDL_OpenAudioDevice(startDevice.c_str(), 0, &fmt, &g_retFmt, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
		if (audioDev <= 0) {
			WARN_LOG(AUDIO, "Failed to open audio device: %s", startDevice.c_str());
		}
	}
	if (audioDev <= 0) {
		INFO_LOG(AUDIO, "SDL: Trying a different audio device");
		audioDev = SDL_OpenAudioDevice(nullptr, 0, &fmt, &g_retFmt, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	}
	if (audioDev <= 0) {
		ERROR_LOG(AUDIO, "Failed to open audio device: %s", SDL_GetError());
	} else {
		if (g_retFmt.samples != fmt.samples) // Notify, but still use it
			ERROR_LOG(AUDIO, "Output audio samples: %d (requested: %d)", g_retFmt.samples, fmt.samples);
		if (g_retFmt.format != fmt.format || g_retFmt.channels != fmt.channels) {
			ERROR_LOG(AUDIO, "Sound buffer format does not match requested format.");
			ERROR_LOG(AUDIO, "Output audio freq: %d (requested: %d)", g_retFmt.freq, fmt.freq);
			ERROR_LOG(AUDIO, "Output audio format: %d (requested: %d)", g_retFmt.format, fmt.format);
			ERROR_LOG(AUDIO, "Output audio channels: %d (requested: %d)", g_retFmt.channels, fmt.channels);
			ERROR_LOG(AUDIO, "Provided output format does not match requirement, turning audio off");
			SDL_CloseAudioDevice(audioDev);
		}
		SDL_PauseAudioDevice(audioDev, 0);
	}
}

static void StopSDLAudioDevice() {
	if (audioDev > 0) {
		SDL_PauseAudioDevice(audioDev, 1);
		SDL_CloseAudioDevice(audioDev);
	}
}

// Simple implementations of System functions


void System_Toast(const char *text) {
#ifdef _WIN32
	std::wstring str = ConvertUTF8ToWString(text);
	MessageBox(0, str.c_str(), L"Toast!", MB_ICONINFORMATION);
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
	} else if (!strcmp(command, "graphics_restart")) {
		// Not sure how we best do this, but do a clean exit, better than being stuck in a bad state.
		g_QuitRequested = true;
	} else if (!strcmp(command, "setclipboardtext")) {
		SDL_SetClipboardText(parameter);
	} else if (!strcmp(command, "audio_resetDevice")) {
		StopSDLAudioDevice();
		InitSDLAudioDevice();
	}
}

void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

void OpenDirectory(const char *path) {
#if PPSSPP_PLATFORM(WINDOWS)
	SFGAOF flags;
	PIDLIST_ABSOLUTE pidl = nullptr;
	HRESULT hr = SHParseDisplayName(ConvertUTF8ToWString(ReplaceAll(path, "/", "\\")).c_str(), nullptr, &pidl, 0, &flags);
	if (pidl) {
		if (SUCCEEDED(hr))
			SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
		CoTaskMemFree(pidl);
	}
#elif PPSSPP_PLATFORM(MAC) || (PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID))
	pid_t pid = fork();
	if (pid < 0)
		return;

	if (pid == 0) {
#if PPSSPP_PLATFORM(MAC)
		execlp("open", "open", path, nullptr);
#else
		execlp("xdg-open", "xdg-open", path, nullptr);
#endif
		exit(1);
	}
#endif
}

void LaunchBrowser(const char *url) {
#if PPSSPP_PLATFORM(SWITCH)
	Uuid uuid = { 0 };
	WebWifiConfig conf;
	webWifiCreate(&conf, NULL, url, uuid, 0);
	webWifiShow(&conf, NULL);
#elif defined(MOBILE_DEVICE)
	INFO_LOG(SYSTEM, "Would have gone to %s but LaunchBrowser is not implemented on this platform", url);
#elif defined(_WIN32)
	std::wstring wurl = ConvertUTF8ToWString(url);
	ShellExecute(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
	std::string command = std::string("open ") + url;
	system(command.c_str());
#else
	std::string command = std::string("xdg-open ") + url;
	int err = system(command.c_str());
	if (err) {
		INFO_LOG(SYSTEM, "Would have gone to %s but xdg-utils seems not to be installed", url);
	}
#endif
}

void LaunchMarket(const char *url) {
#if PPSSPP_PLATFORM(SWITCH)
	Uuid uuid = { 0 };
	WebWifiConfig conf;
	webWifiCreate(&conf, NULL, url, uuid, 0);
	webWifiShow(&conf, NULL);
#elif defined(MOBILE_DEVICE)
	INFO_LOG(SYSTEM, "Would have gone to %s but LaunchMarket is not implemented on this platform", url);
#elif defined(_WIN32)
	std::wstring wurl = ConvertUTF8ToWString(url);
	ShellExecute(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
	std::string command = std::string("open ") + url;
	system(command.c_str());
#else
	std::string command = std::string("xdg-open ") + url;
	int err = system(command.c_str());
	if (err) {
		INFO_LOG(SYSTEM, "Would have gone to %s but xdg-utils seems not to be installed", url);
	}
#endif
}

void LaunchEmail(const char *email_address) {
#if defined(MOBILE_DEVICE)
	INFO_LOG(SYSTEM, "Would have opened your email client for %s but LaunchEmail is not implemented on this platform", email_address);
#elif defined(_WIN32)
	std::wstring mailto = std::wstring(L"mailto:") + ConvertUTF8ToWString(email_address);
	ShellExecute(NULL, L"open", mailto.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
	std::string command = std::string("open mailto:") + email_address;
	system(command.c_str());
#else
	std::string command = std::string("xdg-email ") + email_address;
	int err = system(command.c_str());
	if (err) {
		INFO_LOG(SYSTEM, "Would have gone to %s but xdg-utils seems not to be installed", email_address);
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
		return "SDL:macOS";
#elif PPSSPP_PLATFORM(SWITCH)
		return "SDL:Horizon";
#else
		return "SDL:";
#endif
	case SYSPROP_LANGREGION: {
		// Get user-preferred locale from OS
		setlocale(LC_ALL, "");
		std::string locale(setlocale(LC_ALL, NULL));
		// Set c and c++ strings back to POSIX
		std::locale::global(std::locale("POSIX"));
		if (!locale.empty()) {
			// Technically, this is an opaque string, but try to find the locale code.
			size_t messagesPos = locale.find("LC_MESSAGES=");
			if (messagesPos != std::string::npos) {
				messagesPos += strlen("LC_MESSAGES=");
				size_t semi = locale.find(';', messagesPos);
				locale = locale.substr(messagesPos, semi - messagesPos);
			}

			if (locale.find("_", 0) != std::string::npos) {
				if (locale.find(".", 0) != std::string::npos) {
					return locale.substr(0, locale.find(".",0));
				}
				return locale;
			}
		}
		return "en_US";
	}
	case SYSPROP_CLIPBOARD_TEXT:
		return SDL_HasClipboardText() ? SDL_GetClipboardText() : "";
	case SYSPROP_AUDIO_DEVICE_LIST:
		{
			std::string result;
			for (int i = 0; i < SDL_GetNumAudioDevices(0); ++i) {
				const char *name = SDL_GetAudioDeviceName(i, 0);
				if (!name) {
					continue;
				}

				if (i == 0) {
					result = name;
				} else {
					result.append(1, '\0');
					result.append(name);
				}
			}
			return result;
		}
	default:
		return "";
	}
}

std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop) {
	std::vector<std::string> result;

	switch (prop) {
	case SYSPROP_TEMP_DIRS:
		if (getenv("TMPDIR") && strlen(getenv("TMPDIR")) != 0)
			result.push_back(getenv("TMPDIR"));
		if (getenv("TMP") && strlen(getenv("TMP")) != 0)
			result.push_back(getenv("TMP"));
		if (getenv("TEMP") && strlen(getenv("TEMP")) != 0)
			result.push_back(getenv("TEMP"));
		return result;

	default:
		return result;
	}
}

int System_GetPropertyInt(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_AUDIO_SAMPLE_RATE:
		return g_retFmt.freq;
	case SYSPROP_AUDIO_FRAMES_PER_BUFFER:
		return g_retFmt.samples;
	case SYSPROP_DEVICE_TYPE:
#if defined(MOBILE_DEVICE)
		return DEVICE_TYPE_MOBILE;
#else
		return DEVICE_TYPE_DESKTOP;
#endif
	case SYSPROP_DISPLAY_COUNT:
		return SDL_GetNumVideoDisplays();
	case SYSPROP_KEYBOARD_LAYOUT:
	{
		char q, w, y;
		q = SDL_GetKeyFromScancode(SDL_SCANCODE_Q);
		w = SDL_GetKeyFromScancode(SDL_SCANCODE_W);
		y = SDL_GetKeyFromScancode(SDL_SCANCODE_Y);
		if (q == 'a' && w == 'z' && y == 'y')
			return KEYBOARD_LAYOUT_AZERTY;
		else if (q == 'q' && w == 'w' && y == 'z')
			return KEYBOARD_LAYOUT_QWERTZ;
		return KEYBOARD_LAYOUT_QWERTY;
	}
	default:
		return -1;
	}
}

float System_GetPropertyFloat(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return g_RefreshRate;
	case SYSPROP_DISPLAY_SAFE_INSET_LEFT:
	case SYSPROP_DISPLAY_SAFE_INSET_RIGHT:
	case SYSPROP_DISPLAY_SAFE_INSET_TOP:
	case SYSPROP_DISPLAY_SAFE_INSET_BOTTOM:
		return 0.0f;
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
	case SYSPROP_CAN_JIT:
		return true;
	case SYSPROP_SUPPORTS_OPEN_FILE_IN_EDITOR:
		return true;  // FileUtil.cpp: OpenFileInEditor

	default:
		return false;
	}
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
	SetCurrentThreadName("Emu");

	// There's no real requirement that NativeInit happen on this thread.
	// We just call the update/render loop here.
	emuThreadState = (int)EmuThreadState::RUNNING;

	NativeInitGraphics(graphicsContext);

	while (emuThreadState != (int)EmuThreadState::QUIT_REQUESTED) {
		UpdateRunLoop();
	}
	emuThreadState = (int)EmuThreadState::STOPPED;
	graphicsContext->StopThread();

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
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) {
			printf("%s\n", PPSSPP_GIT_VERSION);
			return 0;
		}
	}

#ifdef HAVE_LIBNX
	socketInitializeDefault();
	nxlinkStdio();
#else // HAVE_LIBNX
	// Ignore sigpipe.
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		perror("Unable to ignore SIGPIPE");
	}
#endif // HAVE_LIBNX

	PROFILE_INIT();
	glslang::InitializeProcess();

#if PPSSPP_PLATFORM(RPI)
	bcm_host_init();
#endif
	putenv((char*)"SDL_VIDEO_CENTERED=1");
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

#ifdef SDL_HINT_TOUCH_MOUSE_EVENTS
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif

	if (VulkanMayBeAvailable()) {
		printf("DEBUG: Vulkan might be available.\n");
	} else {
		printf("DEBUG: Vulkan is not available, not using Vulkan.\n");
	}

	SDL_version compiled;
	SDL_version linked;
	int set_xres = -1;
	int set_yres = -1;
	int w = 0, h = 0;
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

	SDL_VERSION(&compiled);
	SDL_GetVersion(&linked);
	printf("Info: We compiled against SDL version %d.%d.%d", compiled.major, compiled.minor, compiled.patch);
	if (compiled.minor != linked.minor || compiled.patch != linked.patch) {
		printf(", but we are linking against SDL version %d.%d.%d., be aware that this can lead to unexpected behaviors\n", linked.major, linked.minor, linked.patch);
	} else {
		printf(" and we are linking against SDL version %d.%d.%d. :)\n", linked.major, linked.minor, linked.patch);
	}

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
	g_RefreshRate = displayMode.refresh_rate;

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	// Force fullscreen if the resolution is too low to run windowed.
	if (g_DesktopWidth < 480 * 2 && g_DesktopHeight < 272 * 2) {
		mode |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	// If we're on mobile, don't try for windowed either.
#if defined(MOBILE_DEVICE) && !PPSSPP_PLATFORM(SWITCH)
	mode |= SDL_WINDOW_FULLSCREEN;
#elif defined(USING_FBDEV) || PPSSPP_PLATFORM(SWITCH)
	mode |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#else
	mode |= SDL_WINDOW_RESIZABLE;
#endif

	if (mode & SDL_WINDOW_FULLSCREEN_DESKTOP) {
		pixel_xres = g_DesktopWidth;
		pixel_yres = g_DesktopHeight;
		g_Config.iForceFullScreen = 1;
	} else {
		// set a sensible default resolution (2x)
		pixel_xres = 480 * 2 * set_scale;
		pixel_yres = 272 * 2 * set_scale;
		if (portrait) {
			std::swap(pixel_xres, pixel_yres);
		}
		g_Config.iForceFullScreen = 0;
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
#if PPSSPP_PLATFORM(SWITCH)
	strcpy(path, "/switch/ppsspp/");
#else
	const char *the_path = getenv("HOME");
	if (!the_path) {
		struct passwd *pwd = getpwuid(getuid());
		if (pwd)
			the_path = pwd->pw_dir;
	}
	if (the_path)
		strcpy(path, the_path);
#endif
	if (strlen(path) > 0 && path[strlen(path) - 1] != '/')
		strcat(path, "/");

#if PPSSPP_PLATFORM(MAC)
	std::string external_dir_str;
	if (SDL_GetBasePath())
		external_dir_str = std::string(SDL_GetBasePath()) + "/assets";
	else
		external_dir_str = "/tmp";
	const char *external_dir = external_dir_str.c_str();
#else
	const char *external_dir = "/tmp";
#endif
	NativeInit(remain_argc, (const char **)remain_argv, path, external_dir, nullptr);

	// Use the setting from the config when initing the window.
	if (g_Config.UseFullScreen())
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
#if !PPSSPP_PLATFORM(SWITCH)
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
#endif
	}

	bool useEmuThread = g_Config.iGPUBackend == (int)GPUBackend::OPENGL;

	SDL_SetWindowTitle(window, (app_name_nice + " " + PPSSPP_GIT_VERSION).c_str());

	char iconPath[PATH_MAX];
	snprintf(iconPath, PATH_MAX, "%sassets/icon_regular_72.png", SDL_GetBasePath() ? SDL_GetBasePath() : "");
	int width = 0, height = 0;
	unsigned char *imageData;
	if (pngLoad(iconPath, &width, &height, &imageData) == 1) {
		SDL_Surface *surface = SDL_CreateRGBSurface(0, width, height, 32,
							0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
		memcpy(surface->pixels, imageData, width*height*4);
		SDL_SetWindowIcon(window, surface);
		SDL_FreeSurface(surface);
		free(imageData);
		imageData = NULL;
	}

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

	// Ensure that the swap interval is set after context creation (needed for kmsdrm)
	SDL_GL_SetSwapInterval(1);

	InitSDLAudioDevice();

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

	float mouseDeltaX = 0;
	float mouseDeltaY = 0;
	int mouseWheelMovedUpFrames = 0;
	int mouseWheelMovedDownFrames = 0;
	bool mouseCaptured = false;
	bool windowHidden = false;
	while (true) {
		double startTime = time_now_d();

		// SDL2 doesn't consider the mousewheel a button anymore
		// so let's send the KEY_UP if it was moved after some frames
		if (mouseWheelMovedUpFrames > 0) {
			mouseWheelMovedUpFrames--;
			if (mouseWheelMovedUpFrames == 0) {
				KeyInput key;
				key.deviceId = DEVICE_ID_MOUSE;
				key.keyCode = NKCODE_EXT_MOUSEWHEEL_UP;
				key.flags = KEY_UP;
				NativeKey(key);
			}
		}
		if (mouseWheelMovedDownFrames > 0) {
			mouseWheelMovedDownFrames--;
			if (mouseWheelMovedDownFrames == 0) {
				KeyInput key;
				key.deviceId = DEVICE_ID_MOUSE;
				key.keyCode = NKCODE_EXT_MOUSEWHEEL_DOWN;
				key.flags = KEY_UP;
				NativeKey(key);
			}
		}
		SDL_Event event, touchEvent;
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
				case SDL_WINDOWEVENT_SIZE_CHANGED:  // better than RESIZED, more general
				{
					int new_width = event.window.data1;
					int new_height = event.window.data2;

					windowHidden = false;
					Core_NotifyWindowHidden(windowHidden);

					Uint32 window_flags = SDL_GetWindowFlags(window);
					bool fullscreen = (window_flags & SDL_WINDOW_FULLSCREEN);

					// This one calls NativeResized if the size changed.
					UpdateScreenScale(new_width, new_height);

					// Set variable here in case fullscreen was toggled by hotkey
					if (g_Config.UseFullScreen() != fullscreen) {
						g_Config.bFullScreen = fullscreen;
						g_Config.iForceFullScreen = -1;
					}

					// Hide/Show cursor correctly toggling fullscreen
					if (lastUIState == UISTATE_INGAME && fullscreen && !g_Config.bShowTouchControls) {
						SDL_ShowCursor(SDL_DISABLE);
					} else if (lastUIState != UISTATE_INGAME || !fullscreen) {
						SDL_ShowCursor(SDL_ENABLE);
					}
					break;
				}

				case SDL_WINDOWEVENT_MINIMIZED:
				case SDL_WINDOWEVENT_HIDDEN:
					windowHidden = true;
					Core_NotifyWindowHidden(windowHidden);
					break;
				case SDL_WINDOWEVENT_EXPOSED:
				case SDL_WINDOWEVENT_SHOWN:
					windowHidden = false;
					Core_NotifyWindowHidden(windowHidden);
					break;
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
// This behavior doesn't feel right on a macbook with a touchpad.
#if !PPSSPP_PLATFORM(MAC)
			case SDL_FINGERMOTION:
				{
					SDL_GetWindowSize(window, &w, &h);
					TouchInput input;
					input.id = event.tfinger.fingerId;
					input.x = event.tfinger.x * w;
					input.y = event.tfinger.y * h;
					input.flags = TOUCH_MOVE;
					input.timestamp = event.tfinger.timestamp;
					NativeTouch(input);
					break;
				}
			case SDL_FINGERDOWN:
				{
					SDL_GetWindowSize(window, &w, &h);
					TouchInput input;
					input.id = event.tfinger.fingerId;
					input.x = event.tfinger.x * w;
					input.y = event.tfinger.y * h;
					input.flags = TOUCH_DOWN;
					input.timestamp = event.tfinger.timestamp;
					NativeTouch(input);

					KeyInput key;
					key.deviceId = DEVICE_ID_MOUSE;
					key.keyCode = NKCODE_EXT_MOUSEBUTTON_1;
					key.flags = KEY_DOWN;
					NativeKey(key);
					break;
				}
			case SDL_FINGERUP:
				{
					SDL_GetWindowSize(window, &w, &h);
					TouchInput input;
					input.id = event.tfinger.fingerId;
					input.x = event.tfinger.x * w;
					input.y = event.tfinger.y * h;
					input.flags = TOUCH_UP;
					input.timestamp = event.tfinger.timestamp;
					NativeTouch(input);

					KeyInput key;
					key.deviceId = DEVICE_ID_MOUSE;
					key.keyCode = NKCODE_EXT_MOUSEBUTTON_1;
					key.flags = KEY_UP;
					NativeKey(key);
					break;
				}
#endif
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
				case SDL_BUTTON_MIDDLE:
					{
						KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_3, KEY_DOWN);
						NativeKey(key);
					}
					break;
				case SDL_BUTTON_X1:
					{
						KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_4, KEY_DOWN);
						NativeKey(key);
					}
					break;
				case SDL_BUTTON_X2:
					{
						KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_5, KEY_DOWN);
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
						mouseWheelMovedUpFrames = 5;
					} else {
						key.keyCode = NKCODE_EXT_MOUSEWHEEL_DOWN;
						mouseWheelMovedDownFrames = 5;
					}
					key.flags = KEY_DOWN;
					NativeKey(key);
				}
				break;
			case SDL_MOUSEMOTION:
				if (mouseDown) {
					TouchInput input;
					input.x = mx;
					input.y = my;
					input.flags = TOUCH_MOVE | TOUCH_MOUSE;
					input.id = 0;
					NativeTouch(input);
				}
				mouseDeltaX += event.motion.xrel;
				mouseDeltaY += event.motion.yrel;
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
				case SDL_BUTTON_MIDDLE:
					{
						KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_3, KEY_UP);
						NativeKey(key);
					}
					break;
				case SDL_BUTTON_X1:
					{
						KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_4, KEY_UP);
						NativeKey(key);
					}
					break;
				case SDL_BUTTON_X2:
					{
						KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_5, KEY_UP);
						NativeKey(key);
					}
					break;
				}
				break;

#if SDL_VERSION_ATLEAST(2, 0, 4)
			case SDL_AUDIODEVICEADDED:
				// Automatically switch to the new device.
				if (event.adevice.iscapture == 0) {
					const char *name = SDL_GetAudioDeviceName(event.adevice.which, 0);
					if (!name) {
						break;
					}
					// Don't start auto switching for a second, because some devices init on start.
					bool doAutoSwitch = g_Config.bAutoAudioDevice && framecount > 60;
					if (doAutoSwitch || g_Config.sAudioDevice == name) {
						StopSDLAudioDevice();
						InitSDLAudioDevice(name ? name : "");
					}
				}
				break;
			case SDL_AUDIODEVICEREMOVED:
				if (event.adevice.iscapture == 0 && event.adevice.which == audioDev) {
					StopSDLAudioDevice();
					InitSDLAudioDevice();
				}
				break;
#endif

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
			if (lastUIState == UISTATE_INGAME && g_Config.UseFullScreen() && !g_Config.bShowTouchControls)
				SDL_ShowCursor(SDL_DISABLE);
			if (lastUIState != UISTATE_INGAME || !g_Config.UseFullScreen())
				SDL_ShowCursor(SDL_ENABLE);
		}
#endif

		// Disabled by default, needs a workaround to map to psp keys.
		if (g_Config.bMouseControl) {
			float scaleFactor_x = g_dpi_scale_x * 0.1 * g_Config.fMouseSensitivity;
			float scaleFactor_y = g_dpi_scale_y * 0.1 * g_Config.fMouseSensitivity;

			AxisInput axisX, axisY;
			axisX.axisId = JOYSTICK_AXIS_MOUSE_REL_X;
			axisX.deviceId = DEVICE_ID_MOUSE;
			axisX.value = std::max(-1.0f, std::min(1.0f, mouseDeltaX * scaleFactor_x));
			axisY.axisId = JOYSTICK_AXIS_MOUSE_REL_Y;
			axisY.deviceId = DEVICE_ID_MOUSE;
			axisY.value = std::max(-1.0f, std::min(1.0f, mouseDeltaY * scaleFactor_y));

			if (GetUIState() == UISTATE_INGAME || g_Config.bMapMouse) {
				NativeAxis(axisX);
				NativeAxis(axisY);
			}
			mouseDeltaX *= g_Config.fMouseSmoothing;
			mouseDeltaY *= g_Config.fMouseSmoothing;
		}
		bool captureMouseCondition = g_Config.bMouseControl && ((GetUIState() == UISTATE_INGAME && g_Config.bMouseConfine) || g_Config.bMapMouse);
		if (mouseCaptured != captureMouseCondition) {
			mouseCaptured = captureMouseCondition;
			if (captureMouseCondition)
				SDL_SetRelativeMouseMode(SDL_TRUE);
			else
				SDL_SetRelativeMouseMode(SDL_FALSE);
		}

		if (framecount % 60 == 0) {
			// glsl_refresh(); // auto-reloads modified GLSL shaders once per second.
		}

		bool renderThreadPaused = windowHidden && g_Config.bPauseWhenMinimized && emuThreadState != (int)EmuThreadState::DISABLED;
		if (emuThreadState != (int)EmuThreadState::DISABLED && !renderThreadPaused) {
			if (!graphicsContext->ThreadFrame())
				break;
		}


		graphicsContext->SwapBuffers();

		ToggleFullScreenIfFlagSet(window);

		// Simple throttling to not burn the GPU in the menu.
		if (GetUIState() != UISTATE_INGAME || !PSP_IsInited() || renderThreadPaused) {
			double diffTime = time_now_d() - startTime;
			int sleepTime = (int)(1000.0 / 60.0) - (int)(diffTime * 1000.0);
			if (sleepTime > 0)
				sleep_ms(sleepTime);
		}

		framecount++;
	}

	if (useEmuThread) {
		EmuThreadStop();
		while (graphicsContext->ThreadFrame()) {
			// Need to keep eating frames to allow the EmuThread to exit correctly.
			continue;
		}
		EmuThreadJoin();
	}

	delete joystick;

	if (!useEmuThread) {
		NativeShutdownGraphics();
	}
	graphicsContext->ThreadEnd();

	NativeShutdown();

	// Destroys Draw, which is used in NativeShutdown to shutdown.
	graphicsContext->ShutdownFromRenderThread();
	graphicsContext->Shutdown();
	delete graphicsContext;

	if (audioDev > 0) {
		SDL_PauseAudioDevice(audioDev, 1);
		SDL_CloseAudioDevice(audioDev);
	}
	SDL_Quit();
#if PPSSPP_PLATFORM(RPI)
	bcm_host_deinit();
#endif

	glslang::FinalizeProcess();
	printf("Leaving main");
#ifdef HAVE_LIBNX
	socketExit();
#endif
	return 0;
}
