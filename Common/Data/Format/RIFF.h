#pragma once

// Simple RIFF file format reader.
// Unrelated to the ChunkFile.h used in Dolphin and PPSSPP.

// TO REMEMBER WHEN USING:

// EITHER a chunk contains ONLY data
// OR it contains ONLY other chunks
// otherwise the scheme breaks.

#include <cstdint>

class RIFFReader {
public:
	RIFFReader(const uint8_t *data, int dataSize);
	~RIFFReader();

	bool Descend(uint32_t id);
	void Ascend();

	int ReadInt();
	void ReadData(void *data, int count);

	int GetCurrentChunkSize();

private:
	struct ChunkInfo {
		int startLocation;
		int parentStartLocation;
		int parentEOF;
		uint32_t ID;
		int length;
	};
	ChunkInfo stack[32];
	uint8_t *data_;
	int pos_ = 0;
	int eof_ = 0;  // really end of current block
	int depth_ = 0;
	int fileSize_ = 0;
};
