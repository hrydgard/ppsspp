#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "base/NativeApp.h"
#include "base/display.h"
#include "thin3d/thin3d.h"
#include "thin3d/thin3d_create.h"
#include "util/text/parsers.h"

#include "Core/System.h"
#include "SDLVulkanGraphicsContext.h"

bool SDLVulkanGraphicsContext::Init(SDL_Window *&window, int x, int y, int mode, std::string *error_message) {
	window = SDL_CreateWindow("Initializing Vulkan...", x, y, pixel_xres, pixel_yres, mode);
	if (!window) {
		fprintf(stderr, "Error creating SDL window: %s\n", SDL_GetError());
		exit(1);
	}

	init_glslang();

	g_LogOptions.breakOnError = true;
	g_LogOptions.breakOnWarning = true;
	g_LogOptions.msgBoxOnError = false;

	Version gitVer(PPSSPP_GIT_VERSION);

	vulkan_ = new VulkanContext();
	if (vulkan_->InitError().size()) {
		*error_message = vulkan_->InitError();
		delete vulkan_;
		vulkan_ = nullptr;
		return false;
	}

	int vulkanFlags = VULKAN_FLAG_PRESENT_MAILBOX;
	// vulkanFlags |= VULKAN_FLAG_VALIDATE;
	VulkanContext::CreateInfo info{};
	info.app_name = "PPSSPP";
	info.app_ver = gitVer.ToInteger();
	info.flags = vulkanFlags;
	if (VK_SUCCESS != vulkan_->CreateInstance(info)) {
		*error_message = vulkan_->InitError();
		delete vulkan_;
		vulkan_ = nullptr;
		return false;
	}
	vulkan_->ChooseDevice(vulkan_->GetBestPhysicalDevice());
	if (vulkan_->CreateDevice() != VK_SUCCESS) {
		*error_message = vulkan_->InitError();
		delete vulkan_;
		vulkan_ = nullptr;
		return false;
	}

#if !defined(__APPLE__)
	SDL_SysWMinfo sys_info{};
	SDL_VERSION(&sys_info.version); //Set SDL version
	if (!SDL_GetWindowWMInfo(window, &sys_info)) {
		fprintf(stderr, "Error getting SDL window wm info: %s\n", SDL_GetError());
		exit(1);
	}
	switch (sys_info.subsystem) {
	case SDL_SYSWM_X11:
#if defined(VK_USE_PLATFORM_XLIB_KHR)
		vulkan_->InitSurface(WINDOWSYSTEM_XLIB, (void*)sys_info.info.x11.display,
				(void *)(intptr_t)sys_info.info.x11.window, pixel_xres, pixel_yres);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
		vulkan_->InitSurface(WINDOWSYSTEM_XCB, (void*)XGetXCBConnection(sys_info.info.x11.display),
				(void *)(intptr_t)sys_info.info.x11.window, pixel_xres, pixel_yres);
#endif
		break;
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
	case SDL_SYSWM_WAYLAND:
		vulkan_->InitSurface(WINDOWSYSTEM_WAYLAND, (void*)sys_info.info.wl.display,
				(void *)sys_info.info.wl.surface, pixel_xres, pixel_yres);
		break;
#endif
	default:
		fprintf(stderr, "Vulkan subsystem %d not supported\n", sys_info.subsystem);
		exit(1);
		break;
	}
#endif

	if (!vulkan_->InitObjects()) {
		*error_message = vulkan_->InitError();
		Shutdown();
		return false;
	}

	draw_ = Draw::T3DCreateVulkanContext(vulkan_, false);
	SetGPUBackend(GPUBackend::VULKAN);
	bool success = draw_->CreatePresets();
	assert(success);
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());

	return true;
}

void SDLVulkanGraphicsContext::Shutdown() {
	if (draw_)
		draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
	delete draw_;
	draw_ = nullptr;
	vulkan_->WaitUntilQueueIdle();
	vulkan_->DestroyObjects();
	vulkan_->DestroyDevice();
	vulkan_->DestroyDebugMsgCallback();
	vulkan_->DestroyInstance();
	delete vulkan_;
	vulkan_ = nullptr;
	finalize_glslang();
}
