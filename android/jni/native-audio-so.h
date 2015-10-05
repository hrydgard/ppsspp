#pragma once

typedef int (*AndroidAudioCallback)(short *buffer, int num_samples);

bool OpenSLWrap_Init(AndroidAudioCallback cb, int _FramesPerBuffer, int _SampleRate);
void OpenSLWrap_Shutdown();