#define VMA_IMPLEMENTATION

// BEGIN PPSSPP HACKS !!!!!
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "Common/GPU/Vulkan/VulkanLoader.h"
// END PPSSPP HACKS

using namespace PPSSPP_VK;

#undef VK_NO_PROTOTYPES
#include "vk_mem_alloc.h"
#define VK_NO_PROTOTYPES
