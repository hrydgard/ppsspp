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

#include "gfx_es2/draw_buffer.h"
#include "thin3d/thin3d.h"
#include "ui/ui_context.h"
#include "ui/view.h"

#include "DebugVisVulkan.h"
#include "Common/Vulkan/VulkanMemory.h"
#include "Common/Vulkan/VulkanImage.h"
#include "GPU/Vulkan/GPU_Vulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"

void DrawAllocatorVis(UIContext *ui, GPUInterface *gpu) {
	if (!gpu) {
		return;
	}
	using namespace Draw;
	const int padding = 10;
	const int columnWidth = 256;
	const int starty = padding * 8;
	int x = padding;
	int y = starty;
	int w = columnWidth;  // We will double this when actually drawing to make the pixels visible.

	ui->Begin();

	GPU_Vulkan *gpuVulkan = static_cast<GPU_Vulkan *>(gpu);
	VulkanDeviceAllocator *alloc = gpuVulkan->GetTextureCache()->GetAllocator();

	std::vector<Draw::Texture *> texturesToDelete;
	for (int i = 0; i < alloc->GetSlabCount(); i++) {
		std::vector<uint8_t> usage = alloc->GetSlabUsage(i);
		int h = ((int)usage.size() + w - 1) / w;

		if (y + h + padding > ui->GetBounds().h) {
			y = starty;
			x += columnWidth + padding;
		}

		std::vector<uint8_t> initData(w * h * 4);
		uint32_t *wideData = (uint32_t *)initData.data();

		// Convert to nice colors. If we really wanted to save on memory, we could use a 16-bit texture...
		for (size_t j = 0; j < usage.size(); j++) {
			switch (usage[j]) {
			case 0: wideData[j] = 0xFF333333; break;
			case 1: wideData[j] = 0xFF33FF33; break;
			case 2: wideData[j] = 0xFF3333FF; break;
			default: wideData[j] = 0xFFFF00FF; break;  // Magenta - if you see this, need to add more cases.
			}
		}

		Draw::TextureDesc desc{};
		desc.width = w;
		desc.height = h;
		desc.depth = 1;
		desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
		desc.mipLevels = 1;
		desc.type = Draw::TextureType::LINEAR2D;
		desc.tag = "DebugVis";
		desc.initData.push_back(initData.data());

		Draw::DrawContext *draw = ui->GetDrawContext();
		Draw::Texture *tex = draw->CreateTexture(desc);

		UI::Drawable white(0xFFFFFFFF);
		draw->BindTexture(0, tex);
		// Cheap black border.
		ui->Draw()->Rect(x-2, y-2, w+4, h+4, 0xE0000000);
		ui->Draw()->Rect(x, y, w, h, 0xFFFFFFFF);
		ui->Flush();
		texturesToDelete.push_back(tex);

		y += h + padding;
	}
	ui->Flush();

	for (auto iter : texturesToDelete)
		iter->Release();
}
