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
    bool isEnd();
    bool setPlayPos(long ms);
private:
	void *m_pGB;
	void *m_pMC;
	void *m_pMS;
	// 0 for stop, 1 for playing, 2 for pause, -1 for not loaded files
	int m_playmode;
    int m_volume;
};

class audioEngine: public audioPlayer{
public:
	audioEngine(void):audioPlayer(), m_ID(-1){}
	~audioEngine(void){ closeStream();}
	bool loadRIFFStream(u8* stream, int streamsize, int atracID);
	bool closeStream();
	void setLoop(int iloop);
	void decLoopcount();
	bool isneedLoop();
private:
	int m_ID;
	char m_filename[256];
	int m_iloop;
};

void addAtrac3Audio(u8* stream, int streamsize, int atracID);
bool addAtrac3AudioByPackage(const char* package, u32 startpos, int audiosize, 
	                        u8* buffer, int atracID);
audioEngine* getaudioEngineByID(int atracID);
void deleteAtrac3Audio(int atracID);
void shutdownEngine();
void stopAllAtrac3Audio();

#endif // _USE_DSHOW_