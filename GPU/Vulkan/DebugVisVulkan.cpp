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

#include <algorithm>
#include <sstream>
#include <cstring>

#include "Common/Render/DrawBuffer.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"

#include "ext/vma/vk_mem_alloc.h"

#include "DebugVisVulkan.h"
#include "Common/GPU/GPUBackendCommon.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "Common/GPU/Vulkan/VulkanImage.h"
#include "Common/Data/Text/Parsers.h"
#include "GPU/Vulkan/GPU_Vulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"

#include "Core/Config.h"

#undef DrawText

bool comparePushBufferNames(const GPUMemoryManager *a, const GPUMemoryManager *b) {
	return strcmp(a->Name(), b->Name()) < 0;
}

void DrawGPUMemoryVis(UIContext *ui, GPUInterface *gpu) {
	// This one will simply display stats.
	Draw::DrawContext *draw = ui->GetDrawContext();

	std::stringstream str;

	VulkanContext *vulkan = (VulkanContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT);
	if (vulkan) {
		VmaTotalStatistics vmaStats;
		vmaCalculateStatistics(vulkan->Allocator(), &vmaStats);

		std::vector<VmaBudget> budgets;
		budgets.resize(vulkan->GetMemoryProperties().memoryHeapCount);
		vmaGetHeapBudgets(vulkan->Allocator(), &budgets[0]);

		size_t totalBudget = 0;
		size_t totalUsedBytes = 0;
		for (auto &budget : budgets) {
			totalBudget += budget.budget;
			totalUsedBytes += budget.usage;
		}

		str << vulkan->GetPhysicalDeviceProperties().properties.deviceName << std::endl;
		str << "Allocated " << NiceSizeFormat(vmaStats.total.statistics.allocationBytes) << " in " << vmaStats.total.statistics.allocationCount << " allocs" << std::endl;
		// Note: The overall number includes stuff like descriptor sets pools and other things that are not directly visible as allocations.
		str << "Overall " << NiceSizeFormat(totalUsedBytes) << " used out of " << NiceSizeFormat(totalBudget) << " available" << std::endl;
	}

	str << "Push buffers:" << std::endl;

	// Now list the various push buffers.
	auto managers = GetActiveGPUMemoryManagers();
	std::sort(managers.begin(), managers.end(), comparePushBufferNames);

	char buffer[512];
	for (auto manager : managers) {
		manager->GetDebugString(buffer, sizeof(buffer));
		str << "  " << buffer << std::endl;
	}

	const int padding = 10 + System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_LEFT);
	const int starty = 50 + System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_TOP);
	int x = padding;
	int y = starty;

	ui->SetFontScale(0.7f, 0.7f);
	ui->DrawTextShadow(str.str().c_str(), x, y, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
	ui->SetFontScale(1.0f, 1.0f);
	ui->Flush();
}

void DrawGPUProfilerVis(UIContext *ui, GPUInterface *gpu) {
	using namespace Draw;
	const int padding = 10 + System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_LEFT);
	const int starty = 50 + System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_TOP);
	int x = padding;
	int y = starty;

	ui->Begin();

	float scale = 0.4f;
	if (g_Config.iGPUBackend == (int)GPUBackend::OPENGL) {
		// Don't have as much info, let's go bigger.
		scale = 0.7f;
	}

	std::string text = ui->GetDrawContext()->GetGpuProfileString();

	ui->SetFontScale(0.4f, 0.4f);
	ui->DrawTextShadow(text.c_str(), x, y, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
	ui->SetFontScale(1.0f, 1.0f);
	ui->Flush();
}
