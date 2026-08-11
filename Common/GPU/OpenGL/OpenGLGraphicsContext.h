#pragma once

#include "Common/GPU/GraphicsContext.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/GPU/thin3d_create.h"

// This one is mainly useful for the backends that already initialize GL in platform-specific code.
// It doesn't have the real initialization that we need to do on some platforms.
class OpenGLGraphicsContext : public GraphicsContext {
public:
	OpenGLGraphicsContext();
	~OpenGLGraphicsContext() override { delete draw_; }

	bool NeedsSeparateEmuThread() const override { return true; }

	bool InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *error_message) override;
	void ShutdownSurface() override;

	void Resize() override {}

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}

	// Called from render thread
	void ThreadStart() override {
		renderManager_->ThreadStart(draw_);
	}
	bool ThreadFrame() override {
		return renderManager_->ThreadFrame();
	}
	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}

	// Call from emu thread
	void NotifyEmuThreadExit() override {
		renderManager_->SetSkipGLCalls();
		renderManager_->NotifyEmuThreadExit();
	}

private:
	Draw::DrawContext *draw_ = nullptr;
	GLRenderManager *renderManager_ = nullptr;
};

