#pragma once

#include <string>

// inflate/deflate convenience wrapper. Uses zlib.
bool compress_string(const std::string& str, std::string *dest, int compressionlevel = 9);
bool decompress_string(const std::string& str, std::string *dest);
