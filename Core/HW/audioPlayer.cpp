#ifdef _USE_DSHOW_

#include "audioPlayer.h"
#include <dshow.h>
#pragma comment(lib, "Strmiids.lib")
#pragma comment(lib, "Quartz.lib")

#include "OMAConvert.h"
#include <map>
#include <process.h>

#include "../Core/System.h"

#define JIF(x) if (FAILED(hr=(x))) \
    {return false;}
#define KIF(x) if (FAILED(hr=(x))) \
    {return hr;}
#define LIF(x) if (FAILED(hr=(x))) \
    {}

static volatile int g_volume = 20;

audioPlayer::audioPlayer(void)
{
	m_playmode = -1;
	m_volume = g_volume;
	m_pMC = 0;
	m_pGB = 0;
	m_pMS = 0;
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
}


audioPlayer::~audioPlayer(void)
{
	closeMedia();
	CoUninitialize();
}

bool audioPlayer::load(const char* filename)
{
	if (m_playmode == 1) 
		return false;
	WCHAR wstrfilename[MAX_PATH + 1];
	MultiByteToWideChar( CP_ACP, 0, filename, -1, 
         wstrfilename, MAX_PATH );
	wstrfilename[MAX_PATH] = 0;

	HRESULT hr;
	JIF(CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
		IID_IGraphBuilder, (void **)&m_pGB));
	IGraphBuilder *pGB=(IGraphBuilder*)m_pGB;
	JIF(pGB->QueryInterface(IID_IMediaControl, (void **)&m_pMC));
	JIF(pGB->QueryInterface(IID_IMediaSeeking, (void **)&m_pMS));

	JIF(pGB->RenderFile(wstrfilename, 0));
	setVolume(m_volume);
	m_playmode = 0;

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
	stop();
	if (m_pMS)
		((IMediaSeeking*)m_pMS)->Release();
	if (m_pMC)
		((IMediaControl*)m_pMC)->Release();
	if (m_pGB)
		((IGraphBuilder*)m_pGB)->Release();
	m_pMS = 0;
	m_pMC = 0;
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

bool audioPlayer::isEnd()
{
	if (!m_pMS)
		return false;
	IMediaSeeking *pMS=(IMediaSeeking*)m_pMS;
	LONGLONG stoppos, curpos;
	HRESULT hr;
	JIF(pMS->GetStopPosition(&stoppos));
	JIF(pMS->GetCurrentPosition(&curpos));
	if (curpos >= stoppos)
		return true;
	return false;
}

bool audioPlayer::setPlayPos(long ms)
{
	if (!m_pGB)
		return false;
	HRESULT hr;
	IMediaSeeking *pMS = (IMediaSeeking*)m_pMS;
	LONGLONG pos = ((LONGLONG)ms)*10000;
	JIF(pMS->SetPositions(&pos, AM_SEEKING_AbsolutePositioning,
		NULL, AM_SEEKING_NoPositioning));
	return true;
}

////////////////////////////////////////////////////////////////////////
// audioEngine

bool audioEngine::loadRIFFStream(u8* stream, int streamsize, int atracID)
{
	u8 *oma = 0;
	m_ID = atracID;
	int omasize = OMAConvert::convertRIFFtoOMA(stream, streamsize, &oma);
	if (omasize <= 0) {
		char strtemp[260];
		sprintf(m_filename, "tmp\\%d.at3", m_ID);
		FILE *wfp = fopen(m_filename, "wb");
		fwrite(stream, 1, streamsize, wfp);
		fclose(wfp);
		sprintf(strtemp, "at3tool\\at3tool.exe -d tmp\\%d.at3 tmp\\%d.wav", m_ID, m_ID);
		system(strtemp);
		DeleteFileA(m_filename);

		sprintf(m_filename, "tmp\\%d.wav", m_ID);
		wfp = fopen(m_filename, "rb");
		if (!wfp) {
			m_ID = -1;
			return false;
		}
		fclose(wfp);
	} else {
		sprintf(m_filename, "tmp\\%d.oma", m_ID);
		FILE *wfp = fopen(m_filename, "wb");
		fwrite(oma, 1, omasize, wfp);
		fclose(wfp);
		OMAConvert::releaseStream(&oma);
	}

	m_iloop = OMAConvert::getRIFFLoopNum(stream, streamsize);

	return load(m_filename);
}

bool audioEngine::closeStream()
{
	bool bResult = closeMedia();
	if (m_ID >= 0)
		DeleteFileA(m_filename);
	m_ID = -1;
	return bResult;
}

void audioEngine::setLoop(int iloop)
{
	m_iloop = iloop;
}

void audioEngine::decLoopcount()
{
	if (m_iloop > 0) m_iloop--;
}

bool audioEngine::isneedLoop()
{
	return (m_iloop != 0);
}

//////////////////////////////////////////////////////////////////////////////
//

std::map<int, audioEngine*> audioMap;
static bool bFirst = true;

UINT WINAPI loopAtrac3Audio(LPVOID)
{
	while (1)
	{
		bool bChangeVolume = false;
		if (GetAsyncKeyState('1') < 0 && g_volume > 0)
		{
			g_volume--;
			bChangeVolume = true;
		}
		else if (GetAsyncKeyState('2') < 0 && g_volume < 100)
		{
			g_volume++;
			bChangeVolume = true;
		}
		for (auto it = audioMap.begin(); it != audioMap.end(); ++it) {
			audioEngine *temp = it->second;
			if (temp->isneedLoop() && temp->isEnd())
			{
				temp->decLoopcount();
				temp->setPlayPos(0);
				temp->play();
			}
			if (bChangeVolume)
				temp->setVolume(g_volume);
		}
		Sleep(300);
	}
	return 0;
}

void addAtrac3Audio(u8* stream, int streamsize, int atracID)
{
	if (bFirst)
	{
		CreateDirectory("tmp", NULL);
		UINT uiThread;
		HANDLE hThread=(HANDLE)::_beginthreadex(NULL, 0, loopAtrac3Audio,
			                                   0, 0, &uiThread);
		CloseHandle(hThread);
		bFirst = false;
	}
	if (audioMap.find(atracID) != audioMap.end()) 
		return;
	audioEngine *temp = new audioEngine();
	bool bResult = temp->loadRIFFStream(stream, streamsize, atracID);
	audioMap[atracID] = temp;
	if (!bResult)
		temp->closeMedia();
}

bool addAtrac3AudioByPackage(const char* package, u32 startpos, int audiosize, 
	u8* buffer, int atracID)
{
	u32 h = pspFileSystem.OpenFile(package, (FileAccess) FILEACCESS_READ);
	if (h == 0) 
		return false;
	int filesize = OMAConvert::getRIFFSize(buffer, 0x2000) + 0x2000;
	if (filesize == 0x2000)
		return false;
	u8* buf = new u8[filesize];
	pspFileSystem.SeekFile(h, startpos, FILEMOVE_BEGIN);
	if (strlen(package) >= 10)
		filesize = pspFileSystem.ReadFile(h, buf, filesize);
	else
		pspFileSystem.ReadFile(h, buf, filesize / 2048);
	pspFileSystem.CloseFile(h);

	bool bResult = true;
	if (memcmp(buffer, buf , 0x20) == 0)
		addAtrac3Audio(buf, filesize, atracID);
	else
		bResult = false;
	delete [] buf;
	return bResult;
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
	if (audioMap.find(atracID) != audioMap.end()) {
		delete audioMap[atracID];
		audioMap.erase(atracID);
	}
}

void shutdownEngine()
{
	for (auto it = audioMap.begin(); it != audioMap.end(); ++it) {
		delete it->second;
	}
	audioMap.clear();
}

void stopAllAtrac3Audio()
{
	for (auto it = audioMap.begin(); it != audioMap.end(); ++it) {
		audioEngine *temp = it->second;
		temp->stop();
		temp->setPlayPos(0);
	}
}

#endif // _USE_DSHOW_