#include "AndroidJavaGLContext.h"
#include "Common/System/Display.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

AndroidJavaEGLGraphicsContext::AndroidJavaEGLGraphicsContext() {
	SetGPUBackend(GPUBackend::OPENGL);
}

bool AndroidJavaEGLGraphicsContext::InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) {
	INFO_LOG(Log::G3D, "AndroidJavaEGLGraphicsContext::InitFromRenderThread");
	if (!CheckGLExtensions()) {
		ERROR_LOG(Log::G3D, "CheckGLExtensions failed - not gonna attempt starting up.");
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}

	// OpenGL handles rotated rendering in the driver.
	g_display.rotation = DisplayRotation::ROTATE_0;
	g_display.rot_matrix.setIdentity();

	draw_ = Draw::T3DCreateGLContext(false);  // Can't fail
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager_->SetInflightFrames(g_Config.iInflightFrames);

	if (!draw_->CreatePresets()) {
		// This can't really happen now that compilation is async - they're only really queued for compile here.
		_assert_msg_(false, "Failed to compile preset shaders");
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}
	state_ = GraphicsContextState::INITIALIZED;
	return true;
}

void AndroidJavaEGLGraphicsContext::ShutdownFromRenderThread() {
	INFO_LOG(Log::G3D, "AndroidJavaEGLGraphicsContext::Shutdown");
	renderManager_ = nullptr;  // owned by draw_.
	delete draw_;
	draw_ = nullptr;
	state_ = GraphicsContextState::SHUTDOWN;
}
