#include "native/thread/threadutil.h"
#include "Common/CommonWindows.h"

#include <dsound.h>

#include "dsoundstream.h"	

#define BUFSIZE 0x4000
#define MAXWAIT 20   //ms

class DSoundState {
public:
	DSoundState(HWND window, StreamCallback _callback, int sampleRate);
	bool Init();  // If fails, can safely delete the object

	inline int ModBufferSize(int x) { return (x + bufferSize) % bufferSize; }
	int RunThread();
	void UpdateSound();
	void StopSound();
	int GetSampleRate() { return sampleRate; }

private:
	bool CreateBuffer();
	bool WriteDataToBuffer(DWORD dwOffset, // Our own write cursor.
		char* soundData, // Start of our data.
		DWORD dwSoundBytes); // Size of block to copy.

	CRITICAL_SECTION soundCriticalSection;
	HWND window_;
	HANDLE soundSyncEvent = NULL;
	HANDLE hThread = NULL;

	StreamCallback callback;

	IDirectSound8 *ds = NULL;
	IDirectSoundBuffer *dsBuffer = NULL;

	int bufferSize; // bytes
	int totalRenderedBytes;
	int sampleRate;

	volatile int threadData;

	int currentPos;
	int lastPos;
	short realtimeBuffer[BUFSIZE * 2];
};

// TODO: Get rid of this
static DSoundState *g_dsound;

inline int RoundDown128(int x) {
	return x & (~127);
}

int DSound_GetSampleRate() {
	if (g_dsound) {
		return g_dsound->GetSampleRate();
	} else {
		return 0;
	}
}

bool DSoundState::CreateBuffer() {
	PCMWAVEFORMAT pcmwf;
	DSBUFFERDESC dsbdesc;

	memset(&pcmwf, 0, sizeof(PCMWAVEFORMAT));
	memset(&dsbdesc, 0, sizeof(DSBUFFERDESC));

	bufferSize = BUFSIZE;

	pcmwf.wf.wFormatTag = WAVE_FORMAT_PCM;
	pcmwf.wf.nChannels = 2;
	pcmwf.wf.nSamplesPerSec = sampleRate;
	pcmwf.wf.nBlockAlign = 4;
	pcmwf.wf.nAvgBytesPerSec = pcmwf.wf.nSamplesPerSec * pcmwf.wf.nBlockAlign;
	pcmwf.wBitsPerSample = 16;

	dsbdesc.dwSize = sizeof(DSBUFFERDESC);
	dsbdesc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS; // //DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY; 
	dsbdesc.dwBufferBytes = bufferSize;  //FIX32(pcmwf.wf.nAvgBytesPerSec);   //change to set buffer size
	dsbdesc.lpwfxFormat = (WAVEFORMATEX *)&pcmwf;

	if (SUCCEEDED(ds->CreateSoundBuffer(&dsbdesc, &dsBuffer, NULL))) {
		dsBuffer->SetCurrentPosition(0);
		return true;
	} else {
		dsBuffer = NULL;
		return false;
	}
}

bool DSoundState::WriteDataToBuffer(DWORD dwOffset, // Our own write cursor.
																		char* soundData, // Start of our data.
																		DWORD dwSoundBytes) { // Size of block to copy.
	void *ptr1, *ptr2;
	DWORD numBytes1, numBytes2;
	// Obtain memory address of write block. This will be in two parts if the block wraps around.
	HRESULT hr = dsBuffer->Lock(dwOffset, dwSoundBytes, &ptr1, &numBytes1, &ptr2, &numBytes2, 0);

	// If the buffer was lost, restore and retry lock.
	/*
	if (DSERR_BUFFERLOST == hr) {
	dsBuffer->Restore();
	hr=dsBuffer->Lock(dwOffset, dwSoundBytes, &ptr1, &numBytes1, &ptr2, &numBytes2, 0);
	} */
	if (SUCCEEDED(hr)) { 
		memcpy(ptr1, soundData, numBytes1);
		if (ptr2)
			memcpy(ptr2, soundData+numBytes1, numBytes2);

		// Release the data back to DirectSound.
		dsBuffer->Unlock(ptr1, numBytes1, ptr2, numBytes2);
		return true;
	}/* 
		else
		{
		char temp[8];
		sprintf(temp,"%i\n",hr);
		OutputDebugStringUTF8(temp);
		}*/
	return false;
}

unsigned int WINAPI soundThread(void *param) {
	DSoundState *state = (DSoundState *)param;
	return state->RunThread();
}

int DSoundState::RunThread() {
	setCurrentThreadName("DSound");
	currentPos = 0;
	lastPos = 0;
	//writeDataToBuffer(0,realtimeBuffer,bufferSize);
	//  dsBuffer->Lock(0, bufferSize, (void **)&p1, &num1, (void **)&p2, &num2, 0); 

	dsBuffer->Play(0,0,DSBPLAY_LOOPING);

	while (!threadData) {
		EnterCriticalSection(&soundCriticalSection);

		dsBuffer->GetCurrentPosition((DWORD *)&currentPos, 0);
		int numBytesToRender = RoundDown128(ModBufferSize(currentPos - lastPos)); 

		if (numBytesToRender >= 256) {
			int numBytesRendered = 4 * (*callback)(realtimeBuffer, numBytesToRender >> 2, 16, 44100, 2);
			//We need to copy the full buffer, regardless of what the mixer claims to have filled
			//If we don't do this then the sound will loop if the sound stops and the mixer writes only zeroes
			numBytesRendered = numBytesToRender;
			WriteDataToBuffer(lastPos, (char *) realtimeBuffer, numBytesRendered);

			currentPos = ModBufferSize(lastPos + numBytesRendered);
			totalRenderedBytes += numBytesRendered;

			lastPos = currentPos;
		}

		LeaveCriticalSection(&soundCriticalSection);
		WaitForSingleObject(soundSyncEvent, MAXWAIT);
	}
	dsBuffer->Stop();

	threadData = 2;
	return 0;
}

DSoundState::DSoundState(HWND window, StreamCallback _callback, int sampleRate)
	: window_(window), callback(_callback), sampleRate(sampleRate) {

	callback = _callback;
	threadData=0;
}

bool DSoundState::Init() {
	soundSyncEvent = CreateEvent(0, false, false, 0);
	InitializeCriticalSection(&soundCriticalSection);

	if (FAILED(DirectSoundCreate8(0,&ds,0))) {
		CloseHandle(soundSyncEvent);
		DeleteCriticalSection(&soundCriticalSection);
		return false;
	}

	ds->SetCooperativeLevel(window_, DSSCL_PRIORITY);
	if (!CreateBuffer())
		return false;

	DWORD num1;
	short *p1; 

	dsBuffer->Lock(0, bufferSize, (void **)&p1, &num1, 0, 0, 0); 

	memset(p1,0,num1);
	dsBuffer->Unlock(p1,num1,0,0);
	totalRenderedBytes = -bufferSize;
	hThread = (HANDLE)_beginthreadex(0, 0, soundThread, (void *)this, 0, 0);
	SetThreadPriority(hThread, THREAD_PRIORITY_ABOVE_NORMAL);
	return true;
}

void DSoundState::UpdateSound() {
	if (soundSyncEvent != NULL)
		SetEvent(soundSyncEvent);
}


void DSoundState::StopSound() {
	if (!dsBuffer)
		return;

	EnterCriticalSection(&soundCriticalSection);

	if (threadData == 0) {
		threadData = 1;
	}

	if (hThread != NULL) {
		WaitForSingleObject(hThread, 1000);
		CloseHandle(hThread);
		hThread = NULL;
	}

	if (threadData == 2) {
		if (dsBuffer != NULL)
			dsBuffer->Release();
		dsBuffer = NULL;
		if (ds != NULL)
			ds->Release();
		ds = NULL;
	}

	if (soundSyncEvent != NULL) {
		CloseHandle(soundSyncEvent);
	}
	soundSyncEvent = NULL;
	LeaveCriticalSection(&soundCriticalSection);
	DeleteCriticalSection(&soundCriticalSection);
}

void DSound_UpdateSound() {
	if (g_dsound) {
		g_dsound->UpdateSound();
	}
}

bool DSound_StartSound(HWND window, StreamCallback _callback, int sampleRate) {
	g_dsound = new DSoundState(window, _callback, sampleRate);
	if (!g_dsound->Init()) {
		delete g_dsound;
		g_dsound = NULL;
		return false;
	}
	return true;
}

void DSound_StopSound() {
	g_dsound->StopSound();
	delete g_dsound;
	g_dsound = NULL;
}
