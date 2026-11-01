#pragma once

#include <atomic>
#include <cstdint>

#include <mutex>
#include <condition_variable>

#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/Data/Collections/Hashmaps.h"

#if PPSSPP_PLATFORM(IOS)
// iOS doesn't support a large number of timestamp queries natively.
// We don't want MoltenVK to fall back to emulation.
enum {
	MAX_TIMESTAMP_QUERIES = 32,
};
#else
enum {
	MAX_TIMESTAMP_QUERIES = 128,
};
#endif

enum class VKRRunType {
	SUBMIT,
	PRESENT,
	SYNC,
	EXIT,
};

struct QueueProfileContext {
	bool enabled = false;
	bool timestampsEnabled = false;
	VkQueryPool queryPool;
	std::vector<std::string> timestampDescriptions;
	std::string profileSummary;
	double cpuStartTime;
	double cpuEndTime;
	double descWriteTime;
	int descriptorsWritten;
	int descriptorsDeduped;
#ifdef _DEBUG
	int commandCounts[11];
#endif
};

class VKRFramebuffer;

// Hashed and compared as raw bytes (DenseHashMap), so no padding.
struct ReadbackKey {
	const VKRFramebuffer *framebuf;
	int x;
	int y;
	int width;
	int height;
	VkImageAspectFlags aspectMask;
	uint32_t unused;
};
static_assert(sizeof(ReadbackKey) == sizeof(void *) + 6 * sizeof(uint32_t), "ReadbackKey must not have padding");

struct CachedReadback {
	VkBuffer buffer;
	VmaAllocation allocation;
	VkDeviceSize bufferSize;
	bool isCoherent;

	void Destroy(VulkanContext *vulkan);
};

// Swap chain management
struct SwapchainImageData {
	VkImage image;
	VkImageView view;
	VkSemaphore renderingCompleteSemaphore = VK_NULL_HANDLE;
};

static constexpr int MAX_EXTRA_SWAPCHAINS = 4;

// A swapchain other than the main one (see docs/multi-swapchain.md). The VulkanRenderManager owns it.
// It doesn't pace the frame: if it has no image ready, its pass is skipped that frame.
struct VKRSwapchain {
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkExtent2D extent{};
	int requestedWidth = 0;
	int requestedHeight = 0;
	std::vector<SwapchainImageData> images;
	std::vector<VkFramebuffer> framebuffers;
	VkImage depthImage = VK_NULL_HANDLE;
	VmaAllocation depthAlloc = VK_NULL_HANDLE;
	VkImageView depthView = VK_NULL_HANDLE;
	VkSemaphore acquireSemaphores[VulkanContext::MAX_INFLIGHT_FRAMES]{};

	// Render thread. The image acquired for the frame being run, if any.
	uint32_t curImage = 0;
	bool acquired = false;
	int suboptimalFrames = 0;
	// Set by the render thread, which can't recreate it. Checked at the start of a frame.
	std::atomic<bool> outOfDate{false};
};

struct FrameDataShared {
	// For synchronous readbacks.
	VkFence readbackFence = VK_NULL_HANDLE;
	bool useMultiThreading = false;
	bool measurePresentTime = false;

	std::vector<SwapchainImageData> swapchainImages_;
	uint32_t swapchainImageCount_ = 0;

	void Init(VulkanContext *vulkan, bool useMultiThreading, bool measurePresentTime);
	void Destroy(VulkanContext *vulkan);
};

enum class FrameSubmitType {
	Pending,
	Sync,
	FinishFrame,
};

// Per-frame data, round-robin so we can overlap submission with execution of the previous frame.
struct FrameData {
	bool skipSwap = false;

	std::mutex fenceMutex;
	std::condition_variable fenceCondVar;
	bool readyForFence = true;

	VkFence fence = VK_NULL_HANDLE;
	VkSemaphore acquireSemaphore = VK_NULL_HANDLE;

	// These are on different threads so need separate pools.
	VkCommandPool cmdPoolInit = VK_NULL_HANDLE;  // Written to from main thread
	VkCommandPool cmdPoolMain = VK_NULL_HANDLE;  // Written to from render thread, which also submits

	VkCommandBuffer initCmd = VK_NULL_HANDLE;
	VkCommandBuffer mainCmd = VK_NULL_HANDLE;
	VkCommandBuffer presentCmd = VK_NULL_HANDLE;

	bool hasInitCommands = false;
	bool hasMainCommands = false;
	bool hasPresentCommands = false;

	bool hasFencePending = false;
	bool hasAcquired = false;

	bool syncDone = false;

	// Swapchain.
	uint32_t curSwapchainImage = -1;
	// Additional swapchains with an image acquired this frame, to wait for and present.
	std::vector<VKRSwapchain *> acquiredSwapchains;

	// Frames need unique IDs to wait for present on, let's keep them here.
	// Also used for indexing into the frame timing history buffer.
	uint64_t frameId = 0;

	// Profiling.
	QueueProfileContext profile{};

	// Async readback cache.
	DenseHashMap<ReadbackKey, CachedReadback *> readbacks_;

	FrameData() : readbacks_(8) {}

	void Init(VulkanContext *vulkan, int index);
	void Destroy(VulkanContext *vulkan);

	void AcquireNextImage(VulkanContext *vulkan);
	VkResult QueuePresent(VulkanContext *vulkan, FrameDataShared &shared);
	// Additional swapchains. Acquiring doesn't wait, and returns false if there's no image ready.
	bool AcquireExtraSwapchain(VulkanContext *vulkan, VKRSwapchain *swapchain);
	void PresentExtraSwapchains(VulkanContext *vulkan);

	// Generally called from the main thread, unlike most of the rest.
	VkCommandBuffer GetInitCmd(VulkanContext *vulkan);

	// Submits pending command buffers.
	void Submit(VulkanContext *vulkan, FrameSubmitType type, FrameDataShared &shared);

private:
	// Metadata for logging etc
	int index = -1;
};
