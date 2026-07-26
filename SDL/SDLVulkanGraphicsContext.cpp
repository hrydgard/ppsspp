#include "ppsspp_config.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Common/System/System.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Display.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Common/Data/Text/Parsers.h"
#include "GPU/Vulkan/VulkanUtil.h"

#include "Core/System.h"
#include <SDL3/SDL_vulkan.h>
#include "SDLVulkanGraphicsContext.h"

#if defined(VK_USE_PLATFORM_METAL_EXT)
#include "SDLCocoaMetalLayer.h"
#endif

#ifdef _DEBUG
static const bool g_Validate = true;
#else
static const bool g_Validate = false;
#endif

bool SDLVulkanGraphicsContext::InitAPI(void *wnd, std::string *deviceName, std::string *errorMessage) {
	init_glslang();
	errorMessage->clear();

	std::string errorStr;
	if (!VulkanLoad(&errorStr)) {
		*errorMessage = "Failed to load Vulkan driver library: " + errorStr;
		return false;
	}

	vulkan_ = new VulkanContext();

	VulkanContext::CreateInfo info{};
	InitVulkanCreateInfoFromConfig(&info);
	if (VK_SUCCESS != vulkan_->CreateInstance(info)) {
		*errorMessage = vulkan_->InitError();
		delete vulkan_;
		vulkan_ = nullptr;
		return false;
	}

	int deviceNum = vulkan_->GetPhysicalDeviceByName(*deviceName);
	if (deviceNum < 0) {
		deviceNum = vulkan_->GetBestPhysicalDevice();
		if (!deviceName->empty()) {
			*deviceName = vulkan_->GetPhysicalDeviceProperties(deviceNum).properties.deviceName;
		}
	}

	if (vulkan_->CreateDevice(deviceNum) != VK_SUCCESS) {
		*errorMessage = vulkan_->InitError();
		delete vulkan_;
		vulkan_ = nullptr;
		return false;
	}

	return true;
}

bool SDLVulkanGraphicsContext::InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *errorMessage) {
	SDL_Window *window = *(SDL_Window **)data1;
	errorMessage->clear();

	_dbg_assert_(window);
	_dbg_assert_(vulkan_);

	vulkan_->SetCbGetDrawSize([window]() {
		int w=1,h=1;
		SDL_GetWindowSizeInPixels(window, &w, &h);
		return VkExtent2D {(uint32_t)w, (uint32_t)h};
	});

	SDL_PropertiesID windowProps = SDL_GetWindowProperties(window);
	bool surfaceInitialized = false;
	void *x11Display = SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
	if (x11Display != nullptr) {
		intptr_t x11Window = (intptr_t)SDL_GetNumberProperty(windowProps, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
#if defined(VK_USE_PLATFORM_XLIB_KHR)
		vulkan_->InitSurface(WINDOWSYSTEM_XLIB, x11Display, (void *)x11Window);
		surfaceInitialized = true;
#elif defined(VK_USE_PLATFORM_XCB_KHR)
		vulkan_->InitSurface(WINDOWSYSTEM_XCB, (void *)XGetXCBConnection((Display *)x11Display), (void *)x11Window);
		surfaceInitialized = true;
#endif
 	}
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
	if (!surfaceInitialized) {
		void *waylandDisplay = SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
		void *waylandSurface = SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
		if (waylandDisplay != nullptr && waylandSurface != nullptr) {
			vulkan_->InitSurface(WINDOWSYSTEM_WAYLAND, waylandDisplay, waylandSurface);
 			surfaceInitialized = true;
		}
 	}
#endif
#if defined(VK_USE_PLATFORM_METAL_EXT)
#if PPSSPP_PLATFORM(MAC)
	if (!surfaceInitialized) {
		void *cocoaWindow = SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
		if (cocoaWindow != nullptr) {
			vulkan_->InitSurface(WINDOWSYSTEM_METAL_EXT, makeWindowMetalCompatible(cocoaWindow), nullptr);
			surfaceInitialized = true;
		}
 	}
#else
	if (!surfaceInitialized) {
		void *uikitWindow = SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
		if (uikitWindow != nullptr) {
			vulkan_->InitSurface(WINDOWSYSTEM_METAL_EXT, makeWindowMetalCompatible(uikitWindow), nullptr);
			surfaceInitialized = true;
		}
 	}
#endif
#endif
	if (!surfaceInitialized) {
		fprintf(stderr, "Unable to determine Vulkan window system from SDL3 window properties\n");
		exit(1);
	}

	VkPresentModeKHR presentMode = ConfigPresentModeToVulkan(draw_);
	if (!vulkan_->InitSwapchain(presentMode)) {
		*errorMessage = vulkan_->InitError();
		return false;
	}

	bool useMultiThreading = g_Config.bRenderMultiThreading;
	if (g_Config.iInflightFrames == 1) {
		useMultiThreading = false;
	}
	draw_ = Draw::T3DCreateVulkanContext(vulkan_, useMultiThreading);

	SetGPUBackend(GPUBackend::VULKAN);
	bool success = draw_->CreatePresets();
	_assert_(success);
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());

	renderManager_ = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager_->SetInflightFrames(g_Config.iInflightFrames);
	return true;
}

void SDLVulkanGraphicsContext::ShutdownSurface() {
	if (draw_)
		draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	delete draw_;
	draw_ = nullptr;
	vulkan_->WaitUntilQueueIdle();
	vulkan_->DestroySwapchain();
	vulkan_->DestroySurface();
}

void SDLVulkanGraphicsContext::ShutdownAPI() {
	_dbg_assert_(!draw_);
	vulkan_->DestroyDevice();
	vulkan_->DestroyInstance();
	delete vulkan_;
	vulkan_ = nullptr;
	finalize_glslang();
}

void SDLVulkanGraphicsContext::Resize() {
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	// NOTE: Removing DestroySwapchain here causes a double re-create on MacOS with MoltenVK, for some reason.
	// It's like passing on oldSwapchain doesn't really work as expected.
	vulkan_->DestroySwapchain();
	VkPresentModeKHR presentMode = ConfigPresentModeToVulkan(draw_);
	vulkan_->InitSwapchain(presentMode);
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
}

void SDLVulkanGraphicsContext::Poll() {
	// Check for existing swapchain to avoid issues during shutdown.
	if (vulkan_->GetSwapchain() && renderManager_->NeedsSwapchainRecreate()) {
		Resize();
	}
}
