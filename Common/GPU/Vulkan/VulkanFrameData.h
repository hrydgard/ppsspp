#pragma once

#include <cstdint>

#include <mutex>
#include <condition_variable>

#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/Data/Collections/Hashmaps.h"

enum {
	MAX_TIMESTAMP_QUERIES = 128,
};

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

struct ReadbackKey {
	const VKRFramebuffer *framebuf;
	int width;
	int height;
};

struct CachedReadback {
	VkBuffer buffer;
	VmaAllocation allocation;
	VkDeviceSize bufferSize;
	bool isCoherent;

	void Destroy(VulkanContext *vulkan);
};

struct FrameDataShared {
	// For synchronous readbacks.
	VkFence readbackFence = VK_NULL_HANDLE;
	bool useMultiThreading;
	bool measurePresentTime;

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
	VkSemaphore renderingCompleteSemaphore = VK_NULL_HANDLE;

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

	// Generally called from the main thread, unlike most of the rest.
	VkCommandBuffer GetInitCmd(VulkanContext *vulkan);

	// Submits pending command buffers.
	void Submit(VulkanContext *vulkan, FrameSubmitType type, FrameDataShared &shared);

private:
	// Metadata for logging etc
	int index = -1;
};
