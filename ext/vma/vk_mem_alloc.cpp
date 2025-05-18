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


// This chunk should be added to vk_mem_alloc.h when upgrading, right below the #ifndef at the top:

/*

// BEGIN PPSSPP HACKS !!!!!
#ifdef USE_CRT_DBG
#undef new
#endif

#if defined(__APPLE__)
#include <AvailabilityMacros.h>

#if defined(__IPHONE_OS_VERSION_MIN_REQUIRED) && (!defined(__IPHONE_10_0) || __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_10_0)
#define VMA_USE_STL_SHARED_MUTEX 0
#endif
#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && (!defined(MAC_OS_X_VERSION_10_12) || MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_12)
#define VMA_USE_STL_SHARED_MUTEX 0
#endif

#endif
// END PPSSPP HACKS


*/
