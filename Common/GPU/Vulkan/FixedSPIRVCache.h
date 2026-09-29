#pragma once

#include "Common/File/Path.h"

// The SPIR-V cache of thin3d's and other fixed shaders (g_spirvCache in VulkanContext.h), for code
// that shouldn't include the Vulkan headers: on Linux they pull in X11, whose macros (None and
// friends) break unrelated headers.
void SetFixedSPIRVCachePath(const Path &path, int maxEntries);
void SaveFixedSPIRVCache();
