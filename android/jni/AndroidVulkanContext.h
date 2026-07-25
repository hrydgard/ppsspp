#pragma once

#include "Common/GraphicsContext.h"
#include "Common/GPU/thin3d.h"

#include <android/native_window_jni.h>

class VulkanContext;

class AndroidVulkanContext : public GraphicsContext {
public:
	AndroidVulkanContext();
	~AndroidVulkanContext();

	bool InitAPI();

	bool Init(ANativeWindow *wnd);
	void ShutdownFromRenderThread() override;  // Inverses InitFromRenderThread.

	void Shutdown() override;
	void Resize() override;

	void *GetAPIContext() override { return g_Vulkan; }
	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	VulkanContext *g_Vulkan = nullptr;
	Draw::DrawContext *draw_ = nullptr;
};
