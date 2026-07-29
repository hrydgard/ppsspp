#include "OpenGLGraphicsContext.h"
#include "Common/System/Display.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Log.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/Config.h"

OpenGLGraphicsContext::OpenGLGraphicsContext() {
	SetGPUBackend(GPUBackend::OPENGL);
	// NOTE: On some Android devices, OpenGL is not yet available here so calling CheckGLExtension here
	// doesn't work.
	// OpenGL handles rotated rendering in the driver.
	g_display.rotation = DisplayRotation::ROTATE_0;
	g_display.rot_matrix.setIdentity();
}

bool OpenGLGraphicsContext::InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *errorMessage) {
	CheckGLExtensions();
	INFO_LOG(Log::G3D, "OpenGLGraphicsContext::InitSurface");
	draw_ = Draw::T3DCreateGLContext(false);  // Can't fail
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	// TODO: In VR mode, we don't want a lot of inflight frames.
	renderManager_->SetInflightFrames(g_Config.iInflightFrames);
	const bool result = draw_->CreatePresets();
	_dbg_assert_(result);
	return true;
}

void OpenGLGraphicsContext::ShutdownSurface() {
	INFO_LOG(Log::G3D, "OpenGLGraphicsContext::Shutdown");
	renderManager_ = nullptr;  // owned by draw_.
	delete draw_;
	draw_ = nullptr;
}
