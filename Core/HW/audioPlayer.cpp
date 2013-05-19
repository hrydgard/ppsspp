#ifdef _USE_DSHOW_

#include "audioPlayer.h"
#include <dshow.h>
#include "qeditsimple.h"
#pragma comment(lib, "Strmiids.lib")
#pragma comment(lib, "Quartz.lib")

#include "OMAConvert.h"
#include <map>
#include "StdMutex.h"

#include "../Core/System.h"

#define JIF(x) if (FAILED(hr=(x))) \
    {return false;}
#define KIF(x) if (FAILED(hr=(x))) \
    {return hr;}
#define LIF(x) if (FAILED(hr=(x))) \
    {}

// {2AE44C10-B451-4B01-9BBE-A5FBEF68C9D4}
static const GUID CLSID_AsyncStreamSource = 
{ 0x2ae44c10, 0xb451, 0x4b01, { 0x9b, 0xbe, 0xa5, 0xfb, 0xef, 0x68, 0xc9, 0xd4 } };

// {268424D1-B6E9-4B28-8751-B7774F5ECF77}
static const GUID IID_IStreamSourceFilter = 
{ 0x268424d1, 0xb6e9, 0x4b28, { 0x87, 0x51, 0xb7, 0x77, 0x4f, 0x5e, 0xcf, 0x77 } };

// We define the interface the app can use to program us
MIDL_INTERFACE("268424D1-B6E9-4B28-8751-B7774F5ECF77")
IStreamSourceFilter : public IFileSourceFilter
{
public:
	virtual STDMETHODIMP LoadStream(void *stream, LONGLONG readSize, LONGLONG streamSize, AM_MEDIA_TYPE *pmt ) = 0;
	virtual STDMETHODIMP AddStreamData(LONGLONG offset, void *stream, LONGLONG addSize) = 0;
	virtual STDMETHODIMP GetStreamInfo(LONGLONG *readSize, LONGLONG *streamSize) = 0;
	virtual STDMETHODIMP SetReadSize(LONGLONG readSize) = 0;
	virtual BOOL STDMETHODCALLTYPE IsReadPassEnd() = 0;
};

HRESULT GetPin(IBaseFilter *pFilter, PIN_DIRECTION PinDir, IPin **ppPin)  
{  
    IEnumPins  *pEnum;  
    IPin       *pPin;  
    pFilter->EnumPins(&pEnum);  
    while(pEnum->Next(1, &pPin, 0) == S_OK)  
    {  
        PIN_DIRECTION PinDirThis;  
        pPin->QueryDirection(&PinDirThis);  
        if (PinDir == PinDirThis)  
        {  
            pEnum->Release();  
            *ppPin = pPin;  
            return S_OK;  
        }  
        pPin->Release();  
    }  
    pEnum->Release();  
    return E_FAIL;    
}  
   
HRESULT ConnectFilters(IGraphBuilder *pGraph, IBaseFilter *pFirst, IBaseFilter *pSecond)  
{  
    IPin *pOut = NULL, *pIn = NULL;  
    HRESULT hr = GetPin(pFirst, PINDIR_OUTPUT, &pOut);  
    if (FAILED(hr)) return hr;  
    hr = GetPin(pSecond, PINDIR_INPUT, &pIn);  
    if (FAILED(hr))   
    {  
        pOut->Release();  
        return E_FAIL;  
     }  
    hr = pGraph->Connect(pOut, pIn);  
    pIn->Release();  
    pOut->Release();  
    return hr;  
}

class CSampleGrabberCallback : public ISampleGrabberCB
{
public:
	CSampleGrabberCallback(audioPlayer *player) 
	{
		m_player = player;
		// 1MB round buffer size
		m_bufsize = 0x100000;
		m_buf = new u8[m_bufsize];
		isNeedReset = true;
		m_readPos = m_writePos = 0;
	}
	~CSampleGrabberCallback(){ if (m_buf) delete [] m_buf; }
	STDMETHODIMP_(ULONG) AddRef() { return 2; }
    STDMETHODIMP_(ULONG) Release() { return 1; }

	STDMETHODIMP QueryInterface(REFIID riid, void **ppvObject)
    {
		return S_OK;
    }
	
	STDMETHODIMP SampleCB(double Time, IMediaSample *pSample)
    {
		return S_OK;
    }

    STDMETHODIMP BufferCB(double Time, BYTE *pBuffer, long buflen)
    {
		m_mutex.lock();
		if (isNeedReset) {
			isNeedReset = false;
			m_readPos = 0;
			m_writePos = 0;
		}
		if (m_writePos + buflen <= m_bufsize) {
			memcpy(m_buf + m_writePos, pBuffer, buflen);
		} else {
			int size = m_bufsize - m_writePos;
			memcpy(m_buf + m_writePos, pBuffer, size);
			memcpy(m_buf, pBuffer + size, buflen - size);
		}
		m_writePos = (m_writePos + buflen) % m_bufsize;

		// check how much space left to write
		int space = (m_readPos - m_writePos + m_bufsize) % m_bufsize;
		m_mutex.unlock();
		if (space < buflen * 3) {
			m_player->pause();
		}
        return S_OK; 
    }

	int getNextSamples(u8* buf, int wantedbufsize) {
		int timecount = 0;
		while (isNeedReset) {
			Sleep(1);
			timecount++;
			if (timecount >= 10)
				return 0;
		}
		m_mutex.lock();
		// check how much space left to read
		int space = (m_writePos - m_readPos + m_bufsize) % m_bufsize;
		if (m_readPos + wantedbufsize <= m_bufsize) {
			memcpy(buf, m_buf + m_readPos, wantedbufsize);
		} else {
			int size = m_bufsize - m_readPos;
			memcpy(buf, m_buf + m_readPos, size);
			memcpy(buf + size, m_buf, wantedbufsize - size);
		}
		int bytesgot = min(wantedbufsize, space);
		m_readPos = (m_readPos + bytesgot) % m_bufsize;

		// check how much space left to read
		space = (m_writePos - m_readPos + m_bufsize) % m_bufsize;
		m_mutex.unlock();
		if (space < wantedbufsize * 3) {
			m_player->play();
		}
		return bytesgot;
	}

	void setResetflag(bool reset) { isNeedReset = reset; }
private:
	audioPlayer *m_player;
	u8* m_buf;
	int m_bufsize;
	int m_readPos;
	int m_writePos;
	bool isNeedReset;
	std::recursive_mutex m_mutex;
};

bool addSampleGrabber(IGraphBuilder *pGB, IBaseFilter *pSrc, 
	ISampleGrabberCB *callback, void **outgrabber)
{
	HRESULT hr;
	ISampleGrabber *pGrabber = 0;
	IBaseFilter *pGrabberF = 0;
	JIF(CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (void **)&pGrabberF));
	JIF(pGB->AddFilter(pGrabberF, L"Sample Grabber"));
	JIF(pGrabberF->QueryInterface(IID_ISampleGrabber, (void**)&pGrabber));

	AM_MEDIA_TYPE mt;
	ZeroMemory(&mt, sizeof(AM_MEDIA_TYPE));
	mt.majortype = MEDIATYPE_Audio;
	mt.subtype = MEDIASUBTYPE_PCM;
	JIF(pGrabber->SetMediaType(&mt));

	JIF(ConnectFilters(pGB, pSrc, pGrabberF));
	pGrabberF->Release();

	IBaseFilter *pNull = NULL;
	JIF(CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (void**)&pNull));
	JIF(pGB->AddFilter(pNull, L"NullRenderer"));
	JIF(ConnectFilters(pGB, pGrabberF, pNull));

	// Set one-shot mode and buffering.
	JIF(pGrabber->SetOneShot(FALSE));
	JIF(pGrabber->SetBufferSamples(TRUE));

	JIF(pGrabber->SetCallback(callback, 1));

	// close the clock to run as fast as possible
	IMediaFilter *pMediaFilter = 0;
    pGB->QueryInterface(IID_IMediaFilter, (void**)&pMediaFilter);
    pMediaFilter->SetSyncSource(0);
    pMediaFilter->Release();

	*outgrabber = (void*)pGrabber;

	return true;
}

static volatile int g_volume = 60;

audioPlayer::audioPlayer(void)
{
	m_playmode = -1;
	m_volume = g_volume;
	m_pMC = 0;
	m_pGB = 0;
	m_pMS = 0;
	m_pGrabber = 0;
	m_pGrabberCB = 0;
	m_pStreamReader = 0;
}


audioPlayer::~audioPlayer(void)
{
	closeMedia();
}

bool audioPlayer::load(const char* filename, u8* stream, int readSize, int streamSize, bool samplebuffermode, bool isWave)
{
	if (m_playmode == 1) 
		return false;
	WCHAR wstrfilename[MAX_PATH + 1];
	MultiByteToWideChar( CP_ACP, 0, filename ? filename : "stream", -1, 
         wstrfilename, MAX_PATH );
	wstrfilename[MAX_PATH] = 0;

	HRESULT hr;
	JIF(CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
		IID_IGraphBuilder, (void **)&m_pGB));
	IGraphBuilder *pGB=(IGraphBuilder*)m_pGB;
	JIF(pGB->QueryInterface(IID_IMediaControl, (void **)&m_pMC));
	JIF(pGB->QueryInterface(IID_IMediaSeeking, (void **)&m_pMS));

	IBaseFilter *pSrc = 0;
	JIF(CoCreateInstance(CLSID_AsyncStreamSource, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (void**)&pSrc));
	JIF(pGB->AddFilter(pSrc,wstrfilename));
	JIF(pSrc->QueryInterface(IID_IStreamSourceFilter,(void**)&m_pStreamReader));
	IStreamSourceFilter* pStreamReader = (IStreamSourceFilter*)m_pStreamReader;
	AM_MEDIA_TYPE mt;
	ZeroMemory(&mt, sizeof(mt));
	mt.majortype = MEDIATYPE_Stream;
	mt.subtype = isWave ? MEDIASUBTYPE_WAVE : MEDIASUBTYPE_NULL;
	if (filename) {
		JIF(pStreamReader->Load(wstrfilename, &mt));
	} else {
		JIF(pStreamReader->LoadStream(stream, readSize, streamSize, &mt));
	}
	if (samplebuffermode) {
		m_pGrabberCB = new CSampleGrabberCallback(this);
		addSampleGrabber(pGB, pSrc, (ISampleGrabberCB*)m_pGrabberCB, &m_pGrabber);
		pSrc->Release();
		m_playmode = 0;
		play();
	} else {
		IPin *pOut;
		JIF(GetPin(pSrc, PINDIR_OUTPUT, &pOut));
		pSrc->Release();
		JIF(pGB->Render(pOut));
		pOut->Release();
		setVolume(m_volume);
		m_playmode = 0;
	}

	IMediaSeeking *pMS=(IMediaSeeking*)m_pMS;
	m_startpos = 0;
	JIF(pMS->GetStopPosition(&m_endpos));
	return true;
}

bool audioPlayer::play()
{
	if ((!m_pMC) || (m_playmode == -1))
		return false;
	IMediaControl *pMC = (IMediaControl*)m_pMC;
	HRESULT hr;
	JIF(pMC->Run());
	m_playmode = 1;
	return true;
}

bool audioPlayer::pause()
{
	if ((!m_pMC) || (m_playmode == -1))
		return false;
	IMediaControl *pMC = (IMediaControl*)m_pMC;
	HRESULT hr;
	JIF(pMC->Pause());
	m_playmode = 2;
	return true;
}

bool audioPlayer::stop()
{
	if ((!m_pMC) || (m_playmode <= 0))
		return true;
	IMediaControl *pMC = (IMediaControl*)m_pMC;
	HRESULT hr;
	JIF(pMC->Stop());
	m_playmode = 0;
	return true;
}

bool audioPlayer::closeMedia()
{
	if (m_pGrabber) {
		ISampleGrabber *pGrabber = (ISampleGrabber *)m_pGrabber;
		pGrabber->SetCallback(0, 1);
		pGrabber->Release();
	}
	if (m_pGrabberCB) 
		delete ((CSampleGrabberCallback*)m_pGrabberCB);
	m_pGrabber = 0;
	m_pGrabberCB = 0;

	stop();
	if (m_pMS)
		((IMediaSeeking*)m_pMS)->Release();
	if (m_pMC)
		((IMediaControl*)m_pMC)->Release();
	if (m_pStreamReader)
		((IStreamSourceFilter*)m_pStreamReader)->Release();
	if (m_pGB)
		((IGraphBuilder*)m_pGB)->Release();
	m_pMS = 0;
	m_pMC = 0;
	m_pStreamReader = 0;
	m_pGB = 0;
	m_playmode = -1;
	return true;
}

bool audioPlayer::setVolume(int volume)
{
	if ((volume < 0) || (volume > 100))
		return false;
	m_volume = volume;
	if (!m_pGB)
		return true;
	IBasicAudio *pBA = NULL;
	HRESULT hr;
	int now = -(int)(exp(log((double)10001)/100*(100-volume))-1+0.5);
	JIF(((IGraphBuilder*)m_pGB)->QueryInterface(IID_IBasicAudio, (void**)&pBA));
	pBA->put_Volume(now);
	pBA->Release();
	return true;
}

bool audioPlayer::isEnd(long *mstimetoend)
{
	if (!m_pMS)
		return false;
	IMediaSeeking *pMS=(IMediaSeeking*)m_pMS;
	LONGLONG curpos;
	HRESULT hr;
	JIF(pMS->GetCurrentPosition(&curpos));
	if (curpos >= m_endpos)
		return true;
	if (mstimetoend)
		*mstimetoend = (m_endpos - curpos) / 10000;
	return false;
}

bool audioPlayer::setPlayPos(long ms)
{
	if (!m_pGB)
		return false;
	HRESULT hr;
	IMediaSeeking *pMS = (IMediaSeeking*)m_pMS;
	LONGLONG pos = ((LONGLONG)ms)*10000;
	if (!m_pGrabberCB) {
		JIF(pMS->SetPositions(&pos, AM_SEEKING_AbsolutePositioning,
			NULL, AM_SEEKING_NoPositioning));
	} else {
		pause();
		JIF(pMS->SetPositions(&pos, AM_SEEKING_AbsolutePositioning,
			NULL, AM_SEEKING_NoPositioning));
		((CSampleGrabberCallback*)m_pGrabberCB)->setResetflag(true);
		play();
	}
	return true;
}

bool audioPlayer::getPlayPos(long *ms)
{
	if (!m_pGB)
		return false;
	HRESULT hr;
	IMediaSeeking *pMS = (IMediaSeeking*)m_pMS;
	LONGLONG curpos;
	JIF(pMS->GetCurrentPosition(&curpos));
	if (ms)
		*ms = curpos / 10000;
	return true;
}

int audioPlayer::getNextSamples(u8* buf, int wantedbufsize)
{
	if (!m_pGrabberCB || !m_pMC) {
		memset(buf, 0, wantedbufsize);
		return wantedbufsize;
	}
	return ((CSampleGrabberCallback*)m_pGrabberCB)->getNextSamples(buf, wantedbufsize);
}

////////////////////////////////////////////////////////////////////////
// audioEngine

bool audioEngine::loadRIFFStream(u8* stream, int streamsize, int atracID)
{
	u8 *oma = 0;
	m_ID = atracID;
	m_channel = OMAConvert::getRIFFChannels(stream, streamsize);
	bool bResult = false;
	if (m_channel != 1) {
		int readsize = 0;
		int omasize = OMAConvert::convertRIFFtoOMA(stream, streamsize, &oma, &readsize);
		if (omasize > 0){
			bResult = load(0, oma, readsize, omasize, true);
			OMAConvert::releaseStream(&oma);
		}
	}
	return bResult;
}

bool audioEngine::closeStream()
{
	bool bResult = closeMedia();
	m_ID = -1;
	return bResult;
}

bool audioEngine::setPlaySample(int sample)
{
	return setPlayPos(((s64)sample) * 1000 / 44100);
}

void audioEngine::addStreamData(int offset, u8* buf, int size, int cursample)
{
	if ((!m_pGB) || (m_channel == 1))
		return;
	IStreamSourceFilter* pStreamReader = (IStreamSourceFilter*)m_pStreamReader;

	pStreamReader->AddStreamData(offset, buf, size);

	bool bsetpos = pStreamReader->IsReadPassEnd();
	if (bsetpos)
		setPlaySample(cursample);
}

//////////////////////////////////////////////////////////////////////////////
//

std::map<int, audioEngine*> audioMap;
std::recursive_mutex atracsection;

void addAtrac3Audio(u8* stream, int streamsize, int atracID)
{
	if (audioMap.find(atracID) != audioMap.end()) 
		return;
	audioEngine *temp = new audioEngine();
	bool bResult = temp->loadRIFFStream(stream, streamsize, atracID);
	atracsection.lock();
	audioMap[atracID] = temp;
	atracsection.unlock();
	if (!bResult)
		temp->closeMedia();
}

audioEngine* getaudioEngineByID(int atracID)
{
	if (audioMap.find(atracID) == audioMap.end()) {
		return NULL;
	}
	return audioMap[atracID];
}

void deleteAtrac3Audio(int atracID)
{
	atracsection.lock();
	if (audioMap.find(atracID) != audioMap.end()) {
		delete audioMap[atracID];
		audioMap.erase(atracID);
	}
	atracsection.unlock();
}

void initaudioEngine()
{
	CoInitialize(0);
}

void shutdownEngine()
{
	atracsection.lock();
	for (auto it = audioMap.begin(); it != audioMap.end(); ++it) {
		delete it->second;
	}
	audioMap.clear();
	atracsection.unlock();
	CoUninitialize();
	system("cleanAudios.bat");
}

#endif // _USE_DSHOW_