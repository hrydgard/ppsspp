#pragma once

#include <cstdint>

#include <mutex>
#include <condition_variable>

#include "Common/GPU/Vulkan/VulkanContext.h"

enum {
	MAX_TIMESTAMP_QUERIES = 128,
};

enum class VKRRunType {
	PRESENT,
	SYNC,
	EXIT,
};

struct QueueProfileContext {
	VkQueryPool queryPool;
	std::vector<std::string> timestampDescriptions;
	std::string profileSummary;
	double cpuStartTime;
	double cpuEndTime;
};

struct FrameDataShared {
	// Permanent objects
	VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
	VkSemaphore renderingCompleteSemaphore = VK_NULL_HANDLE;

	void Init(VulkanContext *vulkan);
	void Destroy(VulkanContext *vulkan);
};

enum class FrameSubmitType {
	Pending,
	Sync,
	Present,
};

// Per-frame data, round-robin so we can overlap submission with execution of the previous frame.
struct FrameData {
	bool skipSwap = false;

	std::mutex fenceMutex;
	std::condition_variable fenceCondVar;
	bool readyForFence = true;

	VkFence fence = VK_NULL_HANDLE;
	VkFence readbackFence = VK_NULL_HANDLE;  // Strictly speaking we might only need one global of these.

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

	// Profiling.
	QueueProfileContext profile;
	bool profilingEnabled_ = false;

	void Init(VulkanContext *vulkan, int index);
	void Destroy(VulkanContext *vulkan);

	void AcquireNextImage(VulkanContext *vulkan, FrameDataShared &shared);
	VkResult QueuePresent(VulkanContext *vulkan, FrameDataShared &shared);

	// Generally called from the main thread, unlike most of the rest.
	VkCommandBuffer GetInitCmd(VulkanContext *vulkan);

	// This will only submit if we are actually recording init commands.
	void SubmitPending(VulkanContext *vulkan, FrameSubmitType type, FrameDataShared &shared);

private:
	// Metadata for logging etc
	int index;
};
