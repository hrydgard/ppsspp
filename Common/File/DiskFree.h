#pragma once

#include <cstdint>

#include "Common/File/Path.h"

// If this fails, false is returned and space is negative.
// Try to avoid calling this from the main thread, if possible. Not super fast,
// but is also not allowed to do things like scan the entire disk.
bool free_disk_space(const Path &path, int64_t &space);
