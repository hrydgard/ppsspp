#include <xtl.h>
#include <xaudio2.h>
#include "Common/FileUtil.h"
#include "Common/LogManager.h"
#include "Core/PSPMixer.h"
#include "Core/CPU.h"
#include "Core/Config.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/Host.h"
#include "Core/SaveState.h"
#include "Common/MemArena.h"

#include "XaudioSound.h"

#define XAUDIO2_BUFFER_SIZE	2048

static short xaudio_buffer[XAUDIO2_BUFFER_SIZE];

static int byte_per_sample;

static WAVEFORMATEX wfx;
static IXAudio2 *lpXAudio2 = NULL;
static IXAudio2MasteringVoice *lpMasterVoice = NULL;
static IXAudio2SourceVoice *lpSourceVoice = NULL;

static PMixer *g_mixer = 0;
static int NativeMix(short *audio, int num_samples);

class XAudioCallback : public IXAudio2VoiceCallback
{
public:
    void OnBufferEnd(void * pBufferContext) { 
		// Buffer finsihed to play, add new sample to play
		XAudioUpdate();
	}

    //Stubs
    XAudioCallback(){ }
    ~XAudioCallback(){ }
    void OnVoiceProcessingPassEnd() { }
    void OnVoiceProcessingPassStart(UINT32 SamplesRequired) {    }
	void OnStreamEnd()    { }
    void OnBufferStart(void * pBufferContext) {    }
    void OnLoopEnd(void * pBufferContext) {    }
    void OnVoiceError(void * pBufferContext, HRESULT Error) { }
};

static XAudioCallback voice;

int NativeMix(short *audio, int num_samples) {
	// ILOG("Entering mixer");
	if (g_mixer) {
		num_samples = g_mixer->Mix(audio, num_samples);
	}	else {
		memset(audio, 0, num_samples * 2 * sizeof(short));
	}

#if 1//def _XBOX
	// byte swap audio
	unsigned short * ptr = (unsigned short*)audio;
	for(int i = 0; i < num_samples; i++) {
		ptr[i] = _byteswap_ushort(ptr[i]);
	}
#endif

	// ILOG("Leaving mixer");
	return num_samples;
}

void XAudioInit(PMixer *mixer) {
	
	g_mixer = mixer;

	if( FAILED(XAudio2Create( &lpXAudio2, 0 , XAUDIO2_DEFAULT_PROCESSOR ) ) ) {
		DebugBreak();
		return;
	}	

	if ( FAILED( lpXAudio2->CreateMasteringVoice( &lpMasterVoice, 2, 44100, 0, 0, NULL ) )) {
		XAudioShutdown();
		return;
	}	
	
	memset(&wfx, 0, sizeof(WAVEFORMATEX));
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nSamplesPerSec = 44100;
	wfx.nChannels = 2;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
	wfx.cbSize = 0;	
	
	byte_per_sample = 2048;

	if( FAILED(lpXAudio2->CreateSourceVoice( &lpSourceVoice, (WAVEFORMATEX*)&wfx,
                0, XAUDIO2_DEFAULT_FREQ_RATIO, &voice, NULL, NULL ) ) ) {
		XAudioShutdown();
		return;
	}

	lpSourceVoice->FlushSourceBuffers();
	lpSourceVoice->Start( 0, 0 );


	// Submit fake buffer for starting
	XAUDIO2_BUFFER buf = {0};
	buf.AudioBytes = 2048;
	buf.pAudioData = (BYTE*)xaudio_buffer;

	lpSourceVoice->SubmitSourceBuffer( &buf );
}

void XAudioUpdate() {
	int numSamples = NativeMix(xaudio_buffer, byte_per_sample/4);

	if (numSamples > 0) {
		XAUDIO2_BUFFER buf = {0};
		buf.AudioBytes = numSamples * 2;
		buf.pAudioData = (BYTE*)xaudio_buffer;

		lpSourceVoice->SubmitSourceBuffer( &buf );
	}
}

void XAudioShutdown() {
	if( lpSourceVoice ) {
		lpSourceVoice->Stop(0);

		lpSourceVoice->DestroyVoice();
		lpSourceVoice = NULL;
	}

	if( lpMasterVoice ) {
		lpMasterVoice->DestroyVoice();
		lpMasterVoice = NULL;
	}

	if( lpXAudio2 ) {
		lpXAudio2->Release();
		lpXAudio2 = NULL;
	}
}