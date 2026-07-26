#pragma once

#include "Common/GraphicsContext.h"
#include "Common/GPU/thin3d.h"

#include <android/native_window_jni.h>

class VulkanContext;

class AndroidVulkanContext : public GraphicsContext {
public:
	AndroidVulkanContext();
	~AndroidVulkanContext();

	bool InitAPI(void *wnd, std::string *deviceName, std::string *errorMessage) override;
	void ShutdownAPI() override;

	bool InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *error_message) override;
	void ShutdownSurface() override;

	void Resize() override;

	void *GetAPIContext() override { return vulkan_; }
	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	VulkanContext *vulkan_ = nullptr;
	Draw::DrawContext *draw_ = nullptr;
};
