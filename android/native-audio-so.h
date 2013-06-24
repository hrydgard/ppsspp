#pragma once

// Header for dynamic loading

typedef int (*AndroidAudioCallback)(short *buffer, int num_samples);

typedef bool (*OpenSLWrap_Init_T)(AndroidAudioCallback cb, int framesPerBuffer, int sampleRate);
typedef void (*OpenSLWrap_Shutdown_T)();
