
#include "Common/Vulkan/VulkanLoader.h"
#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanDebug.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "GPU/GPUInterface.h"
#include "util/text/parsers.h"

#include "libretro/LibretroVulkanContext.h"
#include "libretro/libretro_vulkan.h"

static VulkanContext *vk;

void vk_libretro_init(VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr, const char **required_device_extensions, unsigned num_required_device_extensions, const char **required_device_layers, unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features);
void vk_libretro_shutdown();
void vk_libretro_set_hwrender_interface(retro_hw_render_interface *hw_render_interface);
void vk_libretro_wait_for_presentation();

LibretroVulkanContext::LibretroVulkanContext()
	: LibretroHWRenderContext(RETRO_HW_CONTEXT_VULKAN, VK_MAKE_VERSION(1, 0, 18)) {}

void LibretroVulkanContext::SwapBuffers() {
	vk_libretro_wait_for_presentation();
	LibretroHWRenderContext::SwapBuffers();
}

static bool create_device(retro_vulkan_context *context, VkInstance instance, VkPhysicalDevice gpu, VkSurfaceKHR surface, PFN_vkGetInstanceProcAddr get_instance_proc_addr, const char **required_device_extensions, unsigned num_required_device_extensions, const char **required_device_layers, unsigned num_required_device_layers, const VkPhysicalDeviceFeatures *required_features) {
	init_glslang();

	vk = new VulkanContext;
	if (!vk->InitError().empty()) {
		ERROR_LOG(G3D, "%s", vk->InitError().c_str());
		return false;
	}

	vk_libretro_init(instance, gpu, surface, get_instance_proc_addr, required_device_extensions, num_required_device_extensions, required_device_layers, num_required_device_layers, required_features);

	vk->CreateInstance({});

	int physical_device = 0;
	while (gpu && vk->GetPhysicalDevice(physical_device) != gpu) {
		physical_device++;
	}

	if (!gpu) {
		physical_device = vk->GetBestPhysicalDevice();
	}

	vk->ChooseDevice(physical_device);
	vk->CreateDevice();
#ifdef _WIN32
	vk->InitSurface(WINDOWSYSTEM_WIN32, nullptr, nullptr);
#elif defined(__ANDROID__)
	vk->InitSurface(WINDOWSYSTEM_ANDROID, nullptr, nullptr);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
	vk->InitSurface(WINDOWSYSTEM_XLIB, nullptr, nullptr);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	vk->InitSurface(WINDOWSYSTEM_XCB, nullptr, nullptr);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	vk->InitSurface(WINDOWSYSTEM_WAYLAND, nullptr, nullptr);
#endif

	if (!vk->InitQueue()) {
		return false;
	}

	context->gpu = vk->GetPhysicalDevice(physical_device);
	context->device = vk->GetDevice();
	context->queue = vk->GetGraphicsQueue();
	context->queue_family_index = vk->GetGraphicsQueueFamilyIndex();
	context->presentation_queue = context->queue;
	context->presentation_queue_family_index = context->queue_family_index;
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

bool LibretroVulkanContext::Init() {
	if (!LibretroHWRenderContext::Init(false)) {
		return false;
	}

	static const struct retro_hw_render_context_negotiation_interface_vulkan iface = { RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN, RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN_VERSION, GetApplicationInfo, create_device, nullptr };
	Libretro::environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, (void *)&iface);

	g_Config.iGPUBackend = (int)GPUBackend::VULKAN;
	return true;
}

void LibretroVulkanContext::Shutdown() {
	LibretroHWRenderContext::Shutdown();

	if (!vk) {
		return;
	}

	vk->WaitUntilQueueIdle();

	vk->DestroyObjects();
	vk->DestroyDevice();
	vk->DestroyInstance();
	delete vk;
	vk = nullptr;

	finalize_glslang();
	vk_libretro_shutdown();
}

void *LibretroVulkanContext::GetAPIContext() { return vk; }

void LibretroVulkanContext::CreateDrawContext() {
	retro_hw_render_interface *vulkan;
	if (!Libretro::environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, (void **)&vulkan) || !vulkan) {
		ERROR_LOG(G3D, "Failed to get HW rendering interface!\n");
		return;
	}
	if (vulkan->interface_version != RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION) {
		ERROR_LOG(G3D, "HW render interface mismatch, expected %u, got %u!\n", RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION, vulkan->interface_version);
		return;
	}
	vk_libretro_set_hwrender_interface(vulkan);

	vk->ReinitSurface(PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);

	if (!vk->InitSwapchain()) {
		return;
	}

	draw_ = Draw::T3DCreateVulkanContext(vk, false);
}
