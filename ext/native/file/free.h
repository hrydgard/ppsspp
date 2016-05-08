#pragma once

#include <string>
#include "base/basictypes.h"

bool free_disk_space(const std::string &dir, uint64_t &space);
