#pragma once

#include <thread>
#include <string>
#include <functional>

#include "AndroidGraphicsContext.h"
#include "thin3d/GLRenderManager.h"

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
		renderManager_->ThreadStart();
	}

	bool ThreadFrame() override {
		return renderManager_->ThreadFrame();
	}

	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}

private:
	Draw::DrawContext *draw_ = nullptr;
	GLRenderManager *renderManager_ = nullptr;
};

