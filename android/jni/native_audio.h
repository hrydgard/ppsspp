#pragma once

#include "native-audio-so.h"
#include <string>

struct AndroidAudioState;

// It's okay for optimalFramesPerBuffer and optimalSampleRate to be 0. Defaults will be used.
AndroidAudioState *AndroidAudio_Init(AndroidAudioCallback cb, int optimalFramesPerBuffer, int optimalSampleRate);
bool AndroidAudio_Pause(AndroidAudioState *state);
bool AndroidAudio_Resume(AndroidAudioState *state);
bool AndroidAudio_Shutdown(AndroidAudioState *state);
