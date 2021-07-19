#pragma once

#include <cstdint>

#include "Common/File/Path.h"

bool free_disk_space(const Path &path, uint64_t &space);
