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

#include <stdio.h>
#include <SDL.h>
#include "gfx/gl_lost_manager.h"

#include "SDLHeadlessHost.h"
#include "Compare.h"

#include "Common/FileUtil.h"
#include "Common/GraphicsContext.h"

#include "Core/CoreParameter.h"
#include "Core/System.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUState.h"

#include "base/logging.h"
#include "gfx/gl_common.h"
#include "gfx_es2/gpu_features.h"
#include "file/vfs.h"
#include "file/zip_read.h"

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
		CheckGLExtensions();
		draw_ = Draw::T3DCreateGLContext();
	}
	~GLDummyGraphicsContext() { delete draw_; }

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}

private:
	Draw::DrawContext *draw_;
};

void SDLHeadlessHost::LoadNativeAssets() {
	VFSRegister("", new DirectoryAssetReader("assets/"));
	VFSRegister("", new DirectoryAssetReader(""));
	VFSRegister("", new DirectoryAssetReader("../"));
}

void SDLHeadlessHost::SendDebugOutput(const std::string &output) {
	fwrite(output.data(), sizeof(char), output.length(), stdout);
	OutputDebugStringUTF8(output.c_str());
}

void SDLHeadlessHost::SendOrCollectDebugOutput(const std::string &data) {
	if (PSP_CoreParameter().printfEmuLog)
		SendDebugOutput(data);
	else if (PSP_CoreParameter().collectEmuLog)
		*PSP_CoreParameter().collectEmuLog += data;
	else
		DEBUG_LOG(COMMON, "%s", data.c_str());
}

void SDLHeadlessHost::SendDebugScreenshot(const u8 *pixbuf, u32 w, u32 h) {
	// Only if we're actually comparing.
	if (comparisonScreenshot.empty()) {
		return;
	}

	// We ignore the current framebuffer parameters and just grab the full screen.
	const static u32 FRAME_STRIDE = 512;
	const static u32 FRAME_WIDTH = 480;
	const static u32 FRAME_HEIGHT = 272;

	GPUDebugBuffer buffer;
	gpuDebug->GetCurrentFramebuffer(buffer, GPU_DBG_FRAMEBUF_RENDER);
	const std::vector<u32> pixels = TranslateDebugBufferToCompare(&buffer, 512, 272);

	std::string error;
	double errors = CompareScreenshot(pixels, FRAME_STRIDE, FRAME_WIDTH, FRAME_HEIGHT, comparisonScreenshot, error);
	if (errors < 0)
		SendOrCollectDebugOutput(error);

	if (errors > 0) {
		char temp[256];
		snprintf(temp, 256, "Screenshot error: %f%%\n", errors * 100.0f);
		SendOrCollectDebugOutput(temp);
	}

	if (errors > 0 && !teamCityMode) {
		// Lazy, just read in the original header to output the failed screenshot.
		u8 header[14 + 40] = {0};
		FILE *bmp = File::OpenCFile(comparisonScreenshot, "rb");
		if (bmp) {
			fread(&header, sizeof(header), 1, bmp);
			fclose(bmp);
		}

		FILE *saved = File::OpenCFile("__testfailure.bmp", "wb");
		if (saved) {
			fwrite(&header, sizeof(header), 1, saved);
			fwrite(pixels.data(), sizeof(u32), FRAME_STRIDE * FRAME_HEIGHT, saved);
			fclose(saved);

			SendOrCollectDebugOutput("Actual output written to: __testfailure.bmp\n");
		}
	}
}

void SDLHeadlessHost::SetComparisonScreenshot(const std::string &filename) {
	comparisonScreenshot = filename;
}

bool SDLHeadlessHost::InitGraphics(std::string *error_message, GraphicsContext **ctx) {
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

	gl_lost_manager_init();

	GraphicsContext *graphicsContext = new GLDummyGraphicsContext();
	*ctx = graphicsContext;
	gfx_ = graphicsContext;

	LoadNativeAssets();

	return true;
}

void SDLHeadlessHost::ShutdownGraphics() {
	gfx_->Shutdown();
	delete gfx_;
	gfx_ = nullptr;

	SDL_GL_DeleteContext(glContext_);
	glContext_ = nullptr;
	SDL_DestroyWindow(screen_);
	screen_ = nullptr;

	gl_lost_manager_shutdown();
}

void SDLHeadlessHost::SwapBuffers() {
	gfx_->SwapBuffers();
	SDL_GL_SwapWindow(screen_);
}
