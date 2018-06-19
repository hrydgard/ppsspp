#pragma once

#include <cassert>
#include <string>
#include <vector>
#include <utility>

#include "base/logging.h"
#include "Common/Vulkan/VulkanLoader.h"

enum {
	VULKAN_FLAG_VALIDATE = 1,
	VULKAN_FLAG_PRESENT_MAILBOX = 2,
	VULKAN_FLAG_PRESENT_IMMEDIATE = 4,
	VULKAN_FLAG_PRESENT_FIFO_RELAXED = 8,
};

enum {
	VULKAN_VENDOR_NVIDIA = 0x000010de,
	VULKAN_VENDOR_INTEL = 0x00008086,   // Haha!
	VULKAN_VENDOR_AMD = 0x00001002,
	VULKAN_VENDOR_ARM = 0x000013B5,  // Mali
	VULKAN_VENDOR_QUALCOMM = 0x00005143,
	VULKAN_VENDOR_IMGTEC = 0x00001010,  // PowerVR
};

std::string VulkanVendorString(uint32_t vendorId);

// Not all will be usable on all platforms, of course...
enum WindowSystem {
#ifdef _WIN32
	WINDOWSYSTEM_WIN32,
#endif
#ifdef __ANDROID__
	WINDOWSYSTEM_ANDROID,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
	WINDOWSYSTEM_XLIB,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
	WINDOWSYSTEM_XCB,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	WINDOWSYSTEM_WAYLAND,
#endif
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
	// NOTE: These all take reference handles so they can zero the input value.
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

	void Take(VulkanDeleteList &del);
	void PerformDeletes(VkDevice device);

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

// For fast extension-enabled checks.
struct VulkanDeviceExtensions {
	bool DEDICATED_ALLOCATION;
};

// Useful for debugging on ARM Mali. This eliminates transaction elimination
// which can cause artifacts if you get barriers wrong (or if there are driver bugs).
// Cost is reduced performance on some GPU architectures.
// #define VULKAN_USE_GENERAL_LAYOUT_FOR_COLOR
// #define VULKAN_USE_GENERAL_LAYOUT_FOR_DEPTH_STENCIL

// VulkanContext manages the device and swapchain, and deferred deletion of objects.
class VulkanContext {
public:
	VulkanContext();
	~VulkanContext();

	struct CreateInfo {
		const char *app_name;
		int app_ver;
		uint32_t flags;
	};

	VkResult CreateInstance(const CreateInfo &info);
	void DestroyInstance();

	int GetBestPhysicalDevice();
	int GetPhysicalDeviceByName(std::string name);
	void ChooseDevice(int physical_device);
	bool EnableDeviceExtension(const char *extension);
	VkResult CreateDevice();

	const std::string &InitError() const { return init_error_; }

	VkDevice GetDevice() const { return device_; }
	VkInstance GetInstance() const { return instance_; }
	uint32_t GetFlags() const { return flags_; }

	VulkanDeleteList &Delete() { return globalDeleteList_; }

	// The parameters are whatever the chosen window system wants.
	void InitSurface(WindowSystem winsys, void *data1, void *data2, int width = -1, int height = -1);
	void ReinitSurface(int width = -1, int height = -1);

	bool InitQueue();
	bool InitObjects();
	bool InitSwapchain();

	// Also destroys the surface.
	void DestroyObjects();
	void DestroyDevice();

	void PerformPendingDeletes();
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
	int GetCurrentPhysicalDevice() const {
		return physical_device_;
	}
	int GetNumPhysicalDevices() const {
		return (int)physical_devices_.size();
	}

	VkQueue GetGraphicsQueue() const {
		return gfx_queue_;
	}

	int GetGraphicsQueueFamilyIndex() const {
		return graphics_queue_family_index_;
	}

	const VkPhysicalDeviceProperties &GetPhysicalDeviceProperties(int i) const {
		return physicalDeviceProperties_[i];
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
	const VkSurfaceCapabilitiesKHR &GetSurfaceCapabilities() const { return surfCapabilities_; }

	bool IsInstanceExtensionAvailable(const char *name) const {
		for (auto &iter : instance_extension_properties_) {
			if (!strcmp(name, iter.extensionName))
				return true;
		}
		return false;
	}

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

	// 1 for no frame overlap and thus minimal latency but worst performance.
	// 2 is an OK compromise, while 3 performs best but risks slightly higher latency.
	enum {
		MAX_INFLIGHT_FRAMES = 3,
	};

	const VulkanDeviceExtensions &DeviceExtensions() { return deviceExtensionsLookup_; }

private:
	// A layer can expose extensions, keep track of those extensions here.
	struct LayerProperties {
		VkLayerProperties properties;
		std::vector<VkExtensionProperties> extensions;
	};

	bool CheckLayers(const std::vector<LayerProperties> &layer_props, const std::vector<const char *> &layer_names) const;

	WindowSystem winsys_;
	// Don't use the real types here to avoid having to include platform-specific stuff
	// that we really don't want in everything that uses VulkanContext.
	void *winsysData1_;
	void *winsysData2_;

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
	VulkanDeviceExtensions deviceExtensionsLookup_{};

	std::vector<VkPhysicalDevice> physical_devices_;

	int physical_device_ = -1;

	uint32_t graphics_queue_family_index_ = -1;
	std::vector<VkPhysicalDeviceProperties> physicalDeviceProperties_{};
	std::vector<VkQueueFamilyProperties> queue_props;
	VkPhysicalDeviceMemoryProperties memory_properties{};

	// Custom collection of things that are good to know
	VulkanPhysicalDeviceInfo deviceInfo_{};

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

	VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
	VkFormat swapchainFormat_;

	uint32_t queue_count = 0;

	VkPhysicalDeviceFeatures featuresAvailable_{};
	VkPhysicalDeviceFeatures featuresEnabled_{};

	VkSurfaceCapabilitiesKHR surfCapabilities_{};

	std::vector<VkCommandBuffer> cmdQueue_;
};

// Detailed control.
void TransitionImageLayout2(VkCommandBuffer cmd, VkImage image, int baseMip, int mipLevels, VkImageAspectFlags aspectMask,
	VkImageLayout oldImageLayout, VkImageLayout newImageLayout,
	VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
	VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask);

// GLSL compiler
void init_glslang();
void finalize_glslang();
bool GLSLtoSPV(const VkShaderStageFlagBits shader_type, const char *pshader, std::vector<uint32_t> &spirv, std::string *errorMessage = nullptr);

const char *VulkanResultToString(VkResult res);
