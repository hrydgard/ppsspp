#include "AndroidJavaGLContext.h"
#include "base/NativeApp.h"
#include "gfx_es2/gpu_features.h"
#include "Core/System.h"

AndroidJavaEGLGraphicsContext::AndroidJavaEGLGraphicsContext() {
	CheckGLExtensions();
	draw_ = Draw::T3DCreateGLContext();
	SetGPUBackend(GPUBackend::OPENGL);
	bool success = draw_->CreatePresets();
	assert(success);
}

void AndroidJavaEGLGraphicsContext::Shutdown() {
	ILOG("AndroidJavaEGLGraphicsContext::Shutdown");
	delete draw_;
	draw_ = nullptr;
	NativeShutdownGraphics();
}
