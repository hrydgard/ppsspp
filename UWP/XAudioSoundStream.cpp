#include "pch.h"
#include <XAudio2.h>

#include "thread/threadutil.h"
#include "XAudioSoundStream.h"

#include <process.h>

#define BUFSIZE 0x80000U

class XAudioBackend : public WindowsAudioBackend {
public:
  XAudioBackend();
  ~XAudioBackend() override;

  bool Init(HWND window, StreamCallback callback, int sampleRate ) override;  // If fails, can safely delete the object
  void Update() override;
  int GetSampleRate() override { return sampleRate_; }

private:
  inline int ModBufferSize( int x ) { return ( x + BUFSIZE ) % BUFSIZE; }
  bool RunSound();
  bool CreateBuffer();
  bool WriteDataToBuffer( char* soundData, DWORD soundBytes );
  void PollLoop();

  StreamCallback callback_;

  IXAudio2*               xaudioDevice;
  IXAudio2MasteringVoice* xaudioMaster;
  IXAudio2SourceVoice*    xaudioVoice;

  int sampleRate_;

  char realtimeBuffer_[ BUFSIZE ];
  unsigned cursor_;

  HANDLE thread_;
  HANDLE exitEvent_;

  bool exit = false;
};

// TODO: Get rid of this
static XAudioBackend *g_dsound;

inline int RoundDown128( int x ) {
  return x & ( ~127 );
}

bool XAudioBackend::CreateBuffer() {
  if FAILED( xaudioDevice->CreateMasteringVoice( &xaudioMaster, 2, sampleRate_, 0, 0, NULL ) )
    return false;

  WAVEFORMATEX waveFormat;
  waveFormat.cbSize = sizeof( waveFormat );
  waveFormat.nAvgBytesPerSec = sampleRate_ * 4;
  waveFormat.nBlockAlign     = 4;
  waveFormat.nChannels       = 2;
  waveFormat.nSamplesPerSec  = sampleRate_;
  waveFormat.wBitsPerSample  = 16;
  waveFormat.wFormatTag      = WAVE_FORMAT_PCM;

  if FAILED( xaudioDevice->CreateSourceVoice( &xaudioVoice, &waveFormat, 0, XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, nullptr, nullptr ) )
    return false;

  return true;
}

bool XAudioBackend::RunSound() {
  if FAILED( XAudio2Create( &xaudioDevice, 0, XAUDIO2_DEFAULT_PROCESSOR ) ) {
    xaudioDevice = NULL;
    return false;
  }

  XAUDIO2_DEBUG_CONFIGURATION dbgCfg;
  ZeroMemory( &dbgCfg, sizeof( dbgCfg ) );
  dbgCfg.TraceMask = XAUDIO2_LOG_WARNINGS | XAUDIO2_LOG_DETAIL;
  //dbgCfg.BreakMask = XAUDIO2_LOG_ERRORS;
  xaudioDevice->SetDebugConfiguration( &dbgCfg );

  if ( !CreateBuffer() ) {
    xaudioDevice->Release();
    xaudioDevice = NULL;
    return false;
  }

  cursor_ = 0;

  if FAILED( xaudioVoice->Start( 0, XAUDIO2_COMMIT_NOW ) ) {
    xaudioDevice->Release();
    xaudioDevice = NULL;
    return false;
  }

  thread_ = (HANDLE)_beginthreadex( 0, 0, []( void* param ) 
  {
		setCurrentThreadName("XAudio2");
    XAudioBackend *backend = (XAudioBackend *)param;
    backend->PollLoop();
    return 0U;
  }, ( void * )this, 0, 0 );
  SetThreadPriority( thread_, THREAD_PRIORITY_ABOVE_NORMAL );

  return true;
}

XAudioBackend::XAudioBackend() : xaudioDevice( nullptr ) {
  exitEvent_ = CreateEvent( nullptr, true, true, L"" );
}

XAudioBackend::~XAudioBackend() {
  if ( !xaudioDevice )
    return;

  if ( !xaudioVoice )
    return;

  exit = true;
  WaitForSingleObject( exitEvent_, INFINITE );
  CloseHandle( exitEvent_ );

  xaudioDevice->Release();
}

bool XAudioBackend::Init(HWND window, StreamCallback _callback, int sampleRate ) {
  callback_ = _callback;
  sampleRate_ = sampleRate;
  return RunSound();
}

void XAudioBackend::Update() {
}

bool XAudioBackend::WriteDataToBuffer( char* soundData, DWORD soundBytes ) {
  XAUDIO2_BUFFER xaudioBuffer;
  ZeroMemory( &xaudioBuffer, sizeof( xaudioBuffer ) );
  xaudioBuffer.pAudioData = (const BYTE*)soundData;
  xaudioBuffer.AudioBytes = soundBytes;

  if FAILED( xaudioVoice->SubmitSourceBuffer( &xaudioBuffer, NULL ) )
    return false;

  return true;
}

void XAudioBackend::PollLoop()
{
  ResetEvent( exitEvent_ );

  while ( !exit )
  {
    XAUDIO2_VOICE_STATE state;
    xaudioVoice->GetState( &state );

    if ( state.BuffersQueued < 1 )
    {
      int a = 0;
      a++;
    }

    unsigned bytesRequired = ( sampleRate_ * 4 ) / 100;

    while ( bytesRequired )
    {
      unsigned bytesLeftInBuffer = BUFSIZE - cursor_;
      unsigned readCount = std::min( bytesRequired, bytesLeftInBuffer );

      int numBytesRendered = 4 * ( *callback_ )( (short*)&realtimeBuffer_[ cursor_ ], readCount / 4, 16, sampleRate_, 2 );

      WriteDataToBuffer( &realtimeBuffer_[ cursor_ ], numBytesRendered );
      cursor_ += numBytesRendered;
      if ( cursor_ >= BUFSIZE )
      {
        cursor_ = 0;
        bytesLeftInBuffer = BUFSIZE;
      }

      bytesRequired -= numBytesRendered;
    }

    Sleep( 2 );
  }

  SetEvent( exitEvent_ );
}

WindowsAudioBackend *CreateAudioBackend( AudioBackendType type ) {
  return new XAudioBackend();
}