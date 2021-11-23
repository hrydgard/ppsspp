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

#include "Common/Render/DrawBuffer.h"
#include "Common/GPU/thin3d.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"

#include "DebugVisVulkan.h"
#include "Common/GPU/Vulkan/VulkanMemory.h"
#include "Common/GPU/Vulkan/VulkanImage.h"
#include "GPU/Vulkan/GPU_Vulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"

#undef DrawText

void DrawAllocatorVis(UIContext *ui, GPUInterface *gpu) {
	// TODO: Make a new allocator visualizer for VMA.
}

void DrawGPUProfilerVis(UIContext *ui, GPUInterface *gpu) {
	if (!gpu) {
		return;
	}
	using namespace Draw;
	const int padding = 10 + System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_LEFT);
	const int starty = 50 + System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_TOP);
	int x = padding;
	int y = starty;

	ui->Begin();

	GPU_Vulkan *gpuVulkan = static_cast<GPU_Vulkan *>(gpu);

	std::string text = gpuVulkan->GetGpuProfileString();

	ui->SetFontScale(0.4f, 0.4f);
	ui->DrawTextShadow(text.c_str(), x, y, 0xFFFFFFFF, FLAG_DYNAMIC_ASCII);
	ui->SetFontScale(1.0f, 1.0f);
	ui->Flush();
}
