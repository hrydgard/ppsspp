#include <cassert>

#include "AndroidVulkanContext.h"
#include "base/NativeApp.h"
#include "base/display.h"
#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanDebug.h"
#include "Common/Vulkan/VulkanLoader.h"
#include "thin3d/VulkanRenderManager.h"
#include "thin3d/thin3d_create.h"
#include "util/text/parsers.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

AndroidVulkanContext::AndroidVulkanContext() {}

AndroidVulkanContext::~AndroidVulkanContext() {
	delete g_Vulkan;
	g_Vulkan = nullptr;
}

static uint32_t FlagsFromConfig() {
	if (g_Config.bVSync) {
		return VULKAN_FLAG_PRESENT_FIFO;
	}
	return VULKAN_FLAG_PRESENT_MAILBOX | VULKAN_FLAG_PRESENT_FIFO_RELAXED;
}

bool AndroidVulkanContext::InitAPI() {
	ILOG("AndroidVulkanContext::Init");
	init_glslang();

	g_LogOptions.breakOnError = true;
	g_LogOptions.breakOnWarning = true;
	g_LogOptions.msgBoxOnError = false;

	ILOG("Creating Vulkan context");
	Version gitVer(PPSSPP_GIT_VERSION);

	if (!VulkanLoad()) {
		ELOG("Failed to load Vulkan driver library");
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
		ELOG("Failed to create vulkan context: %s", g_Vulkan->InitError().c_str());
		VulkanSetAvailable(false);
		delete g_Vulkan;
		g_Vulkan = nullptr;
		return false;
	}

	int physicalDevice = g_Vulkan->GetBestPhysicalDevice();
	if (physicalDevice < 0) {
		ELOG("No usable Vulkan device found.");
		g_Vulkan->DestroyInstance();
		delete g_Vulkan;
		g_Vulkan = nullptr;
		return false;
	}

	g_Vulkan->ChooseDevice(physicalDevice);
	// Here we can enable device extensions if we like.

	ILOG("Creating Vulkan device");
	if (g_Vulkan->CreateDevice() != VK_SUCCESS) {
		ILOG("Failed to create vulkan device: %s", g_Vulkan->InitError().c_str());
		System_SendMessage("toast", "No Vulkan driver found. Using OpenGL instead.");
		g_Vulkan->DestroyInstance();
		delete g_Vulkan;
		g_Vulkan = nullptr;
		return false;
	}
	ILOG("Vulkan device created!");
	return true;
}

bool AndroidVulkanContext::InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) {
	ILOG("AndroidVulkanContext::InitFromRenderThread: desiredwidth=%d desiredheight=%d", desiredBackbufferSizeX, desiredBackbufferSizeY);
	if (!g_Vulkan) {
		ELOG("AndroidVulkanContext::InitFromRenderThread: No Vulkan context");
		return false;
	}

	VkResult res = g_Vulkan->InitSurface(WINDOWSYSTEM_ANDROID, (void *)wnd, nullptr);
	if (res != VK_SUCCESS) {
		ELOG("g_Vulkan->InitSurface failed: '%s'", VulkanResultToString(res));
		return false;
	}

	bool success = true;
	if (g_Vulkan->InitObjects()) {
		draw_ = Draw::T3DCreateVulkanContext(g_Vulkan, g_Config.bGfxDebugSplitSubmit);
		SetGPUBackend(GPUBackend::VULKAN);
		success = draw_->CreatePresets();  // Doesn't fail, we ship the compiler.
		_assert_msg_(G3D, success, "Failed to compile preset shaders");
		draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());

		VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		renderManager->SetInflightFrames(g_Config.iInflightFrames);
		success = renderManager->HasBackbuffers();
	} else {
		success = false;
	}

	ILOG("AndroidVulkanContext::Init completed, %s", success ? "successfully" : "but failed");
	if (!success) {
		g_Vulkan->DestroyObjects();
		g_Vulkan->DestroyDevice();
		g_Vulkan->DestroyInstance();
	}
	return success;
}

void AndroidVulkanContext::ShutdownFromRenderThread() {
	ILOG("AndroidVulkanContext::Shutdown");
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	delete draw_;
	draw_ = nullptr;
	g_Vulkan->WaitUntilQueueIdle();
	g_Vulkan->PerformPendingDeletes();
	g_Vulkan->DestroyObjects();  // Also destroys the surface, a bit asymmetric
	ILOG("Done with ShutdownFromRenderThread");
}

void AndroidVulkanContext::Shutdown() {
	ILOG("Calling NativeShutdownGraphics");
	g_Vulkan->DestroyDevice();
	g_Vulkan->DestroyInstance();
	// We keep the g_Vulkan context around to avoid invalidating a ton of pointers around the app.
	finalize_glslang();
	ILOG("AndroidVulkanContext::Shutdown completed");
}

void AndroidVulkanContext::SwapBuffers() {
}

void AndroidVulkanContext::Resize() {
	ILOG("AndroidVulkanContext::Resize begin (oldsize: %dx%d)", g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());

	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	g_Vulkan->DestroyObjects();

	g_Vulkan->UpdateFlags(FlagsFromConfig());
	g_Vulkan->ReinitSurface();
	g_Vulkan->InitObjects();
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	ILOG("AndroidVulkanContext::Resize end (final size: %dx%d)", g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
}

void AndroidVulkanContext::SwapInterval(int interval) {
}
