#pragma once

#include "Common/GraphicsContext.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/GPU/thin3d_create.h"

class AndroidJavaEGLGraphicsContext : public GraphicsContext {
public:
	AndroidJavaEGLGraphicsContext();
	~AndroidJavaEGLGraphicsContext() override { delete draw_; }

	bool NeedsSeparateEmuThread() const override { return true; }

	bool InitAPI(void *wnd, std::string *deviceName, std::string *errorMessage) override;
	void ShutdownAPI() override;

	bool InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *error_message) override;
	void ShutdownSurface() override;

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

	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}
protected:
	void BeginShutdownSurface() override {
		renderManager_->SetSkipGLCalls();
	}
private:
	Draw::DrawContext *draw_ = nullptr;
	GLRenderManager *renderManager_ = nullptr;
};

