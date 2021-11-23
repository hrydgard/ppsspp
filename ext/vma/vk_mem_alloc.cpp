#define VMA_IMPLEMENTATION

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include "Common/GPU/Vulkan/VulkanLoader.h"

using namespace PPSSPP_VK;

#undef VK_NO_PROTOTYPES
#include "vk_mem_alloc.h"
#define VK_NO_PROTOTYPES
