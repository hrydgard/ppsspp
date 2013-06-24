#pragma once

#include <string>

// The Native App API.
//
// Implement these functions and you've got a native app. These are called
// from the framework, which exposes the native JNI api which is a bit
// more complicated.

// This is defined in input/input_state.h.
struct InputState;
struct TouchInput;

// The first function to get called, just write strings to the two pointers.
// This might get called multiple times in some implementations, you must be able to handle that.
// The detected DP dimensions of the screen are set as dp_xres and dp_yres and you're free to change
// them if you have a fixed-size app that needs to stretch a little to fit.
void NativeGetAppInfo(std::string *app_dir_name, std::string *app_nice_name, bool *landscape);

// Generic host->C++ messaging, used for functionality like system-native popup input boxes.
void NativeMessageReceived(const char *message, const char *value);

// For the back button to work right, this should return true on your main or title screen.
// Otherwise, just return false.
bool NativeIsAtTopLevel();

// The very first function to be called after NativeGetAppInfo. Even NativeMix is not called
// before this, although it may be called at any point in time afterwards (on any thread!)
// This functions must NOT call OpenGL. Main thread.
void NativeInit(int argc, const char *argv[], const char *savegame_directory, const char *external_directory, const char *installID);

// Runs after NativeInit() at some point. May (and probably should) call OpenGL.
void NativeInitGraphics();

// Signals that you need to recreate all buffered OpenGL resources,
// like textures, vbo etc. Also, if you have modified dp_xres and dp_yres, you have to 
// do it again here. Main thread.
void NativeDeviceLost();

// Called ~sixty times a second, delivers the current input state.
// Main thread.
void NativeUpdate(InputState &input);

// Delivers touch events "instantly", without waiting for the next frame so that NativeUpdate can deliver.
// Useful for triggering audio events, saving a few ms.
// If you don't care about touch latency, just do a no-op implementation of this.
// time is not yet implemented. finger can be from 0 to 7, inclusive.
void NativeTouch(const TouchInput &touch);

// Called when it's time to render. If the device can keep up, this
// will also be called sixty times per second. Main thread.
void NativeRender();

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

// Called on app.onCreate and app.onDestroy (?). Tells the app to save/restore
// light state. If app was fully rebooted between these calls, it's okay if some minor
// state is lost (position in level) but the level currently playihg, or the song
// currently being edited, or whatever, should be restored properly. In this case,
// firstTime will be set so that appropriate action can be taken (or not taken when
// it's not set).
//
// Note that NativeRestore is always called on bootup.
void NativeRestoreState(bool firstTime);  // onCreate
void NativeSaveState();  // onDestroy

// Calls back into Java / SDL
// These APIs must be implemented by every port (for example app-android.cpp, PCMain.cpp).
// You are free to call these.
void SystemToast(const char *text);
void ShowKeyboard();
void ShowAd(int x, int y, bool center_x);
void Vibrate(int length_ms);
void LaunchBrowser(const char *url);
void LaunchMarket(const char *url);
void LaunchEmail(const char *email_address);
void System_InputBox(const char *title, const char *defaultValue);
