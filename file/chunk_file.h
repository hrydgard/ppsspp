#pragma once

// RIFF file format reader/writer. Very old code, basically a total mess but it still works.

// TO REMEMBER WHEN USING:

// EITHER a chunk contains ONLY data
// OR it contains ONLY other chunks
// otherwise the scheme breaks.

#include <string>

#include "base/basictypes.h"
#include "file/easy_file.h"

inline uint32 flipID(uint32 id) {
	return ((id>>24)&0xFF) | ((id>>8)&0xFF00) | ((id<<8)&0xFF0000) | ((id<<24)&0xFF000000);
}

class ChunkFile {
public:
	ChunkFile(const char *filename, bool _read);
	~ChunkFile();

	bool descend(uint32 id);
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
	LAMEFile file;
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
	int pos, eof;
	bool fastMode;
	bool read;
	bool didFail;

	void seekTo(int _pos);
	int getPos() const {return pos;}
};
