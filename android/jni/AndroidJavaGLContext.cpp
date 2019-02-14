#include <cassert>

#include "AndroidJavaGLContext.h"
#include "base/NativeApp.h"
#include "gfx_es2/gpu_features.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

AndroidJavaEGLGraphicsContext::AndroidJavaEGLGraphicsContext() {
	SetGPUBackend(GPUBackend::OPENGL);
}

bool AndroidJavaEGLGraphicsContext::InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) {
	ILOG("AndroidJavaEGLGraphicsContext::InitFromRenderThread");
	CheckGLExtensions();
	draw_ = Draw::T3DCreateGLContext();
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	bool success = draw_->CreatePresets();
	_assert_msg_(G3D, success, "Failed to compile preset shaders");
	return success;
}

void AndroidJavaEGLGraphicsContext::ShutdownFromRenderThread() {
	ILOG("AndroidJavaEGLGraphicsContext::Shutdown");
	renderManager_->WaitUntilQueueIdle();
	renderManager_ = nullptr;  // owned by draw_.
	delete draw_;
	draw_ = nullptr;
}

void AndroidJavaEGLGraphicsContext::Shutdown() {
}
