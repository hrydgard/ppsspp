#pragma once

#include <string>

// The Native App API.
//
// Implement these functions and you've got a native app. These are called
// from app-android, which exposes the native JNI api which is a bit
// more complicated.

// This is defined in input/input_state.h.
struct InputState;

// You must implement this. The first function to get called, just write strings to the two pointers.
void NativeGetAppInfo(std::string *app_dir_name, std::string *app_nice_name);

// The very first function to be called after NativeGetAppInfo. Even NativeMix is not called
// before this, although it may be called at any point in time afterwards (on any thread!)
// This functions must NOT call OpenGL. Main thread.
void NativeInit(int argc, const char *argv[], const char *savegame_directory, const char *installID);

// Runs after NativeInit() at some point. May (and probably should) call OpenGL.
void NativeInitGraphics();

// Signals that you need to recreate all buffered OpenGL resources,
// like textures, vbo etc. Main thread.
void NativeDeviceLost();

// Called ~sixty times a second, delivers the current input state.
// Main thread.
void NativeUpdate(const InputState &input);

// Called when it's time to render. If the device can keep up, this
// will also be called sixty times per second. Main thread.
void NativeRender();

// This should render num_samples 44khz stereo samples.
// Try not to make too many assumptions on the granularity
// of num_samples.
// This function may be called from a totally separate thread from
// the rest of the game.
void NativeMix(short *audio, int num_samples);

// Called when it's time to shutdown. After this has been called,
// no more calls to any other function will be made from the framework
// before process exit.
// The graphics context should still be active when calling this, as freeing
// of graphics resources happens here.
// Main thread.
void NativeShutdownGraphics();
void NativeShutdown();

// Calls back into Java / SDL
// These APIs must be implemented by every port (for example app-android.cpp, PCMain.cpp).
// You are free to call these.
void SystemToast(const char *text);
void ShowAd(int x, int y, bool center_x);
void Vibrate(int length_ms);
void LaunchBrowser(const char *url);
void LaunchMarket(const char *url);
void LaunchEmail(const char *email_address);
