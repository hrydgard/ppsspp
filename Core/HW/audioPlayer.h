#pragma once

#ifdef _USE_DSHOW_

#include "../../Globals.h"

class audioPlayer
{
public:
	audioPlayer(void);
	~audioPlayer(void);
	// if samplebuffermode is true, it would provide sample buffers instead play sounds
	// if filename is set, then load a file, otherwise load from stream
	bool load(const char* filename, u8* stream = 0, int readSize = 0, int streamSize = 0, 
		bool samplebuffermode = false, bool isWave = false);
	bool play();
	bool pause();
	bool stop();
	bool closeMedia();
    bool setVolume(int volume);
    bool isEnd(long *mstimetoend = 0);
    bool setPlayPos(long ms);
    bool getPlayPos(long *ms);
	int  getNextSamples(u8* buf, int wantedbufsize);
protected:
	void *m_pGB;
	void *m_pMC;
	void *m_pMS;
	void *m_pStreamReader;
	void *m_pGrabber;
	void *m_pGrabberCB;
	// 0 for stop, 1 for playing, 2 for pause, -1 for not loaded files
	int m_playmode;
    int m_volume;
protected:
	s64 m_startpos;
	s64 m_endpos;
};

class audioEngine: public audioPlayer{
public:
	audioEngine(void):audioPlayer(), m_ID(-1){}
	~audioEngine(void){ closeStream();}
	bool loadRIFFStream(u8* stream, int streamsize, int atracID);
	bool closeStream();
	bool setPlaySample(int sample);
	void addStreamData(int offset, u8* buf, int size, int cursample);
private:
	int m_ID;
	int m_channel;
};

void addAtrac3Audio(u8* stream, int streamsize, int atracID);
audioEngine* getaudioEngineByID(int atracID);
void deleteAtrac3Audio(int atracID);

void initaudioEngine();
void shutdownEngine();

#endif // _USE_DSHOW_