#include "base/buffer.h"

#include <stdarg.h>
#include <stdlib.h>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#undef min
#undef max
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

#ifndef MSG_NOSIGNAL
// Default value to 0x00 (do nothing) in systems where it's not supported.
#define MSG_NOSIGNAL 0x00
#endif

#include "base/logging.h"
#include "base/timeutil.h"
#include "file/fd_util.h"

Buffer::Buffer() { }
Buffer::~Buffer() { }

char *Buffer::Append(ssize_t length) {
  size_t old_size = data_.size();
  data_.resize(old_size + length);
  return &data_[0] + old_size;
}

void Buffer::Append(const std::string &str) {
  char *ptr = Append(str.size());
  memcpy(ptr, str.data(), str.size());
}

void Buffer::Append(const char *str) {
  size_t len = strlen(str);
  char *dest = Append(len);
  memcpy(dest, str, len);
}

void Buffer::Append(const Buffer &other) {
	size_t len = other.size();
	char *dest = Append(len);
	memcpy(dest, &other.data_[0], len);
}

void Buffer::AppendValue(int value) {
  char buf[16];
  // This is slow.
  sprintf(buf, "%i", value);
  Append(buf);
}

void Buffer::Take(size_t length, std::string *dest) {
	if (length > data_.size()) {
		ELOG("Truncating length in Buffer::Take()");
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
  if (after_next_line < 0)
    return after_next_line;
  else {
    Take(after_next_line - 2, dest);
    Skip(2);  // Skip the CRLF
    return after_next_line - 2;
  }
}

void Buffer::Skip(size_t length) {
	if (length > data_.size()) {
		ELOG("Truncating length in Buffer::Skip()");
		length = data_.size();
	}
	data_.erase(data_.begin(), data_.begin() + length);
}

int Buffer::SkipLineCRLF() {
  int after_next_line = OffsetToAfterNextCRLF();
  if (after_next_line < 0)
    return after_next_line;
  else {
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
  ssize_t retval = vsnprintf(buffer, sizeof(buffer), fmt, vl);
  if (retval >= (ssize_t)sizeof(buffer)) {
    // Output was truncated. TODO: Do something.
    ELOG("Buffer::Printf truncated output");
  }
  if (retval < 0) {
    ELOG("Buffer::Printf failed");
  }
  va_end(vl);
  char *ptr = Append(retval);
  memcpy(ptr, buffer, retval);
}

bool Buffer::Flush(int fd) {
  // Look into using send() directly.
  bool success = (ssize_t)data_.size() == fd_util::WriteLine(fd, &data_[0], data_.size());
  if (success) {
    data_.resize(0);
  }
  return success;
}

bool Buffer::FlushToFile(const char *filename) {
	FILE *f = fopen(filename, "wb");
	if (!f)
		return false;
	if (data_.size()) {
		fwrite(&data_[0], 1, data_.size(), f);
	}
	fclose(f);
	return true;
}

bool Buffer::FlushSocket(uintptr_t sock, double timeout) {
	for (size_t pos = 0, end = data_.size(); pos < end; ) {
		if (timeout >= 0.0 && !fd_util::WaitUntilReady(sock, timeout, true)) {
			ELOG("FlushSocket timed out");
			return false;
		}
		int sent = send(sock, &data_[pos], (int)(end - pos), MSG_NOSIGNAL);
		if (sent < 0) {
			ELOG("FlushSocket failed");
			return false;
		}
		pos += sent;

		// Buffer full, don't spin.
		if (sent == 0 && timeout < 0.0) {
			sleep_ms(1);
		}
	}
	data_.resize(0);
	return true;
}

bool Buffer::ReadAll(int fd, int hintSize) {
	std::vector<char> buf;
	if (hintSize >= 65536 * 16) {
		buf.resize(65536);
	} else if (hintSize >= 1024 * 16) {
		buf.resize(hintSize / 16);
	} else {
		buf.resize(1024);
	}

	while (true) {
		int retval = recv(fd, &buf[0], (int)buf.size(), 0);
		if (retval == 0) {
			break;
		} else if (retval < 0) {
			ELOG("Error reading from buffer: %i", retval);
			return false;
		}
		char *p = Append((size_t)retval);
		memcpy(p, &buf[0], retval);
	}
	return true;
}

bool Buffer::ReadAllWithProgress(int fd, int knownSize, float *progress, bool *cancelled) {
	std::vector<char> buf;
	if (knownSize >= 65536 * 16) {
		buf.resize(65536);
	} else if (knownSize >= 1024 * 16) {
		buf.resize(knownSize / 16);
	} else {
		buf.resize(1024);
	}

	int total = 0;
	while (true) {
		if (cancelled && *cancelled)
			return false;
		int retval = recv(fd, &buf[0], (int)buf.size(), 0);
		if (retval == 0) {
			return true;
		} else if (retval < 0) {
			ELOG("Error reading from buffer: %i", retval);
			return false;
		}
		char *p = Append((size_t)retval);
		memcpy(p, &buf[0], retval);
		total += retval;
		*progress = (float)total / (float)knownSize;
	}
	return true;
}

int Buffer::Read(int fd, size_t sz) {
	char buf[1024];
	int retval;
	size_t received = 0;
	while ((retval = recv(fd, buf, (int)std::min(sz, sizeof(buf)), 0)) > 0) {
		if (retval < 0) {
			return retval;
		}
		char *p = Append((size_t)retval);
		memcpy(p, buf, retval);
		sz -= retval;
		received += retval;
		if (sz == 0)
			return 0;
	}
	return (int)received;
}

void Buffer::PeekAll(std::string *dest) {
	dest->resize(data_.size());
	memcpy(&(*dest)[0], &data_[0], data_.size());
}
