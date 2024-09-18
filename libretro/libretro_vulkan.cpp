// Debugging notes
// The crash happens when we try to call vkGetPhysicalDeviceProperties2KHR which seems to be null.
//
// Apparently we don't manage to specify the extensions we want. Still something reports that this one
// is present?
// Failed to load : vkGetPhysicalDeviceProperties2KHR
// Failed to load : vkGetPhysicalDeviceFeatures2KHR

#include <cstring>
#include <cassert>
#include <vector>
#include <mutex>
#include <condition_variable>

#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/Log.h"
#include "Core/Config.h"

#define VK_NO_PROTOTYPES
#include "libretro/libretro_vulkan.h"

using namespace PPSSPP_VK;

static retro_hw_render_interface_vulkan *vulkan;

static struct {
	VkInstance instance;
	VkPhysicalDevice gpu;
	VkSurfaceKHR surface;
	PFN_vkGetInstanceProcAddr get_instance_proc_addr;
	const char **required_device_extensions;
	unsigned num_required_device_extensions;
	const char **required_device_layers;
	unsigned num_required_device_layers;
	const VkPhysicalDeviceFeatures *required_features;
} vk_init_info;
static bool DEDICATED_ALLOCATION;

#define VULKAN_MAX_SWAPCHAIN_IMAGES 8
struct VkSwapchainKHR_T {
	uint32_t count;
	struct {
		VkImage handle;
		VkDeviceMemory memory;
		retro_vulkan_image retro_image;
	} images[VULKAN_MAX_SWAPCHAIN_IMAGES];
	std::mutex mutex;
	std::condition_variable condVar;
	int current_index;
};
static VkSwapchainKHR_T chain;

#define LIBRETRO_VK_WARP_LIST()                                      \
	LIBRETRO_VK_WARP_FUNC(vkCreateInstance);                          \
	LIBRETRO_VK_WARP_FUNC(vkDestroyInstance);                         \
	LIBRETRO_VK_WARP_FUNC(vkCreateDevice);                            \
	LIBRETRO_VK_WARP_FUNC(vkDestroyDevice);                           \
	LIBRETRO_VK_WARP_FUNC(vkGetPhysicalDeviceSurfaceCapabilitiesKHR); \
	LIBRETRO_VK_WARP_FUNC(vkDestroySurfaceKHR);                       \
	LIBRETRO_VK_WARP_FUNC(vkCreateSwapchainKHR);                      \
	LIBRETRO_VK_WARP_FUNC(vkGetSwapchainImagesKHR);                   \
	LIBRETRO_VK_WARP_FUNC(vkAcquireNextImageKHR);                     \
	LIBRETRO_VK_WARP_FUNC(vkQueuePresentKHR);                         \
	LIBRETRO_VK_WARP_FUNC(vkDestroySwapchainKHR);                     \
	LIBRETRO_VK_WARP_FUNC(vkQueueSubmit);                             \
	LIBRETRO_VK_WARP_FUNC(vkQueueWaitIdle);                           \
	LIBRETRO_VK_WARP_FUNC(vkCmdPipelineBarrier);                      \
	LIBRETRO_VK_WARP_FUNC(vkCreateRenderPass);

#define LIBRETRO_VK_WARP_FUNC(x)                                     \
	PFN_##x x##_org

LIBRETRO_VK_WARP_FUNC(vkGetInstanceProcAddr);
LIBRETRO_VK_WARP_FUNC(vkGetDeviceProcAddr);
LIBRETRO_VK_WARP_LIST();

static VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance_libretro(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance) {
	*pInstance = vk_init_info.instance;
	return VK_SUCCESS;
}

static void add_name_unique(std::vector<const char *> &list, const char *value) {
	for (const char *name : list)
		if (!strcmp(value, name))
			return;

	list.push_back(value);
}
static VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice_libretro(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
	VkDeviceCreateInfo info = *pCreateInfo;
	std::vector<const char *> EnabledLayerNames(info.ppEnabledLayerNames, info.ppEnabledLayerNames + info.enabledLayerCount);
	std::vector<const char *> EnabledExtensionNames(info.ppEnabledExtensionNames, info.ppEnabledExtensionNames + info.enabledExtensionCount);
	VkPhysicalDeviceFeatures EnabledFeatures = *info.pEnabledFeatures;

	for (unsigned i = 0; i < vk_init_info.num_required_device_layers; i++)
		add_name_unique(EnabledLayerNames, vk_init_info.required_device_layers[i]);

	for (unsigned i = 0; i < vk_init_info.num_required_device_extensions; i++)
		add_name_unique(EnabledExtensionNames, vk_init_info.required_device_extensions[i]);

	for (unsigned i = 0; i < sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32); i++) {
		if (((VkBool32 *)vk_init_info.required_features)[i])
			((VkBool32 *)&EnabledFeatures)[i] = VK_TRUE;
	}

	for (auto extension_name : EnabledExtensionNames) {
		if (!strcmp(extension_name, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME))
			DEDICATED_ALLOCATION = true;
	}

	info.enabledLayerCount = (uint32_t)EnabledLayerNames.size();
	info.ppEnabledLayerNames = info.enabledLayerCount ? EnabledLayerNames.data() : nullptr;
	info.enabledExtensionCount = (uint32_t)EnabledExtensionNames.size();
	info.ppEnabledExtensionNames = info.enabledExtensionCount ? EnabledExtensionNames.data() : nullptr;
	info.pEnabledFeatures = &EnabledFeatures;

	return vkCreateDevice_org(physicalDevice, &info, pAllocator, pDevice);
}

static VKAPI_ATTR VkResult VKAPI_CALL vkCreateLibretroSurfaceKHR(VkInstance instance, const void *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) {
	*pSurface = vk_init_info.surface;
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR_libretro(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities) {
	VkResult res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR_org(physicalDevice, surface, pSurfaceCapabilities);
	if (res == VK_SUCCESS) {
		int w = g_Config.iInternalResolution * NATIVEWIDTH;
		int h = g_Config.iInternalResolution * NATIVEHEIGHT;

		if (g_Config.bDisplayCropTo16x9)
			h -= g_Config.iInternalResolution * 2;

		pSurfaceCapabilities->minImageExtent.width = w;
		pSurfaceCapabilities->minImageExtent.height = h;
		pSurfaceCapabilities->maxImageExtent.width = w;
		pSurfaceCapabilities->maxImageExtent.height = h;
		pSurfaceCapabilities->currentExtent.width = w;
		pSurfaceCapabilities->currentExtent.height = h;
	}
	return res;
}

static bool MemoryTypeFromProperties(uint32_t typeBits, VkFlags requirements_mask, uint32_t *typeIndex) {
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(vulkan->gpu, &memory_properties);
	// Search memtypes to find first index with those properties
	for (uint32_t i = 0; i < 32; i++) {
		if ((typeBits & 1) == 1) {
			// Type is available, does it match user properties?
			if ((memory_properties.memoryTypes[i].propertyFlags & requirements_mask) == requirements_mask) {
				*typeIndex = i;
				return true;
			}
		}
		typeBits >>= 1;
	}
	// No memory types matched, return failure
	return false;
}

static VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR_libretro(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain) {
	uint32_t swapchain_mask = vulkan->get_sync_index_mask(vulkan->handle);

	chain.count = 0;
	while (swapchain_mask) {
		chain.count++;
		swapchain_mask >>= 1;
	}
	assert(chain.count <= VULKAN_MAX_SWAPCHAIN_IMAGES);

	for (uint32_t i = 0; i < chain.count; i++) {
		{
			VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
			info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
			info.imageType = VK_IMAGE_TYPE_2D;
			info.format = pCreateInfo->imageFormat;
			info.extent.width = pCreateInfo->imageExtent.width;
			info.extent.height = pCreateInfo->imageExtent.height;
			info.extent.depth = 1;
			info.mipLevels = 1;
			info.arrayLayers = 1;
			info.samples = VK_SAMPLE_COUNT_1_BIT;
			info.tiling = VK_IMAGE_TILING_OPTIMAL;
			info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			vkCreateImage(device, &info, pAllocator, &chain.images[i].handle);
		}

		VkMemoryRequirements memreq;
		vkGetImageMemoryRequirements(device, chain.images[i].handle, &memreq);

		VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		alloc.allocationSize = memreq.size;

		VkMemoryDedicatedAllocateInfoKHR dedicated{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR };
		if (DEDICATED_ALLOCATION) {
			alloc.pNext = &dedicated;
			dedicated.image = chain.images[i].handle;
		}

		MemoryTypeFromProperties(memreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &alloc.memoryTypeIndex);
		VkResult res = vkAllocateMemory(device, &alloc, pAllocator, &chain.images[i].memory);
		assert(res == VK_SUCCESS);
		res = vkBindImageMemory(device, chain.images[i].handle, chain.images[i].memory, 0);
		assert(res == VK_SUCCESS);

		chain.images[i].retro_image.create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		chain.images[i].retro_image.create_info.image = chain.images[i].handle;
		chain.images[i].retro_image.create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		chain.images[i].retro_image.create_info.format = pCreateInfo->imageFormat;
		chain.images[i].retro_image.create_info.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
		chain.images[i].retro_image.create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		chain.images[i].retro_image.create_info.subresourceRange.layerCount = 1;
		chain.images[i].retro_image.create_info.subresourceRange.levelCount = 1;
		res = vkCreateImageView(device, &chain.images[i].retro_image.create_info, pAllocator, &chain.images[i].retro_image.image_view);
		assert(res == VK_SUCCESS);

		chain.images[i].retro_image.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}

	chain.current_index = -1;
	*pSwapchain = (VkSwapchainKHR)&chain;

	return VK_SUCCESS;
}
static VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR_libretro(VkDevice device, VkSwapchainKHR swapchain_, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages) {
	VkSwapchainKHR_T *swapchain = (VkSwapchainKHR_T *)swapchain_;
	if (pSwapchainImages) {
		assert(*pSwapchainImageCount <= swapchain->count);
		for (int i = 0; i < *pSwapchainImageCount; i++)
			pSwapchainImages[i] = swapchain->images[i].handle;
	} else
		*pSwapchainImageCount = swapchain->count;

	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR_libretro(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex) {
	vulkan->wait_sync_index(vulkan->handle);
	*pImageIndex = vulkan->get_sync_index(vulkan->handle);
#if 0
	vulkan->set_signal_semaphore(vulkan->handle, semaphore);
#endif
	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR_libretro(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) {
	VkSwapchainKHR_T *swapchain = (VkSwapchainKHR_T *)pPresentInfo->pSwapchains[0];
	std::unique_lock<std::mutex> lock(swapchain->mutex);
#if 0
	if(chain.current_index >= 0)
		chain.condVar.wait(lock);
#endif

	chain.current_index = pPresentInfo->pImageIndices[0];
#if 0
	vulkan->set_image(vulkan->handle, &swapchain->images[pPresentInfo->pImageIndices[0]].retro_image, pPresentInfo->waitSemaphoreCount, pPresentInfo->pWaitSemaphores, vulkan->queue_index);
#else
	vulkan->set_image(vulkan->handle, &swapchain->images[pPresentInfo->pImageIndices[0]].retro_image, 0, nullptr, vulkan->queue_index);
#endif
	swapchain->condVar.notify_all();

	return VK_SUCCESS;
}

void vk_libretro_wait_for_presentation() {
	std::unique_lock<std::mutex> lock(chain.mutex);
	if (chain.current_index < 0)
		chain.condVar.wait(lock);
#if 0
	chain.current_index = -1;
	chain.condVar.notify_all();
#endif
}

static VKAPI_ATTR void VKAPI_CALL vkDestroyInstance_libretro(VkInstance instance, const VkAllocationCallbacks *pAllocator) {}
static VKAPI_ATTR void VKAPI_CALL vkDestroyDevice_libretro(VkDevice device, const VkAllocationCallbacks *pAllocator) {}
static VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR_libretro(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator) {}
static VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR_libretro(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator) {
	for (int i = 0; i < chain.count; i++) {
		vkDestroyImage(device, chain.images[i].handle, pAllocator);
		vkDestroyImageView(device, chain.images[i].retro_image.image_view, pAllocator);
		vkFreeMemory(device, chain.images[i].memory, pAllocator);
	}

	memset(&chain.images, 0x00, sizeof(chain.images));
	chain.count = 0;
	chain.current_index = -1;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit_libretro(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence) {
	VkResult res = VK_SUCCESS;

#if 0
	for(int i = 0; i < submitCount; i++)
		vulkan->set_command_buffers(vulkan->handle, pSubmits[i].commandBufferCount, pSubmits[i].pCommandBuffers);
#else
#if 1
	for (int i = 0; i < submitCount; i++) {
		((VkSubmitInfo *)pSubmits)[i].waitSemaphoreCount = 0;
		((VkSubmitInfo *)pSubmits)[i].pWaitSemaphores = nullptr;
		((VkSubmitInfo *)pSubmits)[i].signalSemaphoreCount = 0;
		((VkSubmitInfo *)pSubmits)[i].pSignalSemaphores = nullptr;
	}
#endif
	vulkan->lock_queue(vulkan->handle);
	res = vkQueueSubmit_org(queue, submitCount, pSubmits, fence);
	vulkan->unlock_queue(vulkan->handle);
#endif

	return res;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle_libretro(VkQueue queue) {
	vulkan->lock_queue(vulkan->handle);
	VkResult res = vkQueueWaitIdle_org(queue);
	vulkan->unlock_queue(vulkan->handle);
	return res;
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier_libretro(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers) {
	VkImageMemoryBarrier *barriers = (VkImageMemoryBarrier *)pImageMemoryBarriers;
	for (int i = 0; i < imageMemoryBarrierCount; i++) {
		if (pImageMemoryBarriers[i].oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
			barriers[i].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barriers[i].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		}
		if (pImageMemoryBarriers[i].newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
			barriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		}
	}
	return vkCmdPipelineBarrier_org(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, barriers);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass_libretro(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass) {
	if (pCreateInfo->pAttachments[0].finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
		((VkAttachmentDescription *)pCreateInfo->pAttachments)[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	return vkCreateRenderPass_org(device, pCreateInfo, pAllocator, pRenderPass);
}

#undef LIBRETRO_VK_WARP_FUNC
#define LIBRETRO_VK_WARP_FUNC(x)                    \
	if (!strcmp(pName, #x)) {                     \
		x##_org = (PFN_##x)fptr;                   \
		return (PFN_vkVoidFunction)x##_libretro;   \
	}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr_libretro(VkInstance instance, const char *pName) {
	if (false
#ifdef _WIN32
		 || !strcmp(pName, "vkCreateWin32SurfaceKHR")
#endif
#ifdef __ANDROID__
		 || !strcmp(pName, "vkCreateAndroidSurfaceKHR")
#endif
#ifdef VK_USE_PLATFORM_METAL_EXT
		 || !strcmp(pName, "vkCreateMetalSurfaceEXT")
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
		 || !strcmp(pName, "vkCreateXlibSurfaceKHR")
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
		 || !strcmp(pName, "vkCreateXcbSurfaceKHR")
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
		 || !strcmp(pName, "vkCreateWaylandSurfaceKHR")
#endif
#ifdef VK_USE_PLATFORM_DISPLAY_KHR
		 || !strcmp(pName, "vkCreateDisplayPlaneSurfaceKHR")
#endif
	) {
		return (PFN_vkVoidFunction)vkCreateLibretroSurfaceKHR;
	}

	PFN_vkVoidFunction fptr = vk_init_info.get_instance_proc_addr(instance, pName);
   if (!fptr) {
      ERROR_LOG(Log::G3D, "Failed to load VK instance function: %s", pName);
      return fptr;
   }

	LIBRETRO_VK_WARP_LIST();

	return fptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr_libretro(VkDevice device, const char *pName) {
	PFN_vkVoidFunction fptr = vkGetDeviceProcAddr_org(device, pName);
	if (!fptr)
		return fptr;

	LIBRETRO_VK_WARP_LIST();

	return fptr;
}

void vk_libretro_init(VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr, const char **required_device_extensions, unsigned num_required_device_extensions, const char **required_device_layers, unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features) {
	assert(surface);

	vk_init_info.instance = instance;
	vk_init_info.gpu = gpu;
	vk_init_info.surface = surface;
	vk_init_info.get_instance_proc_addr = get_instance_proc_addr;
	vk_init_info.required_device_extensions = required_device_extensions;
	vk_init_info.num_required_device_extensions = num_required_device_extensions;
	vk_init_info.required_device_layers = required_device_layers;
	vk_init_info.num_required_device_layers = num_required_device_layers;
	vk_init_info.required_features = required_features;

	vkGetInstanceProcAddr_org = vkGetInstanceProcAddr;
	vkGetInstanceProcAddr = vkGetInstanceProcAddr_libretro;
	vkGetDeviceProcAddr_org = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(instance, "vkGetDeviceProcAddr");;
	vkGetDeviceProcAddr = vkGetDeviceProcAddr_libretro;
	vkCreateInstance = vkCreateInstance_libretro;

	vkEnumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
	vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceExtensionProperties");
	vkEnumerateInstanceLayerProperties = (PFN_vkEnumerateInstanceLayerProperties)vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceLayerProperties");
}

void vk_libretro_set_hwrender_interface(retro_hw_render_interface *hw_render_interface) {
   vulkan = (retro_hw_render_interface_vulkan *)hw_render_interface;
}

void vk_libretro_shutdown() {
	memset(&vk_init_info, 0, sizeof(vk_init_info));
	vulkan = nullptr;
	DEDICATED_ALLOCATION = false;
}
