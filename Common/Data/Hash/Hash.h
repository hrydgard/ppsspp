#pragma once

#include <cstdlib>
#include <string_view>

namespace hash {

// Fairly decent function for hashing strings.
uint32_t Adler32(const uint8_t *data, size_t len);
inline uint32_t Adler32(std::string_view data) {
	return Adler32((const uint8_t *)data.data(), data.size());
}

}  // namespace hash

