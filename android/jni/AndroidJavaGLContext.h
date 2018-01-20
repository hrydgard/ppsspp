#pragma once

#include "AndroidGraphicsContext.h"

// Doesn't do much. Just to fit in.
class AndroidJavaEGLGraphicsContext : public GraphicsContext {
public:
	AndroidJavaEGLGraphicsContext();
	~AndroidJavaEGLGraphicsContext() {
		delete draw_;
	}
	void Shutdown() override;
	void SwapBuffers() override {}
	void SwapInterval(int interval) override {}
	void Resize() override {}
	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}
private:
	Draw::DrawContext *draw_;
};

