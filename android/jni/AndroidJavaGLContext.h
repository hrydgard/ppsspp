#pragma once

#include "AndroidGraphicsContext.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/GPU/thin3d_create.h"

class AndroidJavaEGLGraphicsContext : public AndroidGraphicsContext {
public:
	AndroidJavaEGLGraphicsContext();
	~AndroidJavaEGLGraphicsContext() override { delete draw_; }

	// This performs the actual initialization,
	bool InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) override;

	void ShutdownFromRenderThread() override;

	void Shutdown() override {}
	void Resize() override {}

	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}

	void ThreadStart() override {
		renderManager_->ThreadStart(draw_);
	}

	bool ThreadFrame(bool waitIfEmpty) override {
		return renderManager_->ThreadFrame(waitIfEmpty);
	}

	void BeginAndroidShutdown() override {
		renderManager_->SetSkipGLCalls();
	}

	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}

private:
	Draw::DrawContext *draw_ = nullptr;
	GLRenderManager *renderManager_ = nullptr;
};

