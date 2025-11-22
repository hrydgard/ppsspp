#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <cstdint>
#include <mutex>

// Platform integration

// To run the PPSSPP core, a platform needs to implement all the System_ functions in this file.
// Failure to implement all of these will simply cause linker failures. There are a few that are
// only implemented on specific platforms, but they're also only called on those platforms.

// The platform then calls the entry points from NativeApp.h as appropriate. That's basically it,
// disregarding build system complexities.

enum SystemPermission {
	SYSTEM_PERMISSION_STORAGE,
};

enum PermissionStatus {
	PERMISSION_STATUS_UNKNOWN,
	PERMISSION_STATUS_DENIED,
	PERMISSION_STATUS_PENDING,
	PERMISSION_STATUS_GRANTED,
};

// These APIs must be implemented by every port (for example app-android.cpp, SDLMain.cpp).
// Ideally these should be safe to call from any thread.
void System_Toast(std::string_view text);
void System_ShowKeyboard();

// Vibrate either takes a number of milliseconds to vibrate unconditionally,
// or you can specify these constants for "standard" feedback. On Android,
// these will only be performed if haptic feedback is enabled globally.
// Also, on Android, these will work even if you don't have the VIBRATE permission,
// while generic vibration will not if you don't have it.
enum {
	HAPTIC_SOFT_KEYBOARD = -1,
	HAPTIC_VIRTUAL_KEY = -2,
	HAPTIC_LONG_PRESS_ACTIVATED = -3,
};

enum class LaunchUrlType {
	BROWSER_URL,
	MARKET_URL,
	EMAIL_ADDRESS,
};

void System_Vibrate(int length_ms);
void System_LaunchUrl(LaunchUrlType urlType, std::string_view url);

// It's sometimes a little unclear what should be a request, and what should be a separate function.
// Going forward, "optional" things (PPSSPP will still function alright without it) will be requests,
// to make implementations simpler in the default case.

enum class UIEventNotification {
	MENU_RETURN,
	POPUP_CLOSED,
	TEXT_GOTFOCUS,
	TEXT_LOSTFOCUS,
};

enum class SystemRequestType {
	INPUT_TEXT_MODAL,
	ASK_USERNAME_PASSWORD,
	BROWSE_FOR_IMAGE,
	BROWSE_FOR_FILE,
	BROWSE_FOR_FOLDER,

	BROWSE_FOR_FILE_SAVE,

	EXIT_APP,
	RESTART_APP,  // For graphics backend changes
	RECREATE_ACTIVITY,  // Android
	COPY_TO_CLIPBOARD,
	SHARE_TEXT,
	SET_WINDOW_TITLE,
	TOGGLE_FULLSCREEN_STATE,
	GRAPHICS_BACKEND_FAILED_ALERT,
	CREATE_GAME_SHORTCUT,
	SHOW_FILE_IN_FOLDER,

	// Commonly ignored, used when automated tests generate output.
	SEND_DEBUG_OUTPUT,
	// Note: height specified as param3, width based on param1.size() / param3.
	SEND_DEBUG_SCREENSHOT,

	NOTIFY_UI_EVENT,  // Used to manage events that are useful for popup virtual keyboards.
	SET_KEEP_SCREEN_BRIGHT,

	// High-level hardware control
	CAMERA_COMMAND,
	GPS_COMMAND,
	INFRARED_COMMAND,
	MICROPHONE_COMMAND,

	RUN_CALLBACK_IN_WNDPROC,

	MOVE_TO_TRASH,

	// for iOS IAP support
	IAP_RESTORE_PURCHASES,
	IAP_MAKE_PURCHASE,

	OPEN_DISPLAY_SETTINGS,
};

// Run a closure on the main thread. Used to safely implement UI that runs on another thread.
void System_RunOnMainThread(std::function<void()> func);

// Implementations are supposed to process the request, and post the response to the g_RequestManager (see Message.h).
// This is not to be used directly by applications, instead use the g_RequestManager to make the requests.
// This can return false if it's known that the platform doesn't support the request, the app is supposed to handle
// or ignore that cleanly.
// Some requests don't use responses.
bool System_MakeRequest(SystemRequestType type, int requestId, const std::string &param1, const std::string &param2, int64_t param3, int64_t param4);

PermissionStatus System_GetPermissionStatus(SystemPermission permission);
void System_AskForPermission(SystemPermission permission);

// This will get muddy with multi-screen support :/ But this will always be the type of the main device.
// These are the return values from System_GetPropertyInt(SYSPROP_DEVICE_TYPE).
enum SystemDeviceType {
	DEVICE_TYPE_MOBILE = 0,  // phones and pads
	DEVICE_TYPE_TV = 1,  // Android TV and similar
	DEVICE_TYPE_DESKTOP = 2,  // Desktop computer
	DEVICE_TYPE_VR = 3,  // VR headset
};

enum SystemKeyboardLayout {
	KEYBOARD_LAYOUT_QWERTY = 0,
	KEYBOARD_LAYOUT_QWERTZ = 1,
	KEYBOARD_LAYOUT_AZERTY = 2,
};

enum SystemProperty {
	SYSPROP_NAME,
	SYSPROP_SYSTEMBUILD,
	SYSPROP_LANGREGION,
	SYSPROP_CPUINFO,
	SYSPROP_BOARDNAME,
	SYSPROP_CLIPBOARD_TEXT,
	SYSPROP_GPUDRIVER_VERSION,
	SYSPROP_BUILD_VERSION,
	SYSPROP_COMPUTER_NAME,

	// Separate SD cards or similar.
	// Need hacky solutions to get at this.
	SYSPROP_HAS_ADDITIONAL_STORAGE,
	SYSPROP_ADDITIONAL_STORAGE_DIRS,
	SYSPROP_TEMP_DIRS,

	SYSPROP_HAS_FILE_BROWSER,
	SYSPROP_HAS_FOLDER_BROWSER,
	SYSPROP_HAS_IMAGE_BROWSER,
	SYSPROP_HAS_BACK_BUTTON,
	SYSPROP_HAS_KEYBOARD,
	SYSPROP_KEYBOARD_IS_SOFT,
	SYSPROP_HAS_ACCELEROMETER,  // Used to enable/disable tilt input settings
	SYSPROP_HAS_OPEN_DIRECTORY,
	SYSPROP_HAS_LOGIN_DIALOG,
	SYSPROP_HAS_TEXT_CLIPBOARD,
	SYSPROP_HAS_TEXT_INPUT_DIALOG,  // Indicates that System_InputBoxGetString is available.

	SYSPROP_CAN_CREATE_SHORTCUT,
	SYSPROP_CAN_SHOW_FILE,

	SYSPROP_SUPPORTS_HTTPS,

	SYSPROP_DEBUGGER_PRESENT,

	// Available as Int:
	SYSPROP_SYSTEMVERSION,
	SYSPROP_DISPLAY_XRES,
	SYSPROP_DISPLAY_YRES,
	SYSPROP_DISPLAY_REFRESH_RATE,
	SYSPROP_DISPLAY_LOGICAL_DPI,
	SYSPROP_DISPLAY_DPI,
	SYSPROP_DISPLAY_COUNT,
	SYSPROP_MOGA_VERSION,

	// Float only:
	SYSPROP_DISPLAY_SAFE_INSET_LEFT,
	SYSPROP_DISPLAY_SAFE_INSET_RIGHT,
	SYSPROP_DISPLAY_SAFE_INSET_TOP,
	SYSPROP_DISPLAY_SAFE_INSET_BOTTOM,

	SYSPROP_DEVICE_TYPE,
	SYSPROP_APP_GOLD,  // To avoid having #ifdef GOLD other than in main.cpp and similar.

	// Exposed on Android. Choosing the optimal sample rate for audio
	// will result in lower latencies. Buffer size is automatically matched
	// by the OpenSL audio backend, only exposed here for debugging/info.
	SYSPROP_AUDIO_SAMPLE_RATE,
	SYSPROP_AUDIO_FRAMES_PER_BUFFER,
	SYSPROP_AUDIO_OPTIMAL_SAMPLE_RATE,
	SYSPROP_AUDIO_OPTIMAL_FRAMES_PER_BUFFER,

	// Exposed on SDL.
	SYSPROP_AUDIO_DEVICE_LIST,

	SYSPROP_SUPPORTS_PERMISSIONS,
	SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE,
	SYSPROP_SUPPORTS_OPEN_FILE_IN_EDITOR,  // See FileUtil.cpp: OpenFileInEditor

	// Android-specific.
	SYSPROP_ANDROID_SCOPED_STORAGE,

	SYSPROP_CAN_JIT,

	SYSPROP_HAS_DEBUGGER,

	SYSPROP_KEYBOARD_LAYOUT,

	SYSPROP_SKIP_UI,

	SYSPROP_USER_DOCUMENTS_DIR,

	// iOS app store limitation: The documents directory should be the only browsable directory.
	// We'll not return true for this in non-app-store builds.
	SYSPROP_LIMITED_FILE_BROWSING,

	SYSPROP_OK_BUTTON_LEFT,

	SYSPROP_MAIN_WINDOW_HANDLE,

	SYSPROP_CAN_READ_BATTERY_PERCENTAGE,
	SYSPROP_BATTERY_PERCENTAGE,

	SYSPROP_ENOUGH_RAM_FOR_FULL_ISO,
	SYSPROP_HAS_TRASH_BIN,

	SYSPROP_USE_IAP,
	SYSPROP_SUPPORTS_SHARE_TEXT,

	SYSPROP_HAS_VSYNC_CALLBACK,
};

enum class SystemNotification {
	UI,
	MEM_VIEW,
	DISASSEMBLY,
	DISASSEMBLY_AFTERSTEP,
	DEBUG_MODE_CHANGE,
	BOOT_DONE,  // this is sent from EMU thread! Make sure that Host handles it properly!
	SYMBOL_MAP_UPDATED,
	SWITCH_UMD_UPDATED,
	ROTATE_UPDATED,
	FORCE_RECREATE_ACTIVITY,
	IMMERSIVE_MODE_CHANGE,
	AUDIO_RESET_DEVICE,
	SUSTAINED_PERF_CHANGE,
	POLL_CONTROLLERS,
	TOGGLE_DEBUG_CONSOLE,  // TODO: Kinda weird, just ported forward.
	TEST_JAVA_EXCEPTION,
	KEEP_SCREEN_AWAKE,
	ACTIVITY,
	UI_STATE_CHANGED,
	AUDIO_MODE_CHANGED,
	APP_SWITCH_MODE_CHANGED,
};

// I guess it's not super great architecturally to centralize this, since it's not general - but same with a lot of
// the other stuff, and this is only used by PPSSPP, so... better this than ugly strings.
enum class UIMessage {
	PERMISSION_GRANTED,
	POWER_SAVING,
	RECREATE_VIEWS,
	CONFIG_LOADED,
	REQUEST_GAME_BOOT,
	REQUEST_GAME_RUN, // or continue?
	REQUEST_GAME_PAUSE,
	REQUEST_GAME_RESET,
	REQUEST_GAME_STOP,
	GAME_SELECTED,
	SHOW_CONTROL_MAPPING,
	SHOW_CHAT_SCREEN,
	SHOW_DISPLAY_LAYOUT_EDITOR,
	SHOW_SETTINGS,
	SHOW_LANGUAGE_SCREEN,
	REQUEST_GPU_DUMP_NEXT_FRAME,
	REQUEST_CLEAR_JIT,
	APP_RESUMED,
	REQUEST_PLAY_SOUND,
	WINDOW_MINIMIZED,
	WINDOW_RESTORED,
	LOST_FOCUS,
	GOT_FOCUS,
	GPU_CONFIG_CHANGED,
	GPU_RENDER_RESIZED,
	GPU_DISPLAY_RESIZED,
	POSTSHADER_UPDATED,
	ACHIEVEMENT_LOGIN_STATE_CHANGE,
	SAVESTATE_DISPLAY_SLOT,
	GAMESETTINGS_SEARCH,
	SAVEDATA_SEARCH,
	RESTART_GRAPHICS,
	RECENT_FILES_CHANGED,
	SAVE_FRAME_DUMP,
};

std::string System_GetProperty(SystemProperty prop);
std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop);
int64_t System_GetPropertyInt(SystemProperty prop);
float System_GetPropertyFloat(SystemProperty prop);
bool System_GetPropertyBool(SystemProperty prop);

void System_Notify(SystemNotification notification);

std::vector<std::string> System_GetCameraDeviceList();

bool System_AudioRecordingIsAvailable();
bool System_AudioRecordingState();

// This will be changed to take an enum. Replacement for the old NativeMessageReceived.
void System_PostUIMessage(UIMessage message, std::string_view param = "");

// For these functions, most platforms will use the implementation provided in UI/AudioCommon.cpp,
// no need to implement separately.
void System_AudioGetDebugStats(char *buf, size_t bufSize);
void System_AudioClear();

// These samples really have 16 bits of value, but can be a little out of range.
// This is for pushing rate-controlled 44khz audio from emulation.
// If you push a little too fast, we'll pitch up to a limit, for example.
// Volume is a unit-range multiplier.
void System_AudioPushSamples(const int32_t *audio, int numSamples, float volume);

inline void System_AudioResetStatCounters() {
	return System_AudioGetDebugStats(nullptr, 0);
}
