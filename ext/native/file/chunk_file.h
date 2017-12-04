#pragma once

// RIFF file format reader/writer. Very old code, basically a total mess but it still works.
// Has nothing to do with the ChunkFile.h used in Dolphin or PPSSPP.

// TO REMEMBER WHEN USING:

// EITHER a chunk contains ONLY data
// OR it contains ONLY other chunks
// otherwise the scheme breaks.

#include <string>
#include <cstdio>

#include "base/basictypes.h"

class ChunkFile {
public:
	ChunkFile(const char *filename, bool readMode);
	ChunkFile(const uint8_t *data, int dataSize);

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
	bool failed() const { return didFail_; }
	std::string filename() const { return filename_; }

private:
	struct ChunkInfo {
		int startLocation;
		int parentStartLocation;
		int parentEOF;
		unsigned int ID;
		int length;
	};
	ChunkInfo stack[32];
	int depth_ = 0;

	uint8_t *data_;
	int pos_ = 0;
	int eof_ = 0;
	bool fastMode;
	bool readMode_;
	bool didFail_ = false;

	std::string filename_;
	FILE *file = nullptr;

	void seekTo(int _pos);
	int getPos() const {return pos_;}
};
