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

#ifdef SDL

#include <stdio.h>
#include <SDL.h>
#include <cassert>

#include "headless/SDLHeadlessHost.h"

#include "Common/FileUtil.h"
#include "Common/GraphicsContext.h"

#include "Core/CoreParameter.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUState.h"

#include "base/logging.h"
#include "base/timeutil.h"
#include "gfx/gl_common.h"
#include "gfx_es2/gpu_features.h"
#include "file/vfs.h"
#include "file/zip_read.h"
#include "thin3d/thin3d_create.h"
#include "thin3d/GLRenderManager.h"

const bool WINDOW_VISIBLE = false;
const int WINDOW_WIDTH = 480;
const int WINDOW_HEIGHT = 272;

SDL_Window *CreateHiddenWindow() {
	Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS;
	if (!WINDOW_VISIBLE) {
		flags |= SDL_WINDOW_HIDDEN;
	}
	return SDL_CreateWindow("PPSSPPHeadless", 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, flags);
}

class GLDummyGraphicsContext : public DummyGraphicsContext {
public:
	GLDummyGraphicsContext() {
	}
	~GLDummyGraphicsContext() { delete draw_; }

	bool InitFromRenderThread(std::string *errorMessage) override;

	void ShutdownFromRenderThread() override {
		delete draw_;
		draw_ = nullptr;

		SDL_GL_DeleteContext(glContext_);
		glContext_ = nullptr;
		SDL_DestroyWindow(screen_);
		screen_ = nullptr;
	}

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

	void StopThread() override {
		renderManager_->WaitUntilQueueIdle();
		renderManager_->StopThread();
	}

private:
	Draw::DrawContext *draw_;
	GLRenderManager *renderManager_ = nullptr;
	SDL_Window *screen_;
	SDL_GLContext glContext_;
};

void SDLHeadlessHost::LoadNativeAssets() {
	VFSRegister("", new DirectoryAssetReader("assets/"));
	VFSRegister("", new DirectoryAssetReader(""));
	VFSRegister("", new DirectoryAssetReader("../"));
}

bool GLDummyGraphicsContext::InitFromRenderThread(std::string *errorMessage) {
	SDL_Init(SDL_INIT_VIDEO);

	// TODO
	//SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	//SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	//SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetSwapInterval(1);

	screen_ = CreateHiddenWindow();
	glContext_ = SDL_GL_CreateContext(screen_);

#ifndef USING_GLES2
	// Some core profile drivers elide certain extensions from GL_EXTENSIONS/etc.
	// glewExperimental allows us to force GLEW to search for the pointers anyway.
	if (gl_extensions.IsCoreContext)
		glewExperimental = true;
	if (GLEW_OK != glewInit()) {
		printf("Failed to initialize glew!\n");
		return false;
	}
	// Unfortunately, glew will generate an invalid enum error, ignore.
	if (gl_extensions.IsCoreContext)
		glGetError();

	if (GLEW_VERSION_2_0) {
		printf("OpenGL 2.0 or higher.\n");
	} else {
		printf("Sorry, this program requires OpenGL 2.0.\n");
		return false;
	}
#endif

	CheckGLExtensions();
	draw_ = Draw::T3DCreateGLContext();
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	SetGPUBackend(GPUBackend::OPENGL);
	bool success = draw_->CreatePresets();
	assert(success);
	renderManager_->SetSwapFunction([&]() {
		SDL_GL_SwapWindow(screen_);
	});

	return success;
}

bool SDLHeadlessHost::InitGraphics(std::string *error_message, GraphicsContext **ctx) {
	GraphicsContext *graphicsContext = new GLDummyGraphicsContext();
	*ctx = graphicsContext;
	gfx_ = graphicsContext;

	std::thread th([&]{
		while (threadState_ == RenderThreadState::IDLE)
			sleep_ms(1);
		threadState_ = RenderThreadState::STARTING;

		std::string err;
		if (!gfx_->InitFromRenderThread(&err)) {
			threadState_ = RenderThreadState::START_FAILED;
			return;
		}
		gfx_->ThreadStart();
		threadState_ = RenderThreadState::STARTED;

		while (threadState_ != RenderThreadState::STOP_REQUESTED) {
			if (!gfx_->ThreadFrame()) {
				break;
			}
			gfx_->SwapBuffers();
		}

		threadState_ = RenderThreadState::STOPPING;
		gfx_->ThreadEnd();
		gfx_->ShutdownFromRenderThread();
		threadState_ = RenderThreadState::STOPPED;
	});
	th.detach();

	LoadNativeAssets();

	threadState_ = RenderThreadState::START_REQUESTED;
	while (threadState_ == RenderThreadState::START_REQUESTED || threadState_ == RenderThreadState::STARTING)
		sleep_ms(1);

	return threadState_ == RenderThreadState::STARTED;
}

void SDLHeadlessHost::ShutdownGraphics() {
	gfx_->StopThread();
	while (threadState_ != RenderThreadState::STOPPED)
		sleep_ms(1);

	gfx_->Shutdown();
	delete gfx_;
	gfx_ = nullptr;
}

void SDLHeadlessHost::SwapBuffers() {
	gfx_->SwapBuffers();
}

#endif
