#pragma once

#include <string>
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
