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

#include <cstdio>

#include "ppsspp_config.h"
#include <SDL3/SDL.h>

#include "headless/SDLHeadlessGLGraphicsContext.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/GPU/GraphicsContext.h"
#include "Common/TimeUtil.h"
#include "Common/Thread/ThreadUtil.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/GPUState.h"
#include "SDL/SDLUtil.h"

const bool WINDOW_VISIBLE = false;

void *CreateHiddenWindow(int w, int h, GPUBackend backend, WindowDesc *desc) {
	Uint32 flags = SDL_WINDOW_BORDERLESS;
	if (backend == GPUBackend::OPENGL) {
		flags |= SDL_WINDOW_OPENGL;
	} else if (backend == GPUBackend::VULKAN) {
		flags |= SDL_WINDOW_VULKAN;
	}
	if (!WINDOW_VISIBLE) {
		flags |= SDL_WINDOW_HIDDEN;
	}

	SDL_Window *window = SDL_CreateWindow("PPSSPPHeadless", w, h, flags);
	if (!window) {
		const char *err = SDL_GetError();
		fprintf(stderr, "Failed to create offscreen window: %s\n", err ? err : "(unknown error)");
		return nullptr;
	}

	if (backend == GPUBackend::VULKAN) {
		// Overwrite the surface init params with what we need for Vulkan..
		std::string errorMessage;
		if (!DetermineVulkanWindowSystem(window, desc, &errorMessage)) {
			fprintf(stderr, "Failed to determine Vulkan window system: %s\n", errorMessage.c_str());
			SDL_DestroyWindow(window);
			return nullptr;
		}
	} else {
		desc->winsys = WindowSystem::WINDOWSYSTEM_SDL;
		// For OpenGL, we just need the SDL_Window pointer.
		desc->data2 = window;
	}
	return window;
}

void DestroyHiddenWindow(void *window, WindowDesc desc) {
	if (window) {
		SDL_DestroyWindow(static_cast<SDL_Window *>(window));
		SDL_Quit();
	}
}

void SDLHeadlessGLGraphicsContext::ShutdownSurface() {
	delete draw_;
	draw_ = nullptr;

	SDL_GL_DestroyContext(glContext_);
	glContext_ = nullptr;
}

bool SDLHeadlessGLGraphicsContext::InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *errorMessage) {
	// Not used in this context.
	return true;
}

bool SDLHeadlessGLGraphicsContext::InitAPI(void *wnd, std::string *deviceName, std::string *errorMessage) {
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

	screen_ = (SDL_Window *)wnd;
	_dbg_assert_(screen_);

	glContext_ = SDL_GL_CreateContext(screen_);
	if (!glContext_) {
		const char *err = SDL_GetError();
		printf("Failed to create GL context: %s\n", err ? err : "(unknown error)");
		return false;
	}

	// Ensure that the swap interval is set after context creation (needed for kmsdrm)
	SDL_GL_SetSwapInterval(0);

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
	SetGPUBackend(GPUBackend::OPENGL);
	draw_ = Draw::T3DCreateGLContext(false);
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager_->SetInflightFrames(g_Config.iInflightFrames);
	bool success = draw_->CreatePresets();
	_assert_(success);
	renderManager_->SetSwapFunction([&]() {
		SDL_GL_SwapWindow(screen_);
	});
	return success;
}

#endif
