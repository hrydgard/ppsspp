#include <cassert>

#include "AndroidVulkanContext.h"
#include "base/NativeApp.h"
#include "base/display.h"
#include "Common/Vulkan/VulkanLoader.h"
#include "Common/Vulkan/VulkanContext.h"
#include "thin3d/VulkanRenderManager.h"
#include "thin3d/thin3d_create.h"
#include "util/text/parsers.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

static VulkanLogOptions g_LogOptions;

const char *ObjTypeToString(VkDebugReportObjectTypeEXT type) {
	switch (type) {
	case VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT: return "Instance";
	case VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT: return "PhysicalDevice";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT: return "Device";
	case VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT: return "Queue";
	case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT: return "CommandBuffer";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT: return "DeviceMemory";
	case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT: return "Buffer";
	case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_VIEW_EXT: return "BufferView";
	case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT: return "Image";
	case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT: return "ImageView";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT: return "ShaderModule";
	case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT: return "Pipeline";
	case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT: return "PipelineLayout";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT: return "Sampler";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT: return "DescriptorSet";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT: return "DescriptorSetLayout";
	case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT: return "DescriptorPool";
	case VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT: return "Fence";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT: return "Semaphore";
	case VK_DEBUG_REPORT_OBJECT_TYPE_EVENT_EXT: return "Event";
	case VK_DEBUG_REPORT_OBJECT_TYPE_QUERY_POOL_EXT: return "QueryPool";
	case VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT: return "Framebuffer";
	case VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT: return "RenderPass";
	case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_CACHE_EXT: return "PipelineCache";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT: return "SurfaceKHR";
	case VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT: return "SwapChainKHR";
	case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT: return "CommandPool";
	default: return "Unknown";
	}
}

static VKAPI_ATTR VkBool32 VKAPI_CALL Vulkan_Dbg(VkDebugReportFlagsEXT msgFlags, VkDebugReportObjectTypeEXT objType, uint64_t srcObject, size_t location, int32_t msgCode, const char* pLayerPrefix, const char* pMsg, void *pUserData) {
	const VulkanLogOptions *options = (const VulkanLogOptions *)pUserData;
	int loglevel = ANDROID_LOG_INFO;
	if (msgFlags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
		loglevel = ANDROID_LOG_ERROR;
	} else if (msgFlags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
		loglevel = ANDROID_LOG_WARN;
	} else if (msgFlags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
		loglevel = ANDROID_LOG_WARN;
	} else if (msgFlags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
		loglevel = ANDROID_LOG_WARN;
	} else if (msgFlags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
		loglevel = ANDROID_LOG_WARN;
	}

	__android_log_print(loglevel, APP_NAME, "[%s] %s Code %d : %s",
						pLayerPrefix, ObjTypeToString(objType), msgCode, pMsg);

	// false indicates that layer should not bail-out of an
	// API call that had validation failures. This may mean that the
	// app dies inside the driver due to invalid parameter(s).
	// That's what would happen without validation layers, so we'll
	// keep that behavior here.
	return false;
}

AndroidVulkanContext::AndroidVulkanContext() {
}

AndroidVulkanContext::~AndroidVulkanContext() {
	delete g_Vulkan;
	g_Vulkan = nullptr;
}

bool AndroidVulkanContext::InitAPI() {
	ILOG("AndroidVulkanContext::Init");
	init_glslang();

	g_LogOptions.breakOnError = true;
	g_LogOptions.breakOnWarning = true;
	g_LogOptions.msgBoxOnError = false;

	ILOG("Creating Vulkan context");
	Version gitVer(PPSSPP_GIT_VERSION);

	if (!g_Vulkan) {
		g_Vulkan = new VulkanContext();
	}
	VulkanContext::CreateInfo info{};
	info.app_name = "PPSSPP";
	info.app_ver = gitVer.ToInteger();
	info.flags = VULKAN_FLAG_PRESENT_MAILBOX | VULKAN_FLAG_PRESENT_FIFO_RELAXED;
	VkResult res = g_Vulkan->CreateInstance(info);
	if (res != VK_SUCCESS) {
		ELOG("Failed to create vulkan context: %s", g_Vulkan->InitError().c_str());
		System_SendMessage("toast", "No Vulkan compatible device found. Using OpenGL instead.");
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
	return true;
}

bool AndroidVulkanContext::InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) {
	int width = desiredBackbufferSizeX;
	int height = desiredBackbufferSizeY;
	if (!width || !height) {
		width = pixel_xres;
		height = pixel_yres;
	}
	ILOG("InitSurfaceAndroid: width=%d height=%d", width, height);
	g_Vulkan->InitSurface(WINDOWSYSTEM_ANDROID, (void *)wnd, nullptr, width, height);
	if (g_validate_) {
		int bits = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
		g_Vulkan->InitDebugMsgCallback(&Vulkan_Dbg, bits, &g_LogOptions);
	}

	bool success = true;
	if (g_Vulkan->InitObjects()) {
		draw_ = Draw::T3DCreateVulkanContext(g_Vulkan, g_Config.bGfxDebugSplitSubmit);
		SetGPUBackend(GPUBackend::VULKAN);
		success = draw_->CreatePresets();  // Doesn't fail, we ship the compiler.
		_assert_msg_(G3D, success, "Failed to compile preset shaders");
		draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());

		VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		success = renderManager->HasBackbuffers();
	} else {
		success = false;
	}

	ILOG("AndroidVulkanContext::Init completed, %s", success ? "successfully" : "but failed");
	if (!success) {
		g_Vulkan->DestroyObjects();
		g_Vulkan->DestroyDevice();
		g_Vulkan->DestroyDebugMsgCallback();

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
	g_Vulkan->DestroyDebugMsgCallback();

	g_Vulkan->DestroyInstance();
	// We keep the g_Vulkan context around to avoid invalidating a ton of pointers around the app.
	finalize_glslang();
	ILOG("AndroidVulkanContext::Shutdown completed");
}

void AndroidVulkanContext::SwapBuffers() {
}

void AndroidVulkanContext::Resize() {
	ILOG("AndroidVulkanContext::Resize begin (%d, %d)", g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());

	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	g_Vulkan->DestroyObjects();

	// backbufferResize updated these values.	TODO: Notify another way?
	g_Vulkan->ReinitSurface(pixel_xres, pixel_yres);
	g_Vulkan->InitObjects();
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	ILOG("AndroidVulkanContext::Resize end (%d, %d)", g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
}

void AndroidVulkanContext::SwapInterval(int interval) {
}
