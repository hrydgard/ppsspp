#include <windows.h>
#include <dsound.h>

#include "dsoundstream.h"	

namespace DSound
{
#define BUFSIZE 0x4000
#define MAXWAIT 20   //ms

	CRITICAL_SECTION soundCriticalSection;
	HANDLE soundSyncEvent;
	HANDLE hThread;

	StreamCallback callback;

	IDirectSound8 *ds = NULL;
	IDirectSoundBuffer *dsBuffer = NULL;

	int bufferSize; // bytes
	int totalRenderedBytes;
	int sampleRate;

	volatile int threadData;

	inline int RoundDown128(int x)   
	{
		return x & (~127);
	}

	int DSound_GetSampleRate()
	{
		return sampleRate;
	}

	bool createBuffer()
	{
		PCMWAVEFORMAT pcmwf; 
		DSBUFFERDESC dsbdesc; 

		memset(&pcmwf, 0, sizeof(PCMWAVEFORMAT)); 
		memset(&dsbdesc, 0, sizeof(DSBUFFERDESC)); 

		pcmwf.wf.wFormatTag = WAVE_FORMAT_PCM; 
		pcmwf.wf.nChannels = 2; 
		pcmwf.wf.nSamplesPerSec = sampleRate = 44100; 
		pcmwf.wf.nBlockAlign = 4; 
		pcmwf.wf.nAvgBytesPerSec = pcmwf.wf.nSamplesPerSec * pcmwf.wf.nBlockAlign; 
		pcmwf.wBitsPerSample = 16; 

		dsbdesc.dwSize = sizeof(DSBUFFERDESC); 
		dsbdesc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS; // //DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY; 
		dsbdesc.dwBufferBytes = bufferSize = BUFSIZE;  //FIX32(pcmwf.wf.nAvgBytesPerSec);   //change to set buffer size
		dsbdesc.lpwfxFormat = (WAVEFORMATEX *)&pcmwf; 

		if (SUCCEEDED(ds->CreateSoundBuffer(&dsbdesc, &dsBuffer, NULL)))
		{ 
			dsBuffer->SetCurrentPosition(0);
			return true; 
		} 
		else 
		{ 
			dsBuffer = NULL; 
			return false; 
		} 
	}


	bool writeDataToBuffer(DWORD dwOffset, // Our own write cursor.
												 char* soundData, // Start of our data.
												 DWORD dwSoundBytes) // Size of block to copy.
	{ 
		void *ptr1, *ptr2;
		DWORD numBytes1, numBytes2; 
		// Obtain memory address of write block. This will be in two parts if the block wraps around.
		HRESULT hr=dsBuffer->Lock(dwOffset, dwSoundBytes, &ptr1, &numBytes1, &ptr2, &numBytes2, 0);

		// If the buffer was lost, restore and retry lock. 
		/*
		if (DSERR_BUFFERLOST == hr) { 
		dsBuffer->Restore(); 
		hr=dsBuffer->Lock(dwOffset, dwSoundBytes, &ptr1, &numBytes1, &ptr2, &numBytes2, 0); 
		} */
		if (SUCCEEDED(hr))
		{ 
			memcpy(ptr1, soundData, numBytes1); 
			if (ptr2!=0) 
				memcpy(ptr2, soundData+numBytes1, numBytes2); 

			// Release the data back to DirectSound. 
			dsBuffer->Unlock(ptr1, numBytes1, ptr2, numBytes2); 
			return true; 
		}/* 
		 else
		 {
		 char temp[8];
		 sprintf(temp,"%i\n",hr);
		 OutputDebugString(temp);
		 }*/
		return false; 
	} 


	inline int ModBufferSize(int x)
	{
		return (x+bufferSize)%bufferSize;
	}

	int currentPos;
	int lastPos;
	short realtimeBuffer[BUFSIZE * 2];

	unsigned int WINAPI soundThread(void *)
	{
		currentPos = 0;
		lastPos = 0;
		//writeDataToBuffer(0,realtimeBuffer,bufferSize);
		//  dsBuffer->Lock(0, bufferSize, (void **)&p1, &num1, (void **)&p2, &num2, 0); 

		dsBuffer->Play(0,0,DSBPLAY_LOOPING);

		while (!threadData)
		{
			EnterCriticalSection(&soundCriticalSection);

			dsBuffer->GetCurrentPosition((DWORD *)&currentPos, 0);
			int numBytesToRender = RoundDown128(ModBufferSize(currentPos - lastPos)); 

			//renderStuff(numBytesToRender/2);
			//if (numBytesToRender>bufferSize/2) numBytesToRender=0;

			if (numBytesToRender >= 256)
			{
				int numBytesRendered = 4 * (*callback)(realtimeBuffer, numBytesToRender >> 2, 16, 44100, 2);

				if (numBytesRendered != 0)
					writeDataToBuffer(lastPos, (char *)realtimeBuffer, numBytesRendered);

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

	bool DSound_StartSound(HWND window, StreamCallback _callback)
	{
		callback = _callback;
		threadData=0;

		soundSyncEvent=CreateEvent(0,false,false,0);  
		InitializeCriticalSection(&soundCriticalSection);

		if (FAILED(DirectSoundCreate8(0,&ds,0)))
			return false;

		ds->SetCooperativeLevel(window,DSSCL_PRIORITY);
		if (!createBuffer())
			return false;

		DWORD num1;
		short *p1; 

		dsBuffer->Lock(0, bufferSize, (void **)&p1, &num1, 0, 0, 0); 

		memset(p1,0,num1);
		dsBuffer->Unlock(p1,num1,0,0);
		totalRenderedBytes = -bufferSize;
		hThread = (HANDLE)_beginthreadex(0, 0, soundThread, 0, 0, 0);
		SetThreadPriority(hThread, THREAD_PRIORITY_ABOVE_NORMAL);
		return true;
	}


	void DSound_UpdateSound()
	{
		SetEvent(soundSyncEvent);
	}


	void DSound_StopSound()
	{
		threadData=1;
		WaitForSingleObject(hThread,1000);
		CloseHandle(hThread);
		/*
		while (threadData!=2)
			;*/
		if (dsBuffer != NULL)
			dsBuffer->Release();
		if (ds != NULL)
			ds->Release();

		CloseHandle(soundSyncEvent);
	}


	int DSound_GetCurSample()
	{
		EnterCriticalSection(&soundCriticalSection);
		int playCursor;
		dsBuffer->GetCurrentPosition((DWORD *)&playCursor,0);
		playCursor = ModBufferSize(playCursor-lastPos)+totalRenderedBytes;
		LeaveCriticalSection(&soundCriticalSection);
		return playCursor;
	}



	float DSound_GetTimer()
	{
		return (float)DSound_GetCurSample()*(1.0f/(4.0f*44100.0f));
	}

}
