#include <SDL3/SDL.h>

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(MAC) || PPSSPP_PLATFORM(IOS)
#include "SDL/SDLCocoaMetalLayer.h"
#endif

#include "Common/GPU/MiscTypes.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/GraphicsContext.h"

#include "SDL/SDLUtil.h"

bool DetermineVulkanWindowSystem(SDL_Window *window, WindowDesc *desc, std::string *errorMessage) {
	_dbg_assert_(window);
	SDL_PropertiesID windowProps = SDL_GetWindowProperties(window);
	void *x11Display = SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
	if (x11Display != nullptr) {
		intptr_t x11Window = (intptr_t)SDL_GetNumberProperty(windowProps, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
#if defined(VK_USE_PLATFORM_XLIB_KHR)
		desc->winsys = WINDOWSYSTEM_XLIB;
		desc->data1 = x11Display;
		desc->data2 = (void *)x11Window;
#elif defined(VK_USE_PLATFORM_XCB_KHR)
		desc->winsys = WINDOWSYSTEM_XCB;
		desc->data1 = (void *)XGetXCBConnection((Display *)x11Display);
		desc->data2 = (void *)x11Window;
#endif
		return true;
	}
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
	void *waylandDisplay = SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
	void *waylandSurface = SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
	if (waylandDisplay != nullptr && waylandSurface != nullptr) {
		desc->winsys = WINDOWSYSTEM_WAYLAND;
		desc->data1 = waylandDisplay;
		desc->data2 = waylandSurface;
		return true;
	}
#elif defined(VK_USE_PLATFORM_METAL_EXT)
#if PPSSPP_PLATFORM(MAC)
	void *cocoaWindow = SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
	if (cocoaWindow != nullptr) {
		desc->winsys = WINDOWSYSTEM_METAL_EXT;
		desc->data1 = makeWindowMetalCompatible(cocoaWindow);
		desc->data2 = nullptr;
		return true;
	}
#else
	// This path is currently not used as we do not use SDL on iOS, but it is here for completeness.
	void *uikitWindow = SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
	if (uikitWindow != nullptr) {
		desc->winsys = WINDOWSYSTEM_METAL_EXT;
		desc->data1 = makeWindowMetalCompatible(uikitWindow);
		desc->data2 = nullptr;
		return true;
	}
#endif
#endif  // VK_USE_PLATFORM_METAL_EXT
	if (errorMessage) {
		*errorMessage = "Unable to determine Vulkan window system from SDL3 window properties";
	}
	return false;
}
