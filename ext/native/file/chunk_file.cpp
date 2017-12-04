#include "base/logging.h"
#include "file/chunk_file.h"
#include "file/zip_read.h"
#include "file/file_util.h"

// #define CHUNKDEBUG

inline uint32_t flipID(uint32_t id) {
	return ((id >> 24) & 0xFF) | ((id >> 8) & 0xFF00) | ((id << 8) & 0xFF0000) | ((id << 24) & 0xFF000000);
}

ChunkFile::ChunkFile(const char *filename, bool readMode) {
	data_ = 0;
	filename_ = filename;
	depth_ = 0;
	readMode_ = readMode;
	pos_ = 0;
	didFail_ = false;

	fastMode = readMode_ ? true : false;

	if (fastMode) {
		size_t size;
		data_ = (uint8_t *)VFSReadFile(filename, &size);
		if (!data_) {
			ELOG("Chunkfile fail: %s", filename);
			didFail_ = true;
			return;
		}
		eof_ = (int)size;
		return;
	}

	file = openCFile(filename, "wb");
	if (file) {
		didFail_ = false;
		eof_ = 0;
	} else {
		didFail_ = true;
	}
}

ChunkFile::ChunkFile(const uint8_t *data, int dataSize) {
	data_ = new uint8_t[dataSize];
	memcpy(data_, data, dataSize);
	fastMode = true;
	depth_ = 0;
	readMode_ = true;
	pos_ = 0;
	didFail_ = false;
	eof_ = dataSize;
}

ChunkFile::~ChunkFile() {
	if (fastMode) {
		delete[] data_;
	} else {
		fclose(file);
	}
}

int ChunkFile::readInt() {
	if (data_ && pos_ < eof_) {
		pos_ += 4;
		if (fastMode)
			return *(int *)(data_ + pos_ - 4);
		else {
			int i;
			if (fread(&i, 1, 4, file) == 4) {
				return i;
			}
		}
	}
	return 0;
}

void ChunkFile::writeInt(int i) {
	fwrite(&i, 1, 4, file);
	pos_ += 4;
}

// let's get into the business
bool ChunkFile::descend(uint32_t id) {
	if (depth_ > 30)
		return false;

	id = flipID(id);
	if (readMode_) {
		bool found = false;

		// save information to restore after the next Ascend
		stack[depth_].parentStartLocation = pos_;
		stack[depth_].parentEOF = eof_;

		ChunkInfo temp = stack[depth_];

		int firstID = 0;
		// let's search through children..
		while (pos_ < eof_) {
			stack[depth_].ID = readInt();
			if (firstID == 0) firstID = stack[depth_].ID | 1;
			stack[depth_].length = readInt();
			stack[depth_].startLocation = pos_;

			if (stack[depth_].ID == id) {
				found = true;
				break;
			} else {
				seekTo(pos_ + stack[depth_].length); // try next block
			}
		}

		// if we found nothing, return false so the caller can skip this
		if (!found) {
#ifdef CHUNKDEBUG
			ILOG("Couldn't find %c%c%c%c", id, id >> 8, id >> 16, id >> 24);
#endif
			stack[depth_] = temp;
			seekTo(stack[depth_].parentStartLocation);
			return false;
		}

		// descend into it
		// pos was set inside the loop above
		eof_ = stack[depth_].startLocation + stack[depth_].length;
		depth_++;
#ifdef CHUNKDEBUG
		ILOG("Descended into %c%c%c%c", id, id >> 8, id >> 16, id >> 24);
#endif
		return true;
	} else {
#ifndef DEMO_VERSION	// if this is missing.. heheh
		// write a chunk id, and prepare for filling in length later
		writeInt(id);
		writeInt(0); // will be filled in by Ascend
		stack[depth_].startLocation = pos_;
		depth_++;
		return true;
#else
		return true;
#endif
	}
}

void ChunkFile::seekTo(int _pos) {
	if (!fastMode) {
		fseek(file, 0, SEEK_SET);
	}
	pos_ = _pos;
}

// let's ascend out
void ChunkFile::ascend() {
	if (readMode_) {
		// ascend, and restore information
		depth_--;
		seekTo(stack[depth_].parentStartLocation);
		eof_ = stack[depth_].parentEOF;
#ifdef CHUNKDEBUG
		int id = stack[depth_].ID;
		ILOG("Ascended out of %c%c%c%c", id, id >> 8, id >> 16, id >> 24);
#endif
	} else {
		depth_--;
		// now fill in the written length automatically
		int posNow = pos_;
		seekTo(stack[depth_].startLocation - 4);
		writeInt(posNow - stack[depth_].startLocation);
		seekTo(posNow);
	}
}

// read a block
void ChunkFile::readData(void *what, int count) {
	if (fastMode) {
		memcpy(what, data_ + pos_, count);
	} else {
		if (fread(what, 1, count, file) != (size_t)count) {
			ELOG("Failed to read complete %d bytes", count);
		}
	}

	pos_ += count;
	count &= 3;
	if (count) {
		count = 4 - count;
		if (!fastMode) {
			if (fseek(file, count, SEEK_CUR) != 0) {
				ELOG("Missing padding");
			}
		}
		pos_ += count;
	}
}

// write a block
void ChunkFile::writeData(const void *what, int count) {
	fwrite(what, 1, count, file);
	pos_ += count;
	char temp[5] = { 0,0,0,0,0 };
	count &= 3;
	if (count) {
		count = 4 - count;
		fwrite(temp, 1, count, file);
		pos_ += count;
	}
}

// Takes utf-8
void ChunkFile::writeWString(const std::string &str) {
	unsigned short *text;
	int len = (int)str.length();
	text = new unsigned short[len + 1];
	for (int i = 0; i < len; i++)
		text[i] = str[i];
	text[len] = 0;
	writeInt(len);
	writeData((char *)text, len * sizeof(unsigned short));
	delete[] text;
}

static void toUnicode(const std::string &str, uint16_t *t) {
	for (size_t i = 0; i < str.size(); i++) {
		*t++ = str[i];
	}
	*t++ = '\0';
}

static std::string fromUnicode(const uint16_t *src, int len) {
	std::string str;
	str.resize(len);
	for (int i = 0; i < len; i++) {
		str[i] = src[i] > 255 ? ' ' : src[i];
	}
	return str;
}

std::string ChunkFile::readWString() {
	int len = readInt();
	uint16_t *text = new uint16_t[len + 1];
	readData((char *)text, len * sizeof(uint16_t));
	text[len] = 0;
	std::string temp = fromUnicode(text, len);
	delete[] text;
	return temp;
}

void ChunkFile::writeString(const std::string &str) {
	uint16_t *text;
	int len = (int)str.size();
	text = new uint16_t[len + 1];
	toUnicode(str, text);
	writeInt(len);
	writeData((char *)text, len * sizeof(uint16_t));
	delete[] text;
}

std::string ChunkFile::readString() {
	int len = readInt();
	uint16_t *text = new uint16_t[len + 1];
	readData((char *)text, len * sizeof(uint16_t));
	text[len] = 0;
	std::string temp = fromUnicode(text, len);
	delete[] text;
	return temp;
}
int ChunkFile::getCurrentChunkSize() {
	if (depth_)
		return stack[depth_ - 1].length;
	else
		return 0;
}

