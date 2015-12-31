/*
 * Vulkan Samples Kit
 *
 * Copyright (C) 2015 Valve Corporation
 * Copyright (C) 2015 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef UTIL_INIT
#define UTIL_INIT

#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX              /* Don't let Windows define min() or max() */
#define APP_NAME_STR_LEN 80
#include <Windows.h>
#else  // _WIN32
#define VK_USE_PLATFORM_XCB_KHR
#include <unistd.h>
#endif // _WIN32

#include "vulkan/vulkan.h"
#include "vulkan/vk_sdk_platform.h"

 /* Amount of time, in nanoseconds, to wait for a command buffer to complete */
#define FENCE_TIMEOUT 10000000000

#if defined(NDEBUG) && defined(__GNUC__)
#define U_ASSERT_ONLY __attribute__((unused))
#else
#define U_ASSERT_ONLY
#endif

enum {
	VULKAN_FLAG_VALIDATE = 1,
};

// A layer can expose extensions, keep track of those extensions here.
struct layer_properties {
	VkLayerProperties properties;
	std::vector<VkExtensionProperties> extensions;
};

// VulkanContext sets up the basics necessary for rendering to a window, including framebuffers etc.
// Optionally, it can create a depth buffer for you as well.
class VulkanContext {
public:
	VulkanContext(const char *app_name, uint32_t flags);
	~VulkanContext();

	VkResult CreateDevice(int physical_device);

	VkDevice GetDevice() {
		return device_;
	}

	void InitSurfaceAndQueue(HINSTANCE conn, HWND wnd);
	void InitSwapchain();
	void InitSurfaceRenderPass(bool include_depth, bool clear);
	void InitFramebuffers(bool include_depth);
	void InitDepthStencilBuffer();
	void InitCommandPool();
	void InitCommandBuffer();

	void InitObjects(HINSTANCE hInstance, HWND hWnd, bool depthPresent);
	void DestroyObjects();

	void BeginInitCommandBuffer();
	void SubmitInitCommandBufferSync();
	VkCommandBuffer GetInitCommandBuffer() {
		return cmd_;
	}

	void DestroySurfaceRenderPass();
	void DestroyFramebuffers();
	void DestroySwapChain();
	void DestroyDepthStencilBuffer();
	void DestroyCommandPool();
	void DestroyCommandBuffer();
	void DestroyDevice();

	void WaitUntilQueueIdle();

	// Utility functions for shorter code
	VkFence CreateFence();
	void WaitForFence(VkFence fence);

	int GetWidth() { return width; }
	int GetHeight() { return height; }

	// The surface render pass is special because it has to acquire the backbuffer, and may thus "block".
	// Use the returned command buffer to enqueue commands. Might be a good idea to use secondary cmd buffers.
	VkCommandBuffer BeginSurfaceRenderPass(VkClearValue clear_values[2]);

	void EndSurfaceRenderPass();

	bool MemoryTypeFromProperties(uint32_t typeBits, VkFlags requirements_mask, uint32_t *typeIndex);

	VkResult InitDebugMsgCallback(PFN_vkDebugReportCallbackEXT dbgFunc, int bits, void *userdata = nullptr);
	void DestroyDebugMsgCallback();

	VkSemaphore acquireSemaphore;

	VkRenderPass GetSurfaceRenderPass() {
		return surface_render_pass_;
	}

	VkPhysicalDevice GetPhysicalDevice() {
		return physical_devices_[0];
	}

	VkQueue GetGraphicsQueue() {
		return gfx_queue_;
	}

	int GetGraphicsQueueFamilyIndex() {
		return gfx_queue_family_index_;
	}

	VkResult init_global_extension_properties();
	VkResult init_layer_extension_properties(layer_properties &layer_props);

	VkResult init_global_layer_properties();

	VkResult init_device_extension_properties(layer_properties &layer_props);
	VkResult init_device_layer_properties();


#ifdef _WIN32
#define APP_NAME_STR_LEN 80
	HINSTANCE connection;        // hInstance - Windows Instance
	char name[APP_NAME_STR_LEN]; // Name to put on the window/icon
	HWND        window;          // hWnd - window handle
#else  // _WIN32
	xcb_connection_t *connection;
	xcb_screen_t *screen;
	xcb_window_t window;
	xcb_intern_atom_reply_t *atom_wm_delete_window;
#endif // _WIN32

	VkCommandPool cmd_pool_;
	VkCommandBuffer cmd_;  // Buffer for initialization commands
	bool cmdInitActive_;

	VkInstance instance_;
	VkDevice device_;
	VkQueue gfx_queue_;

	VkSurfaceKHR surface;
	bool prepared;
	bool use_staging_buffer_;

	std::vector<const char *> instance_layer_names;
	std::vector<const char *> instance_extension_names;
	std::vector<layer_properties> instance_layer_properties;
	std::vector<VkExtensionProperties> instance_extension_properties;

	std::vector<const char *> device_layer_names;
	std::vector<const char *> device_extension_names;
	std::vector<layer_properties> device_layer_properties;
	std::vector<VkExtensionProperties> device_extension_properties;
	std::vector<VkPhysicalDevice> physical_devices_;

	uint32_t gfx_queue_family_index_;
	VkPhysicalDeviceProperties gpu_props;
	std::vector<VkQueueFamilyProperties> queue_props;
	VkPhysicalDeviceMemoryProperties memory_properties;

private:
	struct swap_chain_buffer {
		VkImage image;
		VkImageView view;
	};

	// Swap chain
	int width, height;
	VkFormat swapchain_format;
	std::vector<VkFramebuffer> framebuffers_;
	uint32_t swapchainImageCount;
	VkSwapchainKHR swap_chain_;
	std::vector<swap_chain_buffer> swapChainBuffers;


	// Simple loader for the WSI extension.
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
	PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
	PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;
	PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
	PFN_vkQueuePresentKHR fpQueuePresentKHR;
	// And the DEBUG_REPORT extension.
	PFN_vkCreateDebugReportCallbackEXT dbgCreateMsgCallback;
	PFN_vkDestroyDebugReportCallbackEXT dbgDestroyMsgCallback;
	std::vector<VkDebugReportCallbackEXT> msg_callbacks;

	struct {
		VkFormat format;
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
	} depth;

	VkRenderPass surface_render_pass_;
	uint32_t current_buffer;
	uint32_t queue_count;
};

// Wrapper around what you need to use a texture.
// Not very optimal - if you have many small textures you should use other strategies.
class VulkanTexture {
public:
	VkImage image;
	VkImageLayout imageLayout;

	VkDeviceMemory mem;
	VkImageView view;

	int32_t tex_width, tex_height;

	// Always call Create, Lock, Unlock. Unlock performs the upload if necessary.

	void Create(VulkanContext *vulkan, int w, int h);
	uint8_t *Lock(VulkanContext *vulkan, int *rowPitch);
	void Unlock(VulkanContext *vulkan);

	void Destroy(VulkanContext *vulkan);

private:
	VkImage mappableImage;
	VkDeviceMemory mappableMemory;
	VkMemoryRequirements mem_reqs;
	bool needStaging;
};

VkBool32 CheckLayers(const std::vector<layer_properties> &layer_props, const std::vector<const char *> &layer_names);

void VulkanBeginCommandBuffer(VkCommandBuffer cmd);

void vk_submit_sync(VkDevice device, VkSemaphore waitForSemaphore, VkQueue queue, VkCommandBuffer cmd);

void init_glslang();
void finalize_glslang();

bool GLSLtoSPV(const VkShaderStageFlagBits shader_type, const char *pshader, std::vector<unsigned int> &spirv);

void TransitionImageLayout(
	VkCommandBuffer cmd,
	VkImage image,
	VkImageAspectFlags aspectMask,
	VkImageLayout old_image_layout,
	VkImageLayout new_image_layout);

void VulkanAssertImpl(VkResult check, const char *function, const char *file, int line);

// DO NOT call vulkan functions within this! Instead, store the result in a variable and check that.
#define VulkanAssert(x) if ((x) != VK_SUCCESS) VulkanAssertImpl((x), __FUNCTION__, __FILE__, __LINE__);

#endif // UTIL_INIT

