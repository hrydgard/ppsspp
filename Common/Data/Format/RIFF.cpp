#include <cstring>

#include "Common/Log.h"
#include "Common/Data/Format/RIFF.h"

inline uint32_t flipID(uint32_t id) {
	return ((id >> 24) & 0xFF) | ((id >> 8) & 0xFF00) | ((id << 8) & 0xFF0000) | ((id << 24) & 0xFF000000);
}

RIFFReader::RIFFReader(const uint8_t *data, int dataSize) {
	data_ = new uint8_t[dataSize];
	memcpy(data_, data, dataSize);
	depth_ = 0;
	pos_ = 0;
	eof_ = dataSize;
	fileSize_ = dataSize;
}

RIFFReader::~RIFFReader() {
	delete[] data_;
}

int RIFFReader::ReadInt() {
	int value = 0;
	if (data_ && pos_ < eof_ - 3) {
		pos_ += 4;
		memcpy(&value, data_ + pos_ - 4, 4);
	}
	return value;
}

bool RIFFReader::Descend(uint32_t intoId) {
	if (depth_ > 30)
		return false;

	intoId = flipID(intoId);
	bool found = false;

	// save information to restore after the next Ascend
	stack[depth_].parentStartLocation = pos_;
	stack[depth_].parentEOF = eof_;

	// let's search through children..
	while (pos_ < eof_) {
		int id = ReadInt();
		int length = ReadInt();
		int startLocation = pos_;

		if (pos_ + length > fileSize_) {
			ERROR_LOG(Log::IO, "Block extends outside of RIFF file - failing descend");
			pos_ = stack[depth_].parentStartLocation;
			return false;
		}

		if (id == intoId) {
			stack[depth_].ID = intoId;
			stack[depth_].length = length;
			stack[depth_].startLocation = startLocation;
			found = true;
			break;
		} else {
			if (length > 0) {
				pos_ += length; // try next block
			} else {
				ERROR_LOG(Log::IO, "Bad data in RIFF file : block length %d. Not descending.", length);
				pos_ = stack[depth_].parentStartLocation;
				return false;
			}
		}
	}

	// if we found nothing, return false so the caller can skip this
	if (!found) {
		pos_ = stack[depth_].parentStartLocation;
		return false;
	}

	// descend into it
	// pos was set inside the loop above
	eof_ = stack[depth_].startLocation + stack[depth_].length;
	depth_++;
	return true;
}

void RIFFReader::Ascend() {
	// ascend, and restore information
	depth_--;
	pos_ = stack[depth_].parentStartLocation;
	eof_ = stack[depth_].parentEOF;
}

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
