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

// Amount of time, in nanoseconds, to wait for a command buffer to complete
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


struct VulkanPhysicalDeviceInfo {
	VkFormat preferredDepthStencilFormat;
};

// This is a bit repetitive...
class VulkanDeleteList {
	struct Callback {
		explicit Callback(void(*f)(void *userdata), void *u)
			: func(f), userdata(u) {
		}

		void(*func)(void *userdata);
		void *userdata;
	};

public:
	void QueueDeleteDescriptorPool(VkDescriptorPool pool) { descPools_.push_back(pool); }
	void QueueDeleteShaderModule(VkShaderModule module) { modules_.push_back(module); }
	void QueueDeleteBuffer(VkBuffer buffer) { buffers_.push_back(buffer); }
	void QueueDeleteBufferView(VkBufferView bufferView) { bufferViews_.push_back(bufferView); }
	void QueueDeleteImage(VkImage image) { images_.push_back(image); }
	void QueueDeleteImageView(VkImageView imageView) { imageViews_.push_back(imageView); }
	void QueueDeleteDeviceMemory(VkDeviceMemory deviceMemory) { deviceMemory_.push_back(deviceMemory); }
	void QueueDeleteSampler(VkSampler sampler) { samplers_.push_back(sampler); }
	void QueueDeletePipeline(VkPipeline pipeline) { pipelines_.push_back(pipeline); }
	void QueueDeletePipelineCache(VkPipelineCache pipelineCache) { pipelineCaches_.push_back(pipelineCache); }
	void QueueDeleteRenderPass(VkRenderPass renderPass) { renderPasses_.push_back(renderPass); }
	void QueueDeleteFramebuffer(VkFramebuffer framebuffer) { framebuffers_.push_back(framebuffer); }
	void QueueCallback(void(*func)(void *userdata), void *userdata) { callbacks_.push_back(Callback(func, userdata)); }

	void Take(VulkanDeleteList &del) {
		assert(descPools_.size() == 0);
		assert(modules_.size() == 0);
		assert(buffers_.size() == 0);
		assert(bufferViews_.size() == 0);
		assert(images_.size() == 0);
		assert(imageViews_.size() == 0);
		assert(deviceMemory_.size() == 0);
		assert(samplers_.size() == 0);
		assert(pipelines_.size() == 0);
		assert(pipelineCaches_.size() == 0);
		assert(renderPasses_.size() == 0);
		assert(framebuffers_.size() == 0);
		assert(callbacks_.size() == 0);
		descPools_ = std::move(del.descPools_);
		modules_ = std::move(del.modules_);
		buffers_ = std::move(del.buffers_);
		bufferViews_ = std::move(del.bufferViews_);
		images_ = std::move(del.images_);
		imageViews_ = std::move(del.imageViews_);
		deviceMemory_ = std::move(del.deviceMemory_);
		samplers_ = std::move(del.samplers_);
		pipelines_ = std::move(del.pipelines_);
		pipelineCaches_ = std::move(del.pipelineCaches_);
		renderPasses_ = std::move(del.renderPasses_);
		framebuffers_ = std::move(del.framebuffers_);
		callbacks_ = std::move(del.callbacks_);
	}

	void PerformDeletes(VkDevice device) {
		for (auto &descPool : descPools_) {
			vkDestroyDescriptorPool(device, descPool, nullptr);
		}
		descPools_.clear();
		for (auto &module : modules_) {
			vkDestroyShaderModule(device, module, nullptr);
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
		for (auto &pipeline : pipelines_) {
			vkDestroyPipeline(device, pipeline, nullptr);
		}
		pipelines_.clear();
		for (auto &pcache : pipelineCaches_) {
			vkDestroyPipelineCache(device, pcache, nullptr);
		}
		pipelineCaches_.clear();
		for (auto &renderPass : renderPasses_) {
			vkDestroyRenderPass(device, renderPass, nullptr);
		}
		renderPasses_.clear();
		for (auto &framebuffer : framebuffers_) {
			vkDestroyFramebuffer(device, framebuffer, nullptr);
		}
		framebuffers_.clear();
		for (auto &callback : callbacks_) {
			callback.func(callback.userdata);
		}
		callbacks_.clear();
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
	std::vector<VkPipeline> pipelines_;
	std::vector<VkPipelineCache> pipelineCaches_;
	std::vector<VkRenderPass> renderPasses_;
	std::vector<VkFramebuffer> framebuffers_;
	std::vector<Callback> callbacks_;
};

// VulkanContext sets up the basics necessary for rendering to a window, including framebuffers etc.
// Optionally, it can create a depth buffer for you as well.
class VulkanContext {
public:
	VulkanContext(const char *app_name, int app_ver, uint32_t flags);
	~VulkanContext();

	VkResult CreateDevice(int physical_device);

	const std::string &InitError() { return init_error_; }

	VkDevice GetDevice() { return device_; }
	VkInstance GetInstance() { return instance_; }

	VulkanDeleteList &Delete() { return globalDeleteList_; }

	VkPipelineCache CreatePipelineCache();

#ifdef _WIN32
	void InitSurfaceWin32(HINSTANCE conn, HWND wnd);
	void ReinitSurfaceWin32();
#elif ANDROID
	void InitSurfaceAndroid(ANativeWindow *native_window, int width, int height);
	void ReinitSurfaceAndroid(int width, int height);
#endif
	void InitQueue();
	void InitObjects(bool depthPresent);
	void InitSwapchain(VkCommandBuffer cmd);
	void InitSurfaceRenderPass(bool include_depth, bool clear);
	void InitFramebuffers(bool include_depth);
	void InitDepthStencilBuffer(VkCommandBuffer cmd);
	void InitCommandPool();

	// Also destroys the surface.
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

	int GetWidth() { return width_; }
	int GetHeight() { return height_; }

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
	const VulkanPhysicalDeviceInfo &GetDeviceInfo() const { return deviceInfo_; }


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

	std::string init_error_;
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

	// Custom collection of things that are good to know
	VulkanPhysicalDeviceInfo deviceInfo_;

	struct swap_chain_buffer {
		VkImage image;
		VkImageView view;
	};

	// Swap chain
	int width_, height_;
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

// Stand-alone utility functions
void VulkanBeginCommandBuffer(VkCommandBuffer cmd);
void TransitionImageLayout(
	VkCommandBuffer cmd,
	VkImage image,
	VkImageAspectFlags aspectMask,
	VkImageLayout old_image_layout,
	VkImageLayout new_image_layout);

// GLSL compiler
void init_glslang();
void finalize_glslang();
bool GLSLtoSPV(const VkShaderStageFlagBits shader_type, const char *pshader, std::vector<uint32_t> &spirv, std::string *errorMessage = nullptr);

#endif // UTIL_INIT

