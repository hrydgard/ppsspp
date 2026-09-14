#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <functional>

#include "Common/Common.h"
#include "Common/Log.h"
#include "Common/GPU/MiscTypes.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanDebug.h"
#include "Common/GPU/Vulkan/VulkanAlloc.h"
#include "Common/GPU/Vulkan/VulkanProfiler.h"
#include "Common/GPU/Vulkan/VulkanPresentation.h"

// Enable or disable a simple logging profiler for Vulkan.
// Mostly useful for profiling texture uploads currently, but could be useful for
// other things as well. We also have a nice integrated render pass profiler in the queue
// runner, but this one is more convenient for transient events.

#define VK_PROFILE_BEGIN(vulkan, cmd, stage, ...) vulkan->GetProfiler()->Begin(cmd, stage, __VA_ARGS__);
#define VK_PROFILE_END(vulkan, cmd, stage) vulkan->GetProfiler()->End(cmd, stage);

enum class VulkanInitFlags : uint32_t {
	VALIDATE = (1 << 0),
	DISABLE_IMPLICIT_LAYERS = (1 << 5),
};
ENUM_CLASS_BITOPS(VulkanInitFlags);

enum {
	VULKAN_VENDOR_NVIDIA = 0x000010de,
	VULKAN_VENDOR_INTEL = 0x00008086,   // Haha!
	VULKAN_VENDOR_AMD = 0x00001002,
	VULKAN_VENDOR_ARM = 0x000013B5,  // Mali
	VULKAN_VENDOR_QUALCOMM = 0x00005143,
	VULKAN_VENDOR_IMGTEC = 0x00001010,  // PowerVR
	VULKAN_VENDOR_APPLE = 0x0000106b,  // Apple through MoltenVK
	VULKAN_VENDOR_MESA = 0x00010005, // lavapipe
};

VK_DEFINE_HANDLE(VmaAllocator);
VK_DEFINE_HANDLE(VmaAllocation);

std::string VulkanVendorString(uint32_t vendorId);

template<class R, class T> inline void ChainStruct(R &root, T *newStruct) {
	newStruct->pNext = root.pNext;
	root.pNext = newStruct;
}

struct VulkanPhysicalDeviceInfo {
	VkFormat preferredDepthStencilFormat;
	bool canBlitToPreferredDepthStencilFormat;
};

class VulkanProfiler;
class VulkanContext;

// Extremely rough split of capabilities.
enum class PerfClass {
	SLOW,
	FAST,
};

typedef std::function<void(VulkanContext *)> DeleteCallback;

// This is a bit repetitive...
//
// Thread safety: The queueing functions are locked, because the global delete list gets written from
// more than one thread. Most callers are on the main thread, but not all:
//   * VulkanDescSetPool::Recreate, when a descriptor pool has to grow, runs from FlushDescSets on
//     the render thread. This one really happens - some games go past the initial 1024 descriptors.
//   * VulkanQueueRunner::ResizeReadbackBuffer runs from PerformReadback on the render thread. For
//     blocking readbacks the main thread is parked in FlushSync so it can't collide, and the delayed
//     ones never actually resize (the readback key contains the dimensions), but it's not worth
//     relying on that staying true.
// Meanwhile the main thread moves the global list into the current frame's list in EndFrame().
//
// PerformDeletes needs no lock of its own: it drains into a private local list (under Take's lock) and
// destroys from that, so the destruction never touches a list another thread can reach.
class VulkanDeleteList {
	struct BufferWithAlloc {
		VkBuffer buffer;
		VmaAllocation alloc;
	};
	struct ImageWithAlloc {
		VkImage image;
		VmaAllocation alloc;
	};

	struct Callback {
		explicit Callback(void(*f)(VulkanContext *vulkan, void *userdata), void *u)
			: func(f), userdata(u) {
		}

		void (*func)(VulkanContext *vulkan, void *userdata);
		void *userdata;
	};

public:
	// NOTE: These all take reference handles so they can zero the input value.
	void QueueDeleteCommandPool(VkCommandPool &pool) { _dbg_assert_(pool != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); cmdPools_.push_back(pool); pool = VK_NULL_HANDLE; }
	void QueueDeleteDescriptorPool(VkDescriptorPool &pool) { _dbg_assert_(pool != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); descPools_.push_back(pool); pool = VK_NULL_HANDLE; }
	void QueueDeleteShaderModule(VkShaderModule &module) { _dbg_assert_(module != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); modules_.push_back(module); module = VK_NULL_HANDLE; }
	void QueueDeleteBuffer(VkBuffer &buffer) { _dbg_assert_(buffer != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); buffers_.push_back(buffer); buffer = VK_NULL_HANDLE; }
	void QueueDeleteBufferView(VkBufferView &bufferView) { _dbg_assert_(bufferView != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); bufferViews_.push_back(bufferView); bufferView = VK_NULL_HANDLE; }
	void QueueDeleteImageView(VkImageView &imageView) { _dbg_assert_(imageView != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); imageViews_.push_back(imageView); imageView = VK_NULL_HANDLE; }
	void QueueDeleteDeviceMemory(VkDeviceMemory &deviceMemory) { _dbg_assert_(deviceMemory != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); deviceMemory_.push_back(deviceMemory); deviceMemory = VK_NULL_HANDLE; }
	void QueueDeleteSampler(VkSampler &sampler) { _dbg_assert_(sampler != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); samplers_.push_back(sampler); sampler = VK_NULL_HANDLE; }
	void QueueDeletePipeline(VkPipeline &pipeline) { _dbg_assert_(pipeline != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); pipelines_.push_back(pipeline); pipeline = VK_NULL_HANDLE; }
	void QueueDeletePipelineCache(VkPipelineCache &pipelineCache) { _dbg_assert_(pipelineCache != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); pipelineCaches_.push_back(pipelineCache); pipelineCache = VK_NULL_HANDLE; }
	void QueueDeleteRenderPass(VkRenderPass &renderPass) { _dbg_assert_(renderPass != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); renderPasses_.push_back(renderPass); renderPass = VK_NULL_HANDLE; }
	void QueueDeleteFramebuffer(VkFramebuffer &framebuffer) { _dbg_assert_(framebuffer != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); framebuffers_.push_back(framebuffer); framebuffer = VK_NULL_HANDLE; }
	void QueueDeletePipelineLayout(VkPipelineLayout &pipelineLayout) { _dbg_assert_(pipelineLayout != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); pipelineLayouts_.push_back(pipelineLayout); pipelineLayout = VK_NULL_HANDLE; }
	void QueueDeleteDescriptorSetLayout(VkDescriptorSetLayout &descSetLayout) { _dbg_assert_(descSetLayout != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); descSetLayouts_.push_back(descSetLayout); descSetLayout = VK_NULL_HANDLE; }
	void QueueDeleteQueryPool(VkQueryPool &queryPool) { _dbg_assert_(queryPool != VK_NULL_HANDLE); std::lock_guard<std::mutex> lock(mutex_); queryPools_.push_back(queryPool); queryPool = VK_NULL_HANDLE; }
	void QueueCallback(DeleteCallback func) { std::lock_guard<std::mutex> lock(mutex_); callbacks_.push_back(func); }

	void QueueDeleteBufferAllocation(VkBuffer &buffer, VmaAllocation &alloc) {
		_dbg_assert_(buffer != VK_NULL_HANDLE);
		std::lock_guard<std::mutex> lock(mutex_);
		buffersWithAllocs_.push_back(BufferWithAlloc{ buffer, alloc });
		buffer = VK_NULL_HANDLE;
		alloc = VK_NULL_HANDLE;
	}
	void QueueDeleteImageAllocation(VkImage &image, VmaAllocation &alloc) {
		_dbg_assert_(image != VK_NULL_HANDLE && alloc != VK_NULL_HANDLE);
		std::lock_guard<std::mutex> lock(mutex_);
		imagesWithAllocs_.push_back(ImageWithAlloc{ image, alloc });
		image = VK_NULL_HANDLE;
		alloc = VK_NULL_HANDLE;
	}

	// Moves everything from del into this list. Only the source list is locked - the destination is
	// either a frame's own list or a stack local, neither of which another thread can reach.
	void Take(VulkanDeleteList &del);
	void PerformDeletes(VulkanContext *vulkan, VmaAllocator allocator);

	int GetLastDeleteCount() const {
		return deleteCount_;
	}

private:
	// Does the actual destruction, on a list that's been drained out of the shared one. Returns the count.
	int PerformDeletesInternal(VulkanContext *vulkan, VmaAllocator allocator);

	std::mutex mutex_;
	std::vector<VkCommandPool> cmdPools_;
	std::vector<VkDescriptorPool> descPools_;
	std::vector<VkShaderModule> modules_;
	std::vector<VkBuffer> buffers_;
	std::vector<BufferWithAlloc> buffersWithAllocs_;
	std::vector<VkBufferView> bufferViews_;
	std::vector<ImageWithAlloc> imagesWithAllocs_;
	std::vector<VkImageView> imageViews_;
	std::vector<VkDeviceMemory> deviceMemory_;
	std::vector<VkSampler> samplers_;
	std::vector<VkPipeline> pipelines_;
	std::vector<VkPipelineCache> pipelineCaches_;
	std::vector<VkRenderPass> renderPasses_;
	std::vector<VkFramebuffer> framebuffers_;
	std::vector<VkPipelineLayout> pipelineLayouts_;
	std::vector<VkDescriptorSetLayout> descSetLayouts_;
	std::vector<VkQueryPool> queryPools_;
	std::vector<DeleteCallback> callbacks_;
	int deleteCount_ = 0;
};

// VulkanContext manages the device and swapchain, and deferred deletion of objects.
class VulkanContext {
public:
	VulkanContext();
	~VulkanContext();

	struct CreateInfo {
		const char *app_name;
		int app_ver;
		VulkanInitFlags flags;
		std::string customDriver;
	};

	VkResult CreateInstance(const CreateInfo &info);
	// For adopting an already-created VkInstance (e.g. handed to us by a host application/frontend
	// like libretro/RetroArch) instead of creating our own. Runs the same post-creation bookkeeping
	// (function pointer loading, API version/physical device enumeration, etc.) as CreateInstance(), but
	// does not call vkCreateInstance, and DestroyInstance() will not call vkDestroyInstance either.
	VkResult CreateInstanceExternal(VkInstance instance);
	void DestroyInstance();

	int GetBestPhysicalDevice() const;
	int GetPhysicalDeviceByName(std::string_view name) const;

	// Convenience method to avoid code duplication.
	// If it returns false, delete the context.
	bool CreateInstanceAndDevice(const CreateInfo &info, std::string *deviceName);

	// The coreVersion is to avoid enabling extensions that are merged into core Vulkan from a certain version.
	bool EnableInstanceExtension(const char *extension, uint32_t coreVersion);
	bool EnableDeviceExtension(const char *extension, uint32_t coreVersion);

	// Was previously two functions, ChooseDevice and CreateDevice.
	// extraDeviceExtensions/extraRequiredFeatures let a host application (e.g. libretro) merge in extra
	// requirements it needs on top of what PPSSPP would normally request, without needing to intercept
	// the underlying vkCreateDevice call.
	VkResult CreateDevice(int physical_device,
		const std::vector<const char *> &extraDeviceExtensions = {},
		const VkPhysicalDeviceFeatures *extraRequiredFeatures = nullptr);

	// Some host applications (e.g. a libretro frontend, per its create_device contract) take over
	// responsibility for eventually destroying the VkDevice once we've handed it back to them, even
	// though we're the one that called vkCreateDevice. Call this after CreateDevice() succeeds in that
	// case - DestroyDevice() will still run all our own device-resource cleanup, just skip the final
	// vkDestroyDevice call.
	void SetDeviceExternallyOwned() { ownsDevice_ = false; }

	const std::string &InitError() const { return init_error_; }

	VkDevice GetDevice() const { return device_; }
	VkInstance GetInstance() const { return instance_; }
	VulkanInitFlags GetInitFlags() const { return createInfo_.flags; }

	// Of course, this won't update things that can only change on first init.
	void UpdateCreateInfo(const VulkanContext::CreateInfo &info) { createInfo_ = info; }

	VulkanDeleteList &Delete() { return globalDeleteList_; }

	// The parameters are whatever the chosen window system wants.
	// The extents will be automatically determined.
	VkResult InitSurface(WindowSystem winsys, void *data1, void *data2);
	VkResult ReinitSurface();

	// If the present mode is not available, will fall back to the first available (which is almost always FIFO).
	bool InitSwapchain(VkPresentModeKHR desiredPresentMode);
	void SetCbGetDrawSize(std::function<VkExtent2D()>);

	void DestroySwapchain();
	void DestroySurface();

	void DestroyDevice();

	void PerformPendingDeletes();
	void WaitUntilQueueIdle();

	// Utility functions for shorter code
	VkFence CreateFence(bool presignalled);
	bool CreateShaderModule(const std::vector<uint32_t> &spirv, VkShaderModule *shaderModule, const char *tag);

	void BeginFrame(VkCommandBuffer firstCommandBuffer);
	void EndFrame();

	VulkanProfiler *GetProfiler() {
		return &frame_[curFrame_].profiler;
	}

	// Simple workaround for the casting warning.
	template <class T>
	void SetDebugName(T handle, VkObjectType type, const char *name) {
		if (extensionsLookup_.EXT_debug_utils && handle != VK_NULL_HANDLE) {
			_dbg_assert_(handle != VK_NULL_HANDLE);
			SetDebugNameImpl((uint64_t)handle, type, name);
		}
	}
	bool DebugLayerEnabled() const {
		return extensionsLookup_.EXT_debug_utils;
	}

	bool MemoryTypeFromProperties(uint32_t typeBits, VkFlags requirements_mask, uint32_t *typeIndex);

	VkPhysicalDevice GetPhysicalDevice(int n) const {
		return physical_devices_[n];
	}
	VkPhysicalDevice GetCurrentPhysicalDevice() const {
		return physical_devices_[physical_device_];
	}
	int GetCurrentPhysicalDeviceIndex() const {
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

	// Normally, picking the graphics queue (ChooseQueue(), private below) is entangled with surface
	// creation (ReinitSurface()) since it also needs to check which queue family can present to that
	// particular surface. Hosts with no real WSI surface at all (e.g. libretro) still need a graphics
	// queue, just without that presentation-support check - this does exactly that, and nothing else.
	// Only valid to call after CreateDevice() has succeeded.
	bool ChooseGraphicsQueueWithoutSurface();

	struct PhysicalDeviceProps {
		VkPhysicalDeviceProperties properties;
		VkPhysicalDevicePushDescriptorPropertiesKHR pushDescriptorProperties;
		VkPhysicalDeviceExternalMemoryHostPropertiesEXT externalMemoryHostProperties;
		VkPhysicalDeviceDepthStencilResolveProperties depthStencilResolve;
	};

	struct AllPhysicalDeviceFeatures {
		VkPhysicalDeviceFeatures standard;
		VkPhysicalDeviceMultiviewFeatures multiview;
		VkPhysicalDevicePresentWaitFeaturesKHR presentWait;
		VkPhysicalDevicePresentIdFeaturesKHR presentId;
		VkPhysicalDeviceProvokingVertexFeaturesEXT provokingVertex;
		VkPhysicalDevicePresentModeFifoLatestReadyFeaturesKHR presentModeFifoProps;
		VkPhysicalDeviceScalarBlockLayoutFeatures scalarBlockLayout;
	};

	const PhysicalDeviceProps &GetPhysicalDeviceProperties(int i = -1) const {
		if (i < 0)
			i = GetCurrentPhysicalDeviceIndex();
		return physicalDeviceProperties_[i];
	}

	const VkQueueFamilyProperties &GetQueueFamilyProperties(int family) const {
		return queueFamilyProperties_[family];
	}

	VkResult GetInstanceLayerExtensionList(const char *layerName, std::vector<VkExtensionProperties> *extensions);
	VkResult GetInstanceLayerProperties();

	VkResult GetDeviceExtensionList(std::vector<VkExtensionProperties> *extensions);

	const std::vector<VkExtensionProperties> &GetDeviceExtensionsAvailable() const {
		return device_extension_properties_;
	}
	const std::vector<const char *> &GetDeviceExtensionsEnabled() const {
		return device_extensions_enabled_;
	}

	const std::vector<VkExtensionProperties> &GetInstanceExtensionsAvailable() const {
		return instance_extension_properties_;
	}
	const std::vector<const char *> &GetInstanceExtensionsEnabled() const {
		return instance_extensions_enabled_;
	}

	const VkPhysicalDeviceMemoryProperties &GetMemoryProperties() const {
		return memory_properties_;
	}

	struct PhysicalDeviceFeatures {
		AllPhysicalDeviceFeatures available{};
		AllPhysicalDeviceFeatures enabled{};
	};

	const PhysicalDeviceFeatures &GetDeviceFeatures() const { return deviceFeatures_; }
	const VulkanPhysicalDeviceInfo &GetDeviceInfo() const { return deviceInfo_; }
	const VkSurfaceCapabilitiesKHR &GetSurfaceCapabilities() const { return surfCapabilities_; }

	bool IsInstanceExtensionAvailable(const char *extensionName) const {
		for (const auto &iter : instance_extension_properties_) {
			if (!strcmp(extensionName, iter.extensionName))
				return true;
		}

		// Also search through the layers, one of them might carry the extension (especially DEBUG_utils)
		for (const auto &iter : instance_layer_properties_) {
			for (const auto &ext : iter.extensions) {
				if (!strcmp(extensionName, ext.extensionName)) {
					return true;
				}
			}
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
		// out of MAX_INFLIGHT_FRAMES.
		return inflightFrames_;
	}

	// Don't call while a frame is in progress.
	void UpdateInflightFrames(int n);

	int GetCurFrame() const {
		return curFrame_;
	}

	VkSwapchainKHR GetSwapchain() const { return swapchain_; }
	VkFormat GetSwapchainFormat() const { return presentation_ ? presentation_->GetFormat() : swapchainFormat_; }
	bool IsSwapchainInited() const { return swapchainInited_; }

	// Opt-in replacement for the real-swapchain path above (see VulkanPresentation.h for why a host
	// application might want this). Null (the default) means "use the real swapchain".
	void SetPresentation(std::unique_ptr<VulkanPresentation> presentation) { presentation_ = std::move(presentation); }
	VulkanPresentation *GetPresentation() const { return presentation_.get(); }

	VkImageLayout GetPresentLayout() const {
		return presentation_ ? presentation_->GetPresentLayout() : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	}

	// Wrap every real vkQueueSubmit/vkQueueWaitIdle call with these - no-ops unless a presentation backend
	// that shares its VkQueue with another owner (e.g. libretro) is active.
	void LockQueue() { if (presentation_) presentation_->LockQueue(); }
	void UnlockQueue() { if (presentation_) presentation_->UnlockQueue(); }
	void PrepareSubmit(VkSubmitInfo &submitInfo) { if (presentation_) presentation_->PrepareSubmit(submitInfo); }
	bool HasRealSwapchain() const { return swapChainExtent_.width > 0; }

	int GetBackbufferWidth() { return (int)(presentation_ ? presentation_->GetExtent().width : swapChainExtent_.width); }
	int GetBackbufferHeight() { return (int)(presentation_ ? presentation_->GetExtent().height : swapChainExtent_.height); }

	void SetProfilerEnabledPtr(bool *enabled) {
		for (auto &frame : frame_) {
			frame.profiler.SetEnabledPtr(enabled);
		}
	}

	// 1 for no frame overlap and thus minimal latency but worst performance.
	// 2 is an OK compromise, while 3 performs best but risks slightly higher latency.
	enum {
		MAX_INFLIGHT_FRAMES = 3,
	};

	const VulkanExtensions &Extensions() { return extensionsLookup_; }

	PerfClass DevicePerfClass() const {
		return devicePerfClass_;
	}

	void GetImageMemoryRequirements(VkImage image, VkMemoryRequirements *mem_reqs, bool *dedicatedAllocation);

	VmaAllocator Allocator() const {
		return allocator_;
	}

	const std::vector<VkSurfaceFormatKHR> &SurfaceFormats() {
		return surfFormats_;
	}

	VkPresentModeKHR GetPresentMode() const {
		return presentMode_;
	}

#ifdef VK_EXT_full_screen_exclusive
	void SetFullScreenExclusiveMode(VkFullScreenExclusiveEXT mode) { fullScreenExclusiveMode_ = mode; }
#endif

	std::vector<VkPresentModeKHR> GetAvailablePresentModes() const {
		return availablePresentModes_;
	}

	bool PresentModeSupported(VkPresentModeKHR mode) const {
		for (const auto &m : availablePresentModes_) {
			if (m == mode) {
				return true;
			}
		}
		return false;
	}

	int GetLastDeleteCount() const {
		return frame_[curFrame_].deleteList.GetLastDeleteCount();
	}

	u32 InstanceApiVersion() const {
		return vulkanInstanceApiVersion_;
	}

	u32 DeviceApiVersion() const {
		return vulkanDeviceApiVersion_;
	}

	WindowSystem GetWindowSystem() const {
		return winsys_;
	}

	bool SupportsPreRotation() const {
		return surfCapabilities_.supportedTransforms != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	}

private:
	bool ChooseQueue();

	// Shared tail of CreateInstance()/CreateInstanceExternal(): function pointer loading, physical device
	// enumeration, debug callback setup.
	VkResult FinishInstanceInit();
	void DetectInstanceApiVersion();

	void SetDebugNameImpl(uint64_t handle, VkObjectType type, const char *name);

	VkResult InitDebugUtilsCallback();

	// A layer can expose extensions, keep track of those extensions here.
	struct LayerProperties {
		VkLayerProperties properties;
		std::vector<VkExtensionProperties> extensions;
	};

	bool CheckLayers(const std::vector<LayerProperties> &layer_props, const std::vector<const char *> &layer_names) const;

	WindowSystem winsys_ = WINDOWSYSTEM_UNINITIALIZED;

	// Don't use the real types here to avoid having to include platform-specific stuff
	// that we really don't want in everything that uses VulkanContext.
	void *winsysData1_ = nullptr;
	void *winsysData2_ = nullptr;
	std::function<VkExtent2D()> cbGetDrawSize_;

	VkInstance instance_ = VK_NULL_HANDLE;
	VkDevice device_ = VK_NULL_HANDLE;
	VkQueue gfx_queue_ = VK_NULL_HANDLE;
	VkSurfaceKHR surface_ = VK_NULL_HANDLE;
	u32 vulkanInstanceApiVersion_ = 0;
	u32 vulkanDeviceApiVersion_ = 0;

	// False when instance_/device_ were adopted from an external owner (e.g. a libretro frontend) rather
	// than created by us - in that case DestroyInstance()/DestroyDevice() must not actually destroy them.
	bool ownsInstance_ = true;
	bool ownsDevice_ = true;

	std::string init_error_;
	std::vector<const char *> instance_layer_names_;
	std::vector<LayerProperties> instance_layer_properties_;

	std::vector<const char *> instance_extensions_enabled_;
	std::vector<VkExtensionProperties> instance_extension_properties_;

	std::vector<const char *> device_extensions_enabled_;
	std::vector<VkExtensionProperties> device_extension_properties_;
	VulkanExtensions extensionsLookup_{};

	std::vector<VkPhysicalDevice> physical_devices_;

	int physical_device_ = -1;

	uint32_t graphics_queue_family_index_ = -1;
	std::vector<PhysicalDeviceProps> physicalDeviceProperties_;
	std::vector<VkQueueFamilyProperties> queueFamilyProperties_;

	VkPhysicalDeviceMemoryProperties memory_properties_{};

	// Custom collection of things that are good to know
	VulkanPhysicalDeviceInfo deviceInfo_{};

	// Swap chain extent
	VkExtent2D swapChainExtent_{};

	VulkanContext::CreateInfo createInfo_{};

	PerfClass devicePerfClass_ = PerfClass::SLOW;

	int inflightFrames_ = MAX_INFLIGHT_FRAMES;

	struct FrameData {
		FrameData() {}
		VulkanDeleteList deleteList;
		VulkanProfiler profiler;
	};
	FrameData frame_[MAX_INFLIGHT_FRAMES];
	int curFrame_ = 0;

	// At the end of the frame, this is copied into the frame's delete list, so it can be processed
	// the next time the frame comes around again.
	VulkanDeleteList globalDeleteList_;

	std::vector<VkDebugUtilsMessengerEXT> utils_callbacks;

	VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
	VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;

	// When set, replaces the real-swapchain path above. See VulkanPresentation.h.
	std::unique_ptr<VulkanPresentation> presentation_;

	uint32_t queue_count = 0;
	bool swapchainInited_ = false;

	PhysicalDeviceFeatures deviceFeatures_;

#ifdef VK_EXT_full_screen_exclusive
	VkFullScreenExclusiveEXT fullScreenExclusiveMode_ = VK_FULL_SCREEN_EXCLUSIVE_DEFAULT_EXT;
#endif


	VkSurfaceCapabilitiesKHR surfCapabilities_{};
	std::vector<VkSurfaceFormatKHR> surfFormats_{};

	VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
	std::vector<VkPresentModeKHR> availablePresentModes_;

	std::vector<VkCommandBuffer> cmdQueue_;

	VmaAllocator allocator_ = VK_NULL_HANDLE;
};

// GLSL compiler
void init_glslang();
void finalize_glslang();

enum class GLSLVariant {
	VULKAN,
	GL140,
	GLES300,
};

bool GLSLtoSPV(const VkShaderStageFlagBits shader_type, const char *sourceCode, GLSLVariant variant, std::vector<uint32_t> &spirv, std::string *errorMessage);

const char *VulkanColorSpaceToString(VkColorSpaceKHR colorSpace);
const char *VulkanFormatToString(VkFormat format);
const char *VulkanPresentModeToString(VkPresentModeKHR presentMode);
const char *VulkanImageLayoutToString(VkImageLayout imageLayout);

std::string FormatDriverVersion(const VkPhysicalDeviceProperties &props);
std::string FormatAPIVersion(u32 version);

// Simple heuristic.
bool IsHashMaliDriverVersion(const VkPhysicalDeviceProperties &props);

extern VulkanLogOptions g_LogOptions;
