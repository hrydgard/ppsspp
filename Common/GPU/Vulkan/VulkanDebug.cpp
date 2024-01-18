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

#include <string>
#include <sstream>
#include <map>
#include <mutex>

#include "Common/Log.h"
#include "Common/System/System.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanDebug.h"

const int MAX_SAME_ERROR_COUNT = 10;

// Used to stop outputting the same message over and over.
static std::map<int, int> g_errorCount;
std::mutex g_errorCountMutex;

// TODO: Call this when launching games in some clean way.
void VulkanClearValidationErrorCounts() {
	std::lock_guard<std::mutex> lock(g_errorCountMutex);
	g_errorCount.clear();
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugUtilsCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT                  messageType,
	const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
	void *pUserData) {
	const VulkanLogOptions *options = (const VulkanLogOptions *)pUserData;
	std::ostringstream message;

	const char *pMessage = pCallbackData->pMessage;

	int messageCode = pCallbackData->messageIdNumber;
	switch (messageCode) {
	case 101294395:
		// UNASSIGNED-CoreValidation-Shader-OutputNotConsumed - benign perf warning
		return false;
	case 1303270965:
		// Benign perf warning, image blit using GENERAL layout.
		// TODO: Oops, turns out we filtered out a bit too much here!
		// We really need that performance flag check to sort out the stuff that matters.
		// Will enable it soon, but it'll take some fixing.
		//
		if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
			return false;
		break;

	case 606910136:
	case -392708513:
	case -384083808:
		// VUID-vkCmdDraw-None-02686
		// Kinda false positive, or at least very unnecessary, now that I solved the real issue.
		// See https://github.com/hrydgard/ppsspp/pull/16354
		return false;
	case -375211665:
		// VUID-vkAllocateMemory-pAllocateInfo-01713
		// Can happen when VMA aggressively tries to allocate aperture memory for upload. It gracefully
		// falls back to regular video memory, so we just ignore this. I'd argue this is a VMA bug, actually.
		return false;
	case 181611958:
		// Extended validation.
		// UNASSIGNED-BestPractices-vkCreateDevice-deprecated-extension
		// Doing what this one says doesn't seem very reliable - if I rely strictly on the Vulkan version, I don't get some function pointers? Like createrenderpass2.
		return false;
	case 657182421:
		// Extended validation (ARM best practices)
		// Non-fifo validation not recommended
		return false;
	case 337425955:
		// False positive
		// https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/3615
		return false;

	case 1835555994: // [AMD] [NVIDIA] Performance warning : Pipeline VkPipeline 0xa808d50000000033[global_texcolor] was bound twice in the frame.
		// Benign perf warnings.
		return false;

	case 1810669668:
		// Clear value but no LOAD_OP_CLEAR. Not worth fixing right now.
		return false;

	case 1544472022:
		// MSAA depth resolve write-after-write??
		return false;

	default:
		break;
	}

	/*
	// Can be used to temporarily turn errors into info for easier debugging.
	switch (messageCode) {
	case 1544472022:
		if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
			messageSeverity = (VkDebugUtilsMessageSeverityFlagBitsEXT)((messageSeverity & ~VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT);
		}
		break;
	default:
		break;
	}
	*/

	int count;
	{
		std::lock_guard<std::mutex> lock(g_errorCountMutex);
		count = g_errorCount[messageCode]++;
	}
	if (count == MAX_SAME_ERROR_COUNT) {
		WARN_LOG(G3D, "Too many validation messages with message %d, stopping", messageCode);
	}
	if (count >= MAX_SAME_ERROR_COUNT) {
		return false;
	}

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		message << "ERROR(";
	} else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		message << "WARNING(";
	} else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
		message << "INFO(";
	} else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
		message << "VERBOSE(";
	}

	if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
		message << "perf";
	} else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
		message << "general";
	} else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
		message << "validation";
	}
	message << ":" << messageCode << ") " << pMessage << "\n";

	std::string msg = message.str();

#ifdef _WIN32
	OutputDebugStringA(msg.c_str());
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		if (options->breakOnError && System_GetPropertyBool(SYSPROP_DEBUGGER_PRESENT)) {
			DebugBreak();
		}
		if (options->msgBoxOnError) {
			MessageBoxA(NULL, pMessage, "Alert", MB_OK);
		}
	} else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		// Don't break on perf warnings for now, even with a debugger. We log them at least.
		if (options->breakOnWarning && System_GetPropertyBool(SYSPROP_DEBUGGER_PRESENT) && 0 == (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)) {
			DebugBreak();
		}
	}
#endif

	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		ERROR_LOG(G3D, "VKDEBUG: %s", msg.c_str());
	} else {
		WARN_LOG(G3D, "VKDEBUG: %s", msg.c_str());
	}

	// false indicates that layer should not bail-out of an
	// API call that had validation failures. This may mean that the
	// app dies inside the driver due to invalid parameter(s).
	// That's what would happen without validation layers, so we'll
	// keep that behavior here.
	return false;
}
