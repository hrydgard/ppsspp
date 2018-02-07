#pragma once

#include "AndroidGraphicsContext.h"
#include "Common/GL/GLInterfaceBase.h"

class AndroidEGLGraphicsContext : public AndroidGraphicsContext {
public:
	AndroidEGLGraphicsContext() : draw_(nullptr), wnd_(nullptr), gl(nullptr) {}
	bool InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) override;
	void Shutdown() override;
	void SwapBuffers() override;
	void SwapInterval(int interval) override {}
	void Resize() override {}
	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}
	bool Initialized() override {
		return draw_ != nullptr;
	}

private:
	Draw::DrawContext *draw_;
	ANativeWindow *wnd_;
	cInterfaceBase *gl;
};
