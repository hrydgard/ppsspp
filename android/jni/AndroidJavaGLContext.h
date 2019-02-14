#pragma once

#include <thread>
#include <string>
#include <functional>

#include "AndroidGraphicsContext.h"
#include "thin3d/GLRenderManager.h"
#include "thin3d/thin3d_create.h"

// Doesn't do much. Just to fit in.
class AndroidJavaEGLGraphicsContext : public AndroidGraphicsContext {
public:
	AndroidJavaEGLGraphicsContext();
	~AndroidJavaEGLGraphicsContext() {
		delete draw_;
	}

	bool Initialized() override {
		return draw_ != nullptr;
	}

	// This performs the actual initialization,
	bool InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) override;

	void ShutdownFromRenderThread() override;

	void Shutdown() override;
	void SwapBuffers() override {}
	void SwapInterval(int interval) override {}
	void Resize() override {}
	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}

	void ThreadStart() override {
		renderManager_->ThreadStart(draw_);
	}

	bool ThreadFrame() override {
		return renderManager_->ThreadFrame();
	}

	void BeginAndroidShutdown() override {
		renderManager_->SetSkipGLCalls();
	}

	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}

	void StopThread() override {
		renderManager_->WaitUntilQueueIdle();
		renderManager_->StopThread();
	}

private:
	Draw::DrawContext *draw_ = nullptr;
	GLRenderManager *renderManager_ = nullptr;
};

