#pragma once

#include <string>
#include <vector>

#include "Common/Common.h"

class Path;

// Acts as a queue. Intended to be as fast as possible for most uses.
// Does not do synchronization, must use external mutexes.
class Buffer {
public:
	Buffer();
	Buffer(Buffer &&) = default;
	~Buffer();

	static Buffer Void() {
		Buffer buf;
		buf.void_ = true;
		return buf;
	}

	// Write max [length] bytes to the returned pointer.
	// Any other operation on this Buffer invalidates the pointer.
	char *Append(size_t length);

	// These work pretty much like you'd expect.
	void Append(const char *str);  // str null-terminated. The null is not copied.
	void Append(const std::string &str);
	void Append(const Buffer &other);

	// Various types. Useful for varz etc. Appends a string representation of the
	// value, rather than a binary representation.
	void AppendValue(int value);

	// Parsing Helpers

	// Use for easy line skipping. If no CRLF within the buffer, returns -1.
	// If parsing HTML headers, this indicates that you should probably buffer up
	// more data.
	int OffsetToAfterNextCRLF();

	// Takers

	void Take(size_t length, std::string *dest);
	void Take(size_t length, char *dest);
	void TakeAll(std::string *dest) { Take(size(), dest); }
	// On failure, return value < 0 and *dest is unchanged.
	// Strips off the actual CRLF from the result.
	int TakeLineCRLF(std::string *dest);

	// Skippers
	void Skip(size_t length);
	// Returns -1 on failure (no CRLF within sight).
	// Otherwise returns the length of the line skipped, not including CRLF. Can be 0.
	int SkipLineCRLF();

	// Utility functions.
	void Printf(const char *fmt, ...);

	// Dumps the entire buffer to the string, but keeps it around.
	// Only to be used for debugging, since it might not be fast at all.
	void PeekAll(std::string *dest);

	// Simple I/O.

	// Writes the entire buffer to the file descriptor. Also resets the
	// size to zero. On failure, data remains in buffer and nothing is
	// written.
	bool FlushToFile(const Path &filename);

	// Utilities. Try to avoid checking for size.
	size_t size() const { return data_.size(); }
	bool empty() const { return size() == 0; }
	void clear() { data_.resize(0); }
	bool IsVoid() const { return void_; }

protected:
	// TODO: Find a better internal representation, like a cord.
	std::vector<char> data_;
	bool void_ = false;

private:
	DISALLOW_COPY_AND_ASSIGN(Buffer);
};
