#pragma once

#include <string>
#include <cstring>  // for memcpy

#include "Common/CommonFuncs.h"

// Simplified version of ShaderWriter. Would like to have that inherit from this but can't figure out how
// due to the return value chaining. Maybe with the curiously recurring template pattern?
// TODO: We actually also have Buffer, with .Printf(), which is almost as convenient. Should maybe improve that instead?
class StringWriter {
public:
	StringWriter(char *buffer, size_t bufSize) : start_(buffer), p_(buffer), bufSize_(bufSize) {
		buffer[0] = '\0';
	}
	template<size_t sz>
	explicit StringWriter(char(&buffer)[sz]) : start_(buffer), p_(buffer), bufSize_(sz) {
		buffer[0] = '\0';
	}
	StringWriter(const StringWriter &) = delete;

	std::string_view as_view() const {
		return std::string_view(start_, p_ - start_);
	}
	const char *begin() const {
		return start_;
	}
	const char *end() {
		return p_;
	}

	size_t size() const {
		return p_ - start_;
	}

	// Assumes the input is zero-terminated.
	// C: Copies a string literal (which always are zero-terminated, the count includes the zero) directly to the stream.
	template<size_t T>
	StringWriter &C(const char(&text)[T]) {
		const size_t remainder = bufSize_ - (p_ - start_);
		if (remainder <= T)
			return *this;
		memcpy(p_, text, T);
		p_ += T - 1;
		return *this;
	}

	// B: Writes a bool as string.
	StringWriter &B(bool b) {
		return W(b ? "true" : "false");
	}

	// W: Writes a string_view to the stream.
	StringWriter &W(std::string_view text) {
		const size_t remainder = bufSize_ - (p_ - start_);
		if (remainder <= text.length())
			return *this;
		memcpy(p_, text.data(), text.length());
		p_ += text.length();
		*p_ = '\0';
		return *this;
	}

	// F: Formats into the buffer.
	// This one is implemented in Parsers.cpp.
	StringWriter &F(MSVC_FORMAT_PRINTF const char *format, ...) ATTR_FORMAT_PRINTF(2, 3);

	// Adds a line ending, sometimes convenient.
	StringWriter &endl() {
		const size_t remainder = bufSize_ - (p_ - start_);
		if (remainder <= 2)
			return *this;

		return C("\n");
	}

	void Rewind(size_t offset) {
		p_ -= offset;
		if (p_ < start_) {
			p_ = start_;
		}
	}

private:
	char *start_;
	char *p_;
	size_t bufSize_;
};
