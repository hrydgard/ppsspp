#include <cstdlib>
#include <unistd.h>
#include <pwd.h>

#include "ppsspp_config.h"
#if PPSSPP_PLATFORM(MAC)
#include "SDL2/SDL.h"
#include "SDL2/SDL_syswm.h"
#include "SDL2/SDL_mouse.h"
#else
#include "SDL.h"
#include "SDL_syswm.h"
#include "SDL_mouse.h"
#endif
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

#include "ext/portable-file-dialogs/portable-file-dialogs.h"

#include "ext/imgui/imgui.h"
#include "ext/imgui/imgui_impl_platform.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/System/NativeApp.h"
#include "Common/Audio/AudioBackend.h"
#include "ext/glslang/glslang/Public/ShaderLang.h"
#include "Common/Data/Format/PNGLoad.h"
#include "Common/Net/Resolve.h"
#include "Common/File/FileUtil.h"
#include "NKCodeFromSDL.h"
#include "Common/Math/math_util.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/Profiler/Profiler.h"
#include "Common/Log/LogManager.h"

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
#include "Common/StringUtils.h"
#include "Core/System.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "SDLGLGraphicsContext.h"
#include "SDLVulkanGraphicsContext.h"

#if PPSSPP_PLATFORM(MAC)
#include "SDL2/SDL_vulkan.h"
#else
#include "SDL_vulkan.h"
#endif

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
#include "UI/DarwinFileSystemServices.h"
#endif

#if PPSSPP_PLATFORM(MAC)
#include "CocoaBarItems.h"
#endif

#if PPSSPP_PLATFORM(SWITCH)
#define LIBNX_SWKBD_LIMIT 500 // enforced by HOS
extern u32 __nx_applet_type; // Not exposed through a header?
#endif

GlobalUIState lastUIState = UISTATE_MENU;
GlobalUIState GetUIState();

static bool g_QuitRequested = false;
static bool g_RestartRequested = false;

static int g_DesktopWidth = 0;
static int g_DesktopHeight = 0;
static float g_DesktopDPI = 1.0f;
static float g_ForcedDPI = 0.0f; // if this is 0.0f, use g_DesktopDPI
static float g_RefreshRate = 60.f;
static int g_sampleRate = 44100;

static bool g_rebootEmuThread = false;

static SDL_AudioSpec g_retFmt;

static bool g_textFocusChanged;
static bool g_textFocus;
double g_audioStartTime = 0.0;

// Window state to be transferred to the main SDL thread.
static std::mutex g_mutexWindow;
struct WindowState {
	std::string title;
	bool applyFullScreenNextFrame;
	bool clipboardDataAvailable;
	std::string clipboardString;
	bool update;
};
static WindowState g_windowState;

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
	NativeMix((short *)stream, len / (2 * 2), g_sampleRate, userdata);
}

static SDL_AudioDeviceID audioDev = 0;

// Must be called after NativeInit().
static void InitSDLAudioDevice(const std::string &name = "") {
	SDL_AudioSpec fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.freq = g_sampleRate;
	fmt.format = AUDIO_S16;
	fmt.channels = 2;
	fmt.samples = std::max(g_Config.iSDLAudioBufferSize, 128);
	fmt.callback = &sdl_mixaudio_callback;
	fmt.userdata = nullptr;

	std::string startDevice = name;
	if (startDevice.empty()) {
		startDevice = g_Config.sAudioDevice;
	}

	// List available audio devices before trying to open, for debugging purposes.
	const int deviceCount = SDL_GetNumAudioDevices(0);
	if (deviceCount > 0) {
		INFO_LOG(Log::Audio, "Available audio devices:");
		for (int i = 0; i < deviceCount; i++) {
			const char *deviceName = SDL_GetAudioDeviceName(i, 0);
			INFO_LOG(Log::Audio, " * '%s'", deviceName);
		}
	} else {
		INFO_LOG(Log::Audio, "Failed to list audio devices: retval=%d", deviceCount);
	}

	audioDev = 0;
	if (!startDevice.empty()) {
		INFO_LOG(Log::Audio, "Opening audio device: '%s'", startDevice.c_str());
		audioDev = SDL_OpenAudioDevice(startDevice.c_str(), 0, &fmt, &g_retFmt, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
		if (audioDev <= 0) {
			WARN_LOG(Log::Audio, "Failed to open audio device '%s'", startDevice.c_str());
		}
	}
	if (audioDev <= 0) {
		if (audioDev < 0) {
			WARN_LOG(Log::Audio, "SDL: Error: '%s'. Trying the default audio device", SDL_GetError());
		} else {
			INFO_LOG(Log::Audio, "Opening default audio device");
		}
		audioDev = SDL_OpenAudioDevice(nullptr, 0, &fmt, &g_retFmt, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	}
	if (audioDev <= 0) {
		ERROR_LOG(Log::Audio, "Failed to open audio device '%s', second try. Giving up.", SDL_GetError());
	} else {
		if (g_retFmt.samples != fmt.samples) // Notify, but still use it
			ERROR_LOG(Log::Audio, "Output audio samples: %d (requested: %d)", g_retFmt.samples, fmt.samples);
		if (g_retFmt.format != fmt.format || g_retFmt.channels != fmt.channels) {
			ERROR_LOG(Log::Audio, "Sound buffer format does not match requested format.");
			ERROR_LOG(Log::Audio, "Output audio freq: %d (requested: %d)", g_retFmt.freq, fmt.freq);
			ERROR_LOG(Log::Audio, "Output audio format: %d (requested: %d)", g_retFmt.format, fmt.format);
			ERROR_LOG(Log::Audio, "Output audio channels: %d (requested: %d)", g_retFmt.channels, fmt.channels);
			ERROR_LOG(Log::Audio, "Provided output format does not match requirement, turning audio off");
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

static void UpdateScreenDPI(SDL_Window *window) {
	int drawable_width, window_width, window_height;
	SDL_GetWindowSize(window, &window_width, &window_height);

	if (g_Config.iGPUBackend == (int)GPUBackend::OPENGL)
		SDL_GL_GetDrawableSize(window, &drawable_width, NULL);
	else if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN)
		SDL_Vulkan_GetDrawableSize(window, &drawable_width, NULL);
	else {
		// If we add SDL support for more platforms, we'll end up here.
		g_DesktopDPI = 1.0f;
		return;
	}
	// Round up a little otherwise there would be a gap sometimes
	// in fractional scaling
	g_DesktopDPI = ((float) drawable_width + 1.0f) / window_width;
}

// Simple implementations of System functions

void System_Toast(std::string_view text) {
#ifdef _WIN32
	std::wstring str = ConvertUTF8ToWString(text);
	MessageBox(0, str.c_str(), L"Toast!", MB_ICONINFORMATION);
#else
	fprintf(stderr, "%*.s", (int)text.length(), text.data());
#endif
}

void System_ShowKeyboard() {
	// Irrelevant on PC
}

void System_Vibrate(int length_ms) {
	// Ignore on PC
}

AudioBackend *System_CreateAudioBackend() {
	// Use legacy mechanisms.
	return nullptr;
}

static void InitializeFilters(std::vector<std::string> &filters, BrowseFileType type) {
	switch (type) {
	case BrowseFileType::BOOTABLE:
		filters.push_back("All supported file types (*.iso *.cso *.chd *.pbp *.elf *.prx *.zip *.ppdmp)");
		filters.push_back("*.pbp *.elf *.iso *.cso *.chd *.prx *.zip *.ppdmp");
		break;
	case BrowseFileType::INI:
		filters.push_back("Ini files");
		filters.push_back("*.ini");
		break;
	case BrowseFileType::ZIP:
		filters.push_back("ZIP files");
		filters.push_back("*.zip");
		break;
	case BrowseFileType::DB:
		filters.push_back("Cheat db files");
		filters.push_back("*.db");
		break;
	case BrowseFileType::SOUND_EFFECT:
		filters.push_back("Sound effect files (wav, mp3)");
		filters.push_back("*.wav *.mp3");
		break;
	case BrowseFileType::SYMBOL_MAP:
		filters.push_back("PPSSPP Symbol Map files (ppmap)");
		filters.push_back("*.ppmap");
		break;
	case BrowseFileType::SYMBOL_MAP_NOCASH:
		filters.push_back("No$ symbol Map files (sym)");
		filters.push_back("*.sym");
		break;
	case BrowseFileType::ATRAC3:
		filters.push_back("Atrac3 files (at3)");
		filters.push_back("*.at3");
		break;
	case BrowseFileType::IMAGE:
		filters.push_back("Pictures (jpg, png)");
		filters.push_back("*.jpg *.png");
		break;
	case BrowseFileType::ANY:
		break;
	}
	filters.push_back("All files (*.*)");
	filters.push_back("*");
}

bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int64_t param3, int64_t param4) {
	switch (type) {
	case SystemRequestType::RESTART_APP:
		g_RestartRequested = true;
		// TODO: Also save param1 and then split it into an argv.
		return true;
	case SystemRequestType::EXIT_APP:
		// Do a clean exit
		g_QuitRequested = true;
		return true;
#if PPSSPP_PLATFORM(SWITCH)
	case SystemRequestType::INPUT_TEXT_MODAL:
	{
		// swkbd only works on "real" titles
		if (__nx_applet_type != AppletType_Application && __nx_applet_type != AppletType_SystemApplication) {
			g_requestManager.PostSystemFailure(requestId);
			return true;
		}

		SwkbdConfig kbd;
		Result rc = swkbdCreate(&kbd, 0);

		if (R_SUCCEEDED(rc)) {
			char buf[LIBNX_SWKBD_LIMIT] = {'\0'};
			swkbdConfigMakePresetDefault(&kbd);

			swkbdConfigSetHeaderText(&kbd, param1.c_str());
			swkbdConfigSetInitialText(&kbd, param2.c_str());

			rc = swkbdShow(&kbd, buf, sizeof(buf));

			swkbdClose(&kbd);

			g_requestManager.PostSystemSuccess(requestId, buf);
			return true;
		}

		g_requestManager.PostSystemFailure(requestId);
		return true;
	}
#endif // PPSSPP_PLATFORM(SWITCH)
#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
	case SystemRequestType::BROWSE_FOR_FILE:
	{
		DarwinDirectoryPanelCallback callback = [requestId] (bool success, Path path) {
			if (success) {
				g_requestManager.PostSystemSuccess(requestId, path.c_str());
			} else {
				g_requestManager.PostSystemFailure(requestId);
			}
		};
		BrowseFileType fileType = (BrowseFileType)param3;
		DarwinFileSystemServices::presentDirectoryPanel(callback, /* allowFiles = */ true, /* allowDirectories = */ false, fileType);
		return true;
	}
	case SystemRequestType::BROWSE_FOR_IMAGE:
	{
		DarwinDirectoryPanelCallback callback = [requestId] (bool success, Path path) {
			if (success) {
				g_requestManager.PostSystemSuccess(requestId, path.c_str());
			} else {
				g_requestManager.PostSystemFailure(requestId);
			}
		};
		BrowseFileType fileType = BrowseFileType::IMAGE;
		DarwinFileSystemServices::presentDirectoryPanel(callback, /* allowFiles = */ true, /* allowDirectories = */ false, fileType);
		return true;
	}
	case SystemRequestType::BROWSE_FOR_FOLDER:
	{
		DarwinDirectoryPanelCallback callback = [requestId] (bool success, Path path) {
			if (success) {
				g_requestManager.PostSystemSuccess(requestId, path.c_str());
			} else {
				g_requestManager.PostSystemFailure(requestId);
			}
		};
		DarwinFileSystemServices::presentDirectoryPanel(callback, /* allowFiles = */ false, /* allowDirectories = */ true);
		return true;
	}
#else
	case SystemRequestType::BROWSE_FOR_IMAGE:
	{
		// TODO: Add non-blocking support.
		const std::string &title = param1;
		std::vector<std::string> filters;
		InitializeFilters(filters, BrowseFileType::IMAGE);
		std::vector<std::string> result = pfd::open_file(title, "", filters).result();
		if (!result.empty()) {
			g_requestManager.PostSystemSuccess(requestId, result[0]);
		} else {
			g_requestManager.PostSystemFailure(requestId);
		}
		return true;
	}
	case SystemRequestType::BROWSE_FOR_FILE:
	case SystemRequestType::BROWSE_FOR_FILE_SAVE:
	{
		// TODO: Add non-blocking support.
		const BrowseFileType browseType = (BrowseFileType)param3;
		std::string initialFilename = param2;
		const std::string &title = param1;
		std::vector<std::string> filters;
		InitializeFilters(filters, browseType);
		if (type == SystemRequestType::BROWSE_FOR_FILE) {
			std::vector<std::string> result = pfd::open_file(title, initialFilename, filters).result();
			if (!result.empty()) {
				g_requestManager.PostSystemSuccess(requestId, result[0]);
			} else {
				g_requestManager.PostSystemFailure(requestId);
			}
		} else {
			std::string result = pfd::save_file(title, initialFilename, filters).result();
			if (!result.empty()) {
				g_requestManager.PostSystemSuccess(requestId, result);
			} else {
				g_requestManager.PostSystemFailure(requestId);
			}
		}
		return true;
	}
	case SystemRequestType::BROWSE_FOR_FOLDER:
	{
		// TODO: Add non-blocking support.
		std::string result = pfd::select_folder(param1, param2).result();
		if (!result.empty()) {
			g_requestManager.PostSystemSuccess(requestId, result);
		} else {
			g_requestManager.PostSystemFailure(requestId);
		}
		return true;
	}
#endif
	case SystemRequestType::APPLY_FULLSCREEN_STATE:
	{
		std::lock_guard<std::mutex> guard(g_mutexWindow);
		g_windowState.update = true;
		g_windowState.applyFullScreenNextFrame = true;
		return true;
	}
	case SystemRequestType::SET_WINDOW_TITLE:
	{
		std::lock_guard<std::mutex> guard(g_mutexWindow);
		const char *app_name = System_GetPropertyBool(SYSPROP_APP_GOLD) ? "PPSSPP Gold" : "PPSSPP";
		g_windowState.title = param1.empty() ? app_name : param1;
		g_windowState.update = true;
		return true;
	}
	case SystemRequestType::COPY_TO_CLIPBOARD:
	{
		std::lock_guard<std::mutex> guard(g_mutexWindow);
		g_windowState.clipboardString = param1;
		g_windowState.clipboardDataAvailable = true;
		g_windowState.update = true;
		return true;
	}
	case SystemRequestType::SHOW_FILE_IN_FOLDER:
	{
#if PPSSPP_PLATFORM(WINDOWS)
		SFGAOF flags;
		PIDLIST_ABSOLUTE pidl = nullptr;
		HRESULT hr = SHParseDisplayName(ConvertUTF8ToWString(ReplaceAll(path, "/", "\\")).c_str(), nullptr, &pidl, 0, &flags);
		if (pidl) {
			if (SUCCEEDED(hr))
				SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
			CoTaskMemFree(pidl);
		}
#elif PPSSPP_PLATFORM(MAC)
		OSXShowInFinder(param1.c_str());
#elif (PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID))
		pid_t pid = fork();
		if (pid < 0)
			return true;

		if (pid == 0) {
			execlp("xdg-open", "xdg-open", param1.c_str(), nullptr);
			exit(1);
		}
#endif /* PPSSPP_PLATFORM(WINDOWS) */
		return true;
	}
	case SystemRequestType::NOTIFY_UI_EVENT:
	{
		switch ((UIEventNotification)param3) {
		case UIEventNotification::TEXT_GOTFOCUS:
			g_textFocus = true;
			g_textFocusChanged = true;
			break;
		case UIEventNotification::POPUP_CLOSED:
		case UIEventNotification::TEXT_LOSTFOCUS:
			g_textFocus = false;
			g_textFocusChanged = true;
			break;
		default:
			break;
		}
		return true;
	}
	case SystemRequestType::SET_KEEP_SCREEN_BRIGHT:
		INFO_LOG(Log::UI, "SET_KEEP_SCREEN_BRIGHT not implemented.");
		return true;
	default:
		INFO_LOG(Log::UI, "Unhandled system request %s", RequestTypeAsString(type));
		return false;
	}
}

void System_AskForPermission(SystemPermission permission) {}
PermissionStatus System_GetPermissionStatus(SystemPermission permission) { return PERMISSION_STATUS_GRANTED; }

void System_LaunchUrl(LaunchUrlType urlType, std::string_view url) {
	switch (urlType) {
	case LaunchUrlType::BROWSER_URL:
	case LaunchUrlType::MARKET_URL:
	{
#if PPSSPP_PLATFORM(SWITCH)
		Uuid uuid = { 0 };
		WebWifiConfig conf;
		webWifiCreate(&conf, NULL, std::string(url).c_str(), uuid, 0);
		webWifiShow(&conf, NULL);
#elif defined(MOBILE_DEVICE)
		INFO_LOG(Log::System, "Would have gone to %.*s but LaunchBrowser is not implemented on this platform", STR_VIEW(url));
#elif defined(_WIN32)
		std::wstring wurl = ConvertUTF8ToWString(url);
		ShellExecute(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
		OSXOpenURL(std::string(url).c_str());
#else
		std::string command = join("xdg-open ", url);
		int err = system(command.c_str());
		if (err) {
			INFO_LOG(Log::System, "Would have gone to %.*s but xdg-utils seems not to be installed", STR_VIEW(url));
		}
#endif
		break;
	}
	case LaunchUrlType::EMAIL_ADDRESS:
	{
#if defined(MOBILE_DEVICE)
		INFO_LOG(Log::System, "Would have opened your email client for %.*s but LaunchEmail is not implemented on this platform", STR_VIEW(url));
#elif defined(_WIN32)
		std::wstring mailto = std::wstring(L"mailto:") + ConvertUTF8ToWString(url);
		ShellExecute(NULL, L"open", mailto.c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
		OSXOpenURL(join("mailto:", url).c_str());
#else
		int err = system(join("xdg-email ", url).c_str());
		if (err) {
			INFO_LOG(Log::System, "Would have gone to %.*s but xdg-utils seems not to be installed", STR_VIEW(url));
		}
#endif
		break;
	}
	case LaunchUrlType::LOCAL_FILE:
	case LaunchUrlType::LOCAL_FOLDER:
#if defined(__APPLE__)
		// If it's a folder and we're on a mac, open it in finder.
		OSXShowInFinder(std::string(url).c_str());
#endif
		// INFO_LOG(Log::System, "LaunchUrlType::LOCAL_FILE not implemented on this platform");
		break;
	}
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
	case SYSPROP_BUILD_VERSION:
		return PPSSPP_GIT_VERSION;
	case SYSPROP_USER_DOCUMENTS_DIR:
	{
		const char *home = getenv("HOME");
		return home ? std::string(home) : "/";
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

#if PPSSPP_PLATFORM(MAC)
extern "C" {
int Apple_GetCurrentBatteryCapacity();
}
#endif

int64_t System_GetPropertyInt(SystemProperty prop) {
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
	case SYSPROP_DISPLAY_XRES:
		return g_DesktopWidth;
	case SYSPROP_DISPLAY_YRES:
		return g_DesktopHeight;
	case SYSPROP_BATTERY_PERCENTAGE:
#if PPSSPP_PLATFORM(MAC)
	// Let's keep using the old code on Mac for safety. Evaluate later if to be deleted.
		return Apple_GetCurrentBatteryCapacity();
#else
		{
			int seconds = 0;
			int percentage = 0;
			SDL_GetPowerInfo(&seconds, &percentage);
			return percentage;
		}
#endif
	default:
		return -1;
	}
}

float System_GetPropertyFloat(SystemProperty prop) {
	switch (prop) {
	case SYSPROP_DISPLAY_REFRESH_RATE:
		return g_RefreshRate;
	case SYSPROP_DISPLAY_DPI:
		return (g_ForcedDPI == 0.0f ? g_DesktopDPI : g_ForcedDPI) * 96.0;
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
	case SYSPROP_HAS_TEXT_CLIPBOARD:
	case SYSPROP_CAN_SHOW_FILE:
#if PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(MAC) || (PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID))
		return true;
#else
		return false;
#endif
	case SYSPROP_HAS_OPEN_DIRECTORY:
#if PPSSPP_PLATFORM(WINDOWS)
		return true;
#elif PPSSPP_PLATFORM(MAC) || (PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID))
		return true;
#endif
	case SYSPROP_HAS_BACK_BUTTON:
		return true;
#if PPSSPP_PLATFORM(SWITCH)
	case SYSPROP_HAS_TEXT_INPUT_DIALOG:
		return __nx_applet_type == AppletType_Application || __nx_applet_type != AppletType_SystemApplication;
#endif
	case SYSPROP_HAS_KEYBOARD:
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
#ifndef HTTPS_NOT_AVAILABLE
	case SYSPROP_SUPPORTS_HTTPS:
		return !g_Config.bDisableHTTPS;
#endif
case SYSPROP_HAS_FOLDER_BROWSER:
case SYSPROP_HAS_FILE_BROWSER:
#if PPSSPP_PLATFORM(MAC)
		return true;
#else
		return pfd::settings::available();
#endif
	case SYSPROP_HAS_ACCELEROMETER:
#if defined(MOBILE_DEVICE)
		return true;
#else
		return false;
#endif
	case SYSPROP_CAN_READ_BATTERY_PERCENTAGE:
		return true;
	case SYSPROP_ENOUGH_RAM_FOR_FULL_ISO:
#if PPSSPP_ARCH(64BIT) && !defined(MOBILE_DEVICE)
		return true;
#else
		return false;
#endif
	// hack for testing - do not commit
	case SYSPROP_USE_IAP:
		return false;
	default:
		return false;
	}
}

void System_Notify(SystemNotification notification) {
	switch (notification) {
	case SystemNotification::AUDIO_RESET_DEVICE:
		StopSDLAudioDevice();
		InitSDLAudioDevice();
		break;

	default:
		break;
	}
}

// returns -1 on failure
static int parseInt(const char *str) {
	int val;
	int retval = sscanf(str, "%d", &val);
	fprintf(stderr, "%i = scanf %s\n", retval, str);
	if (retval != 1) {
		return -1;
	} else {
		return val;
	}
}

static float parseFloat(const char *str) {
	float val;
	int retval = sscanf(str, "%f", &val);
	fprintf(stderr, "%i = sscanf %s\n", retval, str);
	if (retval != 1) {
		return -1.0f;
	} else {
		return val;
	}
}

void UpdateWindowState(SDL_Window *window) {
	SDL_SetWindowTitle(window, g_windowState.title.c_str());
	if (g_windowState.applyFullScreenNextFrame) {
		g_windowState.applyFullScreenNextFrame = false;

		Uint32 window_flags = SDL_GetWindowFlags(window);
		if (g_Config.bFullScreen) {
			window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
		} else {
			window_flags &= ~SDL_WINDOW_FULLSCREEN_DESKTOP;
		}
		SDL_SetWindowFullscreen(window, window_flags);
	}
	if (g_windowState.clipboardDataAvailable) {
		SDL_SetClipboardText(g_windowState.clipboardString.c_str());
		g_windowState.clipboardDataAvailable = false;
		g_windowState.clipboardString.clear();
	}
	g_windowState.update = false;
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
	SetCurrentThreadName("EmuThread");

	// There's no real requirement that NativeInit happen on this thread.
	// We just call the update/render loop here.
	emuThreadState = (int)EmuThreadState::RUNNING;

	NativeInitGraphics(graphicsContext);

	while (emuThreadState != (int)EmuThreadState::QUIT_REQUESTED) {
		NativeFrame(graphicsContext);
	}
	emuThreadState = (int)EmuThreadState::STOPPED;

	NativeShutdownGraphics();
}

static void EmuThreadStart(GraphicsContext *context) {
	emuThreadState = (int)EmuThreadState::START_REQUESTED;
	emuThread = std::thread(&EmuThreadFunc, context);
}

static void EmuThreadStop(const char *reason) {
	emuThreadState = (int)EmuThreadState::QUIT_REQUESTED;
}

static void EmuThreadJoin() {
	emuThread.join();
	emuThread = std::thread();
}

struct InputStateTracker {
	void MouseCaptureControl() {
		bool captureMouseCondition = g_Config.bMouseControl && ((GetUIState() == UISTATE_INGAME && g_Config.bMouseConfine) || g_IsMappingMouseInput);
		if (mouseCaptured != captureMouseCondition) {
			mouseCaptured = captureMouseCondition;
			if (captureMouseCondition)
				SDL_SetRelativeMouseMode(SDL_TRUE);
			else
				SDL_SetRelativeMouseMode(SDL_FALSE);
		}
	}

	int mouseDown;  // bitflags
	bool mouseCaptured;
};

SDL_Cursor *g_builtinCursors[SDL_NUM_SYSTEM_CURSORS];

static SDL_SystemCursor GetSDLCursorFromImgui(ImGuiMouseCursor cursor) {
	switch (cursor) {
	case ImGuiMouseCursor_Arrow:        return SDL_SYSTEM_CURSOR_ARROW; break;
	case ImGuiMouseCursor_TextInput:    return SDL_SYSTEM_CURSOR_IBEAM; break;
	case ImGuiMouseCursor_ResizeAll:    return SDL_SYSTEM_CURSOR_SIZEALL; break;
	case ImGuiMouseCursor_ResizeEW:     return SDL_SYSTEM_CURSOR_SIZEWE; break;
	case ImGuiMouseCursor_ResizeNS:     return SDL_SYSTEM_CURSOR_SIZENS; break;
	case ImGuiMouseCursor_ResizeNESW:   return SDL_SYSTEM_CURSOR_SIZENESW; break;
	case ImGuiMouseCursor_ResizeNWSE:   return SDL_SYSTEM_CURSOR_SIZENWSE; break;
	case ImGuiMouseCursor_Hand:         return SDL_SYSTEM_CURSOR_HAND; break;
	case ImGuiMouseCursor_NotAllowed:   return SDL_SYSTEM_CURSOR_NO; break;
	default:							return SDL_SYSTEM_CURSOR_ARROW; break;
	}
}

void UpdateCursor() {
	static SDL_SystemCursor curCursor = SDL_SYSTEM_CURSOR_ARROW;
	auto cursor = ImGui_ImplPlatform_GetCursor();
	SDL_SystemCursor sysCursor = GetSDLCursorFromImgui(cursor);
	if (sysCursor != curCursor) {
		curCursor = sysCursor;
		if (!g_builtinCursors[(int)curCursor]) {
			g_builtinCursors[(int)curCursor] = SDL_CreateSystemCursor(curCursor);
		}
	}
	SDL_SetCursor(g_builtinCursors[(int)curCursor]);
}

static void ProcessSDLEvent(SDL_Window *window, const SDL_Event &event, InputStateTracker *inputTracker) {
	// We have to juggle around 3 kinds of "DPI spaces" if a logical DPI is
	// provided (through --dpi, it is equal to system DPI if unspecified):
	// - SDL gives us motion events in "system DPI" points
	// - Native_UpdateScreenScale expects pixels, so in a way "96 DPI" points
	// - The UI code expects motion events in "logical DPI" points
	float mx = event.motion.x * g_DesktopDPI * g_display.dpi_scale_x;
	float my = event.motion.y * g_DesktopDPI * g_display.dpi_scale_x;

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

			// The size given by SDL is in point-units, convert these to
			// pixels before passing to Native_UpdateScreenScale()
			int new_width_px = new_width * g_DesktopDPI;
			int new_height_px = new_height * g_DesktopDPI;

			Native_NotifyWindowHidden(false);

			Uint32 window_flags = SDL_GetWindowFlags(window);
			bool fullscreen = (window_flags & SDL_WINDOW_FULLSCREEN);

			// This one calls NativeResized if the size changed.
			Native_UpdateScreenScale(new_width_px, new_height_px, UIScaleFactorToMultiplier(g_Config.iUIScaleFactor));

			// Set variable here in case fullscreen was toggled by hotkey
			if (g_Config.bFullScreen != fullscreen) {
				g_Config.bFullScreen = fullscreen;
			} else {
				// It is possible for the monitor to change DPI, so recalculate
				// DPI on each resize event.
				UpdateScreenDPI(window);
			}

			if (!g_Config.bFullScreen) {
				g_Config.iWindowWidth = new_width;
				g_Config.iWindowHeight = new_height;
			}
			// Hide/Show cursor correctly toggling fullscreen
			if (lastUIState == UISTATE_INGAME && fullscreen && !g_Config.bShowTouchControls) {
				SDL_ShowCursor(SDL_DISABLE);
			} else if (lastUIState != UISTATE_INGAME || !fullscreen) {
				SDL_ShowCursor(SDL_ENABLE);
			}
			break;
		}

		case SDL_WINDOWEVENT_MOVED:
		{
			Uint32 window_flags = SDL_GetWindowFlags(window);
			bool fullscreen = (window_flags & SDL_WINDOW_FULLSCREEN);
			if (!fullscreen) {
				g_Config.iWindowX = (int)event.window.data1;
				g_Config.iWindowY = (int)event.window.data2;
			}
			break;
		}

		case SDL_WINDOWEVENT_MINIMIZED:
		case SDL_WINDOWEVENT_HIDDEN:
			Native_NotifyWindowHidden(true);
			break;
		case SDL_WINDOWEVENT_EXPOSED:
		case SDL_WINDOWEVENT_SHOWN:
			Native_NotifyWindowHidden(false);
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
			key.flags = KeyInputFlags::DOWN;
			auto mapped = KeyMapRawSDLtoNative.find(k);
			if (mapped == KeyMapRawSDLtoNative.end() || mapped->second == NKCODE_UNKNOWN) {
				break;
			}
			key.keyCode = mapped->second;
			key.deviceId = DEVICE_ID_KEYBOARD;
			NativeKey(key);

#ifdef _DEBUG
			if (k == SDLK_F7) {
				fprintf(stderr, "f7 pressed - rebooting emuthread\n");
				g_rebootEmuThread = true;
			}
#endif
			// Convenience subset of what
			// "Enable standard shortcut keys"
			// does on Windows.
			if (g_Config.bSystemControls) {
				bool ctrl = bool(event.key.keysym.mod & KMOD_CTRL);
				if (ctrl && (k == SDLK_w))
				{
					if (Core_IsStepping())
						Core_Resume();
					Core_Stop();
					System_PostUIMessage(UIMessage::REQUEST_GAME_STOP);
					// NOTE: Unlike Windows version, this
					// does not need Core_WaitInactive();
					// since SDL does not have a separate
					// UI thread.
				}

				/*
				// TODO: Enable this?
				if (k == SDLK_F11) {
#if !defined(MOBILE_DEVICE)
					g_Config.bFullScreen = !g_Config.bFullScreen;
					System_applyFullscreenState("");
#endif
				}
				*/
			}
			break;
		}
	case SDL_KEYUP:
		{
			if (event.key.repeat > 0) { break;}
			int k = event.key.keysym.sym;
			KeyInput key;
			key.flags = KeyInputFlags::UP;
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
			int c = u8_nextchar(event.text.text, &pos, strlen(event.text.text));
			KeyInput key;
			key.flags = KeyInputFlags::CHAR;
			key.unicodeChar = c;
			key.deviceId = DEVICE_ID_KEYBOARD;
			NativeKey(key);
			break;
		}
// This behavior doesn't feel right on a macbook with a touchpad.
#if !PPSSPP_PLATFORM(MAC)
	case SDL_FINGERMOTION:
		{
			int w, h;
			SDL_GetWindowSize(window, &w, &h);
			TouchInput input{};
			input.id = event.tfinger.fingerId;
			input.x = event.tfinger.x * w * g_DesktopDPI * g_display.dpi_scale_x;
			input.y = event.tfinger.y * h * g_DesktopDPI * g_display.dpi_scale_x;
			input.flags = TouchInputFlags::MOVE;
			input.timestamp = event.tfinger.timestamp;
			NativeTouch(input);
			break;
		}
	case SDL_FINGERDOWN:
		{
			int w, h;
			SDL_GetWindowSize(window, &w, &h);
			TouchInput input{};
			input.id = event.tfinger.fingerId;
			input.x = event.tfinger.x * w * g_DesktopDPI * g_display.dpi_scale_x;
			input.y = event.tfinger.y * h * g_DesktopDPI * g_display.dpi_scale_x;
			input.flags = TouchInputFlags::DOWN;
			input.timestamp = event.tfinger.timestamp;
			NativeTouch(input);

			KeyInput key{};
			key.deviceId = DEVICE_ID_MOUSE;
			key.keyCode = NKCODE_EXT_MOUSEBUTTON_1;
			key.flags = KeyInputFlags::DOWN;
			NativeKey(key);
			break;
		}
	case SDL_FINGERUP:
		{
			int w, h;
			SDL_GetWindowSize(window, &w, &h);
			TouchInput input{};
			input.id = event.tfinger.fingerId;
			input.x = event.tfinger.x * w * g_DesktopDPI * g_display.dpi_scale_x;
			input.y = event.tfinger.y * h * g_DesktopDPI * g_display.dpi_scale_x;
			input.flags = TouchInputFlags::UP;
			input.timestamp = event.tfinger.timestamp;
			NativeTouch(input);

			KeyInput key;
			key.deviceId = DEVICE_ID_MOUSE;
			key.keyCode = NKCODE_EXT_MOUSEBUTTON_1;
			key.flags = KeyInputFlags::UP;
			NativeKey(key);
			break;
		}
#endif
	case SDL_MOUSEBUTTONDOWN:
		switch (event.button.button) {
		case SDL_BUTTON_LEFT:
			{
				inputTracker->mouseDown |= 1;
				TouchInput input{};
				input.x = mx;
				input.y = my;
				input.flags = TouchInputFlags::DOWN | TouchInputFlags::MOUSE;
				input.buttons = 1;
				input.id = 0;
				NativeTouch(input);
				KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_1, KeyInputFlags::DOWN);
				NativeKey(key);
			}
			break;
		case SDL_BUTTON_RIGHT:
			{
				inputTracker->mouseDown |= 2;
				TouchInput input{};
				input.x = mx;
				input.y = my;
				input.flags = TouchInputFlags::DOWN | TouchInputFlags::MOUSE;
				input.buttons = 2;
				input.id = 0;
				NativeTouch(input);
				KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_2, KeyInputFlags::DOWN);
				NativeKey(key);
			}
			break;
		case SDL_BUTTON_MIDDLE:
			{
				KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_3, KeyInputFlags::DOWN);
				NativeKey(key);
			}
			break;
		case SDL_BUTTON_X1:
			{
				KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_4, KeyInputFlags::DOWN);
				NativeKey(key);
			}
			break;
		case SDL_BUTTON_X2:
			{
				KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_5, KeyInputFlags::DOWN);
				NativeKey(key);
			}
			break;
		}
		break;
	case SDL_MOUSEWHEEL:
		{
			KeyInput key{};
			key.deviceId = DEVICE_ID_MOUSE;
			key.flags = KeyInputFlags::DOWN;
#if SDL_VERSION_ATLEAST(2, 0, 18)
			if (event.wheel.preciseY != 0.0f) {
				// Should the scale be DPI-driven?
				const float scale = 30.0f;
				key.keyCode = event.wheel.preciseY > 0 ? NKCODE_EXT_MOUSEWHEEL_UP : NKCODE_EXT_MOUSEWHEEL_DOWN;
				key.flags |= KeyInputFlags::HAS_WHEEL_DELTA;
				int wheelDelta = event.wheel.preciseY * scale;
				if (event.wheel.preciseY < 0) {
						wheelDelta = -wheelDelta;
				}
				key.flags = (KeyInputFlags)((u32)key.flags | (wheelDelta << 16));
				NativeKey(key);
				break;
			}
#endif
			if (event.wheel.y > 0) {
				key.keyCode = NKCODE_EXT_MOUSEWHEEL_UP;
				NativeKey(key);
			} else if (event.wheel.y < 0) {
				key.keyCode = NKCODE_EXT_MOUSEWHEEL_DOWN;
				NativeKey(key);
			}
			break;
		}
	case SDL_MOUSEMOTION:
		{
			TouchInput input{};
			input.x = mx;
			input.y = my;
			input.flags = TouchInputFlags::MOVE | TouchInputFlags::MOUSE;
			input.buttons = inputTracker->mouseDown;
			input.id = 0;
			NativeTouch(input);
			NativeMouseDelta(event.motion.xrel, event.motion.yrel);

			UpdateCursor();
			break;
		}
	case SDL_MOUSEBUTTONUP:
		switch (event.button.button) {
		case SDL_BUTTON_LEFT:
			{
				inputTracker->mouseDown &= ~1;
				TouchInput input{};
				input.x = mx;
				input.y = my;
				input.flags = TouchInputFlags::UP | TouchInputFlags::MOUSE;
				input.buttons = 1;
				NativeTouch(input);
				KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_1, KeyInputFlags::UP);
				NativeKey(key);
			}
			break;
		case SDL_BUTTON_RIGHT:
			{
				inputTracker->mouseDown &= ~2;
				// Right button only emits mouse move events. This is weird,
				// but consistent with Windows. Needs cleanup.
				TouchInput input{};
				input.x = mx;
				input.y = my;
				input.flags = TouchInputFlags::UP | TouchInputFlags::MOUSE;
				input.buttons = 2;
				NativeTouch(input);
				KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_2, KeyInputFlags::UP);
				NativeKey(key);
			}
			break;
		case SDL_BUTTON_MIDDLE:
			{
				KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_3, KeyInputFlags::UP);
				NativeKey(key);
			}
			break;
		case SDL_BUTTON_X1:
			{
				KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_4, KeyInputFlags::UP);
				NativeKey(key);
			}
			break;
		case SDL_BUTTON_X2:
			{
				KeyInput key(DEVICE_ID_MOUSE, NKCODE_EXT_MOUSEBUTTON_5, KeyInputFlags::UP);
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
				INFO_LOG(Log::Audio, "Got bogus new audio device notification");
				break;
			}
			// Don't start auto switching for a couple of seconds, because some devices init on start.
			bool doAutoSwitch = g_Config.bAutoAudioDevice;
			if ((time_now_d() - g_audioStartTime) < 3.0) {
				INFO_LOG(Log::Audio, "Ignoring new audio device: %s (current: %s)", name, g_Config.sAudioDevice.c_str());
				doAutoSwitch = false;
			}
			if (doAutoSwitch || g_Config.sAudioDevice == name) {
				StopSDLAudioDevice();

				INFO_LOG(Log::Audio, "!!! Auto-switching to new audio device: '%s'", name);

				InitSDLAudioDevice(name ? name : "");
			}
		}
		break;
	case SDL_AUDIODEVICEREMOVED:
		if (event.adevice.iscapture == 0 && event.adevice.which == audioDev) {
			StopSDLAudioDevice();
			INFO_LOG(Log::Audio, "Audio device removed, reselecting");
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

void UpdateTextFocus() {
	if (g_textFocusChanged) {
		INFO_LOG(Log::System, "Updating text focus: %d", g_textFocus);
		if (g_textFocus) {
			SDL_StartTextInput();
		} else {
			SDL_StopTextInput();
		}
		g_textFocusChanged = false;
	}
}

void UpdateSDLCursor() {
#if !defined(MOBILE_DEVICE)
	if (lastUIState != GetUIState()) {
		lastUIState = GetUIState();
		if (lastUIState == UISTATE_INGAME && g_Config.bFullScreen && !g_Config.bShowTouchControls)
			SDL_ShowCursor(SDL_DISABLE);
		if (lastUIState != UISTATE_INGAME || !g_Config.bFullScreen)
			SDL_ShowCursor(SDL_ENABLE);
	}
#endif
}

static int printUsage(const char *progname)
{
	// NOTE: by convention, --help outputs to stdout,
	// not to stderr, since it is intended output in this
	// case (usage printed under different circumstances,
	// say in response to error during parsing commandline,
	// may go to stderr).
	FILE *dst = stdout;

	// NOTE: wording largely taken from
	// https://www.ppsspp.org/docs/reference/command-line/
	fprintf(dst, "PPSSPP - a PSP emulator (SDL build)\n");
	fprintf(dst, "Usage: %s [options] [FILE]\n\n", progname);
	fprintf(dst, "Launches FILE (e.g. ISO image) if present.\n");
	fprintf(dst, "Options (some of these are specific to SDL backend):\n");
	fprintf(dst, "  -h, --help            show this message and exit\n");
	fprintf(dst, "  --version             show version information and exit\n");

	fprintf(dst, "  -d                    set the log level to debug\n");
	fprintf(dst, "  -v                    set the log level to verbose\n");
	fprintf(dst, "  --loglevel=INTEGER    set the log level to specified value\n");
	fprintf(dst, "  --log=FILE            output log to FILE\n");
	fprintf(dst, "  --state=FILE          load state from FILE\n");

	fprintf(dst, "  -i                    use the interpreter\n");
	fprintf(dst, "  -r                    use IR interpreter\n");
	fprintf(dst, "  -j                    use JIT\n");
	fprintf(dst, "  -J                    use IR JIT\n");

	fprintf(dst, "  --fullscreen          force full screen mode, ignoring saved configuration\n");
	fprintf(dst, "  --windowed            force windowed mode, ignoring saved configuration\n");
	fprintf(dst, "  --xres PIXELS         set X resolution\n");
	fprintf(dst, "  --yres PIXELS         set Y resolution\n");
	fprintf(dst, "  --dpi  FACTOR         set DPI\n");
	fprintf(dst, "  --scale FACTOR        set scale\n");
	fprintf(dst, "  --ipad                set resolution to 1024x768\n");
	fprintf(dst, "  --portrait            portrait mode\n");
	fprintf(dst, "  --graphics=BACKEND    use a different gpu backend\n");
	fprintf(dst, "                        options: gles, software, etc. (also opengl3.1, etc.)\n");

	fprintf(dst, "  --pause-menu-exit     change \"Exit to menu\" in pause menu to \"Exit\"\n");
	fprintf(dst, "  --escape-exit         escape key exits the application\n");
	fprintf(dst, "  --gamesettings        go directly to settings\n");
	fprintf(dst, "  --touchscreentest     go directly to the touchscreentest screen\n");
	fprintf(dst, "  --appendconfig=FILE   merge config FILE into the current configuration\n");

	return 0;
}

#ifdef _WIN32
#undef main
#endif
int main(int argc, char *argv[]) {
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h"))
			return printUsage(argv[0]);
		else if (!strcmp(argv[i], "--version")) {
			printf("%s\n", PPSSPP_GIT_VERSION);
			return 0;
		}
	}

	TimeInit();

	g_logManager.EnableOutput(LogOutput::Stdio);

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

	bool vulkanMayBeAvailable = false;
	if (VulkanMayBeAvailable()) {
		fprintf(stderr, "DEBUG: Vulkan might be available.\n");
		vulkanMayBeAvailable = true;
	} else {
		fprintf(stderr, "DEBUG: Vulkan is not available, not using Vulkan.\n");
	}

	SDL_version compiled;
	SDL_version linked;
	int set_xres = -1;
	int set_yres = -1;
	bool portrait = false;
	bool set_ipad = false;
	float set_dpi = 0.0f;
	float set_scale = 1.0f;

	// Produce a new set of arguments with the ones we skip.
	int remain_argc = 1;
	const char *remain_argv[256] = { argv[0] };

	// Option to force a specific OpenGL version (42="4.2",
	// etc.; -1 means "try them all").
	// Implemented as a workaround for https://github.com/hrydgard/ppsspp/issues/20687
	// NOTE: this is currently not persistent (doesn't
	// go to config), even though --graphics=openglX.Y
	// also sets the GPU backend which does persist.
	int force_gl_version = -1;

	Uint32 mode = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--fullscreen")) {
			mode |= SDL_WINDOW_FULLSCREEN_DESKTOP;
			g_Config.DoNotSaveSetting(&g_Config.bFullScreen);
		} else if (set_xres == -2)
			set_xres = parseInt(argv[i]);
		else if (set_yres == -2)
			set_yres = parseInt(argv[i]);
		else if (set_dpi == -2)
			set_dpi = parseFloat(argv[i]);
		else if (set_scale == -2)
			set_scale = parseFloat(argv[i]);
		else if (!strcmp(argv[i], "--xres"))
			set_xres = -2;
		else if (!strcmp(argv[i], "--yres"))
			set_yres = -2;
		else if (!strcmp(argv[i], "--dpi"))
			set_dpi = -2;
		else if (!strcmp(argv[i], "--scale"))
			set_scale = -2;
		else if (!strcmp(argv[i], "--ipad"))
			set_ipad = true;
		else if (!strcmp(argv[i], "--portrait"))
			portrait = true;
		else if (!strncmp(argv[i], "--graphics=", strlen("--graphics="))) {
			const char *restOfOption = argv[i] + strlen("--graphics=");
			double val=-1.0; // Yes, floating point.
			if (!strcmp(restOfOption, "vulkan")) {
				g_Config.iGPUBackend = (int)GPUBackend::VULKAN;
				g_Config.bSoftwareRendering = false;
			} else if (!strcmp(restOfOption, "software")) {
				// Same as on Windows, software presently implies OpenGL.
				g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
				g_Config.bSoftwareRendering = true;
			} else if (!strcmp(restOfOption, "gles") || !strcmp(restOfOption, "opengl")) {
				// NOTE: OpenGL and GLES are treated the same for
				// the purposes of option parsing.
				g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
				g_Config.bSoftwareRendering = false;
			} else if (sscanf(restOfOption, "gles%lg", &val) == 1 || sscanf(restOfOption, "opengl%lg", &val) == 1) {
				g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
				g_Config.bSoftwareRendering = false;
				force_gl_version = int(10.0 * val + 0.5);
			}
		} else {
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
	fprintf(stderr, "Info: We compiled against SDL version %d.%d.%d", compiled.major, compiled.minor, compiled.patch);
	if (compiled.minor != linked.minor || compiled.patch != linked.patch) {
		fprintf(stderr, ", but we are linking against SDL version %d.%d.%d., be aware that this can lead to unexpected behaviors\n", linked.major, linked.minor, linked.patch);
	} else {
		fprintf(stderr, " and we are linking against SDL version %d.%d.%d. :)\n", linked.major, linked.minor, linked.patch);
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
	mode |= SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#endif

	if (mode & SDL_WINDOW_FULLSCREEN_DESKTOP) {
		g_display.pixel_xres = g_DesktopWidth;
		g_display.pixel_yres = g_DesktopHeight;
		g_Config.bFullScreen = true;
	} else {
		// set a sensible default resolution (2x)
		g_display.pixel_xres = 480 * 2 * set_scale;
		g_display.pixel_yres = 272 * 2 * set_scale;
		if (portrait) {
			std::swap(g_display.pixel_xres, g_display.pixel_yres);
		}
		g_Config.bFullScreen = false;
	}

	if (set_ipad) {
		g_display.pixel_xres = 1024;
		g_display.pixel_yres = 768;
	}
	if (!landscape) {
		std::swap(g_display.pixel_xres, g_display.pixel_yres);
	}

	if (set_xres > 0) {
		g_display.pixel_xres = set_xres;
	}
	if (set_yres > 0) {
		g_display.pixel_yres = set_yres;
	}
	if (set_dpi > 0) {
		g_ForcedDPI = set_dpi;
	}

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
	if (g_Config.bFullScreen)
		mode |= SDL_WINDOW_FULLSCREEN_DESKTOP;

	int x = SDL_WINDOWPOS_UNDEFINED_DISPLAY(getDisplayNumber());
	int y = SDL_WINDOWPOS_UNDEFINED;
	int w = g_display.pixel_xres;
	int h = g_display.pixel_yres;

	if (!g_Config.bFullScreen) {
		if (g_Config.iWindowX != -1)
			x = g_Config.iWindowX;
		if (g_Config.iWindowY != -1)
			y = g_Config.iWindowY;
		if (g_Config.iWindowWidth > 0 && set_xres <= 0)
			w = g_Config.iWindowWidth;
		if (g_Config.iWindowHeight > 0 && set_yres <= 0)
			h = g_Config.iWindowHeight;
	}

	GraphicsContext *graphicsContext = nullptr;
	SDL_Window *window = nullptr;

	// Switch away from Vulkan if not available.
	if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN && !vulkanMayBeAvailable) {
		g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
	}

	std::string error_message;
	if (g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
		SDLGLGraphicsContext *glctx = new SDLGLGraphicsContext();
		if (glctx->Init(window, x, y, w, h, mode, &error_message, force_gl_version) != 0) {
			// Let's try the fallback once per process run.
			fprintf(stderr, "GL init error '%s' - falling back to Vulkan\n", error_message.c_str());
			g_Config.iGPUBackend = (int)GPUBackend::VULKAN;
			SetGPUBackend((GPUBackend)g_Config.iGPUBackend);
			delete glctx;

			// NOTE : This should match the lines below in the Vulkan case.
			SDLVulkanGraphicsContext *vkctx = new SDLVulkanGraphicsContext();
			if (!vkctx->Init(window, x, y, w, h, mode | SDL_WINDOW_VULKAN, &error_message)) {
				fprintf(stderr, "Vulkan fallback failed: %s\n", error_message.c_str());
				return 1;
			}
			graphicsContext = vkctx;
		} else {
			graphicsContext = glctx;
		}
#if !PPSSPP_PLATFORM(SWITCH)
	} else if (g_Config.iGPUBackend == (int)GPUBackend::VULKAN) {
		SDLVulkanGraphicsContext *vkctx = new SDLVulkanGraphicsContext();
		if (!vkctx->Init(window, x, y, w, h, mode | SDL_WINDOW_VULKAN, &error_message)) {
			// Let's try the fallback once per process run.

			fprintf(stderr, "Vulkan init error '%s' - falling back to GL\n", error_message.c_str());
			g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
			SetGPUBackend((GPUBackend)g_Config.iGPUBackend);
			delete vkctx;

			// NOTE : This should match the three lines above in the OpenGL case.
			SDLGLGraphicsContext *glctx = new SDLGLGraphicsContext();
			if (glctx->Init(window, x, y, w, h, mode, &error_message, force_gl_version) != 0) {
				fprintf(stderr, "GL fallback failed: %s\n", error_message.c_str());
				return 1;
			}
			graphicsContext = glctx;
		} else {
			graphicsContext = vkctx;
		}
#endif
	}

	UpdateScreenDPI(window);

	float dpi_scale = 1.0f / (g_ForcedDPI == 0.0f ? g_DesktopDPI : g_ForcedDPI);

	Native_UpdateScreenScale(w * g_DesktopDPI, h * g_DesktopDPI, UIScaleFactorToMultiplier(g_Config.iUIScaleFactor));

	bool mainThreadIsRender = g_Config.iGPUBackend == (int)GPUBackend::OPENGL;

	SDL_SetWindowTitle(window, (app_name_nice + " " + PPSSPP_GIT_VERSION).c_str());

	char iconPath[PATH_MAX];
#if defined(ASSETS_DIR)
	snprintf(iconPath, PATH_MAX, "%sui_images/icon.png", ASSETS_DIR);
	if (access(iconPath, F_OK) != 0)
		snprintf(iconPath, PATH_MAX, "%sassets/ui_images/icon.png", SDL_GetBasePath() ? SDL_GetBasePath() : "");
#else
	snprintf(iconPath, PATH_MAX, "%sassets/ui_images/icon.png", SDL_GetBasePath() ? SDL_GetBasePath() : "");
#endif
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
		fprintf(stderr, "Init from thread error: '%s'\n", error_message.c_str());
		return 1;
	}

	// OK, we have a valid graphics backend selected. Let's clear the failures.
	g_Config.sFailedGPUBackends.clear();

#ifdef MOBILE_DEVICE
	SDL_ShowCursor(SDL_DISABLE);
#endif

	// Avoid the IME popup when holding keys. This doesn't affect all versions of SDL.
	// Note: We re-enable it in text input fields! This is necessary otherwise we don't receive
	// KeyInputFlags::CHAR events.
	SDL_StopTextInput();

	InitSDLAudioDevice();
	g_audioStartTime = time_now_d();

	if (joystick_enabled) {
		joystick = new SDLJoystick();
	} else {
		joystick = nullptr;
	}
	EnableFZ();

	EmuThreadStart(graphicsContext);

	graphicsContext->ThreadStart();

	InputStateTracker inputTracker{};

#if PPSSPP_PLATFORM(MAC)
	// setup menu items for macOS
	initializeOSXExtras();
#endif

	bool waitOnExit = g_Config.iGPUBackend == (int)GPUBackend::OPENGL;

	// Check if the path to a directory containing an unpacked ISO is passed as a command line argument
	for (int i = 1; i < argc; i++) {
		if (File::IsDirectory(Path(argv[i]))) {
			// Display the toast warning
			System_Toast("Warning: Playing unpacked games may cause issues.");
			break;
		}
	}

	if (!mainThreadIsRender) {
		// Vulkan mode uses this.
		// We should only be a message pump. This allows for lower latency
		// input events, and so on.
		while (true) {
			SDL_Event event;
			while (SDL_WaitEventTimeout(&event, 100)) {
				if (g_QuitRequested || g_RestartRequested)
					break;

				ProcessSDLEvent(window, event, &inputTracker);
			}

			if (g_QuitRequested || g_RestartRequested)
				break;

			UpdateTextFocus();
			UpdateSDLCursor();

			inputTracker.MouseCaptureControl();

			{
				std::lock_guard<std::mutex> guard(g_mutexWindow);
				if (g_windowState.update) {
					UpdateWindowState(window);
				}
			}
		}
	} else while (true) {
		{
			SDL_Event event;
			while (SDL_PollEvent(&event)) {
				ProcessSDLEvent(window, event, &inputTracker);
			}
		}
		if (g_QuitRequested || g_RestartRequested)
			break;
		if (emuThreadState == (int)EmuThreadState::DISABLED) {
			NativeFrame(graphicsContext);
		}
		if (g_QuitRequested || g_RestartRequested)
			break;

		UpdateTextFocus();
		UpdateSDLCursor();

		inputTracker.MouseCaptureControl();

		bool renderThreadPaused = Native_IsWindowHidden() && g_Config.bPauseWhenMinimized && emuThreadState != (int)EmuThreadState::DISABLED;
		if (emuThreadState != (int)EmuThreadState::DISABLED && !renderThreadPaused) {
			if (!graphicsContext->ThreadFrame(true))
				break;
		}

		{
			std::lock_guard<std::mutex> guard(g_mutexWindow);
			if (g_windowState.update) {
				UpdateWindowState(window);
			}
		}

		if (g_rebootEmuThread) {
			fprintf(stderr, "rebooting emu thread");
			g_rebootEmuThread = false;
			EmuThreadStop("shutdown");
			graphicsContext->ThreadFrameUntilCondition([]() {
				return emuThreadState == (int)EmuThreadState::STOPPED || emuThreadState == (int)EmuThreadState::DISABLED;
			});
			EmuThreadJoin();
			graphicsContext->ThreadEnd();
			graphicsContext->ShutdownFromRenderThread();

			fprintf(stderr, "OK, shutdown complete. starting up graphics again.\n");

			if (g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
				SDLGLGraphicsContext *ctx  = (SDLGLGraphicsContext *)graphicsContext;
				if (!ctx->Init(window, x, y, w, h, mode, &error_message, force_gl_version)) {
					fprintf(stderr, "Failed to reinit graphics.\n");
				}
			}

			if (!graphicsContext->InitFromRenderThread(&error_message)) {
				System_Toast("Graphics initialization failed. Quitting.");
				return 1;
			}

			EmuThreadStart(graphicsContext);
			graphicsContext->ThreadStart();
		}
	}

	EmuThreadStop("shutdown");

	if (waitOnExit) {
		graphicsContext->ThreadFrameUntilCondition([]() {
			return emuThreadState == (int)EmuThreadState::STOPPED || emuThreadState == (int)EmuThreadState::DISABLED;
		});
	}

	EmuThreadJoin();

	delete joystick;

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
	fprintf(stderr, "Leaving main\n");
#ifdef HAVE_LIBNX
	socketExit();
#endif

	// If a restart was requested (and supported on this platform), respawn the executable.
	if (g_RestartRequested) {
#if PPSSPP_PLATFORM(MAC)
		RestartMacApp();
#elif PPSSPP_PLATFORM(LINUX)
		// Hackery from https://unix.stackexchange.com/questions/207935/how-to-restart-or-reset-a-running-process-in-linux,
		char *exec_argv[] = { argv[0], nullptr };
		execv("/proc/self/exe", exec_argv);
#endif
	}
	return 0;
}
