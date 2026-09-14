#include <memory>
#include <vector>

#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanDebug.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "GPU/GPUCommon.h"
#include "Common/Data/Text/Parsers.h"

#include "libretro/LibretroVulkanContext.h"
#include "libretro/LibretroVulkanPresentation.h"
#include <libretro_vulkan.h>
#include <GPU/Vulkan/VulkanRenderManager.h>
#include "ext/glslang/OGLCompilersDLL/InitializeDll.h"

#undef fflush

using namespace PPSSPP_VK;

static VulkanContext *vk;
// Non-owning: lifetime is tied to vk (owns it via VulkanContext::presentation_).
static LibretroVulkanPresentation *presentation;
// Fetched in ContextReset(), consumed when CreateDrawContext() constructs the presentation backend.
static retro_hw_render_interface_vulkan *hwRenderInterface;

LibretroVulkanContext::LibretroVulkanContext()
	: LibretroHWRenderContext(RETRO_HW_CONTEXT_VULKAN, VK_MAKE_VERSION(1, 0, 18)) {}

void LibretroVulkanContext::SwapBuffers() {
	if (presentation) {
		presentation->WaitForPresentation();
	}
	LibretroHWRenderContext::SwapBuffers();
}

static bool create_device(retro_vulkan_context *context, VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr, const char **required_device_extensions, unsigned num_required_device_extensions, const char **required_device_layers, unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features) {
	init_glslang();

	vk = new VulkanContext();

	if (!VulkanLoadFromGetInstanceProcAddr(instance, get_instance_proc_addr)) {
		ERROR_LOG(Log::G3D, "Failed to resolve Vulkan functions from libretro-provided get_instance_proc_addr");
		delete vk;
		vk = nullptr;
		return false;
	}

	if (vk->CreateInstanceExternal(instance) != VK_SUCCESS) {
		ERROR_LOG(Log::G3D, "Failed to adopt libretro-provided Vulkan instance: %s", vk->InitError().c_str());
		delete vk;
		vk = nullptr;
		return false;
	}

	int physical_device = -1;
	if (gpu) {
		for (int i = 0; i < vk->GetNumPhysicalDevices(); i++) {
			if (vk->GetPhysicalDevice(i) == gpu) {
				physical_device = i;
				break;
			}
		}
		if (physical_device < 0) {
			WARN_LOG(Log::G3D, "libretro-provided VkPhysicalDevice %p not found among our own enumerated physical devices - falling back to best physical device", (void *)gpu);
		}
	}
	if (physical_device < 0) {
		physical_device = vk->GetBestPhysicalDevice();
	}

	std::vector<const char *> extraDeviceExtensions(required_device_extensions, required_device_extensions + num_required_device_extensions);
	if (vk->CreateDevice(physical_device, extraDeviceExtensions, required_features) != VK_SUCCESS) {
		ERROR_LOG(Log::G3D, "Failed to create Vulkan device: %s", vk->InitError().c_str());
		delete vk;
		vk = nullptr;
		return false;
	}
	// CreateDevice() enables whatever of extraDeviceExtensions is available and silently skips the rest -
	// if RetroArch's own subsequent Vulkan calls depend on one of these, silently not enabling it could
	// cause exactly the kind of driver-level crash that's hard to diagnose, so make sure we'd notice.
	for (const char *extraExtension : extraDeviceExtensions) {
		bool enabled = false;
		for (const char *e : vk->GetDeviceExtensionsEnabled()) {
			if (!strcmp(e, extraExtension)) {
				enabled = true;
				break;
			}
		}
		if (!enabled) {
			WARN_LOG(Log::G3D, "libretro required device extension '%s' was not enabled", extraExtension);
		}
	}

	// Per the libretro create_device contract, the frontend takes over responsibility for eventually
	// destroying the VkDevice we just created and handed back to it below.
	vk->SetDeviceExternallyOwned();

	// Normally the graphics queue is only resolved as a side effect of ReinitSurface()/ChooseQueue(),
	// since that also needs to check presentation support against a real WSI surface - libretro has no
	// such surface (RetroArch owns real presentation itself), so pick the queue directly instead.
	if (!vk->ChooseGraphicsQueueWithoutSurface()) {
		ERROR_LOG(Log::G3D, "Failed to choose a graphics queue");
		delete vk;
		vk = nullptr;
		return false;
	}

	context->gpu = vk->GetPhysicalDevice(physical_device);
	context->device = vk->GetDevice();
	context->queue = vk->GetGraphicsQueue();
	context->queue_family_index = vk->GetGraphicsQueueFamilyIndex();
	context->presentation_queue = context->queue;
	context->presentation_queue_family_index = context->queue_family_index;
	INFO_LOG(Log::G3D, "libretro create_device: gpu=%p device=%p queue=%p queue_family_index=%u",
		(void *)context->gpu, (void *)context->device, (void *)context->queue, context->queue_family_index);
#ifdef _DEBUG
	fflush(stdout);
#endif
	return true;
}

static const VkApplicationInfo *GetApplicationInfo(void) {
	static VkApplicationInfo app_info{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
	app_info.pApplicationName = "PPSSPP";
	app_info.applicationVersion = Version(PPSSPP_GIT_VERSION).ToInteger();
	app_info.pEngineName = "PPSSPP";
	app_info.engineVersion = 2;
	app_info.apiVersion = VK_API_VERSION_1_0;
	return &app_info;
}

bool LibretroVulkanContext::InitAPI(void *wnd, std::string *deviceName, std::string *error_message) {
	if (!LibretroHWRenderContext::InitHW(true)) {
		return false;
	}

	static const struct retro_hw_render_context_negotiation_interface_vulkan iface = {
		RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN,
		RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION,
		GetApplicationInfo,
		create_device, // Callback above.
		nullptr,
	};
	Libretro::environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, (void *)&iface);

	g_Config.iGPUBackend = (int)GPUBackend::VULKAN;
	return true;
}

void LibretroVulkanContext::ContextReset() {
   retro_hw_render_interface *iface;
   if (!Libretro::environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, (void **)&iface) || !iface) {
      ERROR_LOG(Log::G3D, "Failed to get HW rendering interface!\n");
      return;
   }
   if (iface->interface_version != RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION) {
      ERROR_LOG(Log::G3D, "HW render interface mismatch, expected %u, got %u!\n", RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION, iface->interface_version);
      return;
   }
   hwRenderInterface = (retro_hw_render_interface_vulkan *)iface;

   LibretroHWRenderContext::ContextReset();
}

void LibretroVulkanContext::ContextDestroy() {
   INFO_LOG(Log::G3D, "LibretroVulkanContext::ContextDestroy()");
   if (vk) {
      vk->WaitUntilQueueIdle();
      LibretroHWRenderContext::ContextDestroy();
   }
}

void LibretroVulkanContext::CreateDrawContext() {
   int w = g_Config.iInternalResolution * NATIVEWIDTH;
   int h = g_Config.iInternalResolution * NATIVEHEIGHT;
   if (g_Config.bDisplayCropTo16x9) {
      h -= g_Config.iInternalResolution * 2;
   }

   auto libretroPresentation = std::make_unique<LibretroVulkanPresentation>(hwRenderInterface, VK_FORMAT_B8G8R8A8_UNORM, VkExtent2D{ (uint32_t)w, (uint32_t)h });
   if (!libretroPresentation->Create(vk)) {
      ERROR_LOG(Log::G3D, "Failed to create libretro Vulkan presentation images");
      return;
   }
   presentation = libretroPresentation.get();
   vk->SetPresentation(std::move(libretroPresentation));

   bool useMultiThreading = g_Config.bRenderMultiThreading;
   if (g_Config.iInflightFrames == 1) {
      useMultiThreading = false;
   }
   draw_ = Draw::T3DCreateVulkanContext(vk, useMultiThreading);

   ((VulkanRenderManager*)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER))->SetInflightFrames(g_Config.iInflightFrames);
   SetGPUBackend(GPUBackend::VULKAN);
}

void LibretroVulkanContext::ShutdownAPI() {
   if (!vk) {
      return;
   }

   if (draw_)
      draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, vk->GetBackbufferWidth(), vk->GetBackbufferHeight());

   LibretroHWRenderContext::ShutdownAPI();

   vk->WaitUntilQueueIdle();

   if (presentation) {
      presentation->Destroy(vk);
      vk->SetPresentation(nullptr);
      presentation = nullptr;
   }

   vk->DestroyDevice();
   vk->DestroyInstance();

   delete vk;
   vk = nullptr;
   hwRenderInterface = nullptr;

   finalize_glslang();
   glslang::DetachProcess();
}

void *LibretroVulkanContext::GetAPIContext() { return vk; }
