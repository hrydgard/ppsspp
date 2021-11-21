#pragma once

#ifdef USE_CRT_DBG
#undef new
#endif

#include "Common/GPU/Vulkan/VulkanLoader.h"

using namespace PPSSPP_VK;

#undef VK_NO_PROTOTYPES
#include "ext/vma/vk_mem_alloc.h"
#define VK_NO_PROTOTYPES

#ifdef USE_CRT_DBG
#define new DBG_NEW
#endif
