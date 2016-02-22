// Copyright (c) 2016- PPSSPP Project.

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

#include "Common/Vulkan/VulkanLoader.h"

#ifndef _WIN32
#include <dlfcn.h>
#endif


PFN_vkCreateInstance vkCreateInstance;
PFN_vkDestroyInstance vkDestroyInstance;
PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;
PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;
PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties;
PFN_vkGetPhysicalDeviceImageFormatProperties vkGetPhysicalDeviceImageFormatProperties;
PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;
PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties;
PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;
PFN_vkCreateDevice vkCreateDevice;
PFN_vkDestroyDevice vkDestroyDevice;
PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;
PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties;
PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties;
PFN_vkEnumerateDeviceLayerProperties vkEnumerateDeviceLayerProperties;
PFN_vkGetDeviceQueue vkGetDeviceQueue;
PFN_vkQueueSubmit vkQueueSubmit;
PFN_vkQueueWaitIdle vkQueueWaitIdle;
PFN_vkDeviceWaitIdle vkDeviceWaitIdle;
PFN_vkAllocateMemory vkAllocateMemory;
PFN_vkFreeMemory vkFreeMemory;
PFN_vkMapMemory vkMapMemory;
PFN_vkUnmapMemory vkUnmapMemory;
PFN_vkFlushMappedMemoryRanges vkFlushMappedMemoryRanges;
PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges;
PFN_vkGetDeviceMemoryCommitment vkGetDeviceMemoryCommitment;
PFN_vkBindBufferMemory vkBindBufferMemory;
PFN_vkBindImageMemory vkBindImageMemory;
PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;
PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;
PFN_vkGetImageSparseMemoryRequirements vkGetImageSparseMemoryRequirements;
PFN_vkGetPhysicalDeviceSparseImageFormatProperties vkGetPhysicalDeviceSparseImageFormatProperties;
PFN_vkQueueBindSparse vkQueueBindSparse;
PFN_vkCreateFence vkCreateFence;
PFN_vkDestroyFence vkDestroyFence;
PFN_vkResetFences vkResetFences;
PFN_vkGetFenceStatus vkGetFenceStatus;
PFN_vkWaitForFences vkWaitForFences;
PFN_vkCreateSemaphore vkCreateSemaphore;
PFN_vkDestroySemaphore vkDestroySemaphore;
PFN_vkCreateEvent vkCreateEvent;
PFN_vkDestroyEvent vkDestroyEvent;
PFN_vkGetEventStatus vkGetEventStatus;
PFN_vkSetEvent vkSetEvent;
PFN_vkResetEvent vkResetEvent;
PFN_vkCreateQueryPool vkCreateQueryPool;
PFN_vkDestroyQueryPool vkDestroyQueryPool;
PFN_vkGetQueryPoolResults vkGetQueryPoolResults;
PFN_vkCreateBuffer vkCreateBuffer;
PFN_vkDestroyBuffer vkDestroyBuffer;
PFN_vkCreateBufferView vkCreateBufferView;
PFN_vkDestroyBufferView vkDestroyBufferView;
PFN_vkCreateImage vkCreateImage;
PFN_vkDestroyImage vkDestroyImage;
PFN_vkGetImageSubresourceLayout vkGetImageSubresourceLayout;
PFN_vkCreateImageView vkCreateImageView;
PFN_vkDestroyImageView vkDestroyImageView;
PFN_vkCreateShaderModule vkCreateShaderModule;
PFN_vkDestroyShaderModule vkDestroyShaderModule;
PFN_vkCreatePipelineCache vkCreatePipelineCache;
PFN_vkDestroyPipelineCache vkDestroyPipelineCache;
PFN_vkGetPipelineCacheData vkGetPipelineCacheData;
PFN_vkMergePipelineCaches vkMergePipelineCaches;
PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines;
PFN_vkCreateComputePipelines vkCreateComputePipelines;
PFN_vkDestroyPipeline vkDestroyPipeline;
PFN_vkCreatePipelineLayout vkCreatePipelineLayout;
PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout;
PFN_vkCreateSampler vkCreateSampler;
PFN_vkDestroySampler vkDestroySampler;
PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;
PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout;
PFN_vkCreateDescriptorPool vkCreateDescriptorPool;
PFN_vkDestroyDescriptorPool vkDestroyDescriptorPool;
PFN_vkResetDescriptorPool vkResetDescriptorPool;
PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;
PFN_vkFreeDescriptorSets vkFreeDescriptorSets;
PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;
PFN_vkCreateFramebuffer vkCreateFramebuffer;
PFN_vkDestroyFramebuffer vkDestroyFramebuffer;
PFN_vkCreateRenderPass vkCreateRenderPass;
PFN_vkDestroyRenderPass vkDestroyRenderPass;
PFN_vkGetRenderAreaGranularity vkGetRenderAreaGranularity;
PFN_vkCreateCommandPool vkCreateCommandPool;
PFN_vkDestroyCommandPool vkDestroyCommandPool;
PFN_vkResetCommandPool vkResetCommandPool;
PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;
PFN_vkFreeCommandBuffers vkFreeCommandBuffers;
PFN_vkBeginCommandBuffer vkBeginCommandBuffer;
PFN_vkEndCommandBuffer vkEndCommandBuffer;
PFN_vkResetCommandBuffer vkResetCommandBuffer;
PFN_vkCmdBindPipeline vkCmdBindPipeline;
PFN_vkCmdSetViewport vkCmdSetViewport;
PFN_vkCmdSetScissor vkCmdSetScissor;
PFN_vkCmdSetLineWidth vkCmdSetLineWidth;
PFN_vkCmdSetDepthBias vkCmdSetDepthBias;
PFN_vkCmdSetBlendConstants vkCmdSetBlendConstants;
PFN_vkCmdSetDepthBounds vkCmdSetDepthBounds;
PFN_vkCmdSetStencilCompareMask vkCmdSetStencilCompareMask;
PFN_vkCmdSetStencilWriteMask vkCmdSetStencilWriteMask;
PFN_vkCmdSetStencilReference vkCmdSetStencilReference;
PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;
PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer;
PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers;
PFN_vkCmdDraw vkCmdDraw;
PFN_vkCmdDrawIndexed vkCmdDrawIndexed;
PFN_vkCmdDrawIndirect vkCmdDrawIndirect;
PFN_vkCmdDrawIndexedIndirect vkCmdDrawIndexedIndirect;
PFN_vkCmdDispatch vkCmdDispatch;
PFN_vkCmdDispatchIndirect vkCmdDispatchIndirect;
PFN_vkCmdCopyBuffer vkCmdCopyBuffer;
PFN_vkCmdCopyImage vkCmdCopyImage;
PFN_vkCmdBlitImage vkCmdBlitImage;
PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;
PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer;
PFN_vkCmdUpdateBuffer vkCmdUpdateBuffer;
PFN_vkCmdFillBuffer vkCmdFillBuffer;
PFN_vkCmdClearColorImage vkCmdClearColorImage;
PFN_vkCmdClearDepthStencilImage vkCmdClearDepthStencilImage;
PFN_vkCmdClearAttachments vkCmdClearAttachments;
PFN_vkCmdResolveImage vkCmdResolveImage;
PFN_vkCmdSetEvent vkCmdSetEvent;
PFN_vkCmdResetEvent vkCmdResetEvent;
PFN_vkCmdWaitEvents vkCmdWaitEvents;
PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;
PFN_vkCmdBeginQuery vkCmdBeginQuery;
PFN_vkCmdEndQuery vkCmdEndQuery;
PFN_vkCmdResetQueryPool vkCmdResetQueryPool;
PFN_vkCmdWriteTimestamp vkCmdWriteTimestamp;
PFN_vkCmdCopyQueryPoolResults vkCmdCopyQueryPoolResults;
PFN_vkCmdPushConstants vkCmdPushConstants;
PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass;
PFN_vkCmdNextSubpass vkCmdNextSubpass;
PFN_vkCmdEndRenderPass vkCmdEndRenderPass;
PFN_vkCmdExecuteCommands vkCmdExecuteCommands;

#ifdef ANDROID
PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR;
#elif defined(_WIN32)
PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
#endif

PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;

// WSI extension.
PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;
PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;
PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;
PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;
PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;
PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;
PFN_vkQueuePresentKHR vkQueuePresentKHR;

// And the DEBUG_REPORT extension.
PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT;
PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT;

#ifdef _WIN32
static HINSTANCE vulkanLibrary;
#define dlsym(x, y) GetProcAddress(x, y)
#else
static void *vulkanLibrary;
#endif

bool VulkanLoad() {

#ifndef _WIN32
  vulkanLibrary = dlopen("libvulkan.sh", RTLD_NOW | RTLD_LOCAL);
  if (!vulkanLibrary) {
    return false;
  }
#else
  // LoadLibrary etc
	vulkanLibrary = LoadLibrary(L"vulkan-1.dll");
#endif

  vkCreateInstance = (PFN_vkCreateInstance)dlsym(vulkanLibrary, "vkCreateInstance");
  vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(vulkanLibrary, "vkGetInstanceProcAddr");
  vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)dlsym(vulkanLibrary, "vkGetDeviceProcAddr");

  // Why do we need to load these separately?
  vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)dlsym(vulkanLibrary, "vkEnumerateInstanceExtensionProperties");
  vkEnumerateInstanceLayerProperties = (PFN_vkEnumerateInstanceLayerProperties)dlsym(vulkanLibrary, "vkEnumerateInstanceLayerProperties");
  return true;
}

void VulkanLoadInstanceFunctions(VkInstance instance) {
  // OK, let's use the above functions to get the rest.
  vkDestroyInstance = (PFN_vkDestroyInstance)vkGetInstanceProcAddr(instance, "vkDestroyInstance");
  vkEnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)vkGetInstanceProcAddr(instance, "vkEnumeratePhysicalDevices");
  vkGetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures");
  vkGetPhysicalDeviceFormatProperties = (PFN_vkGetPhysicalDeviceFormatProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFormatProperties");
  vkGetPhysicalDeviceImageFormatProperties = (PFN_vkGetPhysicalDeviceImageFormatProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceImageFormatProperties");
  vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties");
  vkGetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
  vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceMemoryProperties");
  vkCreateDevice = (PFN_vkCreateDevice)vkGetInstanceProcAddr(instance, "vkCreateDevice");
  vkDestroyDevice = (PFN_vkDestroyDevice)vkGetInstanceProcAddr(instance, "vkDestroyDevice");
  vkEnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)vkGetInstanceProcAddr(instance, "vkEnumerateDeviceExtensionProperties");
  vkEnumerateDeviceLayerProperties = (PFN_vkEnumerateDeviceLayerProperties)vkGetInstanceProcAddr(instance, "vkEnumerateDeviceLayerProperties");
  vkGetDeviceQueue = (PFN_vkGetDeviceQueue)vkGetInstanceProcAddr(instance, "vkGetDeviceQueue");
  vkQueueSubmit = (PFN_vkQueueSubmit)vkGetInstanceProcAddr(instance, "vkQueueSubmit");
  vkQueueWaitIdle = (PFN_vkQueueWaitIdle)vkGetInstanceProcAddr(instance, "vkQueueWaitIdle");
  vkDeviceWaitIdle = (PFN_vkDeviceWaitIdle)vkGetInstanceProcAddr(instance, "vkDeviceWaitIdle");
  vkAllocateMemory = (PFN_vkAllocateMemory)vkGetInstanceProcAddr(instance, "vkAllocateMemory");
  vkFreeMemory = (PFN_vkFreeMemory)vkGetInstanceProcAddr(instance, "vkFreeMemory");
  vkMapMemory = (PFN_vkMapMemory)vkGetInstanceProcAddr(instance, "vkMapMemory");
  vkUnmapMemory = (PFN_vkUnmapMemory)vkGetInstanceProcAddr(instance, "vkUnmapMemory");
  vkFlushMappedMemoryRanges = (PFN_vkFlushMappedMemoryRanges)vkGetInstanceProcAddr(instance, "vkFlushMappedMemoryRanges");
  vkInvalidateMappedMemoryRanges = (PFN_vkInvalidateMappedMemoryRanges)vkGetInstanceProcAddr(instance, "vkInvalidateMappedMemoryRanges");
  vkGetDeviceMemoryCommitment = (PFN_vkGetDeviceMemoryCommitment)vkGetInstanceProcAddr(instance, "vkGetDeviceMemoryCommitment");
  vkBindBufferMemory = (PFN_vkBindBufferMemory)vkGetInstanceProcAddr(instance, "vkBindBufferMemory");
  vkBindImageMemory = (PFN_vkBindImageMemory)vkGetInstanceProcAddr(instance, "vkBindImageMemory");
  vkGetBufferMemoryRequirements = (PFN_vkGetBufferMemoryRequirements)vkGetInstanceProcAddr(instance, "vkGetBufferMemoryRequirements");
  vkGetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)vkGetInstanceProcAddr(instance, "vkGetImageMemoryRequirements");
  vkGetImageSparseMemoryRequirements = (PFN_vkGetImageSparseMemoryRequirements)vkGetInstanceProcAddr(instance, "vkGetImageSparseMemoryRequirements");
  vkGetPhysicalDeviceSparseImageFormatProperties = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSparseImageFormatProperties");
  vkQueueBindSparse = (PFN_vkQueueBindSparse)vkGetInstanceProcAddr(instance, "vkQueueBindSparse");
  vkCreateFence = (PFN_vkCreateFence)vkGetInstanceProcAddr(instance, "vkCreateFence");
  vkDestroyFence = (PFN_vkDestroyFence)vkGetInstanceProcAddr(instance, "vkDestroyFence");
  vkResetFences = (PFN_vkResetFences)vkGetInstanceProcAddr(instance, "vkResetFences");
  vkGetFenceStatus = (PFN_vkGetFenceStatus)vkGetInstanceProcAddr(instance, "vkGetFenceStatus");
  vkWaitForFences = (PFN_vkWaitForFences)vkGetInstanceProcAddr(instance, "vkWaitForFences");
  vkCreateSemaphore = (PFN_vkCreateSemaphore)vkGetInstanceProcAddr(instance, "vkCreateSemaphore");
  vkDestroySemaphore = (PFN_vkDestroySemaphore)vkGetInstanceProcAddr(instance, "vkDestroySemaphore");
  vkCreateEvent = (PFN_vkCreateEvent)vkGetInstanceProcAddr(instance, "vkCreateEvent");
  vkDestroyEvent = (PFN_vkDestroyEvent)vkGetInstanceProcAddr(instance, "vkDestroyEvent");
  vkGetEventStatus = (PFN_vkGetEventStatus)vkGetInstanceProcAddr(instance, "vkGetEventStatus");
  vkSetEvent = (PFN_vkSetEvent)vkGetInstanceProcAddr(instance, "vkSetEvent");
  vkResetEvent = (PFN_vkResetEvent)vkGetInstanceProcAddr(instance, "vkResetEvent");
  vkCreateQueryPool = (PFN_vkCreateQueryPool)vkGetInstanceProcAddr(instance, "vkCreateQueryPool");
  vkDestroyQueryPool = (PFN_vkDestroyQueryPool)vkGetInstanceProcAddr(instance, "vkDestroyQueryPool");
  vkGetQueryPoolResults = (PFN_vkGetQueryPoolResults)vkGetInstanceProcAddr(instance, "vkGetQueryPoolResults");
  vkCreateBuffer = (PFN_vkCreateBuffer)vkGetInstanceProcAddr(instance, "vkCreateBuffer");
  vkDestroyBuffer = (PFN_vkDestroyBuffer)vkGetInstanceProcAddr(instance, "vkDestroyBuffer");
  vkCreateBufferView = (PFN_vkCreateBufferView)vkGetInstanceProcAddr(instance, "vkCreateBufferView");
  vkDestroyBufferView = (PFN_vkDestroyBufferView)vkGetInstanceProcAddr(instance, "vkDestroyBufferView");
  vkCreateImage = (PFN_vkCreateImage)vkGetInstanceProcAddr(instance, "vkCreateImage");
  vkDestroyImage = (PFN_vkDestroyImage)vkGetInstanceProcAddr(instance, "vkDestroyImage");
  vkGetImageSubresourceLayout = (PFN_vkGetImageSubresourceLayout)vkGetInstanceProcAddr(instance, "vkGetImageSubresourceLayout");
  vkCreateImageView = (PFN_vkCreateImageView)vkGetInstanceProcAddr(instance, "vkCreateImageView");
  vkDestroyImageView = (PFN_vkDestroyImageView)vkGetInstanceProcAddr(instance, "vkDestroyImageView");
  vkCreateShaderModule = (PFN_vkCreateShaderModule)vkGetInstanceProcAddr(instance, "vkCreateShaderModule");
  vkDestroyShaderModule = (PFN_vkDestroyShaderModule)vkGetInstanceProcAddr(instance, "vkDestroyShaderModule");
  vkCreatePipelineCache = (PFN_vkCreatePipelineCache)vkGetInstanceProcAddr(instance, "vkCreatePipelineCache");
  vkDestroyPipelineCache = (PFN_vkDestroyPipelineCache)vkGetInstanceProcAddr(instance, "vkDestroyPipelineCache");
  vkGetPipelineCacheData = (PFN_vkGetPipelineCacheData)vkGetInstanceProcAddr(instance, "vkGetPipelineCacheData");
  vkMergePipelineCaches = (PFN_vkMergePipelineCaches)vkGetInstanceProcAddr(instance, "vkMergePipelineCaches");
  vkCreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines)vkGetInstanceProcAddr(instance, "vkCreateGraphicsPipelines");
  vkCreateComputePipelines = (PFN_vkCreateComputePipelines)vkGetInstanceProcAddr(instance, "vkCreateComputePipelines");
  vkDestroyPipeline = (PFN_vkDestroyPipeline)vkGetInstanceProcAddr(instance, "vkDestroyPipeline");
  vkCreatePipelineLayout = (PFN_vkCreatePipelineLayout)vkGetInstanceProcAddr(instance, "vkCreatePipelineLayout");
  vkDestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)vkGetInstanceProcAddr(instance, "vkDestroyPipelineLayout");
  vkCreateSampler = (PFN_vkCreateSampler)vkGetInstanceProcAddr(instance, "vkCreateSampler");
  vkDestroySampler = (PFN_vkDestroySampler)vkGetInstanceProcAddr(instance, "vkDestroySampler");
  vkCreateDescriptorSetLayout = (PFN_vkCreateDescriptorSetLayout)vkGetInstanceProcAddr(instance, "vkCreateDescriptorSetLayout");
  vkDestroyDescriptorSetLayout = (PFN_vkDestroyDescriptorSetLayout)vkGetInstanceProcAddr(instance, "vkDestroyDescriptorSetLayout");
  vkCreateDescriptorPool = (PFN_vkCreateDescriptorPool)vkGetInstanceProcAddr(instance, "vkCreateDescriptorPool");
  vkDestroyDescriptorPool = (PFN_vkDestroyDescriptorPool)vkGetInstanceProcAddr(instance, "vkDestroyDescriptorPool");
  vkResetDescriptorPool = (PFN_vkResetDescriptorPool)vkGetInstanceProcAddr(instance, "vkResetDescriptorPool");
  vkAllocateDescriptorSets = (PFN_vkAllocateDescriptorSets)vkGetInstanceProcAddr(instance, "vkAllocateDescriptorSets");
  vkFreeDescriptorSets = (PFN_vkFreeDescriptorSets)vkGetInstanceProcAddr(instance, "vkFreeDescriptorSets");
  vkUpdateDescriptorSets = (PFN_vkUpdateDescriptorSets)vkGetInstanceProcAddr(instance, "vkUpdateDescriptorSets");
  vkCreateFramebuffer = (PFN_vkCreateFramebuffer)vkGetInstanceProcAddr(instance, "vkCreateFramebuffer");
  vkDestroyFramebuffer = (PFN_vkDestroyFramebuffer)vkGetInstanceProcAddr(instance, "vkDestroyFramebuffer");
  vkCreateRenderPass = (PFN_vkCreateRenderPass)vkGetInstanceProcAddr(instance, "vkCreateRenderPass");
  vkDestroyRenderPass = (PFN_vkDestroyRenderPass)vkGetInstanceProcAddr(instance, "vkDestroyRenderPass");
  vkGetRenderAreaGranularity = (PFN_vkGetRenderAreaGranularity)vkGetInstanceProcAddr(instance, "vkGetRenderAreaGranularity");
  vkCreateCommandPool = (PFN_vkCreateCommandPool)vkGetInstanceProcAddr(instance, "vkCreateCommandPool");
  vkDestroyCommandPool = (PFN_vkDestroyCommandPool)vkGetInstanceProcAddr(instance, "vkDestroyCommandPool");
  vkResetCommandPool = (PFN_vkResetCommandPool)vkGetInstanceProcAddr(instance, "vkResetCommandPool");
  vkAllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)vkGetInstanceProcAddr(instance, "vkAllocateCommandBuffers");
  vkFreeCommandBuffers = (PFN_vkFreeCommandBuffers)vkGetInstanceProcAddr(instance, "vkFreeCommandBuffers");
  vkBeginCommandBuffer = (PFN_vkBeginCommandBuffer)vkGetInstanceProcAddr(instance, "vkBeginCommandBuffer");
  vkEndCommandBuffer = (PFN_vkEndCommandBuffer)vkGetInstanceProcAddr(instance, "vkEndCommandBuffer");
  vkResetCommandBuffer = (PFN_vkResetCommandBuffer)vkGetInstanceProcAddr(instance, "vkResetCommandBuffer");
  vkCmdBindPipeline = (PFN_vkCmdBindPipeline)vkGetInstanceProcAddr(instance, "vkCmdBindPipeline");
  vkCmdSetViewport = (PFN_vkCmdSetViewport)vkGetInstanceProcAddr(instance, "vkCmdSetViewport");
  vkCmdSetScissor = (PFN_vkCmdSetScissor)vkGetInstanceProcAddr(instance, "vkCmdSetScissor");
  vkCmdSetLineWidth = (PFN_vkCmdSetLineWidth)vkGetInstanceProcAddr(instance, "vkCmdSetLineWidth");
  vkCmdSetDepthBias = (PFN_vkCmdSetDepthBias)vkGetInstanceProcAddr(instance, "vkCmdSetDepthBias");
  vkCmdSetBlendConstants = (PFN_vkCmdSetBlendConstants)vkGetInstanceProcAddr(instance, "vkCmdSetBlendConstants");
  vkCmdSetDepthBounds = (PFN_vkCmdSetDepthBounds)vkGetInstanceProcAddr(instance, "vkCmdSetDepthBounds");
  vkCmdSetStencilCompareMask = (PFN_vkCmdSetStencilCompareMask)vkGetInstanceProcAddr(instance, "vkCmdSetStencilCompareMask");
  vkCmdSetStencilWriteMask = (PFN_vkCmdSetStencilWriteMask)vkGetInstanceProcAddr(instance, "vkCmdSetStencilWriteMask");
  vkCmdSetStencilReference = (PFN_vkCmdSetStencilReference)vkGetInstanceProcAddr(instance, "vkCmdSetStencilReference");
  vkCmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)vkGetInstanceProcAddr(instance, "vkCmdBindDescriptorSets");
  vkCmdBindIndexBuffer = (PFN_vkCmdBindIndexBuffer)vkGetInstanceProcAddr(instance, "vkCmdBindIndexBuffer");
  vkCmdBindVertexBuffers = (PFN_vkCmdBindVertexBuffers)vkGetInstanceProcAddr(instance, "vkCmdBindVertexBuffers");
  vkCmdDraw = (PFN_vkCmdDraw)vkGetInstanceProcAddr(instance, "vkCmdDraw");
  vkCmdDrawIndexed = (PFN_vkCmdDrawIndexed)vkGetInstanceProcAddr(instance, "vkCmdDrawIndexed");
  vkCmdDrawIndirect = (PFN_vkCmdDrawIndirect)vkGetInstanceProcAddr(instance, "vkCmdDrawIndirect");
  vkCmdDrawIndexedIndirect = (PFN_vkCmdDrawIndexedIndirect)vkGetInstanceProcAddr(instance, "vkCmdDrawIndexedIndirect");
  vkCmdDispatch = (PFN_vkCmdDispatch)vkGetInstanceProcAddr(instance, "vkCmdDispatch");
  vkCmdDispatchIndirect = (PFN_vkCmdDispatchIndirect)vkGetInstanceProcAddr(instance, "vkCmdDispatchIndirect");
  vkCmdCopyBuffer = (PFN_vkCmdCopyBuffer)vkGetInstanceProcAddr(instance, "vkCmdCopyBuffer");
  vkCmdCopyImage = (PFN_vkCmdCopyImage)vkGetInstanceProcAddr(instance, "vkCmdCopyImage");
  vkCmdBlitImage = (PFN_vkCmdBlitImage)vkGetInstanceProcAddr(instance, "vkCmdBlitImage");
  vkCmdCopyBufferToImage = (PFN_vkCmdCopyBufferToImage)vkGetInstanceProcAddr(instance, "vkCmdCopyBufferToImage");
  vkCmdCopyImageToBuffer = (PFN_vkCmdCopyImageToBuffer)vkGetInstanceProcAddr(instance, "vkCmdCopyImageToBuffer");
  vkCmdUpdateBuffer = (PFN_vkCmdUpdateBuffer)vkGetInstanceProcAddr(instance, "vkCmdUpdateBuffer");
  vkCmdFillBuffer = (PFN_vkCmdFillBuffer)vkGetInstanceProcAddr(instance, "vkCmdFillBuffer");
  vkCmdClearColorImage = (PFN_vkCmdClearColorImage)vkGetInstanceProcAddr(instance, "vkCmdClearColorImage");
  vkCmdClearDepthStencilImage = (PFN_vkCmdClearDepthStencilImage)vkGetInstanceProcAddr(instance, "vkCmdClearDepthStencilImage");
  vkCmdClearAttachments = (PFN_vkCmdClearAttachments)vkGetInstanceProcAddr(instance, "vkCmdClearAttachments");
  vkCmdResolveImage = (PFN_vkCmdResolveImage)vkGetInstanceProcAddr(instance, "vkCmdResolveImage");
  vkCmdSetEvent = (PFN_vkCmdSetEvent)vkGetInstanceProcAddr(instance, "vkCmdSetEvent");
  vkCmdResetEvent = (PFN_vkCmdResetEvent)vkGetInstanceProcAddr(instance, "vkCmdResetEvent");
  vkCmdWaitEvents = (PFN_vkCmdWaitEvents)vkGetInstanceProcAddr(instance, "vkCmdWaitEvents");
  vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)vkGetInstanceProcAddr(instance, "vkCmdPipelineBarrier");
  vkCmdBeginQuery = (PFN_vkCmdBeginQuery)vkGetInstanceProcAddr(instance, "vkCmdBeginQuery");
  vkCmdEndQuery = (PFN_vkCmdEndQuery)vkGetInstanceProcAddr(instance, "vkCmdEndQuery");
  vkCmdResetQueryPool = (PFN_vkCmdResetQueryPool)vkGetInstanceProcAddr(instance, "vkCmdResetQueryPool");
  vkCmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)vkGetInstanceProcAddr(instance, "vkCmdWriteTimestamp");
  vkCmdCopyQueryPoolResults = (PFN_vkCmdCopyQueryPoolResults)vkGetInstanceProcAddr(instance, "vkCmdCopyQueryPoolResults");
  vkCmdPushConstants = (PFN_vkCmdPushConstants)vkGetInstanceProcAddr(instance, "vkCmdPushConstants");
  vkCmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)vkGetInstanceProcAddr(instance, "vkCmdBeginRenderPass");
  vkCmdNextSubpass = (PFN_vkCmdNextSubpass)vkGetInstanceProcAddr(instance, "vkCmdNextSubpass");
  vkCmdEndRenderPass = (PFN_vkCmdEndRenderPass)vkGetInstanceProcAddr(instance, "vkCmdEndRenderPass");
  vkCmdExecuteCommands = (PFN_vkCmdExecuteCommands)vkGetInstanceProcAddr(instance, "vkCmdExecuteCommands");
	
	vkGetPhysicalDeviceSurfaceSupportKHR = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
	vkGetPhysicalDeviceSurfaceFormatsKHR = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
	vkGetPhysicalDeviceSurfacePresentModesKHR = (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");

	vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)vkGetInstanceProcAddr(instance, "vkCreateSwapchainKHR");
	vkDestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)vkGetInstanceProcAddr(instance, "vkDestroySwapchainKHR");
	vkGetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR)vkGetInstanceProcAddr(instance, "vkGetSwapchainImagesKHR");
	vkAcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)vkGetInstanceProcAddr(instance, "vkAcquireNextImageKHR");
	vkQueuePresentKHR = (PFN_vkQueuePresentKHR)vkGetInstanceProcAddr(instance, "vkQueuePresentKHR");

#ifdef _WIN32
	vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");
#elif defined(ANDROID)
	vkCreateAndroidSurfaceKHR = (PFN_vkCreateAndroidSurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR");
#endif

	vkDestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)vkGetInstanceProcAddr(instance, "vkDestroySurfaceKHR");

	vkCreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
	vkDestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
}

// On some implementations, loading functions (that have Device as their first parameter) via vkGetDeviceProcAddr may
// increase performance - but then these function pointers will only work on that specific device. Thus, this loader is not very
// good for multi-device.
void VulkanLoadDeviceFunctions(VkDevice device) {
  // TODO: Move more functions VulkanLoadInstanceFunctions to here.
	vkCreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR");
	vkDestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR");
	vkGetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR)vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR");
	vkAcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR");
	vkQueuePresentKHR = (PFN_vkQueuePresentKHR)vkGetDeviceProcAddr(device, "vkQueuePresentKHR");
}

void VulkanFree() {
#ifdef _WIN32
	if (vulkanLibrary) {
		FreeLibrary(vulkanLibrary);
	}
#else
	if (vulkanLibrary) {
		dlclose(vulkanLibrary);
	}
#endif
}
