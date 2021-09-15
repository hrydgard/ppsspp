#pragma once

#include <functional>
#include <string>

// The Native App API.
//
// Implement these functions and you've got a native app. These are called
// from the framework, which exposes the native JNI api which is a bit
// more complicated.

// These are defined in Common/Input/InputState.cpp
struct TouchInput;
struct KeyInput;
struct AxisInput;

class GraphicsContext;

// The first function to get called, just write strings to the two pointers.
// This might get called multiple times in some implementations, you must be able to handle that.
void NativeGetAppInfo(std::string *app_dir_name, std::string *app_nice_name, bool *landscape, std::string *version);

// Generic host->C++ messaging, used for functionality like system-native popup input boxes.
void NativeMessageReceived(const char *message, const char *value);

// This is used to communicate back and thread requested input box strings.
void NativeInputBoxReceived(std::function<void(bool, const std::string &)> cb, bool result, const std::string &value);

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

void PostLoadConfig();
