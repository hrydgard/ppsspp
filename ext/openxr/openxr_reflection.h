#ifndef OPENXR_REFLECTION_H_
#define OPENXR_REFLECTION_H_ 1

/*
** Copyright (c) 2017-2021, The Khronos Group Inc.
**
** SPDX-License-Identifier: Apache-2.0 OR MIT
*/

/*
** This header is generated from the Khronos OpenXR XML API Registry.
**
*/

#include "openxr.h"

/*
This file contains expansion macros (X Macros) for OpenXR enumerations and structures.
Example of how to use expansion macros to make an enum-to-string function:

#define XR_ENUM_CASE_STR(name, val) case name: return #name;
#define XR_ENUM_STR(enumType)                         \
    constexpr const char* XrEnumStr(enumType e) {     \
        switch (e) {                                  \
            XR_LIST_ENUM_##enumType(XR_ENUM_CASE_STR) \
            default: return "Unknown";                \
        }                                             \
    }                                                 \

XR_ENUM_STR(XrResult);
*/

#define XR_LIST_ENUM_XrResult(_) \
    _(XR_SUCCESS, 0) \
    _(XR_TIMEOUT_EXPIRED, 1) \
    _(XR_SESSION_LOSS_PENDING, 3) \
    _(XR_EVENT_UNAVAILABLE, 4) \
    _(XR_SPACE_BOUNDS_UNAVAILABLE, 7) \
    _(XR_SESSION_NOT_FOCUSED, 8) \
    _(XR_FRAME_DISCARDED, 9) \
    _(XR_ERROR_VALIDATION_FAILURE, -1) \
    _(XR_ERROR_RUNTIME_FAILURE, -2) \
    _(XR_ERROR_OUT_OF_MEMORY, -3) \
    _(XR_ERROR_API_VERSION_UNSUPPORTED, -4) \
    _(XR_ERROR_INITIALIZATION_FAILED, -6) \
    _(XR_ERROR_FUNCTION_UNSUPPORTED, -7) \
    _(XR_ERROR_FEATURE_UNSUPPORTED, -8) \
    _(XR_ERROR_EXTENSION_NOT_PRESENT, -9) \
    _(XR_ERROR_LIMIT_REACHED, -10) \
    _(XR_ERROR_SIZE_INSUFFICIENT, -11) \
    _(XR_ERROR_HANDLE_INVALID, -12) \
    _(XR_ERROR_INSTANCE_LOST, -13) \
    _(XR_ERROR_SESSION_RUNNING, -14) \
    _(XR_ERROR_SESSION_NOT_RUNNING, -16) \
    _(XR_ERROR_SESSION_LOST, -17) \
    _(XR_ERROR_SYSTEM_INVALID, -18) \
    _(XR_ERROR_PATH_INVALID, -19) \
    _(XR_ERROR_PATH_COUNT_EXCEEDED, -20) \
    _(XR_ERROR_PATH_FORMAT_INVALID, -21) \
    _(XR_ERROR_PATH_UNSUPPORTED, -22) \
    _(XR_ERROR_LAYER_INVALID, -23) \
    _(XR_ERROR_LAYER_LIMIT_EXCEEDED, -24) \
    _(XR_ERROR_SWAPCHAIN_RECT_INVALID, -25) \
    _(XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED, -26) \
    _(XR_ERROR_ACTION_TYPE_MISMATCH, -27) \
    _(XR_ERROR_SESSION_NOT_READY, -28) \
    _(XR_ERROR_SESSION_NOT_STOPPING, -29) \
    _(XR_ERROR_TIME_INVALID, -30) \
    _(XR_ERROR_REFERENCE_SPACE_UNSUPPORTED, -31) \
    _(XR_ERROR_FILE_ACCESS_ERROR, -32) \
    _(XR_ERROR_FILE_CONTENTS_INVALID, -33) \
    _(XR_ERROR_FORM_FACTOR_UNSUPPORTED, -34) \
    _(XR_ERROR_FORM_FACTOR_UNAVAILABLE, -35) \
    _(XR_ERROR_API_LAYER_NOT_PRESENT, -36) \
    _(XR_ERROR_CALL_ORDER_INVALID, -37) \
    _(XR_ERROR_GRAPHICS_DEVICE_INVALID, -38) \
    _(XR_ERROR_POSE_INVALID, -39) \
    _(XR_ERROR_INDEX_OUT_OF_RANGE, -40) \
    _(XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED, -41) \
    _(XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED, -42) \
    _(XR_ERROR_NAME_DUPLICATED, -44) \
    _(XR_ERROR_NAME_INVALID, -45) \
    _(XR_ERROR_ACTIONSET_NOT_ATTACHED, -46) \
    _(XR_ERROR_ACTIONSETS_ALREADY_ATTACHED, -47) \
    _(XR_ERROR_LOCALIZED_NAME_DUPLICATED, -48) \
    _(XR_ERROR_LOCALIZED_NAME_INVALID, -49) \
    _(XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING, -50) \
    _(XR_ERROR_ANDROID_THREAD_SETTINGS_ID_INVALID_KHR, -1000003000) \
    _(XR_ERROR_ANDROID_THREAD_SETTINGS_FAILURE_KHR, -1000003001) \
    _(XR_ERROR_CREATE_SPATIAL_ANCHOR_FAILED_MSFT, -1000039001) \
    _(XR_ERROR_SECONDARY_VIEW_CONFIGURATION_TYPE_NOT_ENABLED_MSFT, -1000053000) \
    _(XR_ERROR_CONTROLLER_MODEL_KEY_INVALID_MSFT, -1000055000) \
    _(XR_ERROR_DISPLAY_REFRESH_RATE_UNSUPPORTED_FB, -1000101000) \
    _(XR_ERROR_COLOR_SPACE_UNSUPPORTED_FB, -1000108000) \
    _(XR_RESULT_MAX_ENUM, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrStructureType(_) \
    _(XR_TYPE_UNKNOWN, 0) \
    _(XR_TYPE_API_LAYER_PROPERTIES, 1) \
    _(XR_TYPE_EXTENSION_PROPERTIES, 2) \
    _(XR_TYPE_INSTANCE_CREATE_INFO, 3) \
    _(XR_TYPE_SYSTEM_GET_INFO, 4) \
    _(XR_TYPE_SYSTEM_PROPERTIES, 5) \
    _(XR_TYPE_VIEW_LOCATE_INFO, 6) \
    _(XR_TYPE_VIEW, 7) \
    _(XR_TYPE_SESSION_CREATE_INFO, 8) \
    _(XR_TYPE_SWAPCHAIN_CREATE_INFO, 9) \
    _(XR_TYPE_SESSION_BEGIN_INFO, 10) \
    _(XR_TYPE_VIEW_STATE, 11) \
    _(XR_TYPE_FRAME_END_INFO, 12) \
    _(XR_TYPE_HAPTIC_VIBRATION, 13) \
    _(XR_TYPE_EVENT_DATA_BUFFER, 16) \
    _(XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING, 17) \
    _(XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED, 18) \
    _(XR_TYPE_ACTION_STATE_BOOLEAN, 23) \
    _(XR_TYPE_ACTION_STATE_FLOAT, 24) \
    _(XR_TYPE_ACTION_STATE_VECTOR2F, 25) \
    _(XR_TYPE_ACTION_STATE_POSE, 27) \
    _(XR_TYPE_ACTION_SET_CREATE_INFO, 28) \
    _(XR_TYPE_ACTION_CREATE_INFO, 29) \
    _(XR_TYPE_INSTANCE_PROPERTIES, 32) \
    _(XR_TYPE_FRAME_WAIT_INFO, 33) \
    _(XR_TYPE_COMPOSITION_LAYER_PROJECTION, 35) \
    _(XR_TYPE_COMPOSITION_LAYER_QUAD, 36) \
    _(XR_TYPE_REFERENCE_SPACE_CREATE_INFO, 37) \
    _(XR_TYPE_ACTION_SPACE_CREATE_INFO, 38) \
    _(XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING, 40) \
    _(XR_TYPE_VIEW_CONFIGURATION_VIEW, 41) \
    _(XR_TYPE_SPACE_LOCATION, 42) \
    _(XR_TYPE_SPACE_VELOCITY, 43) \
    _(XR_TYPE_FRAME_STATE, 44) \
    _(XR_TYPE_VIEW_CONFIGURATION_PROPERTIES, 45) \
    _(XR_TYPE_FRAME_BEGIN_INFO, 46) \
    _(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW, 48) \
    _(XR_TYPE_EVENT_DATA_EVENTS_LOST, 49) \
    _(XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING, 51) \
    _(XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED, 52) \
    _(XR_TYPE_INTERACTION_PROFILE_STATE, 53) \
    _(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, 55) \
    _(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, 56) \
    _(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, 57) \
    _(XR_TYPE_ACTION_STATE_GET_INFO, 58) \
    _(XR_TYPE_HAPTIC_ACTION_INFO, 59) \
    _(XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, 60) \
    _(XR_TYPE_ACTIONS_SYNC_INFO, 61) \
    _(XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO, 62) \
    _(XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO, 63) \
    _(XR_TYPE_COMPOSITION_LAYER_CUBE_KHR, 1000006000) \
    _(XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR, 1000008000) \
    _(XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR, 1000010000) \
    _(XR_TYPE_VULKAN_SWAPCHAIN_FORMAT_LIST_CREATE_INFO_KHR, 1000014000) \
    _(XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT, 1000015000) \
    _(XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR, 1000017000) \
    _(XR_TYPE_COMPOSITION_LAYER_EQUIRECT_KHR, 1000018000) \
    _(XR_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, 1000019000) \
    _(XR_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT, 1000019001) \
    _(XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT, 1000019002) \
    _(XR_TYPE_DEBUG_UTILS_LABEL_EXT, 1000019003) \
    _(XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR, 1000023000) \
    _(XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR, 1000023001) \
    _(XR_TYPE_GRAPHICS_BINDING_OPENGL_XCB_KHR, 1000023002) \
    _(XR_TYPE_GRAPHICS_BINDING_OPENGL_WAYLAND_KHR, 1000023003) \
    _(XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR, 1000023004) \
    _(XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR, 1000023005) \
    _(XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR, 1000024001) \
    _(XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR, 1000024002) \
    _(XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR, 1000024003) \
    _(XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR, 1000025000) \
    _(XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR, 1000025001) \
    _(XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR, 1000025002) \
    _(XR_TYPE_GRAPHICS_BINDING_D3D11_KHR, 1000027000) \
    _(XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR, 1000027001) \
    _(XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR, 1000027002) \
    _(XR_TYPE_GRAPHICS_BINDING_D3D12_KHR, 1000028000) \
    _(XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR, 1000028001) \
    _(XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR, 1000028002) \
    _(XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT, 1000030000) \
    _(XR_TYPE_EYE_GAZE_SAMPLE_TIME_EXT, 1000030001) \
    _(XR_TYPE_VISIBILITY_MASK_KHR, 1000031000) \
    _(XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR, 1000031001) \
    _(XR_TYPE_SESSION_CREATE_INFO_OVERLAY_EXTX, 1000033000) \
    _(XR_TYPE_EVENT_DATA_MAIN_SESSION_VISIBILITY_CHANGED_EXTX, 1000033003) \
    _(XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR, 1000034000) \
    _(XR_TYPE_SPATIAL_ANCHOR_CREATE_INFO_MSFT, 1000039000) \
    _(XR_TYPE_SPATIAL_ANCHOR_SPACE_CREATE_INFO_MSFT, 1000039001) \
    _(XR_TYPE_VIEW_CONFIGURATION_DEPTH_RANGE_EXT, 1000046000) \
    _(XR_TYPE_GRAPHICS_BINDING_EGL_MNDX, 1000048004) \
    _(XR_TYPE_SPATIAL_GRAPH_NODE_SPACE_CREATE_INFO_MSFT, 1000049000) \
    _(XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT, 1000051000) \
    _(XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT, 1000051001) \
    _(XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT, 1000051002) \
    _(XR_TYPE_HAND_JOINT_LOCATIONS_EXT, 1000051003) \
    _(XR_TYPE_HAND_JOINT_VELOCITIES_EXT, 1000051004) \
    _(XR_TYPE_SYSTEM_HAND_TRACKING_MESH_PROPERTIES_MSFT, 1000052000) \
    _(XR_TYPE_HAND_MESH_SPACE_CREATE_INFO_MSFT, 1000052001) \
    _(XR_TYPE_HAND_MESH_UPDATE_INFO_MSFT, 1000052002) \
    _(XR_TYPE_HAND_MESH_MSFT, 1000052003) \
    _(XR_TYPE_HAND_POSE_TYPE_INFO_MSFT, 1000052004) \
    _(XR_TYPE_SECONDARY_VIEW_CONFIGURATION_SESSION_BEGIN_INFO_MSFT, 1000053000) \
    _(XR_TYPE_SECONDARY_VIEW_CONFIGURATION_STATE_MSFT, 1000053001) \
    _(XR_TYPE_SECONDARY_VIEW_CONFIGURATION_FRAME_STATE_MSFT, 1000053002) \
    _(XR_TYPE_SECONDARY_VIEW_CONFIGURATION_FRAME_END_INFO_MSFT, 1000053003) \
    _(XR_TYPE_SECONDARY_VIEW_CONFIGURATION_LAYER_INFO_MSFT, 1000053004) \
    _(XR_TYPE_SECONDARY_VIEW_CONFIGURATION_SWAPCHAIN_CREATE_INFO_MSFT, 1000053005) \
    _(XR_TYPE_CONTROLLER_MODEL_KEY_STATE_MSFT, 1000055000) \
    _(XR_TYPE_CONTROLLER_MODEL_NODE_PROPERTIES_MSFT, 1000055001) \
    _(XR_TYPE_CONTROLLER_MODEL_PROPERTIES_MSFT, 1000055002) \
    _(XR_TYPE_CONTROLLER_MODEL_NODE_STATE_MSFT, 1000055003) \
    _(XR_TYPE_CONTROLLER_MODEL_STATE_MSFT, 1000055004) \
    _(XR_TYPE_VIEW_CONFIGURATION_VIEW_FOV_EPIC, 1000059000) \
    _(XR_TYPE_HOLOGRAPHIC_WINDOW_ATTACHMENT_MSFT, 1000063000) \
    _(XR_TYPE_ANDROID_SURFACE_SWAPCHAIN_CREATE_INFO_FB, 1000070000) \
    _(XR_TYPE_INTERACTION_PROFILE_ANALOG_THRESHOLD_VALVE, 1000079000) \
    _(XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR, 1000089000) \
    _(XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR, 1000090000) \
    _(XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR, 1000090001) \
    _(XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR, 1000090003) \
    _(XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR, 1000091000) \
    _(XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB, 1000101000) \
    _(XR_TYPE_SYSTEM_COLOR_SPACE_PROPERTIES_FB, 1000108000) \
    _(XR_TYPE_BINDING_MODIFICATIONS_KHR, 1000120000) \
    _(XR_STRUCTURE_TYPE_MAX_ENUM, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrFormFactor(_) \
    _(XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY, 1) \
    _(XR_FORM_FACTOR_HANDHELD_DISPLAY, 2) \
    _(XR_FORM_FACTOR_MAX_ENUM, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrViewConfigurationType(_) \
    _(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO, 1) \
    _(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2) \
    _(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO, 1000037000) \
    _(XR_VIEW_CONFIGURATION_TYPE_SECONDARY_MONO_FIRST_PERSON_OBSERVER_MSFT, 1000054000) \
    _(XR_VIEW_CONFIGURATION_TYPE_MAX_ENUM, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrEnvironmentBlendMode(_) \
    _(XR_ENVIRONMENT_BLEND_MODE_OPAQUE, 1) \
    _(XR_ENVIRONMENT_BLEND_MODE_ADDITIVE, 2) \
    _(XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND, 3) \
    _(XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrReferenceSpaceType(_) \
    _(XR_REFERENCE_SPACE_TYPE_VIEW, 1) \
    _(XR_REFERENCE_SPACE_TYPE_LOCAL, 2) \
    _(XR_REFERENCE_SPACE_TYPE_STAGE, 3) \
    _(XR_REFERENCE_SPACE_TYPE_UNBOUNDED_MSFT, 1000038000) \
    _(XR_REFERENCE_SPACE_TYPE_MAX_ENUM, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrActionType(_) \
    _(XR_ACTION_TYPE_BOOLEAN_INPUT, 1) \
    _(XR_ACTION_TYPE_FLOAT_INPUT, 2) \
    _(XR_ACTION_TYPE_VECTOR2F_INPUT, 3) \
    _(XR_ACTION_TYPE_POSE_INPUT, 4) \
    _(XR_ACTION_TYPE_VIBRATION_OUTPUT, 100) \
    _(XR_ACTION_TYPE_MAX_ENUM, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrEyeVisibility(_) \
    _(XR_EYE_VISIBILITY_BOTH, 0) \
    _(XR_EYE_VISIBILITY_LEFT, 1) \
    _(XR_EYE_VISIBILITY_RIGHT, 2) \
    _(XR_EYE_VISIBILITY_MAX_ENUM, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrSessionState(_) \
    _(XR_SESSION_STATE_UNKNOWN, 0) \
    _(XR_SESSION_STATE_IDLE, 1) \
    _(XR_SESSION_STATE_READY, 2) \
    _(XR_SESSION_STATE_SYNCHRONIZED, 3) \
    _(XR_SESSION_STATE_VISIBLE, 4) \
    _(XR_SESSION_STATE_FOCUSED, 5) \
    _(XR_SESSION_STATE_STOPPING, 6) \
    _(XR_SESSION_STATE_LOSS_PENDING, 7) \
    _(XR_SESSION_STATE_EXITING, 8) \
    _(XR_SESSION_STATE_MAX_ENUM, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrObjectType(_) \
    _(XR_OBJECT_TYPE_UNKNOWN, 0) \
    _(XR_OBJECT_TYPE_INSTANCE, 1) \
    _(XR_OBJECT_TYPE_SESSION, 2) \
    _(XR_OBJECT_TYPE_SWAPCHAIN, 3) \
    _(XR_OBJECT_TYPE_SPACE, 4) \
    _(XR_OBJECT_TYPE_ACTION_SET, 5) \
    _(XR_OBJECT_TYPE_ACTION, 6) \
    _(XR_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT, 1000019000) \
    _(XR_OBJECT_TYPE_SPATIAL_ANCHOR_MSFT, 1000039000) \
    _(XR_OBJECT_TYPE_HAND_TRACKER_EXT, 1000051000) \
    _(XR_OBJECT_TYPE_MAX_ENUM, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrAndroidThreadTypeKHR(_) \
    _(XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR, 1) \
    _(XR_ANDROID_THREAD_TYPE_APPLICATION_WORKER_KHR, 2) \
    _(XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR, 3) \
    _(XR_ANDROID_THREAD_TYPE_RENDERER_WORKER_KHR, 4) \
    _(XR_ANDROID_THREAD_TYPE_MAX_ENUM_KHR, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrVisibilityMaskTypeKHR(_) \
    _(XR_VISIBILITY_MASK_TYPE_HIDDEN_TRIANGLE_MESH_KHR, 1) \
    _(XR_VISIBILITY_MASK_TYPE_VISIBLE_TRIANGLE_MESH_KHR, 2) \
    _(XR_VISIBILITY_MASK_TYPE_LINE_LOOP_KHR, 3) \
    _(XR_VISIBILITY_MASK_TYPE_MAX_ENUM_KHR, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrPerfSettingsDomainEXT(_) \
    _(XR_PERF_SETTINGS_DOMAIN_CPU_EXT, 1) \
    _(XR_PERF_SETTINGS_DOMAIN_GPU_EXT, 2) \
    _(XR_PERF_SETTINGS_DOMAIN_MAX_ENUM_EXT, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrPerfSettingsSubDomainEXT(_) \
    _(XR_PERF_SETTINGS_SUB_DOMAIN_COMPOSITING_EXT, 1) \
    _(XR_PERF_SETTINGS_SUB_DOMAIN_RENDERING_EXT, 2) \
    _(XR_PERF_SETTINGS_SUB_DOMAIN_THERMAL_EXT, 3) \
    _(XR_PERF_SETTINGS_SUB_DOMAIN_MAX_ENUM_EXT, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrPerfSettingsLevelEXT(_) \
    _(XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT, 0) \
    _(XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT, 25) \
    _(XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT, 50) \
    _(XR_PERF_SETTINGS_LEVEL_BOOST_EXT, 75) \
    _(XR_PERF_SETTINGS_LEVEL_MAX_ENUM_EXT, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrPerfSettingsNotificationLevelEXT(_) \
    _(XR_PERF_SETTINGS_NOTIF_LEVEL_NORMAL_EXT, 0) \
    _(XR_PERF_SETTINGS_NOTIF_LEVEL_WARNING_EXT, 25) \
    _(XR_PERF_SETTINGS_NOTIF_LEVEL_IMPAIRED_EXT, 75) \
    _(XR_PERF_SETTINGS_NOTIFICATION_LEVEL_MAX_ENUM_EXT, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrSpatialGraphNodeTypeMSFT(_) \
    _(XR_SPATIAL_GRAPH_NODE_TYPE_STATIC_MSFT, 1) \
    _(XR_SPATIAL_GRAPH_NODE_TYPE_DYNAMIC_MSFT, 2) \
    _(XR_SPATIAL_GRAPH_NODE_TYPE_MAX_ENUM_MSFT, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrHandEXT(_) \
    _(XR_HAND_LEFT_EXT, 1) \
    _(XR_HAND_RIGHT_EXT, 2) \
    _(XR_HAND_MAX_ENUM_EXT, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrHandJointEXT(_) \
    _(XR_HAND_JOINT_PALM_EXT, 0) \
    _(XR_HAND_JOINT_WRIST_EXT, 1) \
    _(XR_HAND_JOINT_THUMB_METACARPAL_EXT, 2) \
    _(XR_HAND_JOINT_THUMB_PROXIMAL_EXT, 3) \
    _(XR_HAND_JOINT_THUMB_DISTAL_EXT, 4) \
    _(XR_HAND_JOINT_THUMB_TIP_EXT, 5) \
    _(XR_HAND_JOINT_INDEX_METACARPAL_EXT, 6) \
    _(XR_HAND_JOINT_INDEX_PROXIMAL_EXT, 7) \
    _(XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, 8) \
    _(XR_HAND_JOINT_INDEX_DISTAL_EXT, 9) \
    _(XR_HAND_JOINT_INDEX_TIP_EXT, 10) \
    _(XR_HAND_JOINT_MIDDLE_METACARPAL_EXT, 11) \
    _(XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT, 12) \
    _(XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, 13) \
    _(XR_HAND_JOINT_MIDDLE_DISTAL_EXT, 14) \
    _(XR_HAND_JOINT_MIDDLE_TIP_EXT, 15) \
    _(XR_HAND_JOINT_RING_METACARPAL_EXT, 16) \
    _(XR_HAND_JOINT_RING_PROXIMAL_EXT, 17) \
    _(XR_HAND_JOINT_RING_INTERMEDIATE_EXT, 18) \
    _(XR_HAND_JOINT_RING_DISTAL_EXT, 19) \
    _(XR_HAND_JOINT_RING_TIP_EXT, 20) \
    _(XR_HAND_JOINT_LITTLE_METACARPAL_EXT, 21) \
    _(XR_HAND_JOINT_LITTLE_PROXIMAL_EXT, 22) \
    _(XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, 23) \
    _(XR_HAND_JOINT_LITTLE_DISTAL_EXT, 24) \
    _(XR_HAND_JOINT_LITTLE_TIP_EXT, 25) \
    _(XR_HAND_JOINT_MAX_ENUM_EXT, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrHandJointSetEXT(_) \
    _(XR_HAND_JOINT_SET_DEFAULT_EXT, 0) \
    _(XR_HAND_JOINT_SET_MAX_ENUM_EXT, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrHandPoseTypeMSFT(_) \
    _(XR_HAND_POSE_TYPE_TRACKED_MSFT, 0) \
    _(XR_HAND_POSE_TYPE_REFERENCE_OPEN_PALM_MSFT, 1) \
    _(XR_HAND_POSE_TYPE_MAX_ENUM_MSFT, 0x7FFFFFFF)

#define XR_LIST_ENUM_XrColorSpaceFB(_) \
    _(XR_COLOR_SPACE_UNMANAGED_FB, 0) \
    _(XR_COLOR_SPACE_REC2020_FB, 1) \
    _(XR_COLOR_SPACE_REC709_FB, 2) \
    _(XR_COLOR_SPACE_RIFT_CV1_FB, 3) \
    _(XR_COLOR_SPACE_RIFT_S_FB, 4) \
    _(XR_COLOR_SPACE_QUEST_FB, 5) \
    _(XR_COLOR_SPACE_P3_FB, 6) \
    _(XR_COLOR_SPACE_ADOBE_RGB_FB, 7) \
    _(XR_COLOR_SPACE_MAX_ENUM_FB, 0x7FFFFFFF)

#define XR_LIST_BITS_XrInstanceCreateFlags(_)

#define XR_LIST_BITS_XrSessionCreateFlags(_)

#define XR_LIST_BITS_XrSpaceVelocityFlags(_) \
    _(XR_SPACE_VELOCITY_LINEAR_VALID_BIT, 0x00000001) \
    _(XR_SPACE_VELOCITY_ANGULAR_VALID_BIT, 0x00000002) \

#define XR_LIST_BITS_XrSpaceLocationFlags(_) \
    _(XR_SPACE_LOCATION_ORIENTATION_VALID_BIT, 0x00000001) \
    _(XR_SPACE_LOCATION_POSITION_VALID_BIT, 0x00000002) \
    _(XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT, 0x00000004) \
    _(XR_SPACE_LOCATION_POSITION_TRACKED_BIT, 0x00000008) \

#define XR_LIST_BITS_XrSwapchainCreateFlags(_) \
    _(XR_SWAPCHAIN_CREATE_PROTECTED_CONTENT_BIT, 0x00000001) \
    _(XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT, 0x00000002) \

#define XR_LIST_BITS_XrSwapchainUsageFlags(_) \
    _(XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT, 0x00000001) \
    _(XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0x00000002) \
    _(XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT, 0x00000004) \
    _(XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT, 0x00000008) \
    _(XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT, 0x00000010) \
    _(XR_SWAPCHAIN_USAGE_SAMPLED_BIT, 0x00000020) \
    _(XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT, 0x00000040) \
    _(XR_SWAPCHAIN_USAGE_INPUT_ATTACHMENT_BIT_MND, 0x00000080) \

#define XR_LIST_BITS_XrCompositionLayerFlags(_) \
    _(XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT, 0x00000001) \
    _(XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT, 0x00000002) \
    _(XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT, 0x00000004) \

#define XR_LIST_BITS_XrViewStateFlags(_) \
    _(XR_VIEW_STATE_ORIENTATION_VALID_BIT, 0x00000001) \
    _(XR_VIEW_STATE_POSITION_VALID_BIT, 0x00000002) \
    _(XR_VIEW_STATE_ORIENTATION_TRACKED_BIT, 0x00000004) \
    _(XR_VIEW_STATE_POSITION_TRACKED_BIT, 0x00000008) \

#define XR_LIST_BITS_XrInputSourceLocalizedNameFlags(_) \
    _(XR_INPUT_SOURCE_LOCALIZED_NAME_USER_PATH_BIT, 0x00000001) \
    _(XR_INPUT_SOURCE_LOCALIZED_NAME_INTERACTION_PROFILE_BIT, 0x00000002) \
    _(XR_INPUT_SOURCE_LOCALIZED_NAME_COMPONENT_BIT, 0x00000004) \

#define XR_LIST_BITS_XrVulkanInstanceCreateFlagsKHR(_)

#define XR_LIST_BITS_XrVulkanDeviceCreateFlagsKHR(_)

#define XR_LIST_BITS_XrDebugUtilsMessageSeverityFlagsEXT(_) \
    _(XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT, 0x00000001) \
    _(XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT, 0x00000010) \
    _(XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT, 0x00000100) \
    _(XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT, 0x00001000) \

#define XR_LIST_BITS_XrDebugUtilsMessageTypeFlagsEXT(_) \
    _(XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT, 0x00000001) \
    _(XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, 0x00000002) \
    _(XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT, 0x00000004) \
    _(XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT, 0x00000008) \

#define XR_LIST_BITS_XrOverlaySessionCreateFlagsEXTX(_) \
    _(XR_OVERLAY_SESSION_CREATE_RELAXED_DISPLAY_TIME_BIT_EXTX, 0x00000001) \

#define XR_LIST_BITS_XrOverlayMainSessionFlagsEXTX(_) \
    _(XR_OVERLAY_MAIN_SESSION_ENABLED_COMPOSITION_LAYER_INFO_DEPTH_BIT_EXTX, 0x00000001) \

#define XR_LIST_BITS_XrAndroidSurfaceSwapchainFlagsFB(_) \
    _(XR_ANDROID_SURFACE_SWAPCHAIN_SYNCHRONOUS_BIT_FB, 0x00000001) \
    _(XR_ANDROID_SURFACE_SWAPCHAIN_USE_TIMESTAMPS_BIT_FB, 0x00000002) \

#define XR_LIST_STRUCT_XrApiLayerProperties(_) \
    _(type) \
    _(next) \
    _(layerName) \
    _(specVersion) \
    _(layerVersion) \
    _(description) \

#define XR_LIST_STRUCT_XrExtensionProperties(_) \
    _(type) \
    _(next) \
    _(extensionName) \
    _(extensionVersion) \

#define XR_LIST_STRUCT_XrApplicationInfo(_) \
    _(applicationName) \
    _(applicationVersion) \
    _(engineName) \
    _(engineVersion) \
    _(apiVersion) \

#define XR_LIST_STRUCT_XrInstanceCreateInfo(_) \
    _(type) \
    _(next) \
    _(createFlags) \
    _(applicationInfo) \
    _(enabledApiLayerCount) \
    _(enabledApiLayerNames) \
    _(enabledExtensionCount) \
    _(enabledExtensionNames) \

#define XR_LIST_STRUCT_XrInstanceProperties(_) \
    _(type) \
    _(next) \
    _(runtimeVersion) \
    _(runtimeName) \

#define XR_LIST_STRUCT_XrEventDataBuffer(_) \
    _(type) \
    _(next) \
    _(varying) \

#define XR_LIST_STRUCT_XrSystemGetInfo(_) \
    _(type) \
    _(next) \
    _(formFactor) \

#define XR_LIST_STRUCT_XrSystemGraphicsProperties(_) \
    _(maxSwapchainImageHeight) \
    _(maxSwapchainImageWidth) \
    _(maxLayerCount) \

#define XR_LIST_STRUCT_XrSystemTrackingProperties(_) \
    _(orientationTracking) \
    _(positionTracking) \

#define XR_LIST_STRUCT_XrSystemProperties(_) \
    _(type) \
    _(next) \
    _(systemId) \
    _(vendorId) \
    _(systemName) \
    _(graphicsProperties) \
    _(trackingProperties) \

#define XR_LIST_STRUCT_XrSessionCreateInfo(_) \
    _(type) \
    _(next) \
    _(createFlags) \
    _(systemId) \

#define XR_LIST_STRUCT_XrVector3f(_) \
    _(x) \
    _(y) \
    _(z) \

#define XR_LIST_STRUCT_XrSpaceVelocity(_) \
    _(type) \
    _(next) \
    _(velocityFlags) \
    _(linearVelocity) \
    _(angularVelocity) \

#define XR_LIST_STRUCT_XrQuaternionf(_) \
    _(x) \
    _(y) \
    _(z) \
    _(w) \

#define XR_LIST_STRUCT_XrPosef(_) \
    _(orientation) \
    _(position) \

#define XR_LIST_STRUCT_XrReferenceSpaceCreateInfo(_) \
    _(type) \
    _(next) \
    _(referenceSpaceType) \
    _(poseInReferenceSpace) \

#define XR_LIST_STRUCT_XrExtent2Df(_) \
    _(width) \
    _(height) \

#define XR_LIST_STRUCT_XrActionSpaceCreateInfo(_) \
    _(type) \
    _(next) \
    _(action) \
    _(subactionPath) \
    _(poseInActionSpace) \

#define XR_LIST_STRUCT_XrSpaceLocation(_) \
    _(type) \
    _(next) \
    _(locationFlags) \
    _(pose) \

#define XR_LIST_STRUCT_XrViewConfigurationProperties(_) \
    _(type) \
    _(next) \
    _(viewConfigurationType) \
    _(fovMutable) \

#define XR_LIST_STRUCT_XrViewConfigurationView(_) \
    _(type) \
    _(next) \
    _(recommendedImageRectWidth) \
    _(maxImageRectWidth) \
    _(recommendedImageRectHeight) \
    _(maxImageRectHeight) \
    _(recommendedSwapchainSampleCount) \
    _(maxSwapchainSampleCount) \

#define XR_LIST_STRUCT_XrSwapchainCreateInfo(_) \
    _(type) \
    _(next) \
    _(createFlags) \
    _(usageFlags) \
    _(format) \
    _(sampleCount) \
    _(width) \
    _(height) \
    _(faceCount) \
    _(arraySize) \
    _(mipCount) \

#define XR_LIST_STRUCT_XrSwapchainImageBaseHeader(_) \
    _(type) \
    _(next) \

#define XR_LIST_STRUCT_XrSwapchainImageAcquireInfo(_) \
    _(type) \
    _(next) \

#define XR_LIST_STRUCT_XrSwapchainImageWaitInfo(_) \
    _(type) \
    _(next) \
    _(timeout) \

#define XR_LIST_STRUCT_XrSwapchainImageReleaseInfo(_) \
    _(type) \
    _(next) \

#define XR_LIST_STRUCT_XrSessionBeginInfo(_) \
    _(type) \
    _(next) \
    _(primaryViewConfigurationType) \

#define XR_LIST_STRUCT_XrFrameWaitInfo(_) \
    _(type) \
    _(next) \

#define XR_LIST_STRUCT_XrFrameState(_) \
    _(type) \
    _(next) \
    _(predictedDisplayTime) \
    _(predictedDisplayPeriod) \
    _(shouldRender) \

#define XR_LIST_STRUCT_XrFrameBeginInfo(_) \
    _(type) \
    _(next) \

#define XR_LIST_STRUCT_XrCompositionLayerBaseHeader(_) \
    _(type) \
    _(next) \
    _(layerFlags) \
    _(space) \

#define XR_LIST_STRUCT_XrFrameEndInfo(_) \
    _(type) \
    _(next) \
    _(displayTime) \
    _(environmentBlendMode) \
    _(layerCount) \
    _(layers) \

#define XR_LIST_STRUCT_XrViewLocateInfo(_) \
    _(type) \
    _(next) \
    _(viewConfigurationType) \
    _(displayTime) \
    _(space) \

#define XR_LIST_STRUCT_XrViewState(_) \
    _(type) \
    _(next) \
    _(viewStateFlags) \

#define XR_LIST_STRUCT_XrFovf(_) \
    _(angleLeft) \
    _(angleRight) \
    _(angleUp) \
    _(angleDown) \

#define XR_LIST_STRUCT_XrView(_) \
    _(type) \
    _(next) \
    _(pose) \
    _(fov) \

#define XR_LIST_STRUCT_XrActionSetCreateInfo(_) \
    _(type) \
    _(next) \
    _(actionSetName) \
    _(localizedActionSetName) \
    _(priority) \

#define XR_LIST_STRUCT_XrActionCreateInfo(_) \
    _(type) \
    _(next) \
    _(actionName) \
    _(actionType) \
    _(countSubactionPaths) \
    _(subactionPaths) \
    _(localizedActionName) \

#define XR_LIST_STRUCT_XrActionSuggestedBinding(_) \
    _(action) \
    _(binding) \

#define XR_LIST_STRUCT_XrInteractionProfileSuggestedBinding(_) \
    _(type) \
    _(next) \
    _(interactionProfile) \
    _(countSuggestedBindings) \
    _(suggestedBindings) \

#define XR_LIST_STRUCT_XrSessionActionSetsAttachInfo(_) \
    _(type) \
    _(next) \
    _(countActionSets) \
    _(actionSets) \

#define XR_LIST_STRUCT_XrInteractionProfileState(_) \
    _(type) \
    _(next) \
    _(interactionProfile) \

#define XR_LIST_STRUCT_XrActionStateGetInfo(_) \
    _(type) \
    _(next) \
    _(action) \
    _(subactionPath) \

#define XR_LIST_STRUCT_XrActionStateBoolean(_) \
    _(type) \
    _(next) \
    _(currentState) \
    _(changedSinceLastSync) \
    _(lastChangeTime) \
    _(isActive) \

#define XR_LIST_STRUCT_XrActionStateFloat(_) \
    _(type) \
    _(next) \
    _(currentState) \
    _(changedSinceLastSync) \
    _(lastChangeTime) \
    _(isActive) \

#define XR_LIST_STRUCT_XrVector2f(_) \
    _(x) \
    _(y) \

#define XR_LIST_STRUCT_XrActionStateVector2f(_) \
    _(type) \
    _(next) \
    _(currentState) \
    _(changedSinceLastSync) \
    _(lastChangeTime) \
    _(isActive) \

#define XR_LIST_STRUCT_XrActionStatePose(_) \
    _(type) \
    _(next) \
    _(isActive) \

#define XR_LIST_STRUCT_XrActiveActionSet(_) \
    _(actionSet) \
    _(subactionPath) \

#define XR_LIST_STRUCT_XrActionsSyncInfo(_) \
    _(type) \
    _(next) \
    _(countActiveActionSets) \
    _(activeActionSets) \

#define XR_LIST_STRUCT_XrBoundSourcesForActionEnumerateInfo(_) \
    _(type) \
    _(next) \
    _(action) \

#define XR_LIST_STRUCT_XrInputSourceLocalizedNameGetInfo(_) \
    _(type) \
    _(next) \
    _(sourcePath) \
    _(whichComponents) \

#define XR_LIST_STRUCT_XrHapticActionInfo(_) \
    _(type) \
    _(next) \
    _(action) \
    _(subactionPath) \

#define XR_LIST_STRUCT_XrHapticBaseHeader(_) \
    _(type) \
    _(next) \

#define XR_LIST_STRUCT_XrBaseInStructure(_) \
    _(type) \
    _(next) \

#define XR_LIST_STRUCT_XrBaseOutStructure(_) \
    _(type) \
    _(next) \

#define XR_LIST_STRUCT_XrOffset2Di(_) \
    _(x) \
    _(y) \

#define XR_LIST_STRUCT_XrExtent2Di(_) \
    _(width) \
    _(height) \

#define XR_LIST_STRUCT_XrRect2Di(_) \
    _(offset) \
    _(extent) \

#define XR_LIST_STRUCT_XrSwapchainSubImage(_) \
    _(swapchain) \
    _(imageRect) \
    _(imageArrayIndex) \

#define XR_LIST_STRUCT_XrCompositionLayerProjectionView(_) \
    _(type) \
    _(next) \
    _(pose) \
    _(fov) \
    _(subImage) \

#define XR_LIST_STRUCT_XrCompositionLayerProjection(_) \
    _(type) \
    _(next) \
    _(layerFlags) \
    _(space) \
    _(viewCount) \
    _(views) \

#define XR_LIST_STRUCT_XrCompositionLayerQuad(_) \
    _(type) \
    _(next) \
    _(layerFlags) \
    _(space) \
    _(eyeVisibility) \
    _(subImage) \
    _(pose) \
    _(size) \

#define XR_LIST_STRUCT_XrEventDataBaseHeader(_) \
    _(type) \
    _(next) \

#define XR_LIST_STRUCT_XrEventDataEventsLost(_) \
    _(type) \
    _(next) \
    _(lostEventCount) \

#define XR_LIST_STRUCT_XrEventDataInstanceLossPending(_) \
    _(type) \
    _(next) \
    _(lossTime) \

#define XR_LIST_STRUCT_XrEventDataSessionStateChanged(_) \
    _(type) \
    _(next) \
    _(session) \
    _(state) \
    _(time) \

#define XR_LIST_STRUCT_XrEventDataReferenceSpaceChangePending(_) \
    _(type) \
    _(next) \
    _(session) \
    _(referenceSpaceType) \
    _(changeTime) \
    _(poseValid) \
    _(poseInPreviousSpace) \

#define XR_LIST_STRUCT_XrEventDataInteractionProfileChanged(_) \
    _(type) \
    _(next) \
    _(session) \

#define XR_LIST_STRUCT_XrHapticVibration(_) \
    _(type) \
    _(next) \
    _(duration) \
    _(frequency) \
    _(amplitude) \

#define XR_LIST_STRUCT_XrOffset2Df(_) \
    _(x) \
    _(y) \

#define XR_LIST_STRUCT_XrRect2Df(_) \
    _(offset) \
    _(extent) \

#define XR_LIST_STRUCT_XrVector4f(_) \
    _(x) \
    _(y) \
    _(z) \
    _(w) \

#define XR_LIST_STRUCT_XrColor4f(_) \
    _(r) \
    _(g) \
    _(b) \
    _(a) \

#define XR_LIST_STRUCT_XrCompositionLayerCubeKHR(_) \
    _(type) \
    _(next) \
    _(layerFlags) \
    _(space) \
    _(eyeVisibility) \
    _(swapchain) \
    _(imageArrayIndex) \
    _(orientation) \

#define XR_LIST_STRUCT_XrInstanceCreateInfoAndroidKHR(_) \
    _(type) \
    _(next) \
    _(applicationVM) \
    _(applicationActivity) \

#define XR_LIST_STRUCT_XrCompositionLayerDepthInfoKHR(_) \
    _(type) \
    _(next) \
    _(subImage) \
    _(minDepth) \
    _(maxDepth) \
    _(nearZ) \
    _(farZ) \

#define XR_LIST_STRUCT_XrVulkanSwapchainFormatListCreateInfoKHR(_) \
    _(type) \
    _(next) \
    _(viewFormatCount) \
    _(viewFormats) \

#define XR_LIST_STRUCT_XrCompositionLayerCylinderKHR(_) \
    _(type) \
    _(next) \
    _(layerFlags) \
    _(space) \
    _(eyeVisibility) \
    _(subImage) \
    _(pose) \
    _(radius) \
    _(centralAngle) \
    _(aspectRatio) \

#define XR_LIST_STRUCT_XrCompositionLayerEquirectKHR(_) \
    _(type) \
    _(next) \
    _(layerFlags) \
    _(space) \
    _(eyeVisibility) \
    _(subImage) \
    _(pose) \
    _(radius) \
    _(scale) \
    _(bias) \

#define XR_LIST_STRUCT_XrGraphicsBindingOpenGLWin32KHR(_) \
    _(type) \
    _(next) \
    _(hDC) \
    _(hGLRC) \

#define XR_LIST_STRUCT_XrGraphicsBindingOpenGLXlibKHR(_) \
    _(type) \
    _(next) \
    _(xDisplay) \
    _(visualid) \
    _(glxFBConfig) \
    _(glxDrawable) \
    _(glxContext) \

#define XR_LIST_STRUCT_XrGraphicsBindingOpenGLXcbKHR(_) \
    _(type) \
    _(next) \
    _(connection) \
    _(screenNumber) \
    _(fbconfigid) \
    _(visualid) \
    _(glxDrawable) \
    _(glxContext) \

#define XR_LIST_STRUCT_XrGraphicsBindingOpenGLWaylandKHR(_) \
    _(type) \
    _(next) \
    _(display) \

#define XR_LIST_STRUCT_XrSwapchainImageOpenGLKHR(_) \
    _(type) \
    _(next) \
    _(image) \

#define XR_LIST_STRUCT_XrGraphicsRequirementsOpenGLKHR(_) \
    _(type) \
    _(next) \
    _(minApiVersionSupported) \
    _(maxApiVersionSupported) \

#define XR_LIST_STRUCT_XrGraphicsBindingOpenGLESAndroidKHR(_) \
    _(type) \
    _(next) \
    _(display) \
    _(config) \
    _(context) \

#define XR_LIST_STRUCT_XrSwapchainImageOpenGLESKHR(_) \
    _(type) \
    _(next) \
    _(image) \

#define XR_LIST_STRUCT_XrGraphicsRequirementsOpenGLESKHR(_) \
    _(type) \
    _(next) \
    _(minApiVersionSupported) \
    _(maxApiVersionSupported) \

#define XR_LIST_STRUCT_XrGraphicsBindingVulkanKHR(_) \
    _(type) \
    _(next) \
    _(instance) \
    _(physicalDevice) \
    _(device) \
    _(queueFamilyIndex) \
    _(queueIndex) \

#define XR_LIST_STRUCT_XrSwapchainImageVulkanKHR(_) \
    _(type) \
    _(next) \
    _(image) \

#define XR_LIST_STRUCT_XrGraphicsRequirementsVulkanKHR(_) \
    _(type) \
    _(next) \
    _(minApiVersionSupported) \
    _(maxApiVersionSupported) \

#define XR_LIST_STRUCT_XrGraphicsBindingD3D11KHR(_) \
    _(type) \
    _(next) \
    _(device) \

#define XR_LIST_STRUCT_XrSwapchainImageD3D11KHR(_) \
    _(type) \
    _(next) \
    _(texture) \

#define XR_LIST_STRUCT_XrGraphicsRequirementsD3D11KHR(_) \
    _(type) \
    _(next) \
    _(adapterLuid) \
    _(minFeatureLevel) \

#define XR_LIST_STRUCT_XrGraphicsBindingD3D12KHR(_) \
    _(type) \
    _(next) \
    _(device) \
    _(queue) \

#define XR_LIST_STRUCT_XrSwapchainImageD3D12KHR(_) \
    _(type) \
    _(next) \
    _(texture) \

#define XR_LIST_STRUCT_XrGraphicsRequirementsD3D12KHR(_) \
    _(type) \
    _(next) \
    _(adapterLuid) \
    _(minFeatureLevel) \

#define XR_LIST_STRUCT_XrVisibilityMaskKHR(_) \
    _(type) \
    _(next) \
    _(vertexCapacityInput) \
    _(vertexCountOutput) \
    _(vertices) \
    _(indexCapacityInput) \
    _(indexCountOutput) \
    _(indices) \

#define XR_LIST_STRUCT_XrEventDataVisibilityMaskChangedKHR(_) \
    _(type) \
    _(next) \
    _(session) \
    _(viewConfigurationType) \
    _(viewIndex) \

#define XR_LIST_STRUCT_XrCompositionLayerColorScaleBiasKHR(_) \
    _(type) \
    _(next) \
    _(colorScale) \
    _(colorBias) \

#define XR_LIST_STRUCT_XrLoaderInitInfoBaseHeaderKHR(_) \
    _(type) \
    _(next) \

#define XR_LIST_STRUCT_XrLoaderInitInfoAndroidKHR(_) \
    _(type) \
    _(next) \
    _(applicationVM) \
    _(applicationContext) \

#define XR_LIST_STRUCT_XrVulkanInstanceCreateInfoKHR(_) \
    _(type) \
    _(next) \
    _(systemId) \
    _(createFlags) \
    _(pfnGetInstanceProcAddr) \
    _(vulkanCreateInfo) \
    _(vulkanAllocator) \

#define XR_LIST_STRUCT_XrVulkanDeviceCreateInfoKHR(_) \
    _(type) \
    _(next) \
    _(systemId) \
    _(createFlags) \
    _(pfnGetInstanceProcAddr) \
    _(vulkanPhysicalDevice) \
    _(vulkanCreateInfo) \
    _(vulkanAllocator) \

#define XR_LIST_STRUCT_XrVulkanGraphicsDeviceGetInfoKHR(_) \
    _(type) \
    _(next) \
    _(systemId) \
    _(vulkanInstance) \

#define XR_LIST_STRUCT_XrCompositionLayerEquirect2KHR(_) \
    _(type) \
    _(next) \
    _(layerFlags) \
    _(space) \
    _(eyeVisibility) \
    _(subImage) \
    _(pose) \
    _(radius) \
    _(centralHorizontalAngle) \
    _(upperVerticalAngle) \
    _(lowerVerticalAngle) \

#define XR_LIST_STRUCT_XrBindingModificationBaseHeaderKHR(_) \
    _(type) \
    _(next) \

#define XR_LIST_STRUCT_XrBindingModificationsKHR(_) \
    _(type) \
    _(next) \
    _(bindingModificationCount) \
    _(bindingModifications) \

#define XR_LIST_STRUCT_XrEventDataPerfSettingsEXT(_) \
    _(type) \
    _(next) \
    _(domain) \
    _(subDomain) \
    _(fromLevel) \
    _(toLevel) \

#define XR_LIST_STRUCT_XrDebugUtilsObjectNameInfoEXT(_) \
    _(type) \
    _(next) \
    _(objectType) \
    _(objectHandle) \
    _(objectName) \

#define XR_LIST_STRUCT_XrDebugUtilsLabelEXT(_) \
    _(type) \
    _(next) \
    _(labelName) \

#define XR_LIST_STRUCT_XrDebugUtilsMessengerCallbackDataEXT(_) \
    _(type) \
    _(next) \
    _(messageId) \
    _(functionName) \
    _(message) \
    _(objectCount) \
    _(objects) \
    _(sessionLabelCount) \
    _(sessionLabels) \

#define XR_LIST_STRUCT_XrDebugUtilsMessengerCreateInfoEXT(_) \
    _(type) \
    _(next) \
    _(messageSeverities) \
    _(messageTypes) \
    _(userCallback) \
    _(userData) \

#define XR_LIST_STRUCT_XrSystemEyeGazeInteractionPropertiesEXT(_) \
    _(type) \
    _(next) \
    _(supportsEyeGazeInteraction) \

#define XR_LIST_STRUCT_XrEyeGazeSampleTimeEXT(_) \
    _(type) \
    _(next) \
    _(time) \

#define XR_LIST_STRUCT_XrSessionCreateInfoOverlayEXTX(_) \
    _(type) \
    _(next) \
    _(createFlags) \
    _(sessionLayersPlacement) \

#define XR_LIST_STRUCT_XrEventDataMainSessionVisibilityChangedEXTX(_) \
    _(type) \
    _(next) \
    _(visible) \
    _(flags) \

#define XR_LIST_STRUCT_XrSpatialAnchorCreateInfoMSFT(_) \
    _(type) \
    _(next) \
    _(space) \
    _(pose) \
    _(time) \

#define XR_LIST_STRUCT_XrSpatialAnchorSpaceCreateInfoMSFT(_) \
    _(type) \
    _(next) \
    _(anchor) \
    _(poseInAnchorSpace) \

#define XR_LIST_STRUCT_XrViewConfigurationDepthRangeEXT(_) \
    _(type) \
    _(next) \
    _(recommendedNearZ) \
    _(minNearZ) \
    _(recommendedFarZ) \
    _(maxFarZ) \

#define XR_LIST_STRUCT_XrGraphicsBindingEGLMNDX(_) \
    _(type) \
    _(next) \
    _(getProcAddress) \
    _(display) \
    _(config) \
    _(context) \

#define XR_LIST_STRUCT_XrSpatialGraphNodeSpaceCreateInfoMSFT(_) \
    _(type) \
    _(next) \
    _(nodeType) \
    _(nodeId) \
    _(pose) \

#define XR_LIST_STRUCT_XrSystemHandTrackingPropertiesEXT(_) \
    _(type) \
    _(next) \
    _(supportsHandTracking) \

#define XR_LIST_STRUCT_XrHandTrackerCreateInfoEXT(_) \
    _(type) \
    _(next) \
    _(hand) \
    _(handJointSet) \

#define XR_LIST_STRUCT_XrHandJointsLocateInfoEXT(_) \
    _(type) \
    _(next) \
    _(baseSpace) \
    _(time) \

#define XR_LIST_STRUCT_XrHandJointLocationEXT(_) \
    _(locationFlags) \
    _(pose) \
    _(radius) \

#define XR_LIST_STRUCT_XrHandJointVelocityEXT(_) \
    _(velocityFlags) \
    _(linearVelocity) \
    _(angularVelocity) \

#define XR_LIST_STRUCT_XrHandJointLocationsEXT(_) \
    _(type) \
    _(next) \
    _(isActive) \
    _(jointCount) \
    _(jointLocations) \

#define XR_LIST_STRUCT_XrHandJointVelocitiesEXT(_) \
    _(type) \
    _(next) \
    _(jointCount) \
    _(jointVelocities) \

#define XR_LIST_STRUCT_XrSystemHandTrackingMeshPropertiesMSFT(_) \
    _(type) \
    _(next) \
    _(supportsHandTrackingMesh) \
    _(maxHandMeshIndexCount) \
    _(maxHandMeshVertexCount) \

#define XR_LIST_STRUCT_XrHandMeshSpaceCreateInfoMSFT(_) \
    _(type) \
    _(next) \
    _(handPoseType) \
    _(poseInHandMeshSpace) \

#define XR_LIST_STRUCT_XrHandMeshUpdateInfoMSFT(_) \
    _(type) \
    _(next) \
    _(time) \
    _(handPoseType) \

#define XR_LIST_STRUCT_XrHandMeshIndexBufferMSFT(_) \
    _(indexBufferKey) \
    _(indexCapacityInput) \
    _(indexCountOutput) \
    _(indices) \

#define XR_LIST_STRUCT_XrHandMeshVertexMSFT(_) \
    _(position) \
    _(normal) \

#define XR_LIST_STRUCT_XrHandMeshVertexBufferMSFT(_) \
    _(vertexUpdateTime) \
    _(vertexCapacityInput) \
    _(vertexCountOutput) \
    _(vertices) \

#define XR_LIST_STRUCT_XrHandMeshMSFT(_) \
    _(type) \
    _(next) \
    _(isActive) \
    _(indexBufferChanged) \
    _(vertexBufferChanged) \
    _(indexBuffer) \
    _(vertexBuffer) \

#define XR_LIST_STRUCT_XrHandPoseTypeInfoMSFT(_) \
    _(type) \
    _(next) \
    _(handPoseType) \

#define XR_LIST_STRUCT_XrSecondaryViewConfigurationSessionBeginInfoMSFT(_) \
    _(type) \
    _(next) \
    _(viewConfigurationCount) \
    _(enabledViewConfigurationTypes) \

#define XR_LIST_STRUCT_XrSecondaryViewConfigurationStateMSFT(_) \
    _(type) \
    _(next) \
    _(viewConfigurationType) \
    _(active) \

#define XR_LIST_STRUCT_XrSecondaryViewConfigurationFrameStateMSFT(_) \
    _(type) \
    _(next) \
    _(viewConfigurationCount) \
    _(viewConfigurationStates) \

#define XR_LIST_STRUCT_XrSecondaryViewConfigurationLayerInfoMSFT(_) \
    _(type) \
    _(next) \
    _(viewConfigurationType) \
    _(environmentBlendMode) \
    _(layerCount) \
    _(layers) \

#define XR_LIST_STRUCT_XrSecondaryViewConfigurationFrameEndInfoMSFT(_) \
    _(type) \
    _(next) \
    _(viewConfigurationCount) \
    _(viewConfigurationLayersInfo) \

#define XR_LIST_STRUCT_XrSecondaryViewConfigurationSwapchainCreateInfoMSFT(_) \
    _(type) \
    _(next) \
    _(viewConfigurationType) \

#define XR_LIST_STRUCT_XrControllerModelKeyStateMSFT(_) \
    _(type) \
    _(next) \
    _(modelKey) \

#define XR_LIST_STRUCT_XrControllerModelNodePropertiesMSFT(_) \
    _(type) \
    _(next) \
    _(parentNodeName) \
    _(nodeName) \

#define XR_LIST_STRUCT_XrControllerModelPropertiesMSFT(_) \
    _(type) \
    _(next) \
    _(nodeCapacityInput) \
    _(nodeCountOutput) \
    _(nodeProperties) \

#define XR_LIST_STRUCT_XrControllerModelNodeStateMSFT(_) \
    _(type) \
    _(next) \
    _(nodePose) \

#define XR_LIST_STRUCT_XrControllerModelStateMSFT(_) \
    _(type) \
    _(next) \
    _(nodeCapacityInput) \
    _(nodeCountOutput) \
    _(nodeStates) \

#define XR_LIST_STRUCT_XrViewConfigurationViewFovEPIC(_) \
    _(type) \
    _(next) \
    _(recommendedFov) \
    _(maxMutableFov) \

#define XR_LIST_STRUCT_XrHolographicWindowAttachmentMSFT(_) \
    _(type) \
    _(next) \
    _(holographicSpace) \
    _(coreWindow) \

#define XR_LIST_STRUCT_XrAndroidSurfaceSwapchainCreateInfoFB(_) \
    _(type) \
    _(next) \
    _(createFlags) \

#define XR_LIST_STRUCT_XrInteractionProfileAnalogThresholdVALVE(_) \
    _(type) \
    _(next) \
    _(action) \
    _(binding) \
    _(onThreshold) \
    _(offThreshold) \
    _(onHaptic) \
    _(offHaptic) \

#define XR_LIST_STRUCT_XrEventDataDisplayRefreshRateChangedFB(_) \
    _(type) \
    _(next) \
    _(fromDisplayRefreshRate) \
    _(toDisplayRefreshRate) \

#define XR_LIST_STRUCT_XrSystemColorSpacePropertiesFB(_) \
    _(type) \
    _(next) \
    _(colorSpace) \



#define XR_LIST_STRUCTURE_TYPES_CORE(_) \
    _(XrApiLayerProperties, XR_TYPE_API_LAYER_PROPERTIES) \
    _(XrExtensionProperties, XR_TYPE_EXTENSION_PROPERTIES) \
    _(XrInstanceCreateInfo, XR_TYPE_INSTANCE_CREATE_INFO) \
    _(XrInstanceProperties, XR_TYPE_INSTANCE_PROPERTIES) \
    _(XrEventDataBuffer, XR_TYPE_EVENT_DATA_BUFFER) \
    _(XrSystemGetInfo, XR_TYPE_SYSTEM_GET_INFO) \
    _(XrSystemProperties, XR_TYPE_SYSTEM_PROPERTIES) \
    _(XrSessionCreateInfo, XR_TYPE_SESSION_CREATE_INFO) \
    _(XrSpaceVelocity, XR_TYPE_SPACE_VELOCITY) \
    _(XrReferenceSpaceCreateInfo, XR_TYPE_REFERENCE_SPACE_CREATE_INFO) \
    _(XrActionSpaceCreateInfo, XR_TYPE_ACTION_SPACE_CREATE_INFO) \
    _(XrSpaceLocation, XR_TYPE_SPACE_LOCATION) \
    _(XrViewConfigurationProperties, XR_TYPE_VIEW_CONFIGURATION_PROPERTIES) \
    _(XrViewConfigurationView, XR_TYPE_VIEW_CONFIGURATION_VIEW) \
    _(XrSwapchainCreateInfo, XR_TYPE_SWAPCHAIN_CREATE_INFO) \
    _(XrSwapchainImageAcquireInfo, XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO) \
    _(XrSwapchainImageWaitInfo, XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO) \
    _(XrSwapchainImageReleaseInfo, XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO) \
    _(XrSessionBeginInfo, XR_TYPE_SESSION_BEGIN_INFO) \
    _(XrFrameWaitInfo, XR_TYPE_FRAME_WAIT_INFO) \
    _(XrFrameState, XR_TYPE_FRAME_STATE) \
    _(XrFrameBeginInfo, XR_TYPE_FRAME_BEGIN_INFO) \
    _(XrFrameEndInfo, XR_TYPE_FRAME_END_INFO) \
    _(XrViewLocateInfo, XR_TYPE_VIEW_LOCATE_INFO) \
    _(XrViewState, XR_TYPE_VIEW_STATE) \
    _(XrView, XR_TYPE_VIEW) \
    _(XrActionSetCreateInfo, XR_TYPE_ACTION_SET_CREATE_INFO) \
    _(XrActionCreateInfo, XR_TYPE_ACTION_CREATE_INFO) \
    _(XrInteractionProfileSuggestedBinding, XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING) \
    _(XrSessionActionSetsAttachInfo, XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO) \
    _(XrInteractionProfileState, XR_TYPE_INTERACTION_PROFILE_STATE) \
    _(XrActionStateGetInfo, XR_TYPE_ACTION_STATE_GET_INFO) \
    _(XrActionStateBoolean, XR_TYPE_ACTION_STATE_BOOLEAN) \
    _(XrActionStateFloat, XR_TYPE_ACTION_STATE_FLOAT) \
    _(XrActionStateVector2f, XR_TYPE_ACTION_STATE_VECTOR2F) \
    _(XrActionStatePose, XR_TYPE_ACTION_STATE_POSE) \
    _(XrActionsSyncInfo, XR_TYPE_ACTIONS_SYNC_INFO) \
    _(XrBoundSourcesForActionEnumerateInfo, XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO) \
    _(XrInputSourceLocalizedNameGetInfo, XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO) \
    _(XrHapticActionInfo, XR_TYPE_HAPTIC_ACTION_INFO) \
    _(XrCompositionLayerProjectionView, XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW) \
    _(XrCompositionLayerProjection, XR_TYPE_COMPOSITION_LAYER_PROJECTION) \
    _(XrCompositionLayerQuad, XR_TYPE_COMPOSITION_LAYER_QUAD) \
    _(XrEventDataEventsLost, XR_TYPE_EVENT_DATA_EVENTS_LOST) \
    _(XrEventDataInstanceLossPending, XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) \
    _(XrEventDataSessionStateChanged, XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) \
    _(XrEventDataReferenceSpaceChangePending, XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) \
    _(XrEventDataInteractionProfileChanged, XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED) \
    _(XrHapticVibration, XR_TYPE_HAPTIC_VIBRATION) \
    _(XrCompositionLayerCubeKHR, XR_TYPE_COMPOSITION_LAYER_CUBE_KHR) \
    _(XrCompositionLayerDepthInfoKHR, XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR) \
    _(XrCompositionLayerCylinderKHR, XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR) \
    _(XrCompositionLayerEquirectKHR, XR_TYPE_COMPOSITION_LAYER_EQUIRECT_KHR) \
    _(XrVisibilityMaskKHR, XR_TYPE_VISIBILITY_MASK_KHR) \
    _(XrEventDataVisibilityMaskChangedKHR, XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR) \
    _(XrCompositionLayerColorScaleBiasKHR, XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR) \
    _(XrCompositionLayerEquirect2KHR, XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR) \
    _(XrBindingModificationsKHR, XR_TYPE_BINDING_MODIFICATIONS_KHR) \
    _(XrEventDataPerfSettingsEXT, XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT) \
    _(XrDebugUtilsObjectNameInfoEXT, XR_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT) \
    _(XrDebugUtilsLabelEXT, XR_TYPE_DEBUG_UTILS_LABEL_EXT) \
    _(XrDebugUtilsMessengerCallbackDataEXT, XR_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT) \
    _(XrDebugUtilsMessengerCreateInfoEXT, XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT) \
    _(XrSystemEyeGazeInteractionPropertiesEXT, XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT) \
    _(XrEyeGazeSampleTimeEXT, XR_TYPE_EYE_GAZE_SAMPLE_TIME_EXT) \
    _(XrSessionCreateInfoOverlayEXTX, XR_TYPE_SESSION_CREATE_INFO_OVERLAY_EXTX) \
    _(XrEventDataMainSessionVisibilityChangedEXTX, XR_TYPE_EVENT_DATA_MAIN_SESSION_VISIBILITY_CHANGED_EXTX) \
    _(XrSpatialAnchorCreateInfoMSFT, XR_TYPE_SPATIAL_ANCHOR_CREATE_INFO_MSFT) \
    _(XrSpatialAnchorSpaceCreateInfoMSFT, XR_TYPE_SPATIAL_ANCHOR_SPACE_CREATE_INFO_MSFT) \
    _(XrViewConfigurationDepthRangeEXT, XR_TYPE_VIEW_CONFIGURATION_DEPTH_RANGE_EXT) \
    _(XrSpatialGraphNodeSpaceCreateInfoMSFT, XR_TYPE_SPATIAL_GRAPH_NODE_SPACE_CREATE_INFO_MSFT) \
    _(XrSystemHandTrackingPropertiesEXT, XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT) \
    _(XrHandTrackerCreateInfoEXT, XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT) \
    _(XrHandJointsLocateInfoEXT, XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT) \
    _(XrHandJointLocationsEXT, XR_TYPE_HAND_JOINT_LOCATIONS_EXT) \
    _(XrHandJointVelocitiesEXT, XR_TYPE_HAND_JOINT_VELOCITIES_EXT) \
    _(XrSystemHandTrackingMeshPropertiesMSFT, XR_TYPE_SYSTEM_HAND_TRACKING_MESH_PROPERTIES_MSFT) \
    _(XrHandMeshSpaceCreateInfoMSFT, XR_TYPE_HAND_MESH_SPACE_CREATE_INFO_MSFT) \
    _(XrHandMeshUpdateInfoMSFT, XR_TYPE_HAND_MESH_UPDATE_INFO_MSFT) \
    _(XrHandMeshMSFT, XR_TYPE_HAND_MESH_MSFT) \
    _(XrHandPoseTypeInfoMSFT, XR_TYPE_HAND_POSE_TYPE_INFO_MSFT) \
    _(XrSecondaryViewConfigurationSessionBeginInfoMSFT, XR_TYPE_SECONDARY_VIEW_CONFIGURATION_SESSION_BEGIN_INFO_MSFT) \
    _(XrSecondaryViewConfigurationStateMSFT, XR_TYPE_SECONDARY_VIEW_CONFIGURATION_STATE_MSFT) \
    _(XrSecondaryViewConfigurationFrameStateMSFT, XR_TYPE_SECONDARY_VIEW_CONFIGURATION_FRAME_STATE_MSFT) \
    _(XrSecondaryViewConfigurationLayerInfoMSFT, XR_TYPE_SECONDARY_VIEW_CONFIGURATION_LAYER_INFO_MSFT) \
    _(XrSecondaryViewConfigurationFrameEndInfoMSFT, XR_TYPE_SECONDARY_VIEW_CONFIGURATION_FRAME_END_INFO_MSFT) \
    _(XrSecondaryViewConfigurationSwapchainCreateInfoMSFT, XR_TYPE_SECONDARY_VIEW_CONFIGURATION_SWAPCHAIN_CREATE_INFO_MSFT) \
    _(XrControllerModelKeyStateMSFT, XR_TYPE_CONTROLLER_MODEL_KEY_STATE_MSFT) \
    _(XrControllerModelNodePropertiesMSFT, XR_TYPE_CONTROLLER_MODEL_NODE_PROPERTIES_MSFT) \
    _(XrControllerModelPropertiesMSFT, XR_TYPE_CONTROLLER_MODEL_PROPERTIES_MSFT) \
    _(XrControllerModelNodeStateMSFT, XR_TYPE_CONTROLLER_MODEL_NODE_STATE_MSFT) \
    _(XrControllerModelStateMSFT, XR_TYPE_CONTROLLER_MODEL_STATE_MSFT) \
    _(XrViewConfigurationViewFovEPIC, XR_TYPE_VIEW_CONFIGURATION_VIEW_FOV_EPIC) \
    _(XrInteractionProfileAnalogThresholdVALVE, XR_TYPE_INTERACTION_PROFILE_ANALOG_THRESHOLD_VALVE) \
    _(XrEventDataDisplayRefreshRateChangedFB, XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB) \
    _(XrSystemColorSpacePropertiesFB, XR_TYPE_SYSTEM_COLOR_SPACE_PROPERTIES_FB) \




#if defined(XR_USE_GRAPHICS_API_D3D11)
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_D3D11(_) \
    _(XrGraphicsBindingD3D11KHR, XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) \
    _(XrSwapchainImageD3D11KHR, XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR) \
    _(XrGraphicsRequirementsD3D11KHR, XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR) \


#else
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_D3D11(_)
#endif

#if defined(XR_USE_GRAPHICS_API_D3D12)
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_D3D12(_) \
    _(XrGraphicsBindingD3D12KHR, XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) \
    _(XrSwapchainImageD3D12KHR, XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR) \
    _(XrGraphicsRequirementsD3D12KHR, XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR) \


#else
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_D3D12(_)
#endif

#if defined(XR_USE_GRAPHICS_API_OPENGL)
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL(_) \
    _(XrSwapchainImageOpenGLKHR, XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR) \
    _(XrGraphicsRequirementsOpenGLKHR, XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR) \


#else
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL(_)
#endif

#if defined(XR_USE_GRAPHICS_API_OPENGL) && defined(XR_USE_PLATFORM_WAYLAND)
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_XR_USE_PLATFORM_WAYLAND(_) \
    _(XrGraphicsBindingOpenGLWaylandKHR, XR_TYPE_GRAPHICS_BINDING_OPENGL_WAYLAND_KHR) \


#else
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_XR_USE_PLATFORM_WAYLAND(_)
#endif

#if defined(XR_USE_GRAPHICS_API_OPENGL) && defined(XR_USE_PLATFORM_WIN32)
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_XR_USE_PLATFORM_WIN32(_) \
    _(XrGraphicsBindingOpenGLWin32KHR, XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR) \


#else
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_XR_USE_PLATFORM_WIN32(_)
#endif

#if defined(XR_USE_GRAPHICS_API_OPENGL) && defined(XR_USE_PLATFORM_XCB)
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_XR_USE_PLATFORM_XCB(_) \
    _(XrGraphicsBindingOpenGLXcbKHR, XR_TYPE_GRAPHICS_BINDING_OPENGL_XCB_KHR) \


#else
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_XR_USE_PLATFORM_XCB(_)
#endif

#if defined(XR_USE_GRAPHICS_API_OPENGL) && defined(XR_USE_PLATFORM_XLIB)
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_XR_USE_PLATFORM_XLIB(_) \
    _(XrGraphicsBindingOpenGLXlibKHR, XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR) \


#else
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_XR_USE_PLATFORM_XLIB(_)
#endif

#if defined(XR_USE_GRAPHICS_API_OPENGL_ES)
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_ES(_) \
    _(XrSwapchainImageOpenGLESKHR, XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR) \
    _(XrGraphicsRequirementsOpenGLESKHR, XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR) \


#else
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_ES(_)
#endif

#if defined(XR_USE_GRAPHICS_API_VULKAN)
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_VULKAN(_) \
    _(XrVulkanSwapchainFormatListCreateInfoKHR, XR_TYPE_VULKAN_SWAPCHAIN_FORMAT_LIST_CREATE_INFO_KHR) \
    _(XrGraphicsBindingVulkanKHR, XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR) \
    _(XrSwapchainImageVulkanKHR, XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR) \
    _(XrGraphicsRequirementsVulkanKHR, XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR) \
    _(XrVulkanInstanceCreateInfoKHR, XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR) \
    _(XrVulkanDeviceCreateInfoKHR, XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR) \
    _(XrVulkanGraphicsDeviceGetInfoKHR, XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR) \


#else
#define XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_VULKAN(_)
#endif

#if defined(XR_USE_PLATFORM_ANDROID)
#define XR_LIST_STRUCTURE_TYPES_XR_USE_PLATFORM_ANDROID(_) \
    _(XrInstanceCreateInfoAndroidKHR, XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR) \
    _(XrLoaderInitInfoAndroidKHR, XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR) \
    _(XrAndroidSurfaceSwapchainCreateInfoFB, XR_TYPE_ANDROID_SURFACE_SWAPCHAIN_CREATE_INFO_FB) \


#else
#define XR_LIST_STRUCTURE_TYPES_XR_USE_PLATFORM_ANDROID(_)
#endif

#if defined(XR_USE_PLATFORM_ANDROID) && defined(XR_USE_GRAPHICS_API_OPENGL_ES)
#define XR_LIST_STRUCTURE_TYPES_XR_USE_PLATFORM_ANDROID_XR_USE_GRAPHICS_API_OPENGL_ES(_) \
    _(XrGraphicsBindingOpenGLESAndroidKHR, XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR) \


#else
#define XR_LIST_STRUCTURE_TYPES_XR_USE_PLATFORM_ANDROID_XR_USE_GRAPHICS_API_OPENGL_ES(_)
#endif

#if defined(XR_USE_PLATFORM_EGL)
#define XR_LIST_STRUCTURE_TYPES_XR_USE_PLATFORM_EGL(_) \
    _(XrGraphicsBindingEGLMNDX, XR_TYPE_GRAPHICS_BINDING_EGL_MNDX) \


#else
#define XR_LIST_STRUCTURE_TYPES_XR_USE_PLATFORM_EGL(_)
#endif

#if defined(XR_USE_PLATFORM_WIN32)
#define XR_LIST_STRUCTURE_TYPES_XR_USE_PLATFORM_WIN32(_) \
    _(XrHolographicWindowAttachmentMSFT, XR_TYPE_HOLOGRAPHIC_WINDOW_ATTACHMENT_MSFT) \


#else
#define XR_LIST_STRUCTURE_TYPES_XR_USE_PLATFORM_WIN32(_)
#endif

#define XR_LIST_STRUCTURE_TYPES(_) \
    XR_LIST_STRUCTURE_TYPES_CORE(_) \
    XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_D3D11(_) \
    XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_D3D12(_) \
    XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL(_) \
    XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_XR_USE_PLATFORM_WAYLAND(_) \
    XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_XR_USE_PLATFORM_WIN32(_) \
    XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_XR_USE_PLATFORM_XCB(_) \
    XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_XR_USE_PLATFORM_XLIB(_) \
    XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_OPENGL_ES(_) \
    XR_LIST_STRUCTURE_TYPES_XR_USE_GRAPHICS_API_VULKAN(_) \
    XR_LIST_STRUCTURE_TYPES_XR_USE_PLATFORM_ANDROID(_) \
    XR_LIST_STRUCTURE_TYPES_XR_USE_PLATFORM_ANDROID_XR_USE_GRAPHICS_API_OPENGL_ES(_) \
    XR_LIST_STRUCTURE_TYPES_XR_USE_PLATFORM_EGL(_) \
    XR_LIST_STRUCTURE_TYPES_XR_USE_PLATFORM_WIN32(_) \


#define XR_LIST_EXTENSIONS(_) \
    _(XR_KHR_android_thread_settings, 4) \
    _(XR_KHR_android_surface_swapchain, 5) \
    _(XR_KHR_composition_layer_cube, 7) \
    _(XR_KHR_android_create_instance, 9) \
    _(XR_KHR_composition_layer_depth, 11) \
    _(XR_KHR_vulkan_swapchain_format_list, 15) \
    _(XR_EXT_performance_settings, 16) \
    _(XR_EXT_thermal_query, 17) \
    _(XR_KHR_composition_layer_cylinder, 18) \
    _(XR_KHR_composition_layer_equirect, 19) \
    _(XR_EXT_debug_utils, 20) \
    _(XR_KHR_opengl_enable, 24) \
    _(XR_KHR_opengl_es_enable, 25) \
    _(XR_KHR_vulkan_enable, 26) \
    _(XR_KHR_D3D11_enable, 28) \
    _(XR_KHR_D3D12_enable, 29) \
    _(XR_EXT_eye_gaze_interaction, 31) \
    _(XR_KHR_visibility_mask, 32) \
    _(XR_EXTX_overlay, 34) \
    _(XR_KHR_composition_layer_color_scale_bias, 35) \
    _(XR_KHR_win32_convert_performance_counter_time, 36) \
    _(XR_KHR_convert_timespec_time, 37) \
    _(XR_VARJO_quad_views, 38) \
    _(XR_MSFT_unbounded_reference_space, 39) \
    _(XR_MSFT_spatial_anchor, 40) \
    _(XR_MND_headless, 43) \
    _(XR_OCULUS_android_session_state_enable, 45) \
    _(XR_EXT_view_configuration_depth_range, 47) \
    _(XR_EXT_conformance_automation, 48) \
    _(XR_MNDX_egl_enable, 49) \
    _(XR_MSFT_spatial_graph_bridge, 50) \
    _(XR_MSFT_hand_interaction, 51) \
    _(XR_EXT_hand_tracking, 52) \
    _(XR_MSFT_hand_tracking_mesh, 53) \
    _(XR_MSFT_secondary_view_configuration, 54) \
    _(XR_MSFT_first_person_observer, 55) \
    _(XR_MSFT_controller_model, 56) \
    _(XR_MSFT_perception_anchor_interop, 57) \
    _(XR_EXT_win32_appcontainer_compatible, 58) \
    _(XR_EPIC_view_configuration_fov, 60) \
    _(XR_MSFT_holographic_window_attachment, 64) \
    _(XR_HUAWEI_controller_interaction, 70) \
    _(XR_FB_android_surface_swapchain_create, 71) \
    _(XR_VALVE_analog_threshold, 80) \
    _(XR_KHR_loader_init, 89) \
    _(XR_KHR_loader_init_android, 90) \
    _(XR_KHR_vulkan_enable2, 91) \
    _(XR_KHR_composition_layer_equirect2, 92) \
    _(XR_EXT_samsung_odyssey_controller, 95) \
    _(XR_EXT_hp_mixed_reality_controller, 96) \
    _(XR_MND_swapchain_usage_input_attachment_bit, 97) \
    _(XR_FB_display_refresh_rate, 102) \
    _(XR_HTC_vive_cosmos_controller_interaction, 103) \
    _(XR_FB_color_space, 109) \
    _(XR_KHR_binding_modification, 121) \


#endif

