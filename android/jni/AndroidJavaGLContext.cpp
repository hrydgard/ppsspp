#include "AndroidJavaGLContext.h"
#include "Common/System/Display.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Log.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

AndroidJavaEGLGraphicsContext::AndroidJavaEGLGraphicsContext() {
	SetGPUBackend(GPUBackend::OPENGL);
}

bool AndroidJavaEGLGraphicsContext::InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) {
	INFO_LOG(G3D, "AndroidJavaEGLGraphicsContext::InitFromRenderThread");
	CheckGLExtensions();
	// OpenGL handles rotated rendering in the driver.
	g_display_rotation = DisplayRotation::ROTATE_0;
	g_display_rot_matrix.setIdentity();
	draw_ = Draw::T3DCreateGLContext();  // Can't fail
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager_->SetInflightFrames(g_Config.iInflightFrames);
	draw_->CreatePresets();
	return true;
}

void AndroidJavaEGLGraphicsContext::ShutdownFromRenderThread() {
	INFO_LOG(G3D, "AndroidJavaEGLGraphicsContext::Shutdown");
	renderManager_->WaitUntilQueueIdle();
	renderManager_ = nullptr;  // owned by draw_.
	delete draw_;
	draw_ = nullptr;
}

void AndroidJavaEGLGraphicsContext::Shutdown() {
}
