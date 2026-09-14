// Copyright (c) 2017- PPSSPP Project.

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

#ifdef SDL

#include <SDL3/SDL.h>

#include "Common/GPU/GraphicsContext.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Core/ConfigValues.h"

class SDLHeadlessGLGraphicsContext : public GraphicsContext {
public:
	SDLHeadlessGLGraphicsContext() {}
	~SDLHeadlessGLGraphicsContext() { delete draw_; }

	bool InitAPI(void *wnd, std::string *deviceNameSetting, std::string *errorMessage) override;
	bool InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *errorMessage) override;

	void ShutdownSurface() override;

	bool NeedsSeparateEmuThread() const override { return true; }

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}

	void ThreadStart() override {
		renderManager_->ThreadStart(draw_);
	}

	bool ThreadFrame() override {
		return renderManager_->ThreadFrame();
	}

	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}

	void Resize() override {}

	// Call from emu thread
	void NotifyEmuThreadExit() override {
		renderManager_->NotifyEmuThreadExit();
	}

private:
	Draw::DrawContext *draw_ = nullptr;
	GLRenderManager *renderManager_ = nullptr;
	SDL_Window *screen_;
	SDL_GLContext glContext_;
};

void *CreateHiddenWindow(int w, int h, GPUBackend backend, WindowDesc *desc);
void DestroyHiddenWindow(void *window, WindowDesc desc);

#endif
