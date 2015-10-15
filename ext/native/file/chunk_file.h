#pragma once

// RIFF file format reader/writer. Very old code, basically a total mess but it still works.
// Has nothing to do with the ChunkFile.h used in Dolphin or PPSSPP.

// TO REMEMBER WHEN USING:

// EITHER a chunk contains ONLY data
// OR it contains ONLY other chunks
// otherwise the scheme breaks.

#include <string>
#include <stdio.h>

#include "base/basictypes.h"

inline uint32_t flipID(uint32_t id) {
	return ((id>>24)&0xFF) | ((id>>8)&0xFF00) | ((id<<8)&0xFF0000) | ((id<<24)&0xFF000000);
}

class ChunkFile {
public:
	ChunkFile(const char *filename, bool _read);
	ChunkFile(const uint8_t *read_data, int data_size);

	~ChunkFile();

	bool descend(uint32_t id);
	void ascend();

	int	readInt();
	void readInt(int &i) {i = readInt();}
	void readData(void *data, int count);
	// String readWString();
	std::string readWString();

	void writeString(const std::string &str);
	std::string readString();

	void writeInt(int i);
	//void writeWString(String str);
	void writeWString(const std::string &str);
	void writeData(const void *data, int count);

	int getCurrentChunkSize();
	bool failed() const { return didFail; }
	std::string filename() const { return fn; }

private:
	std::string fn;
	FILE *file;
	struct ChunkInfo {
		int startLocation;
		int parentStartLocation;
		int parentEOF;
		unsigned int ID;
		int length;
	};
	ChunkInfo stack[8];
	int numLevels;

	uint8_t *data;
	int pos;
	int eof;
	bool fastMode;
	bool read;
	bool didFail;

	void seekTo(int _pos);
	int getPos() const {return pos;}
};
