#include <assert.h>
#include "gl_state.h"
#ifdef _WIN32
#include "GL/wglew.h"
#endif


#if defined(USING_GLES2) && !defined(IOS)
PFNGLALPHAFUNCQCOMPROC glAlphaFuncQCOM;
PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC eglGetSystemTimeFrequencyNV;
PFNEGLGETSYSTEMTIMENVPROC eglGetSystemTimeNV;
PFNGLDISCARDFRAMEBUFFEREXTPROC glDiscardFramebufferEXT;
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

// http://stackoverflow.com/questions/16147700/opengl-es-using-tegra-specific-extensions-gl-ext-texture-array

void CheckGLExtensions() {
	static bool done = false;
	if (done)
		return;
	done = true;

	memset(&gl_extensions, 0, sizeof(gl_extensions));

	const char *extString = (const char *)glGetString(GL_EXTENSIONS);

#ifdef WIN32
	const char *wglString = wglGetExtensionsStringEXT();
	gl_extensions.EXT_swap_control_tear = strstr(wglString, "WGL_EXT_swap_control_tear") != 0;
#elif !defined(USING_GLES2)
	// const char *glXString = glXQueryExtensionString();
	// gl_extensions.EXT_swap_control_tear = strstr(glXString, "GLX_EXT_swap_control_tear") != 0;
#endif
	gl_extensions.OES_packed_depth_stencil = strstr(extString, "GL_OES_packed_depth_stencil") != 0;
	gl_extensions.OES_depth24 = strstr(extString, "GL_OES_depth24") != 0;
	gl_extensions.OES_depth_texture = strstr(extString, "GL_OES_depth_texture") != 0;
	gl_extensions.OES_mapbuffer = strstr(extString, "GL_OES_mapbuffer") != 0;

#ifdef USING_GLES2
	gl_extensions.EXT_discard_framebuffer = strstr(extString, "GL_EXT_discard_framebuffer");
	if (gl_extensions.EXT_discard_framebuffer) {
		glDiscardFramebufferEXT = (PFNGLDISCARDFRAMEBUFFEREXTPROC)eglGetProcAddress("glDiscardFramebufferEXT");
	}
#endif

#ifdef ANDROID
	gl_extensions.QCOM_alpha_test = strstr(extString, "GL_QCOM_alpha_test") != 0;
	// Load extensions that are not auto-loaded by Android.
	if (gl_extensions.QCOM_alpha_test) {
		glAlphaFuncQCOM = (PFNGLALPHAFUNCQCOMPROC)eglGetProcAddress("glAlphaFuncQCOM");
	}

	// Look for EGL extensions
	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	
	const char *eglString = eglQueryString(display, EGL_EXTENSIONS);

	gl_extensions.EGL_NV_system_time = strstr(eglString, "EGL_NV_system_time") != 0;
	gl_extensions.EGL_NV_coverage_sample = strstr(eglString, "EGL_NV_coverage_sample") != 0;

	if (gl_extensions.EGL_NV_system_time) {
		eglGetSystemTimeNV = (PFNEGLGETSYSTEMTIMENVPROC) eglGetProcAddress("eglGetSystemTimeNV");
		eglGetSystemTimeFrequencyNV = (PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC) eglGetProcAddress("eglGetSystemTimeFrequencyNV");
	}

#endif

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