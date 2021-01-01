#pragma once

#include <cstdint>
#include <string>
#include <vector>

std::string Base64Encode(const uint8_t *p, size_t sz);
std::vector<uint8_t> Base64Decode(const char *s, size_t sz);
