#pragma once

#ifdef _USE_DSHOW_

#include "../../Globals.h"

class audioPlayer
{
public:
	audioPlayer(void);
	~audioPlayer(void);
	bool load(const char* filename);
	bool play();
	bool pause();
	bool stop();
	bool closeMedia();
    bool setVolume(int volume);
    bool isEnd(long *mstimetoend = 0);
    bool setPlayPos(long ms);
    bool getPlayPos(long *ms);
private:
	void *m_pGB;
	void *m_pMC;
	void *m_pMS;
	// 0 for stop, 1 for playing, 2 for pause, -1 for not loaded files
	int m_playmode;
    int m_volume;
protected:
	s64 m_startpos;
	s64 m_endpos;
};

class audioEngine: public audioPlayer{
public:
	audioEngine(void):audioPlayer(), m_ID(-1), m_lenstoplay(0){}
	~audioEngine(void){ closeStream();}
	bool loadRIFFStream(u8* stream, int streamsize, int atracID);
	bool closeStream();
	void setLoop(int iloop);
	void decLoopcount();
	bool isneedLoop();
	bool setLoopStart(int sample);
	bool setLoopEnd(int sample);
	bool setPlaySample(int sample);
	bool replayLoopPart();
	bool play();
private:
	int m_ID;
	char m_filename[256];
	int m_iloop;
	s64 m_stopPosforAudio;
public:
	int m_lenstoplay;
};

void addAtrac3Audio(u8* stream, int streamsize, int atracID);
bool addAtrac3AudioByPackage(const char* package, u32 startpos, int audiosize, 
	                        u8* buffer, int atracID, void* pgd_info = 0);
audioEngine* getaudioEngineByID(int atracID);
void deleteAtrac3Audio(int atracID);
void initaudioEngine();
void shutdownEngine();
void stopAllAtrac3Audio();

#endif // _USE_DSHOW_