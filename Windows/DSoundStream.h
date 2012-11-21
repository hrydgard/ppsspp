#ifndef __SOUNDSTREAM_H__
#define __SOUNDSTREAM_H__

#include <windows.h>

namespace DSound
{
	typedef int (*StreamCallback)(short *buffer, int numSamples, int bits, int rate, int channels);

	bool DSound_StartSound(HWND window, StreamCallback _callback);
	void DSound_UpdateSound();
	void DSound_StopSound();

	float DSound_GetTimer();
	int DSound_GetCurSample();
	int DSound_GetSampleRate();
}

 
#endif //__SOUNDSTREAM_H__