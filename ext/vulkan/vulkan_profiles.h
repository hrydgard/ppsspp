/**
 * Copyright (c) 2021-2022 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License")
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
 * DO NOT EDIT: This file is generated.
 */

#ifndef VULKAN_PROFILES_H_
#define VULKAN_PROFILES_H_ 1

#define VPAPI_ATTR

#ifdef __cplusplus
    extern "C" {
#endif

#include <vulkan/vulkan.h>

#if defined(VK_VERSION_1_0) && \
    defined(VK_EXT_swapchain_colorspace) && \
    defined(VK_GOOGLE_display_timing) && \
    defined(VK_KHR_android_surface) && \
    defined(VK_KHR_dedicated_allocation) && \
    defined(VK_KHR_descriptor_update_template) && \
    defined(VK_KHR_external_fence) && \
    defined(VK_KHR_external_fence_capabilities) && \
    defined(VK_KHR_external_fence_fd) && \
    defined(VK_KHR_external_memory) && \
    defined(VK_KHR_external_memory_capabilities) && \
    defined(VK_KHR_external_semaphore) && \
    defined(VK_KHR_external_semaphore_capabilities) && \
    defined(VK_KHR_external_semaphore_fd) && \
    defined(VK_KHR_get_memory_requirements2) && \
    defined(VK_KHR_get_physical_device_properties2) && \
    defined(VK_KHR_get_surface_capabilities2) && \
    defined(VK_KHR_incremental_present) && \
    defined(VK_KHR_maintenance1) && \
    defined(VK_KHR_storage_buffer_storage_class) && \
    defined(VK_KHR_surface) && \
    defined(VK_KHR_swapchain) && \
    defined(VK_KHR_variable_pointers)
#define VP_ANDROID_baseline_2021 1
#define VP_ANDROID_BASELINE_2021_NAME "VP_ANDROID_baseline_2021"
#define VP_ANDROID_BASELINE_2021_SPEC_VERSION 1
#define VP_ANDROID_BASELINE_2021_MIN_API_VERSION VK_MAKE_VERSION(1, 0, 68)
#endif

#if defined(VK_VERSION_1_3) && \
    defined(VK_KHR_global_priority)
#define VP_KHR_roadmap_2022 1
#define VP_KHR_ROADMAP_2022_NAME "VP_KHR_roadmap_2022"
#define VP_KHR_ROADMAP_2022_SPEC_VERSION 1
#define VP_KHR_ROADMAP_2022_MIN_API_VERSION VK_MAKE_VERSION(1, 3, 204)
#endif

#if defined(VK_VERSION_1_1) && \
    defined(VK_EXT_descriptor_indexing) && \
    defined(VK_EXT_host_query_reset) && \
    defined(VK_EXT_robustness2) && \
    defined(VK_EXT_scalar_block_layout) && \
    defined(VK_EXT_subgroup_size_control) && \
    defined(VK_EXT_texel_buffer_alignment) && \
    defined(VK_EXT_vertex_attribute_divisor) && \
    defined(VK_KHR_8bit_storage) && \
    defined(VK_KHR_create_renderpass2) && \
    defined(VK_KHR_depth_stencil_resolve) && \
    defined(VK_KHR_driver_properties) && \
    defined(VK_KHR_image_format_list) && \
    defined(VK_KHR_imageless_framebuffer) && \
    defined(VK_KHR_sampler_mirror_clamp_to_edge) && \
    defined(VK_KHR_shader_float16_int8) && \
    defined(VK_KHR_swapchain) && \
    defined(VK_KHR_swapchain_mutable_format) && \
    defined(VK_KHR_timeline_semaphore) && \
    defined(VK_KHR_uniform_buffer_standard_layout)
#define VP_LUNARG_desktop_portability_2021 1
#define VP_LUNARG_DESKTOP_PORTABILITY_2021_NAME "VP_LUNARG_desktop_portability_2021"
#define VP_LUNARG_DESKTOP_PORTABILITY_2021_SPEC_VERSION 1
#define VP_LUNARG_DESKTOP_PORTABILITY_2021_MIN_API_VERSION VK_MAKE_VERSION(1, 1, 142)
#endif

#if defined(VK_VERSION_1_1) && \
    defined(VK_EXT_descriptor_indexing) && \
    defined(VK_EXT_host_query_reset) && \
    defined(VK_EXT_robustness2) && \
    defined(VK_EXT_scalar_block_layout) && \
    defined(VK_EXT_subgroup_size_control) && \
    defined(VK_EXT_texel_buffer_alignment) && \
    defined(VK_EXT_vertex_attribute_divisor) && \
    defined(VK_KHR_8bit_storage) && \
    defined(VK_KHR_create_renderpass2) && \
    defined(VK_KHR_depth_stencil_resolve) && \
    defined(VK_KHR_driver_properties) && \
    defined(VK_KHR_image_format_list) && \
    defined(VK_KHR_imageless_framebuffer) && \
    defined(VK_KHR_portability_subset) && \
    defined(VK_KHR_sampler_mirror_clamp_to_edge) && \
    defined(VK_KHR_shader_float16_int8) && \
    defined(VK_KHR_swapchain) && \
    defined(VK_KHR_swapchain_mutable_format) && \
    defined(VK_KHR_timeline_semaphore) && \
    defined(VK_KHR_uniform_buffer_standard_layout)
#define VP_LUNARG_desktop_portability_2021_subset 1
#define VP_LUNARG_DESKTOP_PORTABILITY_2021_SUBSET_NAME "VP_LUNARG_desktop_portability_2021_subset"
#define VP_LUNARG_DESKTOP_PORTABILITY_2021_SUBSET_SPEC_VERSION 1
#define VP_LUNARG_DESKTOP_PORTABILITY_2021_SUBSET_MIN_API_VERSION VK_MAKE_VERSION(1, 1, 154)
#endif

#define VP_MAX_PROFILE_NAME_SIZE 256U

typedef struct VpProfileProperties {
    char        profileName[VP_MAX_PROFILE_NAME_SIZE];
    uint32_t    specVersion;
} VpProfileProperties;

typedef enum VpInstanceCreateFlagBits {
    // Default behavior:
    // - profile extensions are used (application must not specify extensions)

    // Merge application provided extension list and profile extension list
    VP_INSTANCE_CREATE_MERGE_EXTENSIONS_BIT = 0x00000001,

    // Use application provided extension list
    VP_INSTANCE_CREATE_OVERRIDE_EXTENSIONS_BIT = 0x00000002,

    VP_INSTANCE_CREATE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VpInstanceCreateFlagBits;
typedef VkFlags VpInstanceCreateFlags;

typedef struct VpInstanceCreateInfo {
    const VkInstanceCreateInfo* pCreateInfo;
    const VpProfileProperties*  pProfile;
    VpInstanceCreateFlags       flags;
} VpInstanceCreateInfo;

typedef enum VpDeviceCreateFlagBits {
    // Default behavior:
    // - profile extensions are used (application must not specify extensions)
    // - profile feature structures are used (application must not specify any of them) extended
    //   with any other application provided struct that isn't defined by the profile

    // Merge application provided extension list and profile extension list
    VP_DEVICE_CREATE_MERGE_EXTENSIONS_BIT = 0x00000001,

    // Use application provided extension list
    VP_DEVICE_CREATE_OVERRIDE_EXTENSIONS_BIT = 0x00000002,

    // Merge application provided versions of feature structures with the profile features
    // Currently unsupported, but is considered for future inclusion in which case the
    // default behavior could potentially be changed to merging as the currently defined
    // default behavior is forward-compatible with that
    // VP_DEVICE_CREATE_MERGE_FEATURES_BIT = 0x00000004,

    // Use application provided versions of feature structures but use the profile feature
    // structures when the application doesn't provide one (robust access disable flags are
    // ignored if the application overrides the corresponding feature structures)
    VP_DEVICE_CREATE_OVERRIDE_FEATURES_BIT = 0x00000008,

    // Only use application provided feature structures, don't add any profile specific
    // feature structures (robust access disable flags are ignored in this case and only the
    // application provided structures are used)
    VP_DEVICE_CREATE_OVERRIDE_ALL_FEATURES_BIT = 0x00000010,

    VP_DEVICE_CREATE_DISABLE_ROBUST_BUFFER_ACCESS_BIT = 0x00000020,
    VP_DEVICE_CREATE_DISABLE_ROBUST_IMAGE_ACCESS_BIT = 0x00000040,
    VP_DEVICE_CREATE_DISABLE_ROBUST_ACCESS =
        VP_DEVICE_CREATE_DISABLE_ROBUST_BUFFER_ACCESS_BIT | VP_DEVICE_CREATE_DISABLE_ROBUST_IMAGE_ACCESS_BIT,

    VP_DEVICE_CREATE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
} VpDeviceCreateFlagBits;
typedef VkFlags VpDeviceCreateFlags;

typedef struct VpDeviceCreateInfo {
    const VkDeviceCreateInfo*   pCreateInfo;
    const VpProfileProperties*  pProfile;
    VpDeviceCreateFlags         flags;
} VpDeviceCreateInfo;

// Query the list of available profiles in the library
VPAPI_ATTR VkResult vpGetProfiles(uint32_t *pPropertyCount, VpProfileProperties *pProperties);

// List the recommended fallback profiles of a profile
VPAPI_ATTR VkResult vpGetProfileFallbacks(const VpProfileProperties *pProfile, uint32_t *pPropertyCount, VpProfileProperties *pProperties);

// Check whether a profile is supported at the instance level
VPAPI_ATTR VkResult vpGetInstanceProfileSupport(const char *pLayerName, const VpProfileProperties *pProfile, VkBool32 *pSupported);

// Create a VkInstance with the profile instance extensions enabled
VPAPI_ATTR VkResult vpCreateInstance(const VpInstanceCreateInfo *pCreateInfo,
                                     const VkAllocationCallbacks *pAllocator, VkInstance *pInstance);

// Check whether a profile is supported by the physical device
VPAPI_ATTR VkResult vpGetPhysicalDeviceProfileSupport(VkInstance instance, VkPhysicalDevice physicalDevice,
                                                      const VpProfileProperties *pProfile, VkBool32 *pSupported);

// Create a VkDevice with the profile features and device extensions enabled
VPAPI_ATTR VkResult vpCreateDevice(VkPhysicalDevice physicalDevice, const VpDeviceCreateInfo *pCreateInfo,
                                   const VkAllocationCallbacks *pAllocator, VkDevice *pDevice);

// Query the list of instance extensions of a profile
VPAPI_ATTR VkResult vpGetProfileInstanceExtensionProperties(const VpProfileProperties *pProfile, uint32_t *pPropertyCount,
                                                            VkExtensionProperties *pProperties);

// Query the list of device extensions of a profile
VPAPI_ATTR VkResult vpGetProfileDeviceExtensionProperties(const VpProfileProperties *pProfile, uint32_t *pPropertyCount,
                                                          VkExtensionProperties *pProperties);

// Fill the feature structures with the requirements of a profile
VPAPI_ATTR void vpGetProfileFeatures(const VpProfileProperties *pProfile, void *pNext);

// Query the list of feature structure types specified by the profile
VPAPI_ATTR VkResult vpGetProfileFeatureStructureTypes(const VpProfileProperties *pProfile, uint32_t *pStructureTypeCount,
                                                      VkStructureType *pStructureTypes);

// Fill the property structures with the requirements of a profile
VPAPI_ATTR void vpGetProfileProperties(const VpProfileProperties *pProfile, void *pNext);

// Query the list of property structure types specified by the profile
VPAPI_ATTR VkResult vpGetProfilePropertyStructureTypes(const VpProfileProperties *pProfile, uint32_t *pStructureTypeCount,
                                                       VkStructureType *pStructureTypes);

// Query the requirements of queue families by a profile
VPAPI_ATTR VkResult vpGetProfileQueueFamilyProperties(const VpProfileProperties *pProfile, uint32_t *pPropertyCount,
                                                      VkQueueFamilyProperties2KHR *pProperties);

// Query the list of query family structure types specified by the profile
VPAPI_ATTR VkResult vpGetProfileQueueFamilyStructureTypes(const VpProfileProperties *pProfile, uint32_t *pStructureTypeCount,
                                                          VkStructureType *pStructureTypes);

// Query the list of formats with specified requirements by a profile
VPAPI_ATTR VkResult vpGetProfileFormats(const VpProfileProperties *pProfile, uint32_t *pFormatCount, VkFormat *pFormats);

// Query the requirements of a format for a profile
VPAPI_ATTR void vpGetProfileFormatProperties(const VpProfileProperties *pProfile, VkFormat format, void *pNext);

// Query the list of format structure types specified by the profile
VPAPI_ATTR VkResult vpGetProfileFormatStructureTypes(const VpProfileProperties *pProfile, uint32_t *pStructureTypeCount,
                                                     VkStructureType *pStructureTypes);

#ifdef __cplusplus
}
#endif

#endif // VULKAN_PROFILES_H_
