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

		// length is a raw 4-byte value from the file. Validate in 64-bit to avoid
		// pos_ + length overflowing a 32-bit int (which could wrap negative and
		// bypass this check for a length near INT_MAX), and reject negative lengths
		// outright here - the mismatch branch below already checks length > 0 before
		// advancing pos_, but a *matched* chunk with a negative length used to reach
		// stack[depth_].length unchecked, and from there GetCurrentChunkSize() and
		// its callers (e.g. a std::vector::resize() sized from it).
		if (length < 0 || (int64_t)pos_ + length > fileSize_) {
			// This should already catch the case where the file is truncated, but we also check for it in ReadData just in case.
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

bool RIFFReader::ReadData(void *what, int count) {
	bool success = true;
	if (count > 0) {
		int available = pos_ < fileSize_ ? fileSize_ - pos_ : 0;
		int toRead = count < available ? count : available;
		if (toRead > 0) {
			memcpy(what, data_ + pos_, toRead);
		}
		if (toRead < count) {
			// Truncated/corrupt file - don't read past the buffer. Zero the rest so
			// callers don't read uninitialized data, but also return false.
			// However, reaching this is probably impossible due to the check in Descend.
			ERROR_LOG(Log::IO, "RIFFReader::ReadData: wanted %d bytes but only %d available", count, toRead);
			memset((uint8_t *)what + toRead, 0, count - toRead);
			success = false;
		}
	}
	pos_ += count;
	count &= 3;
	if (count) {
		count = 4 - count;
		pos_ += count;
	}
	return success;
}

int RIFFReader::GetCurrentChunkSize() {
	if (depth_)
		return stack[depth_ - 1].length;
	else
		return 0;
}
