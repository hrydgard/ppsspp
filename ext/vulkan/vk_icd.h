//
// File: vk_icd.h
//
/*
 * Copyright (c) 2015-2016 The Khronos Group Inc.
 * Copyright (c) 2015-2016 Valve Corporation
 * Copyright (c) 2015-2016 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef VKICD_H
#define VKICD_H

#include "vulkan.h"

/*
 * Loader-ICD version negotiation API
 */
#define CURRENT_LOADER_ICD_INTERFACE_VERSION 3
#define MIN_SUPPORTED_LOADER_ICD_INTERFACE_VERSION 0
typedef VkResult (VKAPI_PTR *PFN_vkNegotiateLoaderICDInterfaceVersion)(uint32_t *pVersion);
/*
 * The ICD must reserve space for a pointer for the loader's dispatch
 * table, at the start of <each object>.
 * The ICD must initialize this variable using the SET_LOADER_MAGIC_VALUE macro.
 */

#define ICD_LOADER_MAGIC 0x01CDC0DE

typedef union {
    uintptr_t loaderMagic;
    void *loaderData;
} VK_LOADER_DATA;

static inline void set_loader_magic_value(void *pNewObject) {
    VK_LOADER_DATA *loader_info = (VK_LOADER_DATA *)pNewObject;
    loader_info->loaderMagic = ICD_LOADER_MAGIC;
}

static inline bool valid_loader_magic_value(void *pNewObject) {
    const VK_LOADER_DATA *loader_info = (VK_LOADER_DATA *)pNewObject;
    return (loader_info->loaderMagic & 0xffffffff) == ICD_LOADER_MAGIC;
}

/*
 * Windows and Linux ICDs will treat VkSurfaceKHR as a pointer to a struct that
 * contains the platform-specific connection and surface information.
 */
typedef enum {
    VK_ICD_WSI_PLATFORM_MIR,
    VK_ICD_WSI_PLATFORM_WAYLAND,
    VK_ICD_WSI_PLATFORM_WIN32,
    VK_ICD_WSI_PLATFORM_XCB,
    VK_ICD_WSI_PLATFORM_XLIB,
    VK_ICD_WSI_PLATFORM_DISPLAY
} VkIcdWsiPlatform;

typedef struct {
    VkIcdWsiPlatform platform;
} VkIcdSurfaceBase;

#ifdef VK_USE_PLATFORM_MIR_KHR
typedef struct {
    VkIcdSurfaceBase base;
    MirConnection *connection;
    MirSurface *mirSurface;
} VkIcdSurfaceMir;
#endif // VK_USE_PLATFORM_MIR_KHR

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
typedef struct {
    VkIcdSurfaceBase base;
    struct wl_display *display;
    struct wl_surface *surface;
} VkIcdSurfaceWayland;
#endif // VK_USE_PLATFORM_WAYLAND_KHR

#ifdef VK_USE_PLATFORM_WIN32_KHR
typedef struct {
    VkIcdSurfaceBase base;
    HINSTANCE hinstance;
    HWND hwnd;
} VkIcdSurfaceWin32;
#endif // VK_USE_PLATFORM_WIN32_KHR

#ifdef VK_USE_PLATFORM_XCB_KHR
typedef struct {
    VkIcdSurfaceBase base;
    xcb_connection_t *connection;
    xcb_window_t window;
} VkIcdSurfaceXcb;
#endif // VK_USE_PLATFORM_XCB_KHR

#ifdef VK_USE_PLATFORM_XLIB_KHR
typedef struct {
    VkIcdSurfaceBase base;
    Display *dpy;
    Window window;
} VkIcdSurfaceXlib;
#endif // VK_USE_PLATFORM_XLIB_KHR

#ifdef VK_USE_PLATFORM_ANDROID_KHR
typedef struct {
    ANativeWindow* window;
} VkIcdSurfaceAndroid;
#endif //VK_USE_PLATFORM_ANDROID_KHR

typedef struct {
    VkIcdSurfaceBase base;
    VkDisplayModeKHR displayMode;
    uint32_t planeIndex;
    uint32_t planeStackIndex;
    VkSurfaceTransformFlagBitsKHR transform;
    float globalAlpha;
    VkDisplayPlaneAlphaFlagBitsKHR alphaMode;
    VkExtent2D imageExtent;
} VkIcdSurfaceDisplay;

#endif // VKICD_H
