#pragma once

#include <string>
#include <vector>
#include <functional>

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
void System_Toast(const char *text);
void ShowKeyboard();

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
void Vibrate(int length_ms);
void OpenDirectory(const char *path);
void LaunchBrowser(const char *url);
void LaunchMarket(const char *url);
void LaunchEmail(const char *email_address);
void System_InputBoxGetString(const std::string &title, const std::string &defaultValue, std::function<void(bool, const std::string &)> cb);
void System_SendMessage(const char *command, const char *parameter);
PermissionStatus System_GetPermissionStatus(SystemPermission permission);
void System_AskForPermission(SystemPermission permission);

// This will get muddy with multi-screen support :/ But this will always be the type of the main device.
enum SystemDeviceType {
	DEVICE_TYPE_MOBILE = 0,  // phones and pads
	DEVICE_TYPE_TV = 1,  // Android TV and similar
	DEVICE_TYPE_DESKTOP = 2,  // Desktop computer
};

enum SystemKeyboardLayout {
	KEYBOARD_LAYOUT_QWERTY = 0,
	KEYBOARD_LAYOUT_QWERTZ = 1,
	KEYBOARD_LAYOUT_AZERTY = 2,
};

enum SystemProperty {
	SYSPROP_NAME,
	SYSPROP_LANGREGION,
	SYSPROP_CPUINFO,
	SYSPROP_BOARDNAME,
	SYSPROP_CLIPBOARD_TEXT,
	SYSPROP_GPUDRIVER_VERSION,

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

	SYSPROP_KEYBOARD_LAYOUT,
};

std::string System_GetProperty(SystemProperty prop);
std::vector<std::string> System_GetPropertyStringVec(SystemProperty prop);
int System_GetPropertyInt(SystemProperty prop);
float System_GetPropertyFloat(SystemProperty prop);
bool System_GetPropertyBool(SystemProperty prop);

std::vector<std::string> __cameraGetDeviceList();
bool audioRecording_Available();
bool audioRecording_State();
