#pragma once

#include <string>

#include "Common/Common.h"

namespace net {

class InputSink {
public:
	InputSink(size_t fd);

	bool ReadLine(std::string &s);
	std::string ReadLine();
	bool ReadLineWithEnding(std::string &s);
	std::string ReadLineWithEnding();

	// Read exactly this number of bytes, or fail.
	bool TakeExact(char *buf, size_t bytes);
	// Read whatever is convenient (may even return 0 bytes when there's more coming eventually.)
	size_t TakeAtMost(char *buf, size_t bytes);
	// Skip exactly this number of bytes, or fail.
	bool Skip(size_t bytes);
	void Discard();

	size_t ReadBinaryUntilTerminator(char *dest, size_t bufSize, std::string_view terminator, bool *didReadTerminator);

	bool Empty() const;
	bool TryFill();
	bool HasError() const { return hasError_; }
	// True once the peer has closed its end. Sticky - no more data can ever arrive, which is not
	// the same as "nothing right now", and callers that wait for more need to tell them apart.
	bool AtEnd() const { return atEnd_; }

	size_t ValidAmount() const {
		return valid_;
	}

	// Size of the internal buffer. Useful for some clients.
	enum {
		BUFFER_SIZE = 32 * 1024,
	};

private:
	std::pair<std::string_view, std::string_view> BufferParts() const;
	void Fill();
	bool Block();
	void AccountDrain(size_t bytes);
	size_t FindNewline() const;

	static const size_t PRESSURE = 8 * 1024;

	size_t fd_;

	// Circular buffer. read_ is the position to read from, write_ is the position to write to,
	// valid_ is the number of valid bytes. valid_ can wrap around, so you might need to take
	// two segments when reading and split when writing.

	char buf_[BUFFER_SIZE];
	size_t read_;
	size_t write_;
	size_t valid_;
	bool hasError_ = false;
	bool atEnd_ = false;
};

class OutputSink {
public:
	OutputSink(size_t fd);

	bool Push(const std::string &s);
	bool Push(const char *buf, size_t bytes);
	size_t PushAtMost(const char *buf, size_t bytes);
	ATTR_FORMAT_PRINTF(2, 3)
	bool Printf(MSVC_FORMAT_PRINTF const char *fmt, ...);

	bool Flush(bool allowBlock = true);
	void Discard();

	bool Empty() const;
	size_t BytesRemaining() const;
	bool HasError() const { return hasError_; }

private:
	void Drain();
	bool Block();
	void AccountPush(size_t bytes);
	void AccountDrain(int bytes);

	static const size_t BUFFER_SIZE = 32 * 1024;
	static const size_t PRESSURE = 8 * 1024;

	size_t fd_;
	char buf_[BUFFER_SIZE];
	size_t read_;
	size_t write_;
	size_t valid_;
	bool hasError_ = false;
};

}  // namespace net
