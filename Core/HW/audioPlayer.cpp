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

static volatile int g_volume = 60;

audioPlayer::audioPlayer(void)
{
	m_playmode = -1;
	m_volume = g_volume;
	m_pMC = 0;
	m_pGB = 0;
	m_pMS = 0;
}


audioPlayer::~audioPlayer(void)
{
	closeMedia();
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
	JIF(pMS->SetPositions(&pos, AM_SEEKING_AbsolutePositioning,
		NULL, AM_SEEKING_NoPositioning));
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

////////////////////////////////////////////////////////////////////////
// audioEngine

bool audioEngine::loadRIFFStream(u8* stream, int streamsize, int atracID)
{
	u8 *oma = 0;
	m_ID = atracID;
	bool isexist = checkAudioFileExist(stream, m_filename);
	if (!isexist) {
		int omasize = OMAConvert::convertRIFFtoOMA(stream, streamsize, &oma);
		if (omasize <= 0) {
			int pos = generateAudioFilename(stream, m_filename);
			char strtemp[260];
			sprintf(strtemp, "%s.at3", m_filename);
			FILE *wfp = fopen(strtemp, "wb");
			fwrite(stream, 1, streamsize, wfp);
			fclose(wfp);
			sprintf(strtemp, "at3tool\\at3tool.exe -d -repeat 1 %s.at3 %s.wav", m_filename, m_filename);
			system(strtemp);
			sprintf(strtemp, "%s.at3", m_filename);
			DeleteFileA(strtemp);

			memcpy(m_filename + pos, ".wav", 5);
			wfp = fopen(m_filename, "rb");
			if (!wfp) {
				m_ID = -1;
				return false;
			}
			fclose(wfp);
		} else {
			int pos = generateAudioFilename(stream, m_filename);
			memcpy(m_filename + pos, ".oma", 5);
			FILE *wfp = fopen(m_filename, "wb");
			fwrite(oma, 1, omasize, wfp);
			fclose(wfp);
			OMAConvert::releaseStream(&oma);
		}
	}

	m_iloop = 0;

	bool bResult = load(m_filename);
	if (bResult) {
		m_stopPosforAudio = m_endpos;
		int startsample, endsample;
		m_iloop = OMAConvert::getRIFFLoopNum(stream, streamsize, &startsample, &endsample);
		if (m_iloop != 0) {
			setLoopStart(startsample);
			setLoopEnd(endsample);
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

bool audioEngine::setLoopStart(int sample)
{
	m_startpos = ((s64)sample) * 1000 * 10000 / 44100;
	return true;
}

bool audioEngine::setLoopEnd(int sample)
{
	m_endpos = ((s64)sample) * 1000 * 10000 / 44100;

	if (m_endpos > m_stopPosforAudio)
		m_endpos = m_stopPosforAudio;
	return true;
}

bool audioEngine::replayLoopPart()
{
	setPlayPos(m_startpos / 10000);
	return play();
}

bool audioEngine::setPlaySample(int sample)
{
	return setPlayPos(((s64)sample) * 1000 / 44100);
}

bool audioEngine::play()
{
	m_lenstoplay = 2;
	return audioPlayer::play();
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
		int sleeptime = 300;
		const int deltavolume = 3;
		if (GetAsyncKeyState('1') < 0 && g_volume >= deltavolume)
		{
			g_volume -= deltavolume;
			bChangeVolume = true;
		}
		else if (GetAsyncKeyState('2') < 0 && g_volume <= 100 - deltavolume)
		{
			g_volume += deltavolume;
			bChangeVolume = true;
		}
		for (auto it = audioMap.begin(); it != audioMap.end(); ++it) {
			audioEngine *temp = it->second;
			if (temp->m_lenstoplay == 0)
				temp->pause();
			else {
				temp->m_lenstoplay--;
			}
			if (temp->isneedLoop()) {
				long timetoend;
				bool bEnd = temp->isEnd(&timetoend);
				if (bEnd || timetoend < 5) {
					temp->decLoopcount();
					temp->replayLoopPart();
				} else if (timetoend < sleeptime)
					sleeptime = timetoend;
			}
			if (bChangeVolume)
				temp->setVolume(g_volume);
		}
		Sleep(sleeptime);
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

extern u32 npdrmRead(void* pgd_info, u32 handle, u8 *data, int size);

bool addAtrac3AudioByPackage(const char* package, u32 startpos, int audiosize, 
	u8* buffer, int atracID, void* pgd_info)
{
	u32 h = pspFileSystem.OpenFile(package, (FileAccess) FILEACCESS_READ);
	if (h == 0) 
		return false;
	int filesize = OMAConvert::getRIFFSize(buffer, 0x2000);
	if (filesize <= 0)
		return false;
	int readsize = filesize + 0x800;
	u8* buf = new u8[readsize];
	pspFileSystem.SeekFile(h, startpos, FILEMOVE_BEGIN);

	if (strlen(package) >= 10) {
		if (pgd_info)
			readsize = npdrmRead(pgd_info, h, buf, readsize);
		else
			readsize = pspFileSystem.ReadFile(h, buf, readsize);
	}
	else {
		if (pgd_info)
			npdrmRead(pgd_info, h, buf, readsize / 2048);
		else
			pspFileSystem.ReadFile(h, buf, readsize / 2048);
	}
	pspFileSystem.CloseFile(h);

	bool bResult = false;
	for (int i = 0; i < 0x100; i++) {
		if (memcmp(buffer, buf + i , 0x20) == 0) {
			addAtrac3Audio(buf + i, filesize, atracID);
			bResult = true;
			break;
		}
	}
	delete [] buf;
	return bResult;
}

int generateAudioFilename(u8* buf, char* outfilename)
{
	u8 idbuf[0x20];
	Memory::lastestAccessFile.generateidbuf(buf, idbuf);
	static const char charmap[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 
		'A', 'B', 'C', 'D', 'E', 'F'};
	char *filename = outfilename;
	memcpy(filename, "tmp\\",4);
	int pos = 4;
	for (int i = 0; i < 0x20; i++) {
		filename[pos + 2 * i + 1] = charmap[idbuf[i] & 0xf];
		filename[pos + 2 * i] = charmap[idbuf[i] >> 4];
	}
	pos += 0x20 * 2;
	filename[pos] = 0;
	return pos;
}
bool checkAudioFileExist(u8* buf, char* outfilename)
{
	char filename[256];
	int pos = generateAudioFilename(buf, filename);
	memcpy(filename + pos, ".oma", 5);
	FILE *wfp = fopen(filename, "rb");
	if (!wfp) {
		memcpy(filename + pos, ".wav", 5);
		wfp = fopen(filename, "rb");
	}
	if (!wfp)
		return false;
	fclose(wfp);
	if (outfilename)
		strcpy(outfilename, filename);
	return true;
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

void initaudioEngine()
{
	CoInitialize(0);
}

void shutdownEngine()
{
	for (auto it = audioMap.begin(); it != audioMap.end(); ++it) {
		delete it->second;
	}
	audioMap.clear();
	CoUninitialize();
	system("cleanAudios.bat");
}

void stopAllAtrac3Audio()
{
	for (auto it = audioMap.begin(); it != audioMap.end(); ++it) {
		audioEngine *temp = it->second;
		temp->pause();
	}
}

#endif // _USE_DSHOW_