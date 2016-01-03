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
	VULKAN_FLAG_PRESENT_MAILBOX = 2,
	VULKAN_FLAG_PRESENT_IMMEDIATE = 4,
};

// A layer can expose extensions, keep track of those extensions here.
struct layer_properties {
	VkLayerProperties properties;
	std::vector<VkExtensionProperties> extensions;
};

class VulkanDeleteList {
public:
	void QueueDelete(VkImage image) { images_.push_back(image); }
	void QueueDelete(VkDeviceMemory deviceMemory) { memory_.push_back(deviceMemory); }

	void Ingest(VulkanDeleteList &del) {
		images_ = del.images_;
		memory_ = del.memory_;
		del.images_.clear();
		del.memory_.clear();
	}

	void PerformDeletes(VkDevice device) {
		for (auto &mem : memory_) {
			vkFreeMemory(device, mem, nullptr);
		}
		memory_.clear();
		for (auto &image : images_) {
			vkDestroyImage(device, image, nullptr);
		}
		images_.clear();
	}

private:
	std::vector<VkImage> images_;
	std::vector<VkDeviceMemory> memory_;
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

	template <class T>
	void QueueDelete(T mem) {
		globalDeleteList_.QueueDelete(mem);
	}

	void InitSurfaceAndQueue(HINSTANCE conn, HWND wnd);
	void InitSwapchain(VkCommandBuffer cmd);
	void InitSurfaceRenderPass(bool include_depth, bool clear);
	void InitFramebuffers(bool include_depth);
	void InitDepthStencilBuffer(VkCommandBuffer cmd);
	void InitCommandPool();

	void InitObjects(HINSTANCE hInstance, HWND hWnd, bool depthPresent);
	void DestroyObjects();

	VkCommandBuffer GetInitCommandBuffer();

	void DestroySurfaceRenderPass();
	void DestroyFramebuffers();
	void DestroySwapChain();
	void DestroyDepthStencilBuffer();
	void DestroyCommandPool();
	void DestroyDevice();

	void WaitUntilQueueIdle();

	// Utility functions for shorter code
	VkFence CreateFence(bool presignalled);
	void WaitAndResetFence(VkFence fence);

	int GetWidth() { return width; }
	int GetHeight() { return height; }

	// The surface render pass is special because it has to acquire the backbuffer, and may thus "block".
	// Use the returned command buffer to enqueue commands. Might be a good idea to use secondary cmd buffers.
	VkCommandBuffer BeginSurfaceRenderPass(VkClearValue clear_values[2]);

	void EndSurfaceRenderPass();

	bool MemoryTypeFromProperties(uint32_t typeBits, VkFlags requirements_mask, uint32_t *typeIndex);

	VkResult InitDebugMsgCallback(PFN_vkDebugReportCallbackEXT dbgFunc, int bits, void *userdata = nullptr);
	void DestroyDebugMsgCallback();

	VkRenderPass GetSurfaceRenderPass() const {
		return surface_render_pass_;
	}

	VkPhysicalDevice GetPhysicalDevice() const {
		return physical_devices_[0];
	}

	VkQueue GetGraphicsQueue() const {
		return gfx_queue_;
	}

	int GetGraphicsQueueFamilyIndex() const {
		return graphics_queue_family_index_;
	}

	const VkPhysicalDeviceProperties &GetPhysicalDeviceProperties() {
		return gpu_props;
	}

	VkResult InitGlobalExtensionProperties();
	VkResult InitLayerExtensionProperties(layer_properties &layer_props);

	VkResult InitGlobalLayerProperties();

	VkResult InitDeviceExtensionProperties(layer_properties &layer_props);
	VkResult InitDeviceLayerProperties();

private:
	VkSemaphore acquireSemaphore;

#ifdef _WIN32
	HINSTANCE connection;        // hInstance - Windows Instance
	HWND        window;          // hWnd - window handle
#else  // _WIN32
	xcb_connection_t *connection;
	xcb_screen_t *screen;
	xcb_window_t window;
	xcb_intern_atom_reply_t *atom_wm_delete_window;
#endif // _WIN32

	// TODO: Move to frame data
	VkCommandPool cmd_pool_;

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

	uint32_t graphics_queue_family_index_;
	VkPhysicalDeviceProperties gpu_props;
	std::vector<VkQueueFamilyProperties> queue_props;
	VkPhysicalDeviceMemoryProperties memory_properties;

	struct swap_chain_buffer {
		VkImage image;
		VkImageView view;
	};

	// Swap chain
	int width, height;
	int flags_;
	VkFormat swapchain_format;
	std::vector<VkFramebuffer> framebuffers_;
	uint32_t swapchainImageCount;
	VkSwapchainKHR swap_chain_;
	std::vector<swap_chain_buffer> swapChainBuffers;

	// Manages flipping command buffers for the backbuffer render pass.
	// It is recommended to do the same for other rendering passes.
	struct FrameData {
		FrameData() : fence(nullptr), hasInitCommands(false), cmdInit(nullptr), cmdBuf(nullptr) {}

		VkFence fence;
		bool hasInitCommands;
		VkCommandBuffer cmdInit;
		VkCommandBuffer cmdBuf;

		VulkanDeleteList deleteList;
	};

	FrameData frame_[2];
	int curFrame_;

	// At the end of the frame, this is copied into the frame's delete list, so it can be processed
	// the next time the frame comes around again.
	VulkanDeleteList globalDeleteList_;

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

	void Create(VulkanContext *vulkan, int w, int h, VkFormat format);
	uint8_t *Lock(VulkanContext *vulkan, int *rowPitch);
	void Unlock(VulkanContext *vulkan);

	void Destroy(VulkanContext *vulkan);

private:
	VkFormat format_;
	VkImage mappableImage;
	VkDeviceMemory mappableMemory;
	VkMemoryRequirements mem_reqs;
	bool needStaging;
};

VkBool32 CheckLayers(const std::vector<layer_properties> &layer_props, const std::vector<const char *> &layer_names);

void VulkanBeginCommandBuffer(VkCommandBuffer cmd);

void init_glslang();
void finalize_glslang();

bool GLSLtoSPV(const VkShaderStageFlagBits shader_type, const char *pshader, std::vector<uint32_t> &spirv, std::string *errorMessage = nullptr);

void TransitionImageLayout(
	VkCommandBuffer cmd,
	VkImage image,
	VkImageAspectFlags aspectMask,
	VkImageLayout old_image_layout,
	VkImageLayout new_image_layout);

#endif // UTIL_INIT

