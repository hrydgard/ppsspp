#pragma once

#include <string>
#include <cstring>
#include <sstream>
#include <cstdint>

#undef major
#undef minor

// Parses version strings of the format "Major.Minor.Sub" and lets you interact with them conveniently.
struct Version {
	Version() : major(0), minor(0), sub(0) {}
	Version(const std::string &str) {
		if (!ParseVersionString(str)) {
			major = -1;
			minor = -1;
			sub = -1;
		}
	}

	int major;
	int minor;
	int sub;

	bool IsValid() const {
		return sub >= 0 && minor >= 0 && major >= 0;
	}

	bool operator == (const Version &other) const {
		return major == other.major && minor == other.minor && sub == other.sub;
	}
	bool operator != (const Version &other) const {
		return !(*this == other);
	}

	bool operator <(const Version &other) const {
		if (major < other.major) return true;
		if (major > other.major) return false;
		if (minor < other.minor) return true;
		if (minor > other.minor) return false;
		if (sub < other.sub) return true;
		if (sub > other.sub) return false;
		return false;
	}

	bool operator >=(const Version &other) const {
		return !(*this < other);
	}

	std::string ToString() const;
	int ToInteger() const;
private:
	bool ParseVersionString(std::string str);
};

bool ParseMacAddress(const std::string &str, uint8_t macAddr[6]);

bool TryParse(const std::string &str, bool *const output);
bool TryParse(const std::string &str, uint32_t *const output);
bool TryParse(const std::string &str, uint64_t *const output);

template <typename N>
static bool TryParse(const std::string &str, N *const output) {
	std::istringstream iss(str);

	N tmp = 0;
	if (iss >> tmp) {
		*output = tmp;
		return true;
	} else
		return false;
}

void NiceSizeFormat(uint64_t size, char *out, size_t bufSize);

std::string NiceSizeFormat(uint64_t size);

// seconds, or minutes, or hours.
// Uses I18N strings.
std::string NiceTimeFormat(int seconds);

// Not a parser, needs a better location.
// Simplified version of ShaderWriter. Would like to have that inherit from this but can't figure out how
// due to the return value chaining.
// TODO: We actually also have Buffer, with .Printf(), which is almost as convenient. Should maybe improve that instead?
class StringWriter {
public:
	StringWriter(char *buffer, size_t bufSize) : start_(buffer), p_(buffer), bufSize_(bufSize) {
		buffer[0] = '\0';
	}
	template<size_t sz>
	explicit StringWriter(char (&buffer)[sz]) : start_(buffer), p_(buffer), bufSize_(sz) {
		buffer[0] = '\0';
	}
	StringWriter(const StringWriter &) = delete;

	std::string_view as_view() const {
		return std::string_view(start_, p_ - start_);
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
	// Assumes the input is zero-terminated.
	// C: Copies a string literal (which always are zero-terminated, the count includes the zero) directly to the stream.
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
	StringWriter &F(const char *format, ...);
	StringWriter &endl() {
		const size_t remainder = bufSize_ - (p_ - start_);
		if (remainder <= 2)
			return *this;

		return C("\n");
	}

	void Rewind(size_t offset) {
		p_ -= offset;
	}

private:
	char *start_;
	char *p_;
	size_t bufSize_;
};
