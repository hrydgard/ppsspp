#pragma once

#ifdef _WIN32

#include "../../Globals.h"

class audioPlayer
{
public:
	audioPlayer(void);
	~audioPlayer(void);
	bool load(const char* filename);
	bool play();
	bool stop();
	bool closeMedia();
    bool setVolume(int volume);
    bool isEnd();
    bool setPlayPos(long ms);
private:
	void *m_pGB;
	void *m_pMC;
	void *m_pMS;
	// 0 for stop, 1 for playing
	int m_playmode;
    int m_volume;
};

class audioEngine: public audioPlayer{
public:
	audioEngine(void):audioPlayer(), m_ID(-1){}
	~audioEngine(void){ closeStream();}
	bool loadRIFFStream(u8* stream, int streamsize, int atracID);
	bool closeStream();
private:
	int m_ID;
	char m_filename[256];
};

void addAtrac3Audio(u8* stream, int streamsize, int atracID);
audioEngine* getaudioEngineByID(int atracID);
void deleteAtrac3Audio(int atracID);
void shutdownEngine();

#endif // _WIN32