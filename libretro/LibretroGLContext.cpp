
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Common/GPU/OpenGL/GLFeatures.h"

#include "libretro/LibretroGLContext.h"

bool LibretroGLContext::Init() {
	if (!LibretroHWRenderContext::Init(false))
		return false;

	g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
	return true;
}

void LibretroGLContext::CreateDrawContext() {

#ifndef USING_GLES2
    // Some core profile drivers elide certain extensions from GL_EXTENSIONS/etc.
    // glewExperimental allows us to force GLEW to search for the pointers anyway.
    if (gl_extensions.IsCoreContext)
        glewExperimental = true;
    if (GLEW_OK != glewInit()) {
        printf("Failed to initialize glew!\n");
    }
    // Unfortunately, glew will generate an invalid enum error, ignore.
    if (gl_extensions.IsCoreContext)
        glGetError();
#endif

    CheckGLExtensions();
    draw_ = Draw::T3DCreateGLContext(false);
    renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
    renderManager_->SetInflightFrames(g_Config.iInflightFrames);
    SetGPUBackend(GPUBackend::OPENGL);
    draw_->CreatePresets();
}

void LibretroGLContext::DestroyDrawContext() {
	LibretroHWRenderContext::DestroyDrawContext();
	renderManager_ = nullptr;
}
