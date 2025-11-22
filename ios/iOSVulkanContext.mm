#include "ppsspp_config.h"
#import "iOSVulkanContext.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "Common/Data/Text/Parsers.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

// ViewController lifecycle:
// https://www.progressconcepts.com/blog/ios-appdelegate-viewcontroller-method-order/

bool IOSVulkanContext::InitFromRenderThread(CAMetalLayer *layer, int desiredBackbufferSizeX, int desiredBackbufferSizeY) {
	INFO_LOG(Log::G3D, "IOSVulkanContext::InitFromRenderThread: desiredwidth=%d desiredheight=%d", desiredBackbufferSizeX, desiredBackbufferSizeY);
	if (!g_Vulkan) {
		ERROR_LOG(Log::G3D, "IOSVulkanContext::InitFromRenderThread: No Vulkan context");
		return false;
	}

	VkResult res = g_Vulkan->InitSurface(WINDOWSYSTEM_METAL_EXT, (__bridge void *)layer, nullptr);
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

	// This MUST run on the main thread. We're taking our chances with a dispatch_sync here.
	g_Vulkan->InitSwapchain(presentMode);

	if (false) {
		delete draw_;
		ERROR_LOG(Log::G3D, "InitSwapchain failed");
		g_Vulkan->DestroySwapchain();
		g_Vulkan->DestroySurface();
		g_Vulkan->DestroyDevice();
		g_Vulkan->DestroyInstance();
		return false;
	}

	SetGPUBackend(GPUBackend::VULKAN);
	bool shaderSuccess = draw_->CreatePresets();  // Doesn't fail, we ship the compiler.
	_assert_msg_(shaderSuccess, "Failed to compile preset shaders");
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());

	VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager->SetInflightFrames(g_Config.iInflightFrames);
	return true;
}

void IOSVulkanContext::ShutdownFromRenderThread() {
	INFO_LOG(Log::G3D, "IOSVulkanContext::Shutdown");
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	delete draw_;
	draw_ = nullptr;
	g_Vulkan->WaitUntilQueueIdle();
	g_Vulkan->PerformPendingDeletes();
	g_Vulkan->DestroySwapchain();
	g_Vulkan->DestroySurface();
	INFO_LOG(Log::G3D, "Done with ShutdownFromRenderThread");
}

void IOSVulkanContext::Shutdown() {
	INFO_LOG(Log::G3D, "Calling NativeShutdownGraphics");
	g_Vulkan->DestroyDevice();
	g_Vulkan->DestroyInstance();
	// We keep the g_Vulkan context around to avoid invalidating a ton of pointers around the app.
	finalize_glslang();
	INFO_LOG(Log::G3D, "IOSVulkanContext::Shutdown completed");
}

bool IOSVulkanContext::InitAPI() {
	INFO_LOG(Log::G3D, "IOSVulkanContext::Init");
	init_glslang();

	g_LogOptions.breakOnError = true;
	g_LogOptions.breakOnWarning = true;
	g_LogOptions.msgBoxOnError = false;

	INFO_LOG(Log::G3D, "Creating Vulkan context");
	Version gitVer(PPSSPP_GIT_VERSION);

	std::string errorStr;
	if (!VulkanLoad(&errorStr)) {
		ERROR_LOG(Log::G3D, "Failed to load Vulkan driver library: %s", errorStr.c_str());
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}

	if (!g_Vulkan) {
		// TODO: Assert if g_Vulkan already exists here?
		g_Vulkan = new VulkanContext();
	}

	VulkanContext::CreateInfo info{};
	InitVulkanCreateInfoFromConfig(&info);
	if (!g_Vulkan->CreateInstanceAndDevice(info)) {
		delete g_Vulkan;
		g_Vulkan = nullptr;
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}

	g_Vulkan->SetCbGetDrawSize([]() {
		return VkExtent2D {(uint32_t)g_display.pixel_xres, (uint32_t)g_display.pixel_yres};
	});

	INFO_LOG(Log::G3D, "Vulkan device created!");
	state_ = GraphicsContextState::INITIALIZED;
	return true;
}

