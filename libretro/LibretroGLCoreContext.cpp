#include "ppsspp_config.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Common/GPU/OpenGL/GLFeatures.h"

#include "libretro/LibretroGLCoreContext.h"

bool LibretroGLCoreContext::Init() {
	if (!LibretroHWRenderContext::Init(false))
		return false;

	g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
	return true;
}

void LibretroGLCoreContext::CreateDrawContext() {
	if (!glewInitDone) {
#if !PPSSPP_PLATFORM(IOS) && !defined(USING_GLES2)
		if (glewInit() != GLEW_OK) {
			ERROR_LOG(Log::G3D, "glewInit() failed.\n");
			return;
		}
#endif
		glewInitDone = true;
		CheckGLExtensions();
	}
	draw_ = Draw::T3DCreateGLContext(false);
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager_->SetInflightFrames(g_Config.iInflightFrames);
	SetGPUBackend(GPUBackend::OPENGL);
	draw_->CreatePresets();
}

void LibretroGLCoreContext::DestroyDrawContext() {
	LibretroHWRenderContext::DestroyDrawContext();
	renderManager_ = nullptr;
}
