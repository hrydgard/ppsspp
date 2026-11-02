// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <cstdio>
#include <cstdlib>

#include "Common/GPU/thin3d_create.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Common/GraphicsContext.h"
#include "Common/Log.h"
#include "Core/System.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "headless/VulkanHeadlessContext.h"

// Vulkan without a window or swapchain (VulkanContext::InitHeadless), so it works on macOS (where a
// window can only be created on the main thread) and on Android (no window at all).
class VulkanHeadlessGraphicsContext : public GraphicsContext {
public:
	~VulkanHeadlessGraphicsContext() { Shutdown(); }

	bool Init(std::string *errorMessage) {
		init_glslang();
		glslangInited_ = true;

		std::string errorStr;
		if (!VulkanLoad(&errorStr)) {
			*errorMessage = "Failed to load Vulkan driver library: " + errorStr;
			return false;
		}

		vulkan_ = new VulkanContext();
		VulkanContext::CreateInfo info{};
		InitVulkanCreateInfoFromConfig(&info);
		if (vulkan_->CreateInstance(info) != VK_SUCCESS) {
			*errorMessage = vulkan_->InitError();
			return false;
		}
		instanceCreated_ = true;
		int deviceNum = vulkan_->GetBestPhysicalDevice();
		if (vulkan_->CreateDevice(deviceNum) != VK_SUCCESS) {
			*errorMessage = vulkan_->InitError();
			return false;
		}
		deviceCreated_ = true;
		if (vulkan_->InitHeadless() != VK_SUCCESS) {
			*errorMessage = "No graphics queue";
			return false;
		}

		// Headless restores the config defaults only after this, so use the defaults directly:
		// three frames in flight, multithreaded rendering.
		draw_ = Draw::T3DCreateVulkanContext(vulkan_, true);
		vulkan_->InitSwapchain(VK_PRESENT_MODE_FIFO_KHR);
		SetGPUBackend(GPUBackend::VULKAN);
		bool success = draw_->CreatePresets();
		_assert_(success);
		// Only applied when the backbuffers are created, so before GOT_BACKBUFFER. PPSSPP_INFLIGHT_FRAMES
		// overrides the default, for comparing with later versions (which default to 2).
		int inflightFrames = 3;
		if (const char *env = getenv("PPSSPP_INFLIGHT_FRAMES")) {
			inflightFrames = atoi(env);
		}
		VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		renderManager->SetInflightFrames(inflightFrames);
		draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
		return success;
	}

	void Shutdown() override {
		if (draw_) {
			draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
			delete draw_;
			draw_ = nullptr;
		}
		if (vulkan_) {
			if (deviceCreated_) {
				vulkan_->WaitUntilQueueIdle();
				vulkan_->DestroySwapchain();
				vulkan_->DestroyDevice();
			}
			if (instanceCreated_) {
				vulkan_->DestroyInstance();
			}
			delete vulkan_;
			vulkan_ = nullptr;
		}
		if (glslangInited_) {
			finalize_glslang();
			glslangInited_ = false;
		}
	}

	void Resize() override {}
	void *GetAPIContext() override { return vulkan_; }
	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	VulkanContext *vulkan_ = nullptr;
	Draw::DrawContext *draw_ = nullptr;
	bool glslangInited_ = false;
	bool instanceCreated_ = false;
	bool deviceCreated_ = false;
};

GraphicsContext *CreateVulkanHeadlessContext(std::string *errorMessage) {
	VulkanHeadlessGraphicsContext *vulkanContext = new VulkanHeadlessGraphicsContext();
	if (!vulkanContext->Init(errorMessage)) {
		fprintf(stderr, "Vulkan init failed: %s\n", errorMessage->c_str());
		delete vulkanContext;
		return nullptr;
	}
	return vulkanContext;
}

bool VulkanHeadlessHost::InitGraphics(std::string *error_message, GraphicsContext **ctx, GPUCore core) {
	*ctx = CreateVulkanHeadlessContext(error_message);
	gfx_ = *ctx;
	return *ctx != nullptr;
}

void VulkanHeadlessHost::ShutdownGraphics() {
	if (gfx_) {
		gfx_->Shutdown();
		delete gfx_;
		gfx_ = nullptr;
	}
}
