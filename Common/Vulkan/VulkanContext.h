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

#ifdef ANDROID
#undef NDEBUG   // asserts
#endif
#include <cassert>
#include <string>
#include <vector>
#include <utility>

#include "base/logging.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX              /* Don't let Windows define min() or max() */
#define APP_NAME_STR_LEN 80
#include <Windows.h>
#elif defined(ANDROID)  // _WIN32
#include <android/native_window_jni.h>
#define VK_USE_PLATFORM_ANDROID_KHR
#else
#define VK_USE_PLATFORM_XCB_KHR
#include <unistd.h>
#endif // _WIN32

#include "Common/Vulkan/VulkanLoader.h"

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
	VULKAN_FLAG_PRESENT_FIFO_RELAXED = 8,
};

// A layer can expose extensions, keep track of those extensions here.
struct layer_properties {
	VkLayerProperties properties;
	std::vector<VkExtensionProperties> extensions;
};

// This is a bit repetitive...
class VulkanDeleteList {
public:
	void QueueDeleteDescriptorPool(VkDescriptorPool pool) { descPools_.push_back(pool); }
	void QueueDeleteShaderModule(VkShaderModule module) { modules_.push_back(module); }
	void QueueDeleteBuffer(VkBuffer buffer) { buffers_.push_back(buffer); }
	void QueueDeleteBufferView(VkBufferView bufferView) { bufferViews_.push_back(bufferView); }
	void QueueDeleteImage(VkImage image) { images_.push_back(image); }
	void QueueDeleteImageView(VkImageView imageView) { imageViews_.push_back(imageView); }
	void QueueDeleteDeviceMemory(VkDeviceMemory deviceMemory) { deviceMemory_.push_back(deviceMemory); }
	void QueueDeleteSampler(VkSampler sampler) { samplers_.push_back(sampler); }
	void QueueDeletePipelineCache(VkPipelineCache pipelineCache) { pipelineCaches_.push_back(pipelineCache); }

	void Take(VulkanDeleteList &del) {
		descPools_ = std::move(del.descPools_);
		modules_ = std::move(del.modules_);
		buffers_ = std::move(del.buffers_);
		bufferViews_ = std::move(del.bufferViews_);
		images_ = std::move(del.images_);
		imageViews_ = std::move(del.imageViews_);
		deviceMemory_ = std::move(del.deviceMemory_);
		samplers_ = std::move(del.samplers_);
		pipelineCaches_ = std::move(del.pipelineCaches_);
	}

	void PerformDeletes(VkDevice device) {
		for (auto &descPool : descPools_) {
			vkDestroyDescriptorPool(device, descPool, nullptr);
		}
		modules_.clear();
		for (auto &buf : buffers_) {
			vkDestroyBuffer(device, buf, nullptr);
		}
		buffers_.clear();
		for (auto &bufView : bufferViews_) {
			vkDestroyBufferView(device, bufView, nullptr);
		}
		bufferViews_.clear();
		for (auto &image : images_) {
			vkDestroyImage(device, image, nullptr);
		}
		images_.clear();
		for (auto &imageView : imageViews_) {
			vkDestroyImageView(device, imageView, nullptr);
		}
		imageViews_.clear();
		for (auto &mem : deviceMemory_) {
			vkFreeMemory(device, mem, nullptr);
		}
		deviceMemory_.clear();
		for (auto &sampler : samplers_) {
			vkDestroySampler(device, sampler, nullptr);
		}
		samplers_.clear();
		for (auto &pcache : pipelineCaches_) {
			vkDestroyPipelineCache(device, pcache, nullptr);
		}
		pipelineCaches_.clear();
	}

private:
	std::vector<VkDescriptorPool> descPools_;
	std::vector<VkShaderModule> modules_;
	std::vector<VkBuffer> buffers_;
	std::vector<VkBufferView> bufferViews_;
	std::vector<VkImage> images_;
	std::vector<VkImageView> imageViews_;
	std::vector<VkDeviceMemory> deviceMemory_;
	std::vector<VkSampler> samplers_;
	std::vector<VkPipelineCache> pipelineCaches_;
};

// VulkanContext sets up the basics necessary for rendering to a window, including framebuffers etc.
// Optionally, it can create a depth buffer for you as well.
class VulkanContext {
public:
	VulkanContext(const char *app_name, uint32_t flags);
	~VulkanContext();

	VkResult CreateDevice(int physical_device);

	VkDevice GetDevice() { return device_; }
	VkInstance GetInstance() { return instance_; }

	VulkanDeleteList &Delete() { return globalDeleteList_; }

	VkPipelineCache CreatePipelineCache();

#ifdef _WIN32
	void InitSurfaceWin32(HINSTANCE conn, HWND wnd);
#elif ANDROID
	void InitSurfaceAndroid(ANativeWindow *native_window, int width, int height);
#endif
	void InitQueue();
	void InitObjects(bool depthPresent);
	void InitSwapchain(VkCommandBuffer cmd);
	void InitSurfaceRenderPass(bool include_depth, bool clear);
	void InitFramebuffers(bool include_depth);
	void InitDepthStencilBuffer(VkCommandBuffer cmd);
	void InitCommandPool();

	void DestroyObjects();

	void DestroySurfaceRenderPass();
	void DestroyFramebuffers();
	void DestroySwapChain();
	void DestroyDepthStencilBuffer();
	void DestroyCommandPool();
	void DestroyDevice();

	void WaitUntilQueueIdle();

	// Utility functions for shorter code
	VkFence CreateFence(bool presignalled);
	bool CreateShaderModule(const std::vector<uint32_t> &spirv, VkShaderModule *shaderModule);

	void WaitAndResetFence(VkFence fence);

	int GetWidth() { return width; }
	int GetHeight() { return height; }

	VkCommandBuffer GetInitCommandBuffer();

	// This must only be accessed between BeginSurfaceRenderPass and EndSurfaceRenderPass.
	VkCommandBuffer GetSurfaceCommandBuffer() {
		return frame_[curFrame_ & 1].cmdBuf;
	}

	// The surface render pass is special because it has to acquire the backbuffer, and may thus "block".
	// Use the returned command buffer to enqueue commands that render to the backbuffer.
	// To render to other buffers first, you can submit additional commandbuffers using QueueBeforeSurfaceRender(cmd).
	VkCommandBuffer BeginSurfaceRenderPass(VkClearValue clear_values[2]);
	// May eventually need the ability to break and resume the backbuffer render pass in a few rare cases.
	void EndSurfaceRenderPass();

	void QueueBeforeSurfaceRender(VkCommandBuffer cmd);

	bool MemoryTypeFromProperties(uint32_t typeBits, VkFlags requirements_mask, uint32_t *typeIndex);

	VkResult InitDebugMsgCallback(PFN_vkDebugReportCallbackEXT dbgFunc, int bits, void *userdata = nullptr);
	void DestroyDebugMsgCallback();

	VkRenderPass GetSurfaceRenderPass() const {
		return surface_render_pass_;
	}

	VkPhysicalDevice GetPhysicalDevice(int n = 0) const {
		return physical_devices_[n];
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

	const VkPhysicalDeviceFeatures &GetFeaturesAvailable() const { return featuresAvailable_; }
	const VkPhysicalDeviceFeatures &GetFeaturesEnabled() const { return featuresEnabled_; }

private:
	VkSemaphore acquireSemaphore;

#ifdef _WIN32
	HINSTANCE connection;        // hInstance - Windows Instance
	HWND window;          // hWnd - window handle
#elif ANDROID  // _WIN32
	ANativeWindow *native_window;
#endif // _WIN32

	// TODO: Move to frame data
	VkCommandPool cmd_pool_;

	VkInstance instance_;
	VkDevice device_;
	VkQueue gfx_queue_;

	VkSurfaceKHR surface_;
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
		FrameData() : hasInitCommands(false), cmdInit(nullptr), cmdBuf(nullptr) {}

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

	VkPhysicalDeviceFeatures featuresAvailable_;
	VkPhysicalDeviceFeatures featuresEnabled_;

	std::vector<VkCommandBuffer> cmdQueue_;
};

// Wrapper around what you need to use a texture.
// Not very optimal - if you have many small textures you should use other strategies.
// Only supports simple 2D textures for now. Mipmap support will be added later.
class VulkanTexture {
public:
	VulkanTexture(VulkanContext *vulkan)
		: vulkan_(vulkan), image(0), imageLayout(VK_IMAGE_LAYOUT_UNDEFINED), mem(0), view(0), tex_width(0), tex_height(0), format_(VK_FORMAT_UNDEFINED),
		mappableImage(0), mappableMemory(0), needStaging(false) {
		memset(&mem_reqs, 0, sizeof(mem_reqs));
	}
	~VulkanTexture() {
		Destroy();
	}
	// Always call Create, Lock, Unlock. Unlock performs the upload if necessary.
	// Can later Lock and Unlock again. This cannot change the format. Create cannot
	// be called a second time without recreating the texture object until Destroy has
	// been called.

	VkResult Create(int w, int h, VkFormat format);
	uint8_t *Lock(int level, int *rowPitch);
	void Unlock();

	void Destroy();

	VkImageView GetImageView() const { return view; }

private:
	void CreateMappableImage();

	VulkanContext *vulkan_;
	VkImage image;
	VkImageLayout imageLayout;
	VkDeviceMemory mem;
	VkImageView view;
	int32_t tex_width, tex_height;
	VkFormat format_;
	VkImage mappableImage;
	VkDeviceMemory mappableMemory;
	VkMemoryRequirements mem_reqs;
	bool needStaging;
};

// Placeholder

class VulkanFramebuffer {
public:
	void Create(VulkanContext *vulkan, int w, int h, VkFormat format);

	void BeginPass(VkCommandBuffer cmd);
	void EndPass(VkCommandBuffer cmd);
	void TransitionToTexture(VkCommandBuffer cmd);

	VkImageView GetColorImageView();

private:
	VkImage image_;
	VkFramebuffer framebuffer_;
};

// Use these to push vertex, index and uniform data. Generally you'll have two of these
// and alternate on each frame.
// TODO: Make it possible to suballocate pushbuffers from a large DeviceMemory block.
// We'll have two of these that we alternate between on each frame.
// TODO: Make this auto-grow and shrink. Need to be careful about returning and using the new
// buffer handle on overflow.
class VulkanPushBuffer {
public:
	VulkanPushBuffer(VulkanContext *vulkan, size_t size) : offset_(0), size_(size), writePtr_(nullptr), deviceMemory_(0) {
		VkDevice device = vulkan->GetDevice();

		VkBufferCreateInfo b = {};
		b.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		b.pNext = nullptr;
		b.size = size;
		b.flags = 0;
		b.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		b.queueFamilyIndexCount = 0;
		b.pQueueFamilyIndices = nullptr;
		VkResult res = vkCreateBuffer(device, &b, nullptr, &buffer_);
		assert(VK_SUCCESS == res);

		// Okay, that's the buffer. Now let's allocate some memory for it.
		VkMemoryAllocateInfo alloc = {};
		alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc.pNext = nullptr;
		vulkan->MemoryTypeFromProperties(0xFFFFFFFF, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &alloc.memoryTypeIndex);
		alloc.allocationSize = size;

		res = vkAllocateMemory(device, &alloc, nullptr, &deviceMemory_);
		assert(VK_SUCCESS == res);
		res = vkBindBufferMemory(device, buffer_, deviceMemory_, 0);
		assert(VK_SUCCESS == res);
	}

	void Destroy(VulkanContext *vulkan) {
		vulkan->Delete().QueueDeleteBuffer(buffer_);
		vulkan->Delete().QueueDeleteDeviceMemory(deviceMemory_);
	}

	void Reset() { offset_ = 0; }

	void Begin(VkDevice device) {
		offset_ = 0;
		VkResult res = vkMapMemory(device, deviceMemory_, 0, size_, 0, (void **)(&writePtr_));
		assert(VK_SUCCESS == res);
	}

	void End(VkDevice device) {
		vkUnmapMemory(device, deviceMemory_);
		writePtr_ = nullptr;
	}

	size_t Allocate(size_t numBytes) {
		size_t out = offset_;
		offset_ += (numBytes + 3) & ~3;  // Round up to 4 bytes.
		if (offset_ >= size_) {
			// TODO: Allocate a second buffer, then combine them on the next frame.
#ifdef _WIN32
			DebugBreak();
#endif
		}
		return out;
	}

	// TODO: Add alignment support?
	// Returns the offset that should be used when binding this buffer to get this data.
	size_t Push(const void *data, size_t size) {
		size_t off = Allocate(size);
		memcpy(writePtr_ + off, data, size);
		return off;
	}

	uint32_t PushAligned(const void *data, size_t size, int align) {
		offset_ = (offset_ + align - 1) & ~(align - 1);
		size_t off = Allocate(size);
		memcpy(writePtr_ + off, data, size);
		return (uint32_t)off;
	}

	// "Zero-copy" variant - you can write the data directly as you compute it.
	void *Push(size_t size, size_t *bindOffset) {
		size_t off = Allocate(size);
		*bindOffset = off;
		return writePtr_ + off;
	}

	VkBuffer GetVkBuffer() const { return buffer_; }

private:
	VkDeviceMemory deviceMemory_;
	VkBuffer buffer_;
	size_t offset_;
	size_t size_;
	uint8_t *writePtr_;
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

