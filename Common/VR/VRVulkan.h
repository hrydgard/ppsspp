// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

// OpenXR with a Vulkan session (XR_KHR_vulkan_enable). See docs/vr-openxr-vulkan.md.

#include <memory>
#include <string>
#include <vector>

class VulkanContext;
class VulkanPresentation;

// True if OpenXR is up and the runtime can take a Vulkan session.
bool VR_VulkanSupported();
// True if we know there will be no Vulkan session: OpenXR is up without it, or this build can't have one.
// Before OpenXR is up (the config is checked before it), false.
bool VR_VulkanUnsupported();

// What the runtime needs enabled on our VkInstance and VkDevice. The device list's strings stay valid.
std::vector<std::string> VR_GetVulkanInstanceExtensions();
const std::vector<const char *> &VR_GetVulkanDeviceExtensions();

// The physical device the headset is driven by, as an index for VulkanContext::CreateDevice. -1 if unknown.
int VR_GetVulkanPhysicalDevice(VulkanContext *vulkan);

// Creates the session the first time, then a presentation that renders into the XR swapchain.
// The device and graphics queue must exist.
std::unique_ptr<VulkanPresentation> VR_CreateVulkanPresentation(VulkanContext *vulkan, std::string *error);

// Before the device goes.
void VR_DestroyVulkanSession();
