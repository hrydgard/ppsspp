#include <assert.h>
#include "gl_state.h"
#ifdef _WIN32
#include "GL/wglew.h"
#endif


OpenGLState glstate;
GLExtensions gl_extensions;

int OpenGLState::state_count = 0;

void OpenGLState::Initialize() {
	if(initialized) return;

	Restore();

	initialized = true;
}

void OpenGLState::Restore() {
	int count = 0;
	blend.restore(); count++;
	blendEquation.restore(); count++;
	blendFunc.restore(); count++;
	blendColor.restore(); count++;

	scissorTest.restore(); count++;
	scissorRect.restore(); count++;

	cullFace.restore(); count++;
	cullFaceMode.restore(); count++;
	frontFace.restore(); count++;

	depthTest.restore(); count++;
	depthRange.restore(); count++;
	depthFunc.restore(); count++;
	depthWrite.restore(); count++;

	colorMask.restore(); count++;
	viewport.restore(); count++;

	stencilTest.restore(); count++;
	stencilOp.restore(); count++;
	stencilFunc.restore(); count++;

	dither.restore(); count++;

	assert(count == state_count && "OpenGLState::Restore is missing some states");
}

void CheckGLExtensions() {
	static bool done = false;
	if (done)
		return;
	done = true;

	memset(&gl_extensions, 0, sizeof(gl_extensions));

	const char *extString = (const char *)glGetString(GL_EXTENSIONS);

	gl_extensions.OES_packed_depth_stencil = strstr(extString, "GL_OES_packed_depth_stencil") != 0;
	gl_extensions.OES_depth24 = strstr(extString, "GL_OES_depth24") != 0;
	gl_extensions.OES_depth_texture = strstr(extString, "GL_OES_depth_texture") != 0;
	gl_extensions.EXT_discard_framebuffer = strstr(extString, "GL_EXT_discard_framebuffer") != 0;
#ifdef USING_GLES2
	gl_extensions.FBO_ARB = true;
	gl_extensions.FBO_EXT = false;
#else
	gl_extensions.FBO_ARB = strstr(extString, "GL_ARB_framebuffer_object") != 0;
	gl_extensions.FBO_EXT = strstr(extString, "GL_EXT_framebuffer_object") != 0;
#endif
}

void OpenGLState::SetVSyncInterval(int interval) {
#ifdef _WIN32
	if( wglSwapIntervalEXT )
		wglSwapIntervalEXT(interval);
#endif
}