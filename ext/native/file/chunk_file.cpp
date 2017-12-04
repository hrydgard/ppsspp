#include "base/logging.h"
#include "file/chunk_file.h"
#include "file/zip_read.h"
#include "file/file_util.h"

inline uint32_t flipID(uint32_t id) {
	return ((id >> 24) & 0xFF) | ((id >> 8) & 0xFF00) | ((id << 8) & 0xFF0000) | ((id << 24) & 0xFF000000);
}

RIFFReader::RIFFReader(const uint8_t *data, int dataSize) {
	data_ = new uint8_t[dataSize];
	memcpy(data_, data, dataSize);
	depth_ = 0;
	pos_ = 0;
	didFail_ = false;
	eof_ = dataSize;
}

RIFFReader::~RIFFReader() {
	delete[] data_;
}

int RIFFReader::ReadInt() {
	if (data_ && pos_ < eof_) {
		pos_ += 4;
		return *(int *)(data_ + pos_ - 4);
	}
	return 0;
}

// let's get into the business
bool RIFFReader::Descend(uint32_t id) {
	if (depth_ > 30)
		return false;

	id = flipID(id);
	bool found = false;

	// save information to restore after the next Ascend
	stack[depth_].parentStartLocation = pos_;
	stack[depth_].parentEOF = eof_;

	ChunkInfo temp = stack[depth_];

	int firstID = 0;
	// let's search through children..
	while (pos_ < eof_) {
		stack[depth_].ID = ReadInt();
		if (firstID == 0) firstID = stack[depth_].ID | 1;
		stack[depth_].length = ReadInt();
		stack[depth_].startLocation = pos_;

		if (stack[depth_].ID == id) {
			found = true;
			break;
		} else {
			pos_ += stack[depth_].length; // try next block
		}
	}

	// if we found nothing, return false so the caller can skip this
	if (!found) {
		stack[depth_] = temp;
		pos_ = stack[depth_].parentStartLocation;
		return false;
	}

	// descend into it
	// pos was set inside the loop above
	eof_ = stack[depth_].startLocation + stack[depth_].length;
	depth_++;
	return true;
}

// let's ascend out
void RIFFReader::Ascend() {
	// ascend, and restore information
	depth_--;
	pos_ = stack[depth_].parentStartLocation;
	eof_ = stack[depth_].parentEOF;
}

// read a block
void RIFFReader::ReadData(void *what, int count) {
	memcpy(what, data_ + pos_, count);
	pos_ += count;
	count &= 3;
	if (count) {
		count = 4 - count;
		pos_ += count;
	}
}

int RIFFReader::GetCurrentChunkSize() {
	if (depth_)
		return stack[depth_ - 1].length;
	else
		return 0;
}

