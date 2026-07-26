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
	delete vulkan_;
	vulkan_ = nullptr;
}

bool AndroidVulkanContext::InitAPI(void *wnd, std::string *deviceName, std::string *errorMessage) {
	_dbg_assert_(!vulkan_);
	INFO_LOG(Log::G3D, "AndroidVulkanContext::Init");
	init_glslang();

	g_LogOptions.breakOnError = true;
	g_LogOptions.breakOnWarning = true;
	g_LogOptions.msgBoxOnError = false;

	std::string errorStr;
	if (!VulkanLoad(&errorStr)) {
		ERROR_LOG(Log::G3D, "Failed to load Vulkan driver library: %s", errorStr.c_str());
		return false;
	}

	if (!vulkan_) {
		// TODO: Assert if vulkan_ already exists here ?
		INFO_LOG(Log::G3D, "Creating Vulkan context.");
		vulkan_ = new VulkanContext();
	} else {
		INFO_LOG(Log::G3D, "Reusing existing Vulkan context.");
	}

	VulkanContext::CreateInfo info{};
	InitVulkanCreateInfoFromConfig(&info);
	if (!vulkan_->CreateInstanceAndDevice(info, deviceName)) {
		delete vulkan_;
		vulkan_ = nullptr;
		return false;
	}

	INFO_LOG(Log::G3D, "Vulkan device created!");
	return true;
}

bool AndroidVulkanContext::InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *error_message) {
	ANativeWindow *wnd_ = (ANativeWindow *)data1;

	INFO_LOG(Log::G3D, "AndroidVulkanContext::Init");
	if (!vulkan_) {
		ERROR_LOG(Log::G3D, "AndroidVulkanContext::Init: No Vulkan context");
		return false;
	}

	VkResult res = vulkan_->InitSurface(WINDOWSYSTEM_ANDROID, (void *)wnd_, nullptr);
	if (res != VK_SUCCESS) {
		ERROR_LOG(Log::G3D, "vulkan_->InitSurface failed: '%s'", VulkanResultToString(res));
		return false;
	}

	bool useMultiThreading = g_Config.bRenderMultiThreading;
	if (g_Config.iInflightFrames == 1) {
		useMultiThreading = false;
	}
	draw_ = Draw::T3DCreateVulkanContext(vulkan_, useMultiThreading);

	VkPresentModeKHR presentMode = ConfigPresentModeToVulkan(draw_);
	if (!vulkan_->InitSwapchain(presentMode)) {
		vulkan_->DestroySurface();
		return false;
	}

	SetGPUBackend(GPUBackend::VULKAN);
	bool success = draw_->CreatePresets();  // Doesn't fail, we ship the compiler.
	_assert_msg_(success, "Failed to compile preset shaders");
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());

	VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager->SetInflightFrames(g_Config.iInflightFrames);
	if (!renderManager->HasBackbuffers()) {
		ERROR_LOG(Log::G3D, "VulkanRenderManager has no backbuffers after InitFromRenderThread");
		vulkan_->DestroySwapchain();
		vulkan_->DestroySurface();
		delete draw_;
		draw_ = nullptr;
		return false;
	}

	INFO_LOG(Log::G3D, "AndroidVulkanContext::Init completed successfully");
	return true;
}

void AndroidVulkanContext::ShutdownSurface() {
	INFO_LOG(Log::G3D, "AndroidVulkanContext::ShutdownSurface");
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	delete draw_;
	draw_ = nullptr;
	vulkan_->WaitUntilQueueIdle();
	vulkan_->PerformPendingDeletes();
	vulkan_->DestroySwapchain();
	vulkan_->DestroySurface();
	INFO_LOG(Log::G3D, "Done with ShutdownFromRenderThread");
}

void AndroidVulkanContext::ShutdownAPI() {
	INFO_LOG(Log::G3D, "AndroidVulkanContext::Shutdown");
	vulkan_->DestroyDevice();
	vulkan_->DestroyInstance();
	// We keep the vulkan_ context around to avoid invalidating a ton of pointers around the app.
	// TODO: Actually the above is inaccurate..
	finalize_glslang();
	INFO_LOG(Log::G3D, "AndroidVulkanContext::Shutdown completed");
}

void AndroidVulkanContext::Resize() {
	INFO_LOG(Log::G3D, "AndroidVulkanContext::Resize begin (oldsize: %dx%d)", vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	VkPresentModeKHR presentMode = ConfigPresentModeToVulkan(draw_);
	vulkan_->InitSwapchain(presentMode);  // This also destroys the old swapchain and makes a nice transition.
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	INFO_LOG(Log::G3D, "AndroidVulkanContext::Resize end (final size: %dx%d)", vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
}
