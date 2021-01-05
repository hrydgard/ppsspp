#pragma once

#include <string>
#include <cstdint>

bool free_disk_space(const std::string &dir, uint64_t &space);
