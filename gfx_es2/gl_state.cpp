#include "gl_state.h"

OpenGLState glstate;
GLExtensions gl_extensions;

void OpenGLState::Initialize() {
	if(initialized) return;

	Restore();

	initialized = true;
}

void OpenGLState::Restore() {
	blend.restore();
	blendEquation.restore();
	blendFunc.restore();
	blendColor.restore();

	scissorTest.restore();

	cullFace.restore();
	cullFaceMode.restore();
	frontFace.restore();

	depthTest.restore();
	depthRange.restore();
	depthFunc.restore();
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
}

