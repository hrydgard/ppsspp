#pragma once

#include <string>

// inflate/deflate convenience wrapper. Uses zlib.
bool compress_string(const std::string& str, std::string *dest, int compressionlevel = 9);
// Fails if the output would be larger than maxSize (a small input can expand enormously).
bool decompress_string(const std::string& str, std::string *dest, size_t maxSize = 256 * 1024 * 1024);
