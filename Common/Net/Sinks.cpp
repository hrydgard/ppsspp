#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(WINDOWS)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>

#else

#include <sys/socket.h>       /*  socket definitions        */
#include <sys/types.h>        /*  socket types              */
#include <sys/wait.h>         /*  for waitpid()             */
#include <netinet/in.h>       /*  struct sockaddr_in        */
#include <arpa/inet.h>        /*  inet (3) funtions         */
#include <unistd.h>           /*  misc. UNIX functions      */

#endif

#include <algorithm>
#include <cerrno>
#include <cstdarg>

#include "Common/Net/Sinks.h"

#include "Common/Log.h"
#include "Common/File/FileDescriptor.h"

#ifndef MSG_NOSIGNAL
// Default value to 0x00 (do nothing) in systems where it's not supported
#define MSG_NOSIGNAL 0
#endif

namespace net {

InputSink::InputSink(size_t fd) : fd_(fd), read_(0), write_(0), valid_(0) {
	fd_util::SetNonBlocking((int)fd_, true);
}

bool InputSink::ReadLineWithEnding(std::string &s) {
	size_t newline = FindNewline();
	if (newline == BUFFER_SIZE) {
		Block();
		newline = FindNewline();
	}
	if (newline == BUFFER_SIZE) {
		// Timed out.
		return false;
	}

	s.resize(newline + 1);
	if (read_ + newline + 1 > BUFFER_SIZE) {
		// Need to do two reads.
		size_t chunk1 = BUFFER_SIZE - read_;
		size_t chunk2 = read_ + newline + 1 - BUFFER_SIZE;
		memcpy(&s[0], buf_ + read_, chunk1);
		memcpy(&s[chunk1], buf_, chunk2);
	} else {
		memcpy(&s[0], buf_ + read_, newline + 1);
	}
	AccountDrain(newline + 1);

	return true;
}

std::string InputSink::ReadLineWithEnding() {
	std::string s;
	ReadLineWithEnding(s);
	return s;
}

bool InputSink::ReadLine(std::string &s) {
	bool result = ReadLineWithEnding(s);
	if (result) {
		size_t l = s.length();
		if (l >= 2 && s[l - 2] == '\r' && s[l - 1] == '\n') {
			s.resize(l - 2);
		} else if (l >= 1 && s[l - 1] == '\n') {
			s.resize(l - 1);
		}
	}
	return result;
}

std::string InputSink::ReadLine() {
	std::string s;
	ReadLine(s);
	return s;
}

size_t InputSink::FindNewline() const {
	// Technically, \r\n, but most parsers are lax... let's follow suit.
	size_t until_end = std::min(valid_, BUFFER_SIZE - read_);
	for (const char *p = buf_ + read_, *end = buf_ + read_ + until_end; p < end; ++p) {
		if (*p == '\n') {
			return p - (buf_ + read_);
		}
	}

	// Were there more bytes wrapped around?
	if (read_ + valid_ > BUFFER_SIZE) {
		size_t wrapped = read_ + valid_ - BUFFER_SIZE;
		for (const char *p = buf_, *end = buf_ + wrapped; p < end; ++p) {
			if (*p == '\n') {
				// Offset by the skipped portion before wrapping.
				return (p - buf_) + until_end;
			}
		}
	}

	// Never found, return an invalid position to indicate.
	return BUFFER_SIZE;
}

bool InputSink::TakeExact(char *buf, size_t bytes) {
	while (bytes > 0) {
		size_t drained = TakeAtMost(buf, bytes);
		buf += drained;
		bytes -= drained;

		if (drained == 0) {
			if (!Block()) {
				// Timed out reading more bytes.
				return false;
			}
		}
	}

	return true;
}

size_t InputSink::TakeAtMost(char *buf, size_t bytes) {
	Fill();

	// The least of: contiguous to read, actually populated in buffer, and wanted.
	size_t avail = std::min(std::min(BUFFER_SIZE - read_, valid_), bytes);

	if (avail != 0) {
		memcpy(buf, buf_ + read_, avail);
		AccountDrain(avail);
	}

	return avail;
}

bool InputSink::Skip(size_t bytes) {
	while (bytes > 0) {
		size_t drained = std::min(valid_, bytes);
		AccountDrain(drained);
		bytes -= drained;

		// Nothing left to read?  Get more.
		if (drained == 0) {
			if (!Block()) {
				// Timed out reading more bytes.
				return false;
			}
		}
	}

	return true;
}

void InputSink::Discard() {
	read_ = 0;
	write_ = 0;
	valid_ = 0;
}

void InputSink::Fill() {
	// Avoid small reads if possible.
	if (BUFFER_SIZE - valid_ > PRESSURE) {
		// Whatever isn't valid and follows write_ is what's available.
		size_t avail = BUFFER_SIZE - std::max(write_, valid_);

		int bytes = recv(fd_, buf_ + write_, (int)avail, MSG_NOSIGNAL);
		AccountFill(bytes);
	}
}

bool InputSink::Block() {
	if (!fd_util::WaitUntilReady((int)fd_, 5.0)) {
		return false;
	}

	Fill();
	return true;
}

void InputSink::AccountFill(int bytes) {
	if (bytes < 0) {
#if PPSSPP_PLATFORM(WINDOWS)
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK)
			return;
#else
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			return;
#endif
		ERROR_LOG(IO, "Error reading from socket");
		return;
	}

	// Okay, move forward (might be by zero.)
	valid_ += bytes;
	write_ += bytes;
	if (write_ >= BUFFER_SIZE) {
		write_ -= BUFFER_SIZE;
	}
}

void InputSink::AccountDrain(size_t bytes) {
	valid_ -= bytes;
	read_ += bytes;
	if (read_ >= BUFFER_SIZE) {
		read_ -= BUFFER_SIZE;
	}
}

bool InputSink::Empty() const {
	return valid_ == 0;
}

bool InputSink::TryFill() {
	Fill();
	return !Empty();
}

OutputSink::OutputSink(size_t fd) : fd_(fd), read_(0), write_(0), valid_(0) {
	fd_util::SetNonBlocking((int)fd_, true);
}

bool OutputSink::Push(const std::string &s) {
	return Push(&s[0], s.length());
}

bool OutputSink::Push(const char *buf, size_t bytes) {
	while (bytes > 0) {
		size_t pushed = PushAtMost(buf, bytes);
		buf += pushed;
		bytes -= pushed;

		if (pushed == 0) {
			if (!Block()) {
				// We couldn't write all the bytes.
				return false;
			}
		}
	}

	return true;
}

bool OutputSink::PushCRLF(const std::string &s) {
	if (Push(s)) {
		return Push("r\n", 2);
	}
	return false;
}

size_t OutputSink::PushAtMost(const char *buf, size_t bytes) {
	Drain();

	if (valid_ == 0 && bytes > PRESSURE) {
		// Special case for pushing larger buffers: let's try to send directly.
		int sentBytes = send(fd_, buf, (int)bytes, MSG_NOSIGNAL);
		// If it was 0 or EWOULDBLOCK, that's fine, we'll enqueue as we can.
		if (sentBytes > 0) {
			return sentBytes;
		}
	}

	// Look for contiguous free space after write_ that's valid.
	size_t avail = std::min(BUFFER_SIZE - std::max(write_, valid_), bytes);

	if (avail != 0) {
		memcpy(buf_ + write_, buf, avail);
		AccountPush(avail);
	}

	return avail;
}


bool OutputSink::Printf(const char *fmt, ...) {
	// Let's start by checking how much space we have.
	size_t avail = BUFFER_SIZE - std::max(write_, valid_);

	va_list args;
	va_start(args, fmt);
	// Make a backup in case we don't have sufficient space.
	va_list backup;
	va_copy(backup, args);

	bool success = true;

	int result = vsnprintf(buf_ + write_, avail, fmt, args);
	if (result >= (int)avail) {
		// There wasn't enough space.  Let's use a buffer instead.
		// This could be caused by wraparound.
		char temp[BUFFER_SIZE];
		result = vsnprintf(temp, BUFFER_SIZE, fmt, args);

		if ((size_t)result < BUFFER_SIZE && result > 0) {
			// In case it did return the null terminator.
			if (temp[result - 1] == '\0') {
				result--;
			}

			success = Push(temp, result);
			// We've written so there's nothing more.
			result = 0;
		}
	}
	va_end(args);
	va_end(backup);

	// Okay, did we actually write?
	if (result >= (int)avail) {
		// This means the result string was too big for the buffer.
		ERROR_LOG(IO, "Not enough space to format output.");
		return false;
	} else if (result < 0) {
		ERROR_LOG(IO, "vsnprintf failed.");
		return false;
	}

	if (result > 0) {
		AccountPush(result);
	}

	return success;
}

bool OutputSink::Block() {
	if (!fd_util::WaitUntilReady((int)fd_, 5.0, true)) {
		return false;
	}

	Drain();
	return true;
}

bool OutputSink::Flush(bool allowBlock) {
	while (valid_ > 0) {
		size_t avail = std::min(BUFFER_SIZE - read_, valid_);

		int bytes = send(fd_, buf_ + read_, (int)avail, MSG_NOSIGNAL);
#if !PPSSPP_PLATFORM(WINDOWS)
		if (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			bytes = 0;
#endif
		AccountDrain(bytes);

		if (bytes == 0) {
			// This may also drain.  Either way, keep looping.
			if (!allowBlock || !Block()) {
				return false;
			}
		} else if (bytes < 0) {
			return false;
		}
	}

	return true;
}

void OutputSink::Discard() {
	read_ = 0;
	write_ = 0;
	valid_ = 0;
}

void OutputSink::Drain() {
	// Avoid small reads if possible.
	if (valid_ > PRESSURE) {
		// Let's just do contiguous valid.
		size_t avail = std::min(BUFFER_SIZE - read_, valid_);

		int bytes = send(fd_, buf_ + read_, (int)avail, MSG_NOSIGNAL);
#if !PPSSPP_PLATFORM(WINDOWS)
		if (bytes == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))
			bytes = 0;
#endif
		AccountDrain(bytes);
	}
}

void OutputSink::AccountPush(size_t bytes) {
	valid_ += bytes;
	write_ += bytes;
	if (write_ >= BUFFER_SIZE) {
		write_ -= BUFFER_SIZE;
	}
}

void OutputSink::AccountDrain(int bytes) {
	if (bytes < 0) {
#if PPSSPP_PLATFORM(WINDOWS)
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK)
			return;
#else
		if (errno == EWOULDBLOCK || errno == EAGAIN)
			return;
#endif
		ERROR_LOG(IO, "Error writing to socket");
		return;
	}

	valid_ -= bytes;
	read_ += bytes;
	if (read_ >= BUFFER_SIZE) {
		read_ -= BUFFER_SIZE;
	}
}

bool OutputSink::Empty() const {
	return valid_ == 0;
}

size_t OutputSink::BytesRemaining() const {
	return valid_;
}

}  // namespace net
