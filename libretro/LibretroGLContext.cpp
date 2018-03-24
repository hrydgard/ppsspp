
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "gfx_es2/gpu_features.h"

#include "libretro/LibretroGLContext.h"

bool LibretroGLContext::Init()
{
	if (!LibretroHWRenderContext::Init())
		return false;

	libretro_get_proc_address = hw_render_.get_proc_address;

	g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
	return true;
}

void LibretroGLContext::Shutdown()
{
	LibretroGraphicsContext::Shutdown();
	libretro_get_proc_address = nullptr;
}

void LibretroGLContext::CreateDrawContext()
{
	if (!glewInitDone)
	{
#if !defined(IOS) && !defined(USING_GLES2)
		if (glewInit() != GLEW_OK)
		{
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
void LibretroGLContext::DestroyDrawContext()
{
	LibretroHWRenderContext::DestroyDrawContext();
	renderManager_ = nullptr;
}
