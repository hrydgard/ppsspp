#include <stdlib.h>

#include "base/logging.h"
#include "base/NativeApp.h"
#include "gl_state.h"

#ifdef _WIN32
#include "GL/wglew.h"
#endif

#if defined(USING_GLES2)
#if defined(ANDROID) || defined(BLACKBERRY)
PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC eglGetSystemTimeFrequencyNV;
PFNEGLGETSYSTEMTIMENVPROC eglGetSystemTimeNV;
PFNGLDRAWTEXTURENVPROC glDrawTextureNV;
PFNGLCOPYIMAGESUBDATANVPROC glCopyImageSubDataNV ;
PFNGLMAPBUFFERPROC glMapBuffer;

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
	if (initialized)
		return;
	initialized = true;

	Restore();
}

void OpenGLState::Restore() {
	int count = 0;

	blend.restore(); count++;
	blendEquation.restore(); count++;
	blendFuncSeparate.restore(); count++;
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
	// Make sure to only do this once. It's okay to call CheckGLExtensions from wherever.
	static bool done = false;
	if (done)
		return;
	done = true;
	memset(&gl_extensions, 0, sizeof(gl_extensions));

	const char *renderer = (const char *)glGetString(GL_RENDERER);
	const char *versionStr = (const char *)glGetString(GL_VERSION);

	// Check vendor string to try and guess GPU
	const char *cvendor = (char *)glGetString(GL_VENDOR);
	// TODO: move this stuff to gpu_features.cpp
	if (cvendor) {
		const std::string vendor(cvendor);
		if (vendor == "NVIDIA Corporation"
			|| vendor == "Nouveau"
			|| vendor == "nouveau") {
				gl_extensions.gpuVendor = GPU_VENDOR_NVIDIA;
		} else if (vendor == "Advanced Micro Devices, Inc."
			|| vendor == "ATI Technologies Inc.") {
				gl_extensions.gpuVendor = GPU_VENDOR_AMD;
		} else if (vendor == "Intel"
			|| vendor == "Intel Inc."
			|| vendor == "Intel Corporation"
			|| vendor == "Tungsten Graphics, Inc") { // We'll assume this last one means Intel
				gl_extensions.gpuVendor = GPU_VENDOR_INTEL;
		} else if (vendor == "ARM") {
			gl_extensions.gpuVendor = GPU_VENDOR_ARM;
		} else if (vendor == "Imagination Technologies") {
			gl_extensions.gpuVendor = GPU_VENDOR_POWERVR;
		} else if (vendor == "Qualcomm") {
			gl_extensions.gpuVendor = GPU_VENDOR_ADRENO;
		} else if (vendor == "Broadcom") {
			gl_extensions.gpuVendor = GPU_VENDOR_BROADCOM;
			// Just for reference: Galaxy Y has renderer == "VideoCore IV HW"
		} else {
			gl_extensions.gpuVendor = GPU_VENDOR_UNKNOWN;
		}
	} else {
		gl_extensions.gpuVendor = GPU_VENDOR_UNKNOWN;
	}

	ILOG("GPU Vendor : %s", cvendor);

#ifndef USING_GLES2

	char buffer[64] = {0};
	if (versionStr) {
		ILOG("GL version str: %s", versionStr);
		strncpy(buffer, versionStr, 63);
	}
	const char *lastNumStart = buffer;
	int numVer = 0;
	int len = (int)strlen(buffer);
	for (int i = 0; i < len && numVer < 3; i++) {
		if (buffer[i] == '.') {
			buffer[i] = 0;
			gl_extensions.ver[numVer++] = strtol(lastNumStart, NULL, 10);
			i++;
			lastNumStart = buffer + i;
		}
	}
	if (numVer < 3)
		gl_extensions.ver[numVer++] = strtol(lastNumStart, NULL, 10);
#else
	gl_extensions.ver[0] = 2;
#endif

#if defined(USING_GLES2)
	// MAY_HAVE_GLES3 defined on all platforms that support it
#if defined(MAY_HAVE_GLES3)
	// Try to load GLES 3.0 only if "3.0" found in version
	// This simple heuristic avoids issues on older devices where you can only call eglGetProcAddress a limited
	// number of times.
	if (strstr(versionStr, "3.0") && GL_TRUE == gl3stubInit()) {
		gl_extensions.ver[0] = 3;
		gl_extensions.GLES3 = true;
		ILOG("Full OpenGL ES 3.0 support detected!\n");
		// Though, let's ban Mali from the GLES 3 path for now, see #4078
		if (strstr(renderer, "Mali") != 0) {
			gl_extensions.GLES3 = false;
		}
	}
#endif
#else
	// If the GL version >= 4.3, we know it's a true superset of OpenGL ES 3.0 and can thus enable
	// modern paths.
	// Most of it could be enabled on lower GPUs as well, but let's start this way.
	if ((gl_extensions.ver[0] == 4 && gl_extensions.ver[1] >= 3) || gl_extensions.ver[0] > 4) {
		gl_extensions.GLES3 = true;
	}
#endif

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
	gl_extensions.EXT_unpack_subimage = strstr(extString, "GL_EXT_unpack_subimage") != 0;
#if defined(ANDROID) || defined(BLACKBERRY)
	// On Android, incredibly, this is not consistently non-zero! It does seem to have the same value though.
	// https://twitter.com/ID_AA_Carmack/status/387383037794603008
	void *invalidAddress = (void *)eglGetProcAddress("InvalidGlCall1");
	void *invalidAddress2 = (void *)eglGetProcAddress("AnotherInvalidGlCall2");
	ILOG("Addresses returned for invalid extensions: %p %p", invalidAddress, invalidAddress2);
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
#else
	gl_extensions.OES_vertex_array_object = false;
	gl_extensions.EXT_discard_framebuffer = false;
#endif
#else
	// Desktops support minmax and subimage unpack (GL_UNPACK_ROW_LENGTH etc)
	gl_extensions.EXT_blend_minmax = true;
	gl_extensions.EXT_unpack_subimage = true;
#endif
	// GLES 3 subsumes many ES2 extensions.
	if (gl_extensions.GLES3) {
		gl_extensions.EXT_unpack_subimage = true;
	}

#if defined(ANDROID) || defined(BLACKBERRY)
	if (gl_extensions.OES_mapbuffer) {
		glMapBuffer = (PFNGLMAPBUFFERPROC)eglGetProcAddress( "glMapBufferOES" );
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
		gl_extensions.ATIClampBug = ((strncmp (renderer, "ATI RADEON X", 12) != 0) || (strncmp (renderer, "ATI MOBILITY RADEON X",21) != 0));
	}
#endif

	ProcessGPUFeatures();
}

void OpenGLState::SetVSyncInterval(int interval) {
#ifdef _WIN32
	if (wglSwapIntervalEXT)
		wglSwapIntervalEXT(interval);
#endif
}
