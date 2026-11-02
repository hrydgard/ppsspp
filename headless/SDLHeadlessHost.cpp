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
#if PPSSPP_PLATFORM(MAC)
#include "SDL2/SDL.h"
#else
#include "SDL.h"
#endif

#include "headless/SDLHeadlessHost.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/VFS/DirectoryReader.h"
#include "Common/GraphicsContext.h"
#include "Common/TimeUtil.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "GPU/Vulkan/VulkanUtil.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/GPUState.h"

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

class GLDummyGraphicsContext : public GraphicsContext {
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

		SDL_Quit();
	}

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}

	void ThreadStart() override {
		renderManager_->ThreadStart(draw_);
	}

	bool ThreadFrame(bool waitIfEmpty) override {
		return renderManager_->ThreadFrame(waitIfEmpty);
	}

	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}

	void Shutdown() override {}
	void Resize() override {}

private:
	Draw::DrawContext *draw_ = nullptr;
	GLRenderManager *renderManager_ = nullptr;
	SDL_Window *screen_;
	SDL_GLContext glContext_;
};

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

	screen_ = CreateHiddenWindow();
	if (!screen_) {
		const char *err = SDL_GetError();
		printf("Failed to create offscreen window: %s\n", err ? err : "(unknown error)");
		return false;
	}
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
	draw_ = Draw::T3DCreateGLContext(false);
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager_->SetInflightFrames(g_Config.iInflightFrames);
	SetGPUBackend(GPUBackend::OPENGL);
	bool success = draw_->CreatePresets();
	_assert_(success);
	renderManager_->SetSwapFunction([&]() {
		SDL_GL_SwapWindow(screen_);
	});
	return success;
}

// Vulkan without a window or swapchain (VulkanContext::InitHeadless), so it works on macOS, where a
// window can only be created on the main thread.
class VulkanHeadlessGraphicsContext : public GraphicsContext {
public:
	~VulkanHeadlessGraphicsContext() { Shutdown(); }

	bool Init(std::string *errorMessage) {
		init_glslang();
		glslangInited_ = true;

		std::string errorStr;
		if (!VulkanLoad(&errorStr)) {
			*errorMessage = "Failed to load Vulkan driver library: " + errorStr;
			return false;
		}

		vulkan_ = new VulkanContext();
		VulkanContext::CreateInfo info{};
		InitVulkanCreateInfoFromConfig(&info);
		if (vulkan_->CreateInstance(info) != VK_SUCCESS) {
			*errorMessage = vulkan_->InitError();
			return false;
		}
		instanceCreated_ = true;
		int deviceNum = vulkan_->GetBestPhysicalDevice();
		if (vulkan_->CreateDevice(deviceNum) != VK_SUCCESS) {
			*errorMessage = vulkan_->InitError();
			return false;
		}
		deviceCreated_ = true;
		if (vulkan_->InitHeadless() != VK_SUCCESS) {
			*errorMessage = "No graphics queue";
			return false;
		}

		// Headless restores the config defaults only after this, so use the defaults directly:
		// three frames in flight, multithreaded rendering.
		draw_ = Draw::T3DCreateVulkanContext(vulkan_, true);
		vulkan_->InitSwapchain(VK_PRESENT_MODE_FIFO_KHR);
		SetGPUBackend(GPUBackend::VULKAN);
		bool success = draw_->CreatePresets();
		_assert_(success);
		// Only applied when the backbuffers are created, so before GOT_BACKBUFFER. PPSSPP_INFLIGHT_FRAMES
		// overrides the default, for comparing with later versions (which default to 2).
		int inflightFrames = 3;
		if (const char *env = getenv("PPSSPP_INFLIGHT_FRAMES")) {
			inflightFrames = atoi(env);
		}
		VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		renderManager->SetInflightFrames(inflightFrames);
		draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
		return success;
	}

	void Shutdown() override {
		if (draw_) {
			draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, vulkan_->GetBackbufferWidth(), vulkan_->GetBackbufferHeight());
			delete draw_;
			draw_ = nullptr;
		}
		if (vulkan_) {
			if (deviceCreated_) {
				vulkan_->WaitUntilQueueIdle();
				vulkan_->DestroySwapchain();
				vulkan_->DestroyDevice();
			}
			if (instanceCreated_) {
				vulkan_->DestroyInstance();
			}
			delete vulkan_;
			vulkan_ = nullptr;
		}
		if (glslangInited_) {
			finalize_glslang();
			glslangInited_ = false;
		}
	}

	void Resize() override {}
	void *GetAPIContext() override { return vulkan_; }
	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	VulkanContext *vulkan_ = nullptr;
	Draw::DrawContext *draw_ = nullptr;
	bool glslangInited_ = false;
	bool instanceCreated_ = false;
	bool deviceCreated_ = false;
};

bool SDLHeadlessHost::InitGraphics(std::string *error_message, GraphicsContext **ctx, GPUCore core) {
	if (core == GPUCORE_VULKAN) {
#if PPSSPP_PLATFORM(MAC)
		// MoltenVK's console logging would mix into the output.
		setenv("MVK_CONFIG_LOG_LEVEL", "1", 0);
#endif
		VulkanHeadlessGraphicsContext *vulkanContext = new VulkanHeadlessGraphicsContext();
		if (!vulkanContext->Init(error_message)) {
			fprintf(stderr, "Vulkan init failed: %s\n", error_message->c_str());
			delete vulkanContext;
			*ctx = nullptr;
			return false;
		}
		*ctx = vulkanContext;
		gfx_ = vulkanContext;
		return true;
	}

	GraphicsContext *graphicsContext = new GLDummyGraphicsContext();
	*ctx = graphicsContext;
	gfx_ = graphicsContext;

	std::thread th([&]{
		// This is the "EmuThread".
		SetCurrentThreadName("SDL-EmuThread");

		while (threadState_ == RenderThreadState::IDLE)
			sleep_ms(1, "sdl-idle-poll");
		threadState_ = RenderThreadState::STARTING;

		std::string err;
		if (!gfx_->InitFromRenderThread(&err)) {
			threadState_ = RenderThreadState::START_FAILED;
			return;
		}
		gfx_->ThreadStart();
		threadState_ = RenderThreadState::STARTED;

		while (threadState_ != RenderThreadState::STOP_REQUESTED) {
			if (!gfx_->ThreadFrame(true)) {
				break;
			}
		}

		threadState_ = RenderThreadState::STOPPING;
		gfx_->ThreadEnd();
		gfx_->ShutdownFromRenderThread();
		threadState_ = RenderThreadState::STOPPED;
	});
	th.detach();

	threadState_ = RenderThreadState::START_REQUESTED;
	while (threadState_ == RenderThreadState::START_REQUESTED || threadState_ == RenderThreadState::STARTING)
		sleep_ms(1, "sdl-start-poll");

	return threadState_ == RenderThreadState::STARTED;
}

void SDLHeadlessHost::ShutdownGraphics() {
	if (!gfx_) {
		return;
	}
	// Vulkan has no render thread here (its render manager runs its own).
	while (threadState_ != RenderThreadState::IDLE && threadState_ != RenderThreadState::STOPPED && threadState_ != RenderThreadState::START_FAILED)
		sleep_ms(1, "sdl-stop-poll");

	gfx_->Shutdown();
	delete gfx_;
	gfx_ = nullptr;
}

void SDLHeadlessHost::SwapBuffers() {
}

#endif
