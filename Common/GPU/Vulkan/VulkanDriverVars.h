#pragma once

#include <string>
#include <string_view>
#include "Common/Common.h"

enum class AdrenoEnvVar : int {
	enable_tp_ubwc_flag_hint = (1 << 0),
	storage_8bit = (1 << 1),
};
ENUM_CLASS_BITOPS(AdrenoEnvVar);

struct AdrenoEnvVarDef {
	std::string_view name;
	AdrenoEnvVar var;
};

const AdrenoEnvVarDef *GetAdrenoEnvVarDefs(int *count);

// False where AdrenoTools' linker namespace tricks are known or suspected to break (Android 17 and its previews).
bool VulkanCustomDriverAllowedByOS();

// Declared here rather than in VulkanLoader.h, whose Vulkan headers bring in X11 on Linux, and its Screen
// clashes with ours.

// When Vulkan didn't work with a custom driver: clears the setting and unloads the library, so the next
// try gets the system's driver. Returns false if no custom driver was in use.
bool VulkanDisableCustomDriver();
// The custom driver that failed (and was cleared) since the last call, if any.
std::string VulkanTakeFailedCustomDriver();
