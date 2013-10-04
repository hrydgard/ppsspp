#include "base/logging.h"
#include "gl_state.h"
#ifdef _WIN32
#include "GL/wglew.h"
#endif

#if defined(USING_GLES2)
#if defined(ANDROID) || defined(BLACKBERRY)
PFNGLALPHAFUNCQCOMPROC glAlphaFuncQCOM;
PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC eglGetSystemTimeFrequencyNV;
PFNEGLGETSYSTEMTIMENVPROC eglGetSystemTimeNV;
PFNGLDRAWTEXTURENVPROC glDrawTextureNV;
PFNGLCOPYIMAGESUBDATANVPROC glCopyImageSubDataNV ;
PFNGLMAPBUFFERPROC glMapBuffer;
#endif
#if !defined(IOS) && !defined(__SYMBIAN32__) && !defined(MEEGO_EDITION_HARMATTAN) && !defined(MAEMO)
PFNGLDISCARDFRAMEBUFFEREXTPROC glDiscardFramebufferEXT;
PFNGLGENVERTEXARRAYSOESPROC glGenVertexArraysOES;
PFNGLBINDVERTEXARRAYOESPROC glBindVertexArrayOES;
PFNGLDELETEVERTEXARRAYSOESPROC glDeleteVertexArraysOES;
PFNGLISVERTEXARRAYOESPROC glIsVertexArrayOES;
#endif
#endif

OpenGLState glstate;
GLExtensions gl_extensions;
std::string g_all_gl_extensions;
std::string g_all_egl_extensions;

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
	blendFuncSeparate.restore(); count++;
	blendColor.restore(); count++;

#if defined(ANDROID) || defined(BLACKBERRY)
	if (gl_extensions.QCOM_alpha_test) {
		alphaTestQCOM.restore();
	}
	count++;
	if (gl_extensions.QCOM_alpha_test) {
		alphaFuncQCOM.restore();
	}
	count++;
#endif

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

#if !defined(USING_GLES2)
	colorLogicOp.restore(); count++;
	logicOp.restore(); count++;
#endif

	if (count != state_count) {
		FLOG("OpenGLState::Restore is missing some states");
	}
}

// http://stackoverflow.com/questions/16147700/opengl-es-using-tegra-specific-extensions-gl-ext-texture-array

void CheckGLExtensions() {
	static bool done = false;
	if (done)
		return;
	done = true;

	memset(&gl_extensions, 0, sizeof(gl_extensions));

	const char *extString = (const char *)glGetString(GL_EXTENSIONS);
	if (extString) {
		g_all_gl_extensions = extString;
	} else {
		g_all_gl_extensions = "";
	}

#ifdef WIN32
	const char *wglString = 0;
	if (wglGetExtensionsStringEXT)
		wglString = wglGetExtensionsStringEXT();
	if (wglString) {
		gl_extensions.EXT_swap_control_tear = strstr(wglString, "WGL_EXT_swap_control_tear") != 0;
		g_all_egl_extensions = wglString;
	} else {
		g_all_egl_extensions = "";
	}
#elif !defined(USING_GLES2)
	// const char *glXString = glXQueryExtensionString();
	// gl_extensions.EXT_swap_control_tear = strstr(glXString, "GLX_EXT_swap_control_tear") != 0;
#endif

#ifdef USING_GLES2
	gl_extensions.OES_packed_depth_stencil = strstr(extString, "GL_OES_packed_depth_stencil") != 0;
	gl_extensions.OES_depth24 = strstr(extString, "GL_OES_depth24") != 0;
	gl_extensions.OES_depth_texture = strstr(extString, "GL_OES_depth_texture") != 0;
	gl_extensions.OES_mapbuffer = strstr(extString, "GL_OES_mapbuffer") != 0;
	gl_extensions.EXT_blend_minmax = strstr(extString, "GL_EXT_blend_minmax") != 0;
	gl_extensions.EXT_shader_framebuffer_fetch = (strstr(extString, "GL_EXT_shader_framebuffer_fetch") != 0) || (strstr(extString, "GL_NV_shader_framebuffer_fetch") != 0);
	gl_extensions.NV_draw_texture = strstr(extString, "GL_NV_draw_texture") != 0;
	gl_extensions.NV_copy_image = strstr(extString, "GL_NV_copy_image") != 0;
#if defined(IOS) || defined(__SYMBIAN32__) || defined(MEEGO_EDITION_HARMATTAN) || defined(MAEMO)
	gl_extensions.OES_vertex_array_object = false;
	gl_extensions.EXT_discard_framebuffer = false;
#else
	if (gl_extensions.NV_draw_texture) {
		glDrawTextureNV = (PFNGLDRAWTEXTURENVPROC)eglGetProcAddress("glDrawTextureNV");
	}
	if (gl_extensions.NV_copy_image) {
		glCopyImageSubDataNV = (PFNGLCOPYIMAGESUBDATANVPROC)eglGetProcAddress("glCopyImageSubDataNV");
	}
	gl_extensions.OES_vertex_array_object = strstr(extString, "GL_OES_vertex_array_object") != 0;
	if (gl_extensions.OES_vertex_array_object) {
		glGenVertexArraysOES = (PFNGLGENVERTEXARRAYSOESPROC)eglGetProcAddress ( "glGenVertexArraysOES" );
		glBindVertexArrayOES = (PFNGLBINDVERTEXARRAYOESPROC)eglGetProcAddress ( "glBindVertexArrayOES" );
		glDeleteVertexArraysOES = (PFNGLDELETEVERTEXARRAYSOESPROC)eglGetProcAddress ( "glDeleteVertexArraysOES" );
		glIsVertexArrayOES = (PFNGLISVERTEXARRAYOESPROC)eglGetProcAddress ( "glIsVertexArrayOES" );
	}

	gl_extensions.EXT_discard_framebuffer = strstr(extString, "GL_EXT_discard_framebuffer") != 0;
	if (gl_extensions.EXT_discard_framebuffer) {
		glDiscardFramebufferEXT = (PFNGLDISCARDFRAMEBUFFEREXTPROC)eglGetProcAddress("glDiscardFramebufferEXT");
	}

#endif
#else
	// Desktops support minmax
	gl_extensions.EXT_blend_minmax = true;
#endif

#if defined(ANDROID) || defined(BLACKBERRY)
	if (gl_extensions.OES_mapbuffer) {
		glMapBuffer = (PFNGLMAPBUFFERPROC)eglGetProcAddress( "glMapBufferOES" );
	}
	gl_extensions.QCOM_binning_control = strstr(extString, "GL_QCOM_binning_control") != 0;
	gl_extensions.QCOM_alpha_test = strstr(extString, "GL_QCOM_alpha_test") != 0;
	// Load extensions that are not auto-loaded by Android.
	if (gl_extensions.QCOM_alpha_test) {
		glAlphaFuncQCOM = (PFNGLALPHAFUNCQCOMPROC)eglGetProcAddress("glAlphaFuncQCOM");
	}

	// Look for EGL extensions
	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

	const char *eglString = eglQueryString(display, EGL_EXTENSIONS);
	if (eglString) {
		g_all_egl_extensions = eglString;

		gl_extensions.EGL_NV_system_time = strstr(eglString, "EGL_NV_system_time") != 0;
		gl_extensions.EGL_NV_coverage_sample = strstr(eglString, "EGL_NV_coverage_sample") != 0;

		if (gl_extensions.EGL_NV_system_time) {
			eglGetSystemTimeNV = (PFNEGLGETSYSTEMTIMENVPROC) eglGetProcAddress("eglGetSystemTimeNV");
			eglGetSystemTimeFrequencyNV = (PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC) eglGetProcAddress("eglGetSystemTimeFrequencyNV");
		}
	} else {
		g_all_egl_extensions = "";
	}

#endif

#ifdef USING_GLES2
	gl_extensions.FBO_ARB = true;
	gl_extensions.FBO_EXT = false;
#else
	gl_extensions.FBO_ARB = false;
	gl_extensions.FBO_EXT = false;
	gl_extensions.PBO_ARB = true;
	if (extString) {
		gl_extensions.FBO_ARB = strstr(extString, "GL_ARB_framebuffer_object") != 0;
		gl_extensions.FBO_EXT = strstr(extString, "GL_EXT_framebuffer_object") != 0;
		gl_extensions.PBO_ARB = strstr(extString, "GL_ARB_pixel_buffer_object") != 0;
		gl_extensions.ATIClampBug = ((strncmp ((char *)glGetString(GL_RENDERER), "ATI RADEON X", 12) != 0) || (strncmp ((char *)glGetString(GL_RENDERER), "ATI MOBILITY RADEON X",21) != 0));
	}
#endif
}

void OpenGLState::SetVSyncInterval(int interval) {
#ifdef _WIN32
	if( wglSwapIntervalEXT )
		wglSwapIntervalEXT(interval);
#endif
}
