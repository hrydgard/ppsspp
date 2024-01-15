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


#ifdef _DEBUG
static const bool g_Validate = true;
#else
static const bool g_Validate = false;
#endif

// TODO: Share this between backends.
static uint32_t FlagsFromConfig() {
	uint32_t flags;
	if (g_Config.bVSync) {
		flags = VULKAN_FLAG_PRESENT_FIFO;
	} else {
		flags = VULKAN_FLAG_PRESENT_MAILBOX | VULKAN_FLAG_PRESENT_IMMEDIATE;
	}
	if (g_Validate) {
		flags |= VULKAN_FLAG_VALIDATE;
	}
	return flags;
}

AndroidVulkanContext::AndroidVulkanContext() {}

AndroidVulkanContext::~AndroidVulkanContext() {
	delete g_Vulkan;
	g_Vulkan = nullptr;
}

bool AndroidVulkanContext::InitAPI() {
	INFO_LOG(G3D, "AndroidVulkanContext::Init");
	init_glslang();

	g_LogOptions.breakOnError = true;
	g_LogOptions.breakOnWarning = true;
	g_LogOptions.msgBoxOnError = false;

	INFO_LOG(G3D, "Creating Vulkan context");
	Version gitVer(PPSSPP_GIT_VERSION);

	std::string errorStr;
	if (!VulkanLoad(&errorStr)) {
		ERROR_LOG(G3D, "Failed to load Vulkan driver library: %s", errorStr.c_str());
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}

	if (!g_Vulkan) {
		// TODO: Assert if g_Vulkan already exists here?
		g_Vulkan = new VulkanContext();
	}

	VulkanContext::CreateInfo info{};
	info.app_name = "PPSSPP";
	info.app_ver = gitVer.ToInteger();
	info.flags = FlagsFromConfig();
	VkResult res = g_Vulkan->CreateInstance(info);
	if (res != VK_SUCCESS) {
		ERROR_LOG(G3D, "Failed to create vulkan context: %s", g_Vulkan->InitError().c_str());
		VulkanSetAvailable(false);
		delete g_Vulkan;
		g_Vulkan = nullptr;
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}

	int physicalDevice = g_Vulkan->GetBestPhysicalDevice();
	if (physicalDevice < 0) {
		ERROR_LOG(G3D, "No usable Vulkan device found.");
		g_Vulkan->DestroyInstance();
		delete g_Vulkan;
		g_Vulkan = nullptr;
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}

	g_Vulkan->ChooseDevice(physicalDevice);

	INFO_LOG(G3D, "Creating Vulkan device (flags: %08x)", info.flags);
	if (g_Vulkan->CreateDevice() != VK_SUCCESS) {
		INFO_LOG(G3D, "Failed to create vulkan device: %s", g_Vulkan->InitError().c_str());
		System_Toast("No Vulkan driver found. Using OpenGL instead.");
		g_Vulkan->DestroyInstance();
		delete g_Vulkan;
		g_Vulkan = nullptr;
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}

	INFO_LOG(G3D, "Vulkan device created!");
	state_ = GraphicsContextState::INITIALIZED;
	return true;
}

bool AndroidVulkanContext::InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) {
	INFO_LOG(G3D, "AndroidVulkanContext::InitFromRenderThread: desiredwidth=%d desiredheight=%d", desiredBackbufferSizeX, desiredBackbufferSizeY);
	if (!g_Vulkan) {
		ERROR_LOG(G3D, "AndroidVulkanContext::InitFromRenderThread: No Vulkan context");
		return false;
	}

	VkResult res = g_Vulkan->InitSurface(WINDOWSYSTEM_ANDROID, (void *)wnd, nullptr);
	if (res != VK_SUCCESS) {
		ERROR_LOG(G3D, "g_Vulkan->InitSurface failed: '%s'", VulkanResultToString(res));
		return false;
	}

	bool success = true;
	if (g_Vulkan->InitSwapchain()) {
		bool useMultiThreading = g_Config.bRenderMultiThreading;
		if (g_Config.iInflightFrames == 1) {
			useMultiThreading = false;
		}
		draw_ = Draw::T3DCreateVulkanContext(g_Vulkan, useMultiThreading);
		SetGPUBackend(GPUBackend::VULKAN);
		success = draw_->CreatePresets();  // Doesn't fail, we ship the compiler.
		_assert_msg_(success, "Failed to compile preset shaders");
		draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());

		VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		renderManager->SetInflightFrames(g_Config.iInflightFrames);
		success = renderManager->HasBackbuffers();
	} else {
		success = false;
	}

	INFO_LOG(G3D, "AndroidVulkanContext::Init completed, %s", success ? "successfully" : "but failed");
	if (!success) {
		g_Vulkan->DestroySwapchain();
		g_Vulkan->DestroySurface();
		g_Vulkan->DestroyDevice();
		g_Vulkan->DestroyInstance();
	}
	return success;
}

void AndroidVulkanContext::ShutdownFromRenderThread() {
	INFO_LOG(G3D, "AndroidVulkanContext::Shutdown");
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	delete draw_;
	draw_ = nullptr;
	g_Vulkan->WaitUntilQueueIdle();
	g_Vulkan->PerformPendingDeletes();
	g_Vulkan->DestroySwapchain();
	g_Vulkan->DestroySurface();
	INFO_LOG(G3D, "Done with ShutdownFromRenderThread");
}

void AndroidVulkanContext::Shutdown() {
	INFO_LOG(G3D, "Calling NativeShutdownGraphics");
	g_Vulkan->DestroyDevice();
	g_Vulkan->DestroyInstance();
	// We keep the g_Vulkan context around to avoid invalidating a ton of pointers around the app.
	finalize_glslang();
	INFO_LOG(G3D, "AndroidVulkanContext::Shutdown completed");
}

void AndroidVulkanContext::Resize() {
	INFO_LOG(G3D, "AndroidVulkanContext::Resize begin (oldsize: %dx%d)", g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());

	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	g_Vulkan->DestroySwapchain();
	g_Vulkan->DestroySurface();

	g_Vulkan->UpdateFlags(FlagsFromConfig());

	g_Vulkan->ReinitSurface();
	g_Vulkan->InitSwapchain();
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	INFO_LOG(G3D, "AndroidVulkanContext::Resize end (final size: %dx%d)", g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
}
