#pragma once

#include <functional>
#include <string>

#include "Common/Common.h"
#include "Common/System/Application.h"
#include "Common/GPU/MiscTypes.h"

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
class AudioBackend;

enum class KeyModifier {
	NONE = 0,
	LCTRL = 1,
	RCTRL = 2,
	LSHIFT = 4,
	RSHIFT = 8,
	LALT = 16,
	RALT = 32,
	LMETA = 64,
	RMETA = 128,
};
ENUM_CLASS_BITOPS(KeyModifier);

// The first function to get called, just write strings to the two pointers.
// This might get called multiple times in some implementations, you must be able to handle that.
void NativeGetAppInfo(std::string *app_dir_name, std::string *app_nice_name, bool *landscape, std::string *version);

// For the back button to work right, this should return true on your main or title screen.
// Otherwise, just return false.
bool NativeIsAtTopLevel();

struct CommandLineOptions;

// The very first function to be called after NativeGetAppInfo. Even NativeMix is not called
// before this, although it may be called at any point in time afterwards (on any thread!)
// This functions must NOT call OpenGL. Main thread.
void NativeInit(int argc, const char *argv[], const CommandLineOptions &cmdLineOptions, const char *savegame_dir, const char *external_dir, const char *cache_dir);
void NativeSetAchievementsHostOverride(std::string_view host);
void NativeClearAchievementsHostOverride();

// Runs after NativeInit() at some point. May (and probably should) call OpenGL.
// Should not initialize anything screen-size-dependent - do that in NativeResized.
bool NativeInitGraphics(GraphicsContext *graphicsContext);

// If you want to change DPI stuff (such as modifying dp_xres and dp_yres), this is the
// place to do it. You should only read g_dpi_scale and pixel_xres and pixel_yres in this,
// and only write dp_xres and dp_yres.
void NativeResized();

// Delivers touch/key/axis events "instantly", without waiting for the next frame so that NativeFrame can deliver.
// Some systems like UI will buffer these events internally but at least in gameplay we can get the minimum possible
// input latency - assuming your main loop is architected properly (NativeFrame called from a different thread than input event handling).
void NativeTouch(const TouchInput &touch);
bool NativeKey(const KeyInput &key);
void NativeAxis(const AxisInput *axis, size_t count);
// In m/s^2, the way Android reports it: about +9.81 on z with the device lying screen up.
void NativeAccelerometer(float tiltX, float tiltY, float tiltZ);
// Angular rates in rad/s around the NativeAccelerometer axes. The timestamp is in seconds, on any clock (only deltas are used).
void NativeGyroscope(float x, float y, float z, double timestamp);
void NativeMouseDelta(float dx, float dy);
KeyModifier NativeGetKeyModifiers();  // TODO: Move this to input handling somewhere? But don't want it in each backend...

// Called when it's process a frame, including rendering. If the device can keep up, this
// will be called sixty times per second. Main thread.
void NativeFrame(GraphicsContext *graphicsContext);

// This should render num_samples 44khz stereo samples.
// Try not to make too many assumptions on the granularity of num_samples.
// This function will likely be called from a totally separate thread from
// the rest of the app, so be careful with synchronization.
// The app must fill the buffer completely, doing its own internal buffering if needed.
void NativeMix(short *audio, int num_samples, int sampleRateHz, void *userdata);

// Called when it's time to shutdown. After this has been called,
// no more calls to any other function will be made from the framework
// before process exit.
// The graphics context should still be active when calling this, as freeing
// of graphics resources happens here.
// Main thread.
void NativeShutdownGraphics(GraphicsContext *graphicsContext);
void NativeShutdown();

void PostLoadConfig();

// Returns false on failure. Shouldn't really happen, though.
bool NativeSaveSecret(std::string_view nameOfSecret, std::string_view data);
inline bool NativeClearSecret(std::string_view nameOfSecret) {
	return NativeSaveSecret(nameOfSecret, "");
}
// On failure, returns an empty string. Good enough since any real secret is non-empty.
std::string NativeLoadSecret(std::string_view nameOfSecret);

// Don't run the core when minimized etc.
void Native_NotifyWindowHidden(bool hidden);
bool Native_IsWindowHidden();

// TODO: Feels like this belongs elsewhere.
bool Native_UpdateScreenScale(int width, int height, float customScale);

namespace Draw {
struct SwapchainDesc;
}

enum class OutputRole {
	Game,  // The emulated display, with no UI.
};

// Secondary outputs, like a second window or a TV (see ScreenManager::AddOutput). Safe to call from any
// thread. They take effect at the start of the next frame. The surface in desc has to stay valid until
// the output is removed, and that frame has started.
int Native_AddOutput(OutputRole role, const Draw::SwapchainDesc &desc);
void Native_ResizeOutput(int id, int width, int height);
void Native_RemoveOutput(int id);
// Also waits until the output's swapchain is gone, so its surface can go away right after (as on Android,
// where a surface mustn't be touched once surfaceDestroyed returns). Don't call it from the render thread.
bool Native_RemoveOutputAndWait(int id);

AudioBackend *System_CreateAudioBackend();

class NativeApplication : public Application {
public:
	bool InitGraphics(GraphicsContext *graphicsContext) override {
		return NativeInitGraphics(graphicsContext);
	}
	void ShutdownGraphics(GraphicsContext *graphicsContext) override {
		NativeShutdownGraphics(graphicsContext);
	}
};
