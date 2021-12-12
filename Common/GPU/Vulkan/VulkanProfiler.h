#pragma once

#include <vector>
#include <string>

#include "Common/Log.h"
#include "VulkanLoader.h"

// Simple scoped based profiler, initially meant for instant one-time tasks like texture uploads
// etc. Supports recursive scopes. Scopes are not yet tracked separately for each command buffer.
// For the pass profiler in VulkanQueueRunner, a purpose-built separate profiler that can take only
// one measurement between each pass makes more sense.
//
// Put the whole thing in a FrameData to allow for overlap.

struct ProfilerScope {
	std::string name;
	size_t startQueryId;
	size_t endQueryId;
	int level;
};

class VulkanContext; 


class VulkanProfiler {
public:
	void Init(VulkanContext *vulkan);
	void Shutdown();

	void BeginFrame(VulkanContext *vulkan, VkCommandBuffer firstCommandBuffer);

	void EndFrame();

	void Begin(VkCommandBuffer cmdBuf, std::string scopeName, VkPipelineStageFlagBits stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
	void End(VkCommandBuffer cmdBuf, VkPipelineStageFlagBits stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

private:
	VulkanContext *vulkan_;

	VkQueryPool queryPool_ = VK_NULL_HANDLE;
	std::vector<ProfilerScope> scopes_;
	int numQueries_ = 0;
	bool firstFrame_ = true;

	std::vector<size_t> scopeStack_;

	const int MAX_QUERY_COUNT = 1024;
};
