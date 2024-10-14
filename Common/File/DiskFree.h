#pragma once

#include <cstdint>

#include "Common/File/Path.h"

// If this fails, false is returned and space is negative.
// Try to avoid calling this from the main thread, if possible. Can be SLOW.
bool free_disk_space(const Path &path, int64_t &space);
