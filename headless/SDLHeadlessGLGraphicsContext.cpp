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
#if PPSSPP_PLATFORM(MAC)
// After glew, which has its own definitions of what this would include.
#include <OpenGL/OpenGL.h>
#endif
#include "Common/GPU/thin3d_create.h"
#include "Common/StringUtils.h"
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

#if PPSSPP_PLATFORM(MAC)

// Bound by the GL backend wherever it would bind the default framebuffer.
extern GLuint g_defaultFBO;

bool CGLHeadlessGraphicsContext::InitAPI(void *wnd, std::string *deviceName, std::string *errorMessage) {
	const CGLPixelFormatAttribute attributes[] = {
		kCGLPFAAccelerated,
		kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
		kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
		kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
		(CGLPixelFormatAttribute)0,
	};
	CGLPixelFormatObj pixelFormat = nullptr;
	GLint formatCount = 0;
	if (CGLChoosePixelFormat(attributes, &pixelFormat, &formatCount) != kCGLNoError || !pixelFormat) {
		*errorMessage = "CGLChoosePixelFormat failed";
		return false;
	}
	CGLContextObj context = nullptr;
	CGLError err = CGLCreateContext(pixelFormat, nullptr, &context);
	CGLDestroyPixelFormat(pixelFormat);
	if (err != kCGLNoError) {
		*errorMessage = StringFromFormat("CGLCreateContext failed: %s", CGLErrorString(err));
		return false;
	}
	context_ = context;
	CGLSetCurrentContext(context);

	// Core profile drivers leave some extensions out of the list, so glew has to look for them anyway.
	SetGLCoreContext(true);
	glewExperimental = true;
	if (glewInit() != GLEW_OK) {
		*errorMessage = "Failed to initialize glew";
		return false;
	}
	// glew causes an invalid enum error with core profiles, ignore it.
	glGetError();

	// There's no drawable, so the backbuffer is a framebuffer object of our own.
	glGenRenderbuffers(1, &colorBuffer_);
	glBindRenderbuffer(GL_RENDERBUFFER, colorBuffer_);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width_, height_);
	glGenRenderbuffers(1, &depthStencilBuffer_);
	glBindRenderbuffer(GL_RENDERBUFFER, depthStencilBuffer_);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width_, height_);
	glGenFramebuffers(1, &fbo_);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorBuffer_);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthStencilBuffer_);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		*errorMessage = "The offscreen framebuffer is incomplete";
		return false;
	}
	g_defaultFBO = fbo_;

	CheckGLExtensions();
	SetGPUBackend(GPUBackend::OPENGL);
	draw_ = Draw::T3DCreateGLContext(false);
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager_->SetInflightFrames(g_Config.iInflightFrames);
	bool success = draw_->CreatePresets();
	_assert_(success);
	// Nothing to swap. Flushing keeps the frames moving like a swap would.
	renderManager_->SetSwapFunction([]() {
		glFlush();
	});
	return success;
}

void CGLHeadlessGraphicsContext::ShutdownSurface() {
	delete draw_;
	draw_ = nullptr;

	g_defaultFBO = 0;
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fbo_);
	glDeleteRenderbuffers(1, &colorBuffer_);
	glDeleteRenderbuffers(1, &depthStencilBuffer_);
	CGLSetCurrentContext(nullptr);
	CGLDestroyContext((CGLContextObj)context_);
	context_ = nullptr;
}

#endif

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
