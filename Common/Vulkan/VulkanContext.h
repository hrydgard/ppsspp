#ifndef UTIL_INIT
#define UTIL_INIT

#ifdef __ANDROID__
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
#elif defined(__ANDROID__)  // _WIN32
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
	void QueueDeleteCommandPool(VkCommandPool &pool) { cmdPools_.push_back(pool); pool = VK_NULL_HANDLE; }
	void QueueDeleteDescriptorPool(VkDescriptorPool &pool) { descPools_.push_back(pool); pool = VK_NULL_HANDLE; }
	void QueueDeleteShaderModule(VkShaderModule &module) { modules_.push_back(module); module = VK_NULL_HANDLE; }
	void QueueDeleteBuffer(VkBuffer &buffer) { buffers_.push_back(buffer); buffer = VK_NULL_HANDLE; }
	void QueueDeleteBufferView(VkBufferView &bufferView) { bufferViews_.push_back(bufferView); bufferView = VK_NULL_HANDLE; }
	void QueueDeleteImage(VkImage &image) { images_.push_back(image); image = VK_NULL_HANDLE; }
	void QueueDeleteImageView(VkImageView &imageView) { imageViews_.push_back(imageView); imageView = VK_NULL_HANDLE; }
	void QueueDeleteDeviceMemory(VkDeviceMemory &deviceMemory) { deviceMemory_.push_back(deviceMemory); deviceMemory = VK_NULL_HANDLE; }
	void QueueDeleteSampler(VkSampler &sampler) { samplers_.push_back(sampler); sampler = VK_NULL_HANDLE; }
	void QueueDeletePipeline(VkPipeline &pipeline) { pipelines_.push_back(pipeline); pipeline = VK_NULL_HANDLE; }
	void QueueDeletePipelineCache(VkPipelineCache &pipelineCache) { pipelineCaches_.push_back(pipelineCache); pipelineCache = VK_NULL_HANDLE; }
	void QueueDeleteRenderPass(VkRenderPass &renderPass) { renderPasses_.push_back(renderPass); renderPass = VK_NULL_HANDLE; }
	void QueueDeleteFramebuffer(VkFramebuffer &framebuffer) { framebuffers_.push_back(framebuffer); framebuffer = VK_NULL_HANDLE; }
	void QueueDeletePipelineLayout(VkPipelineLayout &pipelineLayout) { pipelineLayouts_.push_back(pipelineLayout); pipelineLayout = VK_NULL_HANDLE; }
	void QueueDeleteDescriptorSetLayout(VkDescriptorSetLayout &descSetLayout) { descSetLayouts_.push_back(descSetLayout); descSetLayout = VK_NULL_HANDLE; }
	void QueueCallback(void(*func)(void *userdata), void *userdata) { callbacks_.push_back(Callback(func, userdata)); }

	void Take(VulkanDeleteList &del) {
		assert(cmdPools_.size() == 0);
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
		cmdPools_ = std::move(del.cmdPools_);
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
		pipelineLayouts_ = std::move(del.pipelineLayouts_);
		descSetLayouts_ = std::move(del.descSetLayouts_);
		callbacks_ = std::move(del.callbacks_);
	}

	void PerformDeletes(VkDevice device) {
		for (auto &cmdPool : cmdPools_) {
			vkDestroyCommandPool(device, cmdPool, nullptr);
		}
		cmdPools_.clear();
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
		for (auto &pipeLayout : pipelineLayouts_) {
			vkDestroyPipelineLayout(device, pipeLayout, nullptr);
		}
		pipelineLayouts_.clear();
		for (auto &descSetLayout : descSetLayouts_) {
			vkDestroyDescriptorSetLayout(device, descSetLayout, nullptr);
		}
		descSetLayouts_.clear();
		for (auto &callback : callbacks_) {
			callback.func(callback.userdata);
		}
		callbacks_.clear();
	}

private:
	std::vector<VkCommandPool> cmdPools_;
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
	std::vector<VkPipelineLayout> pipelineLayouts_;
	std::vector<VkDescriptorSetLayout> descSetLayouts_;
	std::vector<Callback> callbacks_;
};

// VulkanContext manages the device and swapchain, and deferred deletion of objects.
class VulkanContext {
public:
	VulkanContext();
	~VulkanContext();

	VkResult CreateInstance(const char *app_name, int app_ver, uint32_t flags);
	
	// TODO: Actually do some checks?
	int GetBestPhysicalDevice() const { return 0; }
	void ChooseDevice(int physical_device);
	bool EnableDeviceExtension(const char *extension);
	VkResult CreateDevice();

	const std::string &InitError() { return init_error_; }

	VkDevice GetDevice() { return device_; }
	VkInstance GetInstance() { return instance_; }

	VulkanDeleteList &Delete() { return globalDeleteList_; }

	VkPipelineCache CreatePipelineCache();

#ifdef _WIN32
	void InitSurfaceWin32(HINSTANCE conn, HWND wnd);
	void ReinitSurfaceWin32();
#elif __ANDROID__
	void InitSurfaceAndroid(ANativeWindow *native_window, int width, int height);
	void ReinitSurfaceAndroid(int width, int height);
#endif
	void InitQueue();
	bool InitObjects();
	bool InitSwapchain();

	// Also destroys the surface.
	void DestroyObjects();

	void DestroyDevice();

	void WaitUntilQueueIdle();

	// Utility functions for shorter code
	VkFence CreateFence(bool presignalled);
	bool CreateShaderModule(const std::vector<uint32_t> &spirv, VkShaderModule *shaderModule);

	int GetBackbufferWidth() { return width_; }
	int GetBackbufferHeight() { return height_; }

	void BeginFrame();
	void EndFrame();

	bool MemoryTypeFromProperties(uint32_t typeBits, VkFlags requirements_mask, uint32_t *typeIndex);

	VkResult InitDebugMsgCallback(PFN_vkDebugReportCallbackEXT dbgFunc, int bits, void *userdata = nullptr);
	void DestroyDebugMsgCallback();

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

	VkResult GetInstanceLayerExtensionList(const char *layerName, std::vector<VkExtensionProperties> &extensions);
	VkResult GetInstanceLayerProperties();

	VkResult GetDeviceLayerExtensionList(const char *layerName, std::vector<VkExtensionProperties> &extensions);
	VkResult GetDeviceLayerProperties();

	const std::vector<VkExtensionProperties> &GetDeviceExtensionsAvailable() const {
		return device_extension_properties_;
	}
	const std::vector<const char *> &GetDeviceExtensionsEnabled() const {
		return device_extensions_enabled_;
	}
	const VkPhysicalDeviceFeatures &GetFeaturesAvailable() const { return featuresAvailable_; }
	const VkPhysicalDeviceFeatures &GetFeaturesEnabled() const { return featuresEnabled_; }
	const VulkanPhysicalDeviceInfo &GetDeviceInfo() const { return deviceInfo_; }

	bool IsDeviceExtensionAvailable(const char *name) const {
		for (auto &iter : device_extension_properties_) {
			if (!strcmp(name, iter.extensionName))
				return true;
		}
		return false;
	}

	int GetInflightFrames() const {
		return inflightFrames_;
	}

	int GetCurFrame() const {
		return curFrame_;
	}

	VkSwapchainKHR GetSwapchain() const {
		return swapchain_;
	}
	VkFormat GetSwapchainFormat() const {
		return swapchainFormat_;
	}

	enum {
		MAX_INFLIGHT_FRAMES = 3,
	};

private:
	// A layer can expose extensions, keep track of those extensions here.
	struct LayerProperties {
		VkLayerProperties properties;
		std::vector<VkExtensionProperties> extensions;
	};

	bool CheckLayers(const std::vector<LayerProperties> &layer_props, const std::vector<const char *> &layer_names) const;

#ifdef _WIN32
	HINSTANCE connection = nullptr;        // hInstance - Windows Instance
	HWND window = nullptr;          // hWnd - window handle
#elif __ANDROID__  // _WIN32
	ANativeWindow *native_window = nullptr;
#endif // _WIN32

	VkInstance instance_ = VK_NULL_HANDLE;
	VkDevice device_ = VK_NULL_HANDLE;
	VkQueue gfx_queue_ = VK_NULL_HANDLE;
	VkSurfaceKHR surface_ = VK_NULL_HANDLE;

	std::string init_error_;
	std::vector<const char *> instance_layer_names_;
	std::vector<LayerProperties> instance_layer_properties_;
	
	std::vector<const char *> instance_extensions_enabled_;
	std::vector<VkExtensionProperties> instance_extension_properties_;

	std::vector<const char *> device_layer_names_;
	std::vector<LayerProperties> device_layer_properties_;
	
	std::vector<const char *> device_extensions_enabled_;
	std::vector<VkExtensionProperties> device_extension_properties_;
	
	std::vector<VkPhysicalDevice> physical_devices_;

	int physical_device_ = -1;

	uint32_t graphics_queue_family_index_ = -1;
	VkPhysicalDeviceProperties gpu_props{};
	std::vector<VkQueueFamilyProperties> queue_props;
	VkPhysicalDeviceMemoryProperties memory_properties{};

	// Custom collection of things that are good to know
	VulkanPhysicalDeviceInfo deviceInfo_;

	// Swap chain
	int width_ = 0;
	int height_ = 0;
	int flags_ = 0;

	int inflightFrames_ = MAX_INFLIGHT_FRAMES;

	struct FrameData {
		FrameData() {}
		VulkanDeleteList deleteList;
	};
	FrameData frame_[MAX_INFLIGHT_FRAMES];
	int curFrame_ = 0;

	// At the end of the frame, this is copied into the frame's delete list, so it can be processed
	// the next time the frame comes around again.
	VulkanDeleteList globalDeleteList_;

	std::vector<VkDebugReportCallbackEXT> msg_callbacks;

	VkSwapchainKHR swapchain_;
	VkFormat swapchainFormat_;

	uint32_t queue_count = 0;

	VkPhysicalDeviceFeatures featuresAvailable_;
	VkPhysicalDeviceFeatures featuresEnabled_;

	std::vector<VkCommandBuffer> cmdQueue_;
};

// Detailed control.
void TransitionImageLayout2(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspectMask,
	VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
	VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
	VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask);

void TransitionFromPresent(VkCommandBuffer cmd, VkImage image);
void TransitionToPresent(VkCommandBuffer cmd, VkImage image);

// GLSL compiler
void init_glslang();
void finalize_glslang();
bool GLSLtoSPV(const VkShaderStageFlagBits shader_type, const char *pshader, std::vector<uint32_t> &spirv, std::string *errorMessage = nullptr);

#endif // UTIL_INIT

