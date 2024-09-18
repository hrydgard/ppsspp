#pragma once

#include "AndroidGraphicsContext.h"

class VulkanContext;

class AndroidVulkanContext : public AndroidGraphicsContext {
public:
	AndroidVulkanContext();
	~AndroidVulkanContext();

	bool InitAPI();

	bool InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) override;
	void ShutdownFromRenderThread() override;  // Inverses InitFromRenderThread.

	void Shutdown() override;
	void Resize() override;

	void *GetAPIContext() override { return g_Vulkan; }
	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	VulkanContext *g_Vulkan = nullptr;
	Draw::DrawContext *draw_ = nullptr;
};
