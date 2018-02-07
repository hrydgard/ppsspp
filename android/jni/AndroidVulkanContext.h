#pragma once

#include "AndroidGraphicsContext.h"

static const bool g_validate_ = true;

class VulkanContext;

class AndroidVulkanContext : public AndroidGraphicsContext {
public:
	AndroidVulkanContext() : draw_(nullptr) {}
	~AndroidVulkanContext();

	bool InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) override;
	void Shutdown() override;
	void SwapInterval(int interval) override;
	void SwapBuffers() override;
	void Resize() override;

	void *GetAPIContext() override { return g_Vulkan; }

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}
	bool Initialized() override {
		return draw_ != nullptr;
	}

private:
	VulkanContext *g_Vulkan = nullptr;
	Draw::DrawContext *draw_ = nullptr;
};

struct VulkanLogOptions {
	bool breakOnWarning;
	bool breakOnError;
	bool msgBoxOnError;
};
