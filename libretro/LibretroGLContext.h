#pragma once

#include "Common/GPU/OpenGL/GLCommon.h"
#include "libretro/LibretroGraphicsContext.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"

class LibretroGLContext : public LibretroHWRenderContext {
public:
	LibretroGLContext()
#ifdef USING_GLES2
		: LibretroHWRenderContext(RETRO_HW_CONTEXT_OPENGLES2)
#else
		: LibretroHWRenderContext(RETRO_HW_CONTEXT_OPENGL)
#endif
	{
		hw_render_.bottom_left_origin = true;
	}

	bool Init() override;
	void CreateDrawContext() override;
	void DestroyDrawContext() override;
	void SetRenderTarget() override {
		extern GLuint g_defaultFBO;
		g_defaultFBO = hw_render_.get_current_framebuffer();
	}

	void ThreadStart() override { renderManager_->ThreadStart(draw_); }
	bool ThreadFrame() override { return renderManager_->ThreadFrame(); }
	void ThreadEnd() override { renderManager_->ThreadEnd(); }
	void StopThread() override {
		renderManager_->StopThread();
	}

	GPUCore GetGPUCore() override { return GPUCORE_GLES; }
	const char *Ident() override { return "OpenGL"; }

private:
	GLRenderManager *renderManager_ = nullptr;
	bool glewInitDone = false;
};
