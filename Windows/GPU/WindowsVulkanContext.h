// Copyright (c) 2015- PPSSPP Project.

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

#include "Common/GraphicsContext.h"
#include "Windows/GPU/WindowsGraphicsContext.h"
#include "Common/GPU/thin3d.h"

class VulkanContext;
class VulkanRenderManager;

class WindowsVulkanContext : public WindowsGraphicsContext {
public:
	WindowsVulkanContext() : draw_(nullptr) {}
	bool Init(HINSTANCE hInst, HWND window, std::string *error_message) override;
	void Shutdown() override;
	void Resize() override;
	void Poll() override;

	void *GetAPIContext() override;

	Draw::DrawContext *GetDrawContext() override { return draw_; }
private:
	Draw::DrawContext *draw_;
	VulkanContext *vulkan_ = nullptr;
	VulkanRenderManager *renderManager_ = nullptr;
};

