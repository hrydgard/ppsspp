#pragma once

#include "gfx/gl_common.h"
#include "libretro/LibretroGraphicsContext.h"
#include "thin3d/GLRenderManager.h"

class LibretroGLContext : public LibretroHWRenderContext {
public:
	LibretroGLContext()
#ifdef USING_GLES2
		: LibretroHWRenderContext(RETRO_HW_CONTEXT_OPENGLES2)
#elif defined(HAVE_OPENGL_CORE)
		: LibretroHWRenderContext(RETRO_HW_CONTEXT_OPENGL_CORE, 3, 1)
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
		renderManager_->WaitUntilQueueIdle();
		renderManager_->StopThread();
	}

	GPUCore GetGPUCore() override { return GPUCORE_GLES; }
	const char *Ident() override { return "OpenGL"; }

private:
	GLRenderManager *renderManager_ = nullptr;
	bool glewInitDone = false;
};
