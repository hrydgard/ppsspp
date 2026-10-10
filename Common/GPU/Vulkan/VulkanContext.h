#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <functional>
#include <unordered_map>

#include "Common/Common.h"
#include "Common/Log.h"
#include "Common/File/Path.h"
#include "Common/GPU/MiscTypes.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanDebug.h"
#include "Common/GPU/Vulkan/VulkanAlloc.h"
#include "Common/GPU/Vulkan/VulkanProfiler.h"
#include "Common/GPU/Vulkan/VulkanPresentation.h"
#include "Common/Thread/MpscQueue.h"
#include "Common/Thread/ParkingLot.h"

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
// Thread safety: the queueing functions can be called from any thread - the render thread queues
// deletes too (VulkanDescSetPool::Recreate from FlushDescSets). They push onto
// a lock-free MPSC queue. Take() and PerformDeletes() belong to one thread, the main thread, which
// moves the queued items into the per-kind vectors.
class VulkanDeleteList {
	struct BufferWithAlloc {
		VkBuffer buffer;
		VmaAllocation alloc;
	};
	struct ImageWithAlloc {
		VkImage image;
		VmaAllocation alloc;
	};

	enum class Kind : uint8_t {
		CMD_POOL, DESC_POOL, SHADER_MODULE, BUFFER, BUFFER_VIEW, IMAGE_VIEW, DEVICE_MEMORY, SAMPLER,
		PIPELINE, PIPELINE_CACHE, RENDER_PASS, FRAMEBUFFER, PIPELINE_LAYOUT, DESC_SET_LAYOUT, QUERY_POOL,
		BUFFER_ALLOC, IMAGE_ALLOC, CALLBACK_FN,
	};
	struct Item {
		Kind kind;
		uint64_t handle;
		VmaAllocation alloc;
		DeleteCallback callback;
	};

	// Handles are pointers or 64-bit integers depending on the platform.
	template <class H>
	static uint64_t Bits(H h) {
		uint64_t v = 0;
		memcpy(&v, &h, sizeof(h));
		return v;
	}
	template <class H>
	static H FromBits(uint64_t v) {
		H h;
		memcpy(&h, &v, sizeof(h));
		return h;
	}
	template <class H>
	void Push(Kind kind, H &handle) {
		_dbg_assert_(handle != VK_NULL_HANDLE);
		pending_.Push(Item{ kind, Bits(handle), VK_NULL_HANDLE, nullptr });
		handle = VK_NULL_HANDLE;
	}

public:
	// NOTE: These all take reference handles so they can zero the input value.
	void QueueDeleteCommandPool(VkCommandPool &pool) { Push(Kind::CMD_POOL, pool); }
	void QueueDeleteDescriptorPool(VkDescriptorPool &pool) { Push(Kind::DESC_POOL, pool); }
	void QueueDeleteShaderModule(VkShaderModule &module) { Push(Kind::SHADER_MODULE, module); }
	void QueueDeleteBuffer(VkBuffer &buffer) { Push(Kind::BUFFER, buffer); }
	void QueueDeleteBufferView(VkBufferView &bufferView) { Push(Kind::BUFFER_VIEW, bufferView); }
	void QueueDeleteImageView(VkImageView &imageView) { Push(Kind::IMAGE_VIEW, imageView); }
	void QueueDeleteDeviceMemory(VkDeviceMemory &deviceMemory) { Push(Kind::DEVICE_MEMORY, deviceMemory); }
	void QueueDeleteSampler(VkSampler &sampler) { Push(Kind::SAMPLER, sampler); }
	void QueueDeletePipeline(VkPipeline &pipeline) { Push(Kind::PIPELINE, pipeline); }
	void QueueDeletePipelineCache(VkPipelineCache &pipelineCache) { Push(Kind::PIPELINE_CACHE, pipelineCache); }
	void QueueDeleteRenderPass(VkRenderPass &renderPass) { Push(Kind::RENDER_PASS, renderPass); }
	void QueueDeleteFramebuffer(VkFramebuffer &framebuffer) { Push(Kind::FRAMEBUFFER, framebuffer); }
	void QueueDeletePipelineLayout(VkPipelineLayout &pipelineLayout) { Push(Kind::PIPELINE_LAYOUT, pipelineLayout); }
	void QueueDeleteDescriptorSetLayout(VkDescriptorSetLayout &descSetLayout) { Push(Kind::DESC_SET_LAYOUT, descSetLayout); }
	void QueueDeleteQueryPool(VkQueryPool &queryPool) { Push(Kind::QUERY_POOL, queryPool); }
	void QueueCallback(DeleteCallback func) { pending_.Push(Item{ Kind::CALLBACK_FN, 0, VK_NULL_HANDLE, std::move(func) }); }

	void QueueDeleteBufferAllocation(VkBuffer &buffer, VmaAllocation &alloc) {
		_dbg_assert_(buffer != VK_NULL_HANDLE);
		pending_.Push(Item{ Kind::BUFFER_ALLOC, Bits(buffer), alloc, nullptr });
		buffer = VK_NULL_HANDLE;
		alloc = VK_NULL_HANDLE;
	}
	void QueueDeleteImageAllocation(VkImage &image, VmaAllocation &alloc) {
		_dbg_assert_(image != VK_NULL_HANDLE && alloc != VK_NULL_HANDLE);
		pending_.Push(Item{ Kind::IMAGE_ALLOC, Bits(image), alloc, nullptr });
		image = VK_NULL_HANDLE;
		alloc = VK_NULL_HANDLE;
	}

	// Main thread. Moves everything from del into this list.
	void Take(VulkanDeleteList &del);
	void PerformDeletes(VulkanContext *vulkan, VmaAllocator allocator);

	int GetLastDeleteCount() const {
		return deleteCount_;
	}

private:
	// Does the actual destruction, on a list that's been drained out of the shared one. Returns the count.
	int PerformDeletesInternal(VulkanContext *vulkan, VmaAllocator allocator);
	// Main thread. Sorts the queued items into the vectors below.
	void Collect();

	MpscQueue<Item> pending_;
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
		VkPhysicalDeviceProvokingVertexFeaturesEXT provokingVertex;
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

// Compiled SPIR-V, keyed on the GLSL source, stage and variant, so that a shader compiled in an earlier
// run doesn't have to go through glslang again.
//
// Lookup and Insert are lock-free and can run on any thread. Lookups read a published table; inserts
// queue up and join the table at the next Write or SaveIfDirty. The other calls are for one thread
// at a time (the GPU thread that owns the cache).
class SPIRVCache {
public:
	SPIRVCache();
	~SPIRVCache();

	bool Lookup(VkShaderStageFlagBits stage, GLSLVariant variant, const char *source, std::vector<uint32_t> *spirv);
	void Insert(VkShaderStageFlagBits stage, GLSLVariant variant, const char *source, const std::vector<uint32_t> &spirv);
	void Clear();

	// For a cache stored inside another file. Read replaces the contents; Write can skip entries that
	// haven't been looked up or inserted since, so that ones nothing uses anymore age out.
	bool Read(FILE *f);
	bool Write(FILE *f, bool onlyUsed);

	// For a cache with a file of its own, loaded here. If it has grown to maxEntries, it's flushed
	// and starts over.
	void SetPath(const Path &path, int maxEntries);
	void SaveIfDirty();

private:
	// The source length along with the hash makes a collision, which would hand a shader the wrong
	// SPIR-V, far less likely than a 32-bit hash alone.
	struct Key {
		uint32_t hash;
		uint32_t length;
		bool operator==(const Key &other) const { return hash == other.hash && length == other.length; }
	};
	struct KeyHash {
		size_t operator()(const Key &key) const { return key.hash; }
	};
	struct Entry {
		Entry() { retro_atomic_int_init(&used, 0); }
		std::vector<uint32_t> spirv;
		retro_atomic_int_t used;
	};
	typedef std::unordered_map<Key, Entry *, KeyHash> Table;
	struct PendingEntry {
		Key key;
		Entry *entry;
	};

	static Key MakeKey(VkShaderStageFlagBits stage, GLSLVariant variant, const char *source);
	static void FreeTable(Table *table);
	const Table *Current() const;
	// Publishes table, waits out readers of the old one, then frees the entries in retired.
	void Publish(Table *table, std::vector<Entry *> &retired);
	// Moves pending inserts into the table.
	void Merge();
	bool ReadTable(FILE *f, Table *table);

	retro_atomic_ptr_t table_;
	ReaderGate gate_;
	MpscQueue<PendingEntry> pending_;
	retro_atomic_int_t dirty_;
	Path path_;
};

// For thin3d's and other fixed shaders. Game shaders use a cache of their own, stored with the rest of
// the game's shader cache.
extern SPIRVCache g_spirvCache;

// With a cache, a shader found there skips glslang, and a newly compiled one is added to it.
bool GLSLtoSPV(const VkShaderStageFlagBits shader_type, const char *sourceCode, GLSLVariant variant, std::vector<uint32_t> &spirv, std::string *errorMessage, SPIRVCache *cache = nullptr);

const char *VulkanColorSpaceToString(VkColorSpaceKHR colorSpace);
const char *VulkanFormatToString(VkFormat format);
const char *VulkanPresentModeToString(VkPresentModeKHR presentMode);
const char *VulkanImageLayoutToString(VkImageLayout imageLayout);

std::string FormatDriverVersion(const VkPhysicalDeviceProperties &props);
std::string FormatAPIVersion(u32 version);

// Simple heuristic.
bool IsHashMaliDriverVersion(const VkPhysicalDeviceProperties &props);

extern VulkanLogOptions g_LogOptions;
