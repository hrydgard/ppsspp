#include "AndroidVulkanContext.h"
#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/Log.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanDebug.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/Data/Text/Parsers.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "GPU/Vulkan/VulkanUtil.h"

AndroidVulkanContext::AndroidVulkanContext() {}

AndroidVulkanContext::~AndroidVulkanContext() {
	delete g_Vulkan;
	g_Vulkan = nullptr;
}

bool AndroidVulkanContext::InitAPI() {
	INFO_LOG(Log::G3D, "AndroidVulkanContext::Init");
	init_glslang();

	g_LogOptions.breakOnError = true;
	g_LogOptions.breakOnWarning = true;
	g_LogOptions.msgBoxOnError = false;

	std::string errorStr;
	if (!VulkanLoad(&errorStr)) {
		ERROR_LOG(Log::G3D, "Failed to load Vulkan driver library: %s", errorStr.c_str());
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}

	if (!g_Vulkan) {
		// TODO: Assert if g_Vulkan already exists here ?
		INFO_LOG(Log::G3D, "Creating Vulkan context.");
		g_Vulkan = new VulkanContext();
	} else {
		INFO_LOG(Log::G3D, "Reusing existing Vulkan context.");
	}

	VulkanContext::CreateInfo info{};
	InitVulkanCreateInfoFromConfig(&info);
	if (!g_Vulkan->CreateInstanceAndDevice(info)) {
		delete g_Vulkan;
		g_Vulkan = nullptr;
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}

	INFO_LOG(Log::G3D, "Vulkan device created!");
	state_ = GraphicsContextState::INITIALIZED;
	return true;
}

bool AndroidVulkanContext::InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) {
	INFO_LOG(Log::G3D, "AndroidVulkanContext::InitFromRenderThread: desiredwidth=%d desiredheight=%d", desiredBackbufferSizeX, desiredBackbufferSizeY);
	if (!g_Vulkan) {
		ERROR_LOG(Log::G3D, "AndroidVulkanContext::InitFromRenderThread: No Vulkan context");
		return false;
	}

	VkResult res = g_Vulkan->InitSurface(WINDOWSYSTEM_ANDROID, (void *)wnd, nullptr);
	if (res != VK_SUCCESS) {
		ERROR_LOG(Log::G3D, "g_Vulkan->InitSurface failed: '%s'", VulkanResultToString(res));
		return false;
	}

	bool useMultiThreading = g_Config.bRenderMultiThreading;
	if (g_Config.iInflightFrames == 1) {
		useMultiThreading = false;
	}
	draw_ = Draw::T3DCreateVulkanContext(g_Vulkan, useMultiThreading);

	VkPresentModeKHR presentMode = ConfigPresentModeToVulkan(draw_);
	if (!g_Vulkan->InitSwapchain(presentMode)) {
		g_Vulkan->DestroySurface();
		return false;
	}

	SetGPUBackend(GPUBackend::VULKAN);
	bool success = draw_->CreatePresets();  // Doesn't fail, we ship the compiler.
	_assert_msg_(success, "Failed to compile preset shaders");
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());

	VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager->SetInflightFrames(g_Config.iInflightFrames);
	if (!renderManager->HasBackbuffers()) {
		ERROR_LOG(Log::G3D, "VulkanRenderManager has no backbuffers after InitFromRenderThread");
		g_Vulkan->DestroySwapchain();
		g_Vulkan->DestroySurface();
		delete draw_;
		draw_ = nullptr;
		return false;
	}

	INFO_LOG(Log::G3D, "AndroidVulkanContext::Init completed successfully");
	return true;
}

void AndroidVulkanContext::ShutdownFromRenderThread() {
	INFO_LOG(Log::G3D, "AndroidVulkanContext::Shutdown");
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	delete draw_;
	draw_ = nullptr;
	g_Vulkan->WaitUntilQueueIdle();
	g_Vulkan->PerformPendingDeletes();
	g_Vulkan->DestroySwapchain();
	g_Vulkan->DestroySurface();
	INFO_LOG(Log::G3D, "Done with ShutdownFromRenderThread");
}

void AndroidVulkanContext::Shutdown() {
	INFO_LOG(Log::G3D, "Calling NativeShutdownGraphics");
	g_Vulkan->DestroyDevice();
	g_Vulkan->DestroyInstance();
	// We keep the g_Vulkan context around to avoid invalidating a ton of pointers around the app.
	finalize_glslang();
	INFO_LOG(Log::G3D, "AndroidVulkanContext::Shutdown completed");
}

void AndroidVulkanContext::Resize() {
	INFO_LOG(Log::G3D, "AndroidVulkanContext::Resize begin (oldsize: %dx%d)", g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	g_Vulkan->DestroySwapchain();

	// TODO: We should only destroy the surface here if the window changed. We can track this inside g_Vulkan.
	g_Vulkan->DestroySurface();
	g_Vulkan->ReinitSurface();

	VkPresentModeKHR presentMode = ConfigPresentModeToVulkan(draw_);
	g_Vulkan->InitSwapchain(presentMode);
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	INFO_LOG(Log::G3D, "AndroidVulkanContext::Resize end (final size: %dx%d)", g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
}
