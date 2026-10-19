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

// Plain C++, so it can be used from the platform code.

#include "Common/GPU/GraphicsContext.h"

class MetalRenderManager;

class MetalGraphicsContext : public GraphicsContext {
public:
	MetalGraphicsContext() {}
	~MetalGraphicsContext() override;

	// Renders into a texture of this size instead of a layer (headless). Call before InitAPI.
	void SetOffscreen(int width, int height) {
		offscreenWidth_ = width;
		offscreenHeight_ = height;
	}

	bool InitAPI(void *wnd, std::string *deviceName, std::string *errorMessage) override;
	// data1 is the CAMetalLayer (bridged), unless offscreen.
	bool InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *errorMessage) override;
	void ShutdownSurface() override;
	void ShutdownAPI() override;
	void Resize() override;

	void *GetAPIContext() override { return renderManager_; }
	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	void *device_ = nullptr;  // Retained id<MTLDevice>.
	void *layer_ = nullptr;  // Not retained, owned by the view.
	MetalRenderManager *renderManager_ = nullptr;
	Draw::DrawContext *draw_ = nullptr;
	int offscreenWidth_ = 0;
	int offscreenHeight_ = 0;
};
