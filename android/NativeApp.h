#pragma once

// The Native App API.
//
// Implement these functions and you've got a native app. These are called
// from app-android, which exposes the native JNI api which is a bit
// more complicated.

// This is defined in input/input_state.h.
struct InputState;

// The very first function to be called. Even NativeMix is not called
// before this, although it may be called at any point in time afterwards.
// Must not call OpenGL. Main thread.
void NativeInit(int argc, const char *argv[], const char *savegame_directory, const char *installID);

// Runs after NativeInit() at some point. Can call OpenGL.
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
