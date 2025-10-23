#include <cstdarg>
#include <cstdlib>
#include <cstring>

#include "Common/Buffer.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/Log.h"

char *Buffer::Append(size_t length) {
	if (length > 0) {
		return data_.push_back_write(length);
	} else {
		return nullptr;
	}
}

void Buffer::Append(std::string_view str) {
	char *ptr = Append(str.size());
	if (ptr) {
		memcpy(ptr, str.data(), str.size());
	}
}

void Buffer::Append(const Buffer &other) {
	size_t len = other.size();
	if (len > 0) {
		// Append other to the current buffer.
		other.data_.iterate_blocks([&](const char *data, size_t size) {
			data_.push_back(data, size);
			return true;
		});
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
	size_t retval = data_.pop_front_bulk(dest, length);
	_dbg_assert_(retval == length);
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
	data_.skip(length);
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

// This relies on having buffered data!
int Buffer::OffsetToAfterNextCRLF() {
	int offset = data_.next_crlf_offset();
	if (offset >= 0) {
		return offset + 2;
	} else {
		return -1;
	}
}

void Buffer::Printf(const char *fmt, ...) {
	char buffer[4096];
	va_list vl;
	va_start(vl, fmt);
	int retval = vsnprintf(buffer, sizeof(buffer), fmt, vl);
	if (retval >= (int)sizeof(buffer)) {
		// Output was truncated. TODO: Do something.
		ERROR_LOG(Log::IO, "Buffer::Printf truncated output");
	}
	va_end(vl);
	if (retval < 0) {
		ERROR_LOG(Log::IO, "Buffer::Printf failed, bad args?");
		return;
	}
	char *ptr = Append(retval);
	memcpy(ptr, buffer, retval);
}

bool Buffer::FlushToFile(const Path &filename, bool clear) {
	FILE *f = File::OpenCFile(filename, "wb");
	if (!f)
		return false;
	if (!data_.empty()) {
		// Write the buffer to the file.
		data_.iterate_blocks([f](const char *blockData, size_t blockSize) {
			return fwrite(blockData, 1, blockSize, f) == blockSize;
		});
		if (clear) {
			data_.clear();
		}
	}
	fclose(f);
	return true;
}

void Buffer::PeekAll(std::string *dest) {
	dest->resize(data_.size());
	data_.iterate_blocks(([dest](const char *blockData, size_t blockSize) {
		dest->append(blockData, blockSize);
		return true;
	}));
}
