#pragma once

#include <string>

// The Native App API.
//
// Implement these functions and you've got a native app. These are called
// from the framework, which exposes the native JNI api which is a bit
// more complicated.

// These are defined in input/input_state.h.
struct TouchInput;
struct KeyInput;
struct AxisInput;

class GraphicsContext;

enum SystemPermission {
	SYSTEM_PERMISSION_STORAGE,
};

enum PermissionStatus {
	PERMISSION_STATUS_UNKNOWN,
	PERMISSION_STATUS_DENIED,
	PERMISSION_STATUS_PENDING,
	PERMISSION_STATUS_GRANTED,
};

// The first function to get called, just write strings to the two pointers.
// This might get called multiple times in some implementations, you must be able to handle that.
void NativeGetAppInfo(std::string *app_dir_name, std::string *app_nice_name, bool *landscape, std::string *version);

// Generic host->C++ messaging, used for functionality like system-native popup input boxes.
void NativeMessageReceived(const char *message, const char *value);

// Easy way for the Java side to ask the C++ side for configuration options, such as
// the rotation lock which must be controlled from Java on Android.
// It is currently not called on non-Android platforms.
std::string NativeQueryConfig(std::string query);

// For the back button to work right, this should return true on your main or title screen.
// Otherwise, just return false.
bool NativeIsAtTopLevel();

// The very first function to be called after NativeGetAppInfo. Even NativeMix is not called
// before this, although it may be called at any point in time afterwards (on any thread!)
// This functions must NOT call OpenGL. Main thread.
void NativeInit(int argc, const char *argv[], const char *savegame_dir, const char *external_dir, const char *cache_dir);

// Runs after NativeInit() at some point. May (and probably should) call OpenGL.
// Should not initialize anything screen-size-dependent - do that in NativeResized.
bool NativeInitGraphics(GraphicsContext *graphicsContext);

// If you want to change DPI stuff (such as modifying dp_xres and dp_yres), this is the
// place to do it. You should only read g_dpi_scale and pixel_xres and pixel_yres in this,
// and only write dp_xres and dp_yres.
void NativeResized();

// Set a flag to indicate a restart.  Reset after NativeInit().
void NativeSetRestarting();

// Retrieve current restarting flag.
bool NativeIsRestarting();

// Called ~sixty times a second, delivers the current input state.
// Main thread.
void NativeUpdate();

// Delivers touch events "instantly", without waiting for the next frame so that NativeUpdate can deliver.
// Useful for triggering audio events, saving a few ms.
// If you don't care about touch latency, just do a no-op implementation of this.
// time is not yet implemented. finger can be from 0 to 7, inclusive.
bool NativeTouch(const TouchInput &touch);
bool NativeKey(const KeyInput &key);
bool NativeAxis(const AxisInput &axis);

// Called when it's time to render. If the device can keep up, this
// will also be called sixty times per second. Main thread.
void NativeRender(GraphicsContext *graphicsContext);

// This should render num_samples 44khz stereo samples.
// Try not to make too many assumptions on the granularity
// of num_samples.
// This function may be called from a totally separate thread from
// the rest of the game, so be careful with synchronization.
// Returns the number of samples actually output. The app should do everything it can
// to fill the buffer completely.
int NativeMix(short *audio, int num_samples);
void NativeSetMixer(void* mixer);

// Called when it's time to shutdown. After this has been called,
// no more calls to any other function will be made from the framework
// before process exit.
// The graphics context should still be active when calling this, as freeing
// of graphics resources happens here.
// Main thread.
void NativeShutdownGraphics();
void NativeShutdown();

// Calls back into Java / SDL
// These APIs must be implemented by every port (for example app-android.cpp, SDLMain.cpp).
// You are free to call these.
void SystemToast(const char *text);
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
void LaunchBrowser(const char *url);
void LaunchMarket(const char *url);
void LaunchEmail(const char *email_address);
bool System_InputBoxGetString(const char *title, const char *defaultValue, char *outValue, size_t outlength);
bool System_InputBoxGetWString(const wchar_t *title, const std::wstring &defaultValue, std::wstring &outValue);
void System_SendMessage(const char *command, const char *parameter);
PermissionStatus System_GetPermissionStatus(SystemPermission permission);
void System_AskForPermission(SystemPermission permission);

// This will get muddy with multi-screen support :/ But this will always be the type of the main device.
enum SystemDeviceType {
	DEVICE_TYPE_MOBILE = 0,  // phones and pads
	DEVICE_TYPE_TV = 1,  // Android TV and similar
	DEVICE_TYPE_DESKTOP = 2,  // Desktop computer
};

enum SystemProperty {
	SYSPROP_NAME,
	SYSPROP_LANGREGION,
	SYSPROP_CPUINFO,
	SYSPROP_BOARDNAME,
	SYSPROP_CLIPBOARD_TEXT,
	SYSPROP_GPUDRIVER_VERSION,

	SYSPROP_HAS_FILE_BROWSER,
	SYSPROP_HAS_IMAGE_BROWSER,
	SYSPROP_HAS_BACK_BUTTON,

	// Available as Int:
	SYSPROP_SYSTEMVERSION,
	SYSPROP_DISPLAY_XRES,
	SYSPROP_DISPLAY_YRES,
	SYSPROP_DISPLAY_REFRESH_RATE,  // returns 1000*the refresh rate in Hz as it can be non-integer
	SYSPROP_DISPLAY_DPI,
	SYSPROP_DISPLAY_COUNT,
	SYSPROP_MOGA_VERSION,

	SYSPROP_DEVICE_TYPE,
	SYSPROP_APP_GOLD,  // To avoid having #ifdef GOLD other than in main.cpp and similar.

	// Exposed on Android. Choosing the optimal sample rate for audio
	// will result in lower latencies. Buffer size is automatically matched
	// by the OpenSL audio backend, only exposed here for debugging/info.
	SYSPROP_AUDIO_SAMPLE_RATE,
	SYSPROP_AUDIO_FRAMES_PER_BUFFER,
	SYSPROP_AUDIO_OPTIMAL_SAMPLE_RATE,
	SYSPROP_AUDIO_OPTIMAL_FRAMES_PER_BUFFER,

	SYSPROP_SUPPORTS_PERMISSIONS,
	SYSPROP_SUPPORTS_SUSTAINED_PERF_MODE,
};

std::string System_GetProperty(SystemProperty prop);
int System_GetPropertyInt(SystemProperty prop);
bool System_GetPropertyBool(SystemProperty prop);

void PushNewGpsData(float latitude, float longitude, float altitude, float speed, float bearing, long long time);
void PushCameraImage(long long length, unsigned char* image);
