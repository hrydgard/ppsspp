#pragma once

#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Common/GraphicsContext.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/thin3d_create.h"

enum class GraphicsContextState {
	PENDING,
	INITIALIZED,
	FAILED_INIT,
	SHUTDOWN,
};

class IOSVulkanContext : public GraphicsContext {
public:
	IOSVulkanContext() {}
	~IOSVulkanContext() {
		delete g_Vulkan;
		g_Vulkan = nullptr;
	}

	bool InitAPI();

	bool InitFromRenderThread(CAMetalLayer *layer, int desiredBackbufferSizeX, int desiredBackbufferSizeY);
	void ShutdownFromRenderThread() override;  // Inverses InitFromRenderThread.

	void Shutdown() override;
	void Resize() override {}

	void *GetAPIContext() override { return g_Vulkan; }
	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	VulkanContext *g_Vulkan = nullptr;
	Draw::DrawContext *draw_ = nullptr;
	GraphicsContextState state_ = GraphicsContextState::PENDING;
};
