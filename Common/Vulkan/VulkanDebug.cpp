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
#include <cassert>
#include <sstream>

#include "base/logging.h"
#include "Common/Vulkan/VulkanContext.h"
#include "Common/Vulkan/VulkanDebug.h"

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugUtilsCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT                  messageType,
	const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
	void*                                            pUserData) {
	const VulkanLogOptions *options = (const VulkanLogOptions *)pUserData;
	std::ostringstream message;

	const char *pMessage = pCallbackData->pMessage;

	// Apparent bugs around timestamp validation in the validation layers
	if (strstr(pMessage, "vkCmdBeginQuery(): VkQueryPool"))
		return false;
	if (strstr(pMessage, "vkGetQueryPoolResults() on VkQueryPool"))
		return false;

	int messageCode = pCallbackData->messageIdNumber;
	const char *pLayerPrefix = "";
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
	message << ":" << pCallbackData->messageIdNumber << ") " << pMessage << "\n";

	std::string msg = message.str();

#ifdef _WIN32
	OutputDebugStringA(msg.c_str());
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		if (options->breakOnError && IsDebuggerPresent()) {
			DebugBreak();
		}
		if (options->msgBoxOnError) {
			MessageBoxA(NULL, pMessage, "Alert", MB_OK);
		}
	} else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		// Don't break on perf warnings for now, even with a debugger. We log them at least.
		if (options->breakOnWarning && IsDebuggerPresent() && 0 == (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)) {
			DebugBreak();
		}
	}
#else
	// TODO: Improve.
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
		ELOG("VKDEBUG: %s", msg.c_str());
	} else {
		WLOG("VKDEBUG: %s", msg.c_str());
	}
#endif

	// false indicates that layer should not bail-out of an
	// API call that had validation failures. This may mean that the
	// app dies inside the driver due to invalid parameter(s).
	// That's what would happen without validation layers, so we'll
	// keep that behavior here.
	return false;
}
