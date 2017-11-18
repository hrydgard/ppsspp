#pragma once

#include "native-audio-so.h"
#include <string>

struct AndroidAudioState;

// This is the file you should include from your program. It dynamically loads
// the native_audio.so shared object and sets up the function pointers.

// Do not call this if you have detected that the android version is below
// 2.2, as it will fail miserably.

// It's okay for optimalFramesPerBuffer and optimalSampleRate to be 0. Defaults will be used.
AndroidAudioState *AndroidAudio_Init(AndroidAudioCallback cb, std::string libraryDir, int optimalFramesPerBuffer, int optimalSampleRate);
bool AndroidAudio_Pause(AndroidAudioState *state);
bool AndroidAudio_Resume(AndroidAudioState *state);
bool AndroidAudio_Shutdown(AndroidAudioState *state);
