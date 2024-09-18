#include <cstdarg>
#include <cstdlib>
#include <cstring>

#include "Common/Buffer.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Log.h"

Buffer::Buffer() { }
Buffer::~Buffer() { }

char *Buffer::Append(size_t length) {
	if (length > 0) {
		size_t old_size = data_.size();
		data_.resize(old_size + length);
		return &data_[0] + old_size;
	} else {
		return nullptr;
	}
}

void Buffer::Append(const std::string &str) {
	char *ptr = Append(str.size());
	if (ptr) {
		memcpy(ptr, str.data(), str.size());
	}
}

void Buffer::Append(const char *str) {
	size_t len = strlen(str);
	char *dest = Append(len);
	memcpy(dest, str, len);
}

void Buffer::Append(const Buffer &other) {
	size_t len = other.size();
	if (len > 0) {
		char *dest = Append(len);
		memcpy(dest, &other.data_[0], len);
	}
}

void Buffer::AppendValue(int value) {
	char buf[16];
	// This is slow.
	snprintf(buf, sizeof(buf), "%i", value);
	Append(buf);
}

void Buffer::Take(size_t length, std::string *dest) {
	if (length > data_.size()) {
		ERROR_LOG(Log::IO, "Truncating length in Buffer::Take()");
		length = data_.size();
	}
	dest->resize(length);
	if (length > 0) {
		Take(length, &(*dest)[0]);
	}
}

void Buffer::Take(size_t length, char *dest) {
	memcpy(dest, &data_[0], length);
	data_.erase(data_.begin(), data_.begin() + length);
}

int Buffer::TakeLineCRLF(std::string *dest) {
	int after_next_line = OffsetToAfterNextCRLF();
	if (after_next_line < 0) {
		return after_next_line;
	} else {
		_dbg_assert_(after_next_line >= 2);
		if (after_next_line != 2)
			Take((size_t)after_next_line - 2, dest);
		Skip(2);  // Skip the CRLF
		return after_next_line - 2;
	}
}

void Buffer::Skip(size_t length) {
	if (length > data_.size()) {
		ERROR_LOG(Log::IO, "Truncating length in Buffer::Skip()");
		length = data_.size();
	}
	data_.erase(data_.begin(), data_.begin() + length);
}

int Buffer::SkipLineCRLF() {
	int after_next_line = OffsetToAfterNextCRLF();
	if (after_next_line < 0) {
		return after_next_line;
	} else {
		Skip(after_next_line);
		return after_next_line - 2;
	}
}

int Buffer::OffsetToAfterNextCRLF() {
	for (int i = 0; i < (int)data_.size() - 1; i++) {
		if (data_[i] == '\r' && data_[i + 1] == '\n') {
			return i + 2;
		}
	}
	return -1;
}

void Buffer::Printf(const char *fmt, ...) {
	char buffer[2048];
	va_list vl;
	va_start(vl, fmt);
	size_t retval = vsnprintf(buffer, sizeof(buffer), fmt, vl);
	if ((int)retval >= (int)sizeof(buffer)) {
		// Output was truncated. TODO: Do something.
		ERROR_LOG(Log::IO, "Buffer::Printf truncated output");
	}
	if ((int)retval < 0) {
		ERROR_LOG(Log::IO, "Buffer::Printf failed");
	}
	va_end(vl);
	char *ptr = Append(retval);
	memcpy(ptr, buffer, retval);
}

bool Buffer::FlushToFile(const Path &filename) {
	FILE *f = File::OpenCFile(filename, "wb");
	if (!f)
		return false;
	if (data_.size()) {
		fwrite(&data_[0], 1, data_.size(), f);
	}
	fclose(f);
	return true;
}

void Buffer::PeekAll(std::string *dest) {
	dest->resize(data_.size());
	memcpy(&(*dest)[0], &data_[0], data_.size());
}
