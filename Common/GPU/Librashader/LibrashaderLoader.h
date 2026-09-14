// Copyright (c) 2026- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once
#include <string>
#include "ppsspp_config.h"

#if USE_LIBRASHADER
// Vulkan types must come from PPSSPP's loader so VK_NO_PROTOTYPES etc. match.
#include "Common/GPU/Vulkan/VulkanLoader.h"
#define LIBRA_RUNTIME_VULKAN
#include "librashader_ld.h"

namespace Librashader {
// Loads the shared library once. Search order: $LIBRASHADER_PATH, <exe dir>/<platform name>,
// then the loader's default search path. Returns false (with *error set) if the library is
// absent or its ABI != LIBRASHADER_CURRENT_ABI. Safe to call from any thread, idempotent.
bool Load(std::string *error);
bool IsLoaded();
const libra_instance_t &Instance();
// Converts and frees a libra_error_t. Returns "" for nullptr.
std::string ErrorToString(libra_error_t err);
// For tests and shutdown only.
void Unload();
}
#endif
