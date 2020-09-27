
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "gfx_es2/gpu_features.h"

#include "libretro/LibretroGLCoreContext.h"

bool LibretroGLCoreContext::Init() {
	if (!LibretroHWRenderContext::Init(true))
		return false;

	g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
	return true;
}

void LibretroGLCoreContext::CreateDrawContext() {
	if (!glewInitDone) {
#if !defined(IOS) && !defined(USING_GLES2)
		if (glewInit() != GLEW_OK) {
			ERROR_LOG(G3D, "glewInit() failed.\n");
			return;
		}
#endif
		glewInitDone = true;
		CheckGLExtensions();
	}
	draw_ = Draw::T3DCreateGLContext();
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
}

void LibretroGLCoreContext::DestroyDrawContext() {
	LibretroHWRenderContext::DestroyDrawContext();
	renderManager_ = nullptr;
}
