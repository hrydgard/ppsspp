#include <cstring>

#include "base/logging.h"
#include "base/stringutil.h"
#include "gfx/gl_common.h"
#include "gfx_es2/gpu_features.h"

#ifdef _WIN32
#include "GL/wglew.h"
#endif

#if defined(USING_GLES2)
#if defined(ANDROID) || defined(BLACKBERRY)
PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC eglGetSystemTimeFrequencyNV;
PFNEGLGETSYSTEMTIMENVPROC eglGetSystemTimeNV;
PFNGLDRAWTEXTURENVPROC glDrawTextureNV;
PFNGLBLITFRAMEBUFFERNVPROC glBlitFramebufferNV;
PFNGLMAPBUFFERPROC glMapBuffer;

PFNGLDISCARDFRAMEBUFFEREXTPROC glDiscardFramebufferEXT;
PFNGLGENVERTEXARRAYSOESPROC glGenVertexArraysOES;
PFNGLBINDVERTEXARRAYOESPROC glBindVertexArrayOES;
PFNGLDELETEVERTEXARRAYSOESPROC glDeleteVertexArraysOES;
PFNGLISVERTEXARRAYOESPROC glIsVertexArrayOES;
#endif
#ifndef IOS
#include "EGL/egl.h"
#endif
#endif

GLExtensions gl_extensions;
std::string g_all_gl_extensions;
std::string g_all_egl_extensions;

static bool extensionsDone = false;
static bool useCoreContext = false;

bool GLExtensions::VersionGEThan(int major, int minor, int sub) {
	if (gl_extensions.ver[0] > major)
		return true;
	if (gl_extensions.ver[0] < major)
		return false;
	if (gl_extensions.ver[1] > minor)
		return true;
	if (gl_extensions.ver[1] < minor)
		return false;
	return gl_extensions.ver[2] >= sub;
}

void ProcessGPUFeatures() {
	gl_extensions.bugs = 0;

	DLOG("Checking for GL driver bugs... vendor=%i model='%s'", (int)gl_extensions.gpuVendor, gl_extensions.model);

	// Should be table driven instead, this is a quick hack for Galaxy Y
	if (System_GetProperty(SYSPROP_NAME) == "samsung:GT-S5360") {
		gl_extensions.bugs |= BUG_FBO_UNUSABLE;
	}

	if (gl_extensions.gpuVendor == GPU_VENDOR_POWERVR) {
		if (!strcmp(gl_extensions.model, "PowerVR SGX 543") ||
			  !strcmp(gl_extensions.model, "PowerVR SGX 540") ||
			  !strcmp(gl_extensions.model, "PowerVR SGX 530") ||
				!strcmp(gl_extensions.model, "PowerVR SGX 520") ) {
			WLOG("GL DRIVER BUG: PVR with bad and terrible precision");
			gl_extensions.bugs |= BUG_PVR_SHADER_PRECISION_TERRIBLE | BUG_PVR_SHADER_PRECISION_BAD;
		} else {
			WLOG("GL DRIVER BUG: PVR with bad precision");
			gl_extensions.bugs |= BUG_PVR_SHADER_PRECISION_BAD;
		}
		gl_extensions.bugs |= BUG_PVR_GENMIPMAP_HEIGHT_GREATER;
	}
}

// http://stackoverflow.com/questions/16147700/opengl-es-using-tegra-specific-extensions-gl-ext-texture-array

void CheckGLExtensions() {
	// Make sure to only do this once. It's okay to call CheckGLExtensions from wherever.
	if (extensionsDone)
		return;
	extensionsDone = true;
	memset(&gl_extensions, 0, sizeof(gl_extensions));
	gl_extensions.IsCoreContext = useCoreContext;

#ifdef USING_GLES2
	gl_extensions.IsGLES = true;
#endif

	const char *renderer = (const char *)glGetString(GL_RENDERER);
	const char *versionStr = (const char *)glGetString(GL_VERSION);
	const char *glslVersionStr = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);

	// Check vendor string to try and guess GPU
	const char *cvendor = (char *)glGetString(GL_VENDOR);
	// TODO: move this stuff to gpu_features.cpp
	if (cvendor) {
		const std::string vendor = StripSpaces(std::string(cvendor));
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

	ILOG("GPU Vendor : %s ; renderer: %s version str: %s ; GLSL version str: %s", cvendor, renderer ? renderer : "N/A", versionStr ? versionStr : "N/A", glslVersionStr ? glslVersionStr : "N/A");

	if (renderer) {
		strncpy(gl_extensions.model, renderer, sizeof(gl_extensions.model));
		gl_extensions.model[sizeof(gl_extensions.model) - 1] = 0;
	}

	if (!gl_extensions.IsGLES) {
		// For desktop GL, grab the version and attempt to parse.
		char buffer[64] = { 0 };
		if (versionStr) {
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

		// If the GL version >= 4.3, we know it's a true superset of OpenGL ES 3.0 and can thus enable
		// all the same modern paths.
		// Most of it could be enabled on lower GPUs as well, but let's start this way.
		if (gl_extensions.VersionGEThan(4, 3, 0)) {
			gl_extensions.GLES3 = true;
		}
	} else {
		// Start by assuming we're at 2.0.
		gl_extensions.ver[0] = 2;

#ifdef USING_GLES2
#ifdef GL_MAJOR_VERSION
		// Before grabbing the values, reset the error.
		glGetError();
		glGetIntegerv(GL_MAJOR_VERSION, &gl_extensions.ver[0]);
		glGetIntegerv(GL_MINOR_VERSION, &gl_extensions.ver[1]);
		// We check error here to detect if these properties were supported.
		if (glGetError() != GL_NO_ERROR) {
			// They weren't, reset to GLES 2.0.
			gl_extensions.ver[0] = 2;
			gl_extensions.ver[1] = 0;
		}
#endif

		// If the above didn't give us a version, or gave us a crazy version, fallback.
		if (gl_extensions.ver[0] < 3 || gl_extensions.ver[0] > 5) {
			// Try to load GLES 3.0 only if "3.0" found in version
			// This simple heuristic avoids issues on older devices where you can only call eglGetProcAddress a limited
			// number of times. Make sure to check for 3.0 in the shader version too to avoid false positives, see #5584.
			bool gl_3_0_in_string = strstr(versionStr, "3.0") && strstr(glslVersionStr, "3.0");
			bool gl_3_1_in_string = strstr(versionStr, "3.1") && strstr(glslVersionStr, "3.1");  // intentionally left out .1
			if ((gl_3_0_in_string || gl_3_1_in_string) && gl3stubInit()) {
				gl_extensions.ver[0] = 3;
				if (gl_3_1_in_string) {
					gl_extensions.ver[1] = 1;
				}
				gl_extensions.GLES3 = true;
				// Though, let's ban Mali from the GLES 3 path for now, see #4078
				if (strstr(renderer, "Mali") != 0) {
					gl_extensions.GLES3 = false;
				}
			} else {
				// Just to be safe.
				gl_extensions.ver[0] = 2;
				gl_extensions.ver[1] = 0;
			}
		} else {
			// Otherwise, let's trust GL_MAJOR_VERSION.  Note that Mali is intentionally not banned here.
			if (gl_extensions.ver[0] >= 3) {
				gl_extensions.GLES3 = gl3stubInit();
			}
		}
#endif

		if (gl_extensions.GLES3) {
			if (gl_extensions.ver[1] >= 1) {
				ILOG("OpenGL ES 3.1 support detected!\n");
			} else {
				ILOG("OpenGL ES 3.0 support detected!\n");
			}
		}
	}

	const char *extString = nullptr;
	if (gl_extensions.ver[0] >= 3) {
		// Let's use the new way for OpenGL 3.x+, required in the core profile.
		GLint numExtensions = 0;
		glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
		g_all_gl_extensions = "";
		for (GLint i = 0; i < numExtensions; ++i) {
			g_all_gl_extensions += (const char *)glGetStringi(GL_EXTENSIONS, i);
			g_all_gl_extensions += " ";
		}
		extString = g_all_gl_extensions.c_str();
	} else {
		extString = (const char *)glGetString(GL_EXTENSIONS);
		if (extString) {
			g_all_gl_extensions = extString;
		} else {
			g_all_gl_extensions = "";
			extString = "";
		}
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

	// Check the desktop extension instead of the OES one. They are very similar.
	// Also explicitly check those ATI devices that claims to support npot
	gl_extensions.OES_texture_npot = strstr(extString, "GL_ARB_texture_non_power_of_two") != 0
		&& !(((strncmp(renderer, "ATI RADEON X", 12) == 0) || (strncmp(renderer, "ATI MOBILITY RADEON X", 21) == 0)));

	gl_extensions.ARB_blend_func_extended = strstr(extString, "GL_ARB_blend_func_extended") != 0;
	gl_extensions.EXT_blend_func_extended = strstr(extString, "GL_EXT_blend_func_extended") != 0;
	gl_extensions.ARB_conservative_depth = strstr(extString, "GL_ARB_conservative_depth") != 0;
	gl_extensions.ARB_shader_image_load_store = (strstr(extString, "GL_ARB_shader_image_load_store") != 0) || (strstr(extString, "GL_EXT_shader_image_load_store") != 0);
	gl_extensions.EXT_bgra = strstr(extString, "GL_EXT_bgra") != 0;
	gl_extensions.EXT_gpu_shader4 = strstr(extString, "GL_EXT_gpu_shader4") != 0;
	gl_extensions.NV_framebuffer_blit = strstr(extString, "GL_NV_framebuffer_blit") != 0;
	gl_extensions.NV_copy_image = strstr(extString, "GL_NV_copy_image") != 0;
	gl_extensions.OES_copy_image = strstr(extString, "GL_OES_copy_image") != 0;
	gl_extensions.EXT_copy_image = strstr(extString, "GL_EXT_copy_image") != 0;
	gl_extensions.ARB_copy_image = strstr(extString, "GL_ARB_copy_image") != 0;
	gl_extensions.ARB_vertex_array_object = strstr(extString, "GL_ARB_vertex_array_object") != 0;

	if (gl_extensions.IsGLES) {
		gl_extensions.OES_texture_npot = strstr(extString, "OES_texture_npot") != 0;
		gl_extensions.OES_packed_depth_stencil = (strstr(extString, "GL_OES_packed_depth_stencil") != 0) || gl_extensions.GLES3;
		gl_extensions.OES_depth24 = strstr(extString, "GL_OES_depth24") != 0;
		gl_extensions.OES_depth_texture = strstr(extString, "GL_OES_depth_texture") != 0;
		gl_extensions.OES_mapbuffer = strstr(extString, "GL_OES_mapbuffer") != 0;
		gl_extensions.EXT_blend_minmax = strstr(extString, "GL_EXT_blend_minmax") != 0;
		gl_extensions.EXT_unpack_subimage = strstr(extString, "GL_EXT_unpack_subimage") != 0;
		gl_extensions.EXT_shader_framebuffer_fetch = strstr(extString, "GL_EXT_shader_framebuffer_fetch") != 0;
		gl_extensions.NV_shader_framebuffer_fetch = strstr(extString, "GL_NV_shader_framebuffer_fetch") != 0;
		gl_extensions.ARM_shader_framebuffer_fetch = strstr(extString, "GL_ARM_shader_framebuffer_fetch") != 0;

#if defined(ANDROID) || defined(BLACKBERRY)
		// On Android, incredibly, this is not consistently non-zero! It does seem to have the same value though.
		// https://twitter.com/ID_AA_Carmack/status/387383037794603008
#ifdef _DEBUG
		void *invalidAddress = (void *)eglGetProcAddress("InvalidGlCall1");
		void *invalidAddress2 = (void *)eglGetProcAddress("AnotherInvalidGlCall2");
		DLOG("Addresses returned for invalid extensions: %p %p", invalidAddress, invalidAddress2);
#endif

		// These are all the same.  Let's alias.
		if (!gl_extensions.OES_copy_image) {
			if (gl_extensions.NV_copy_image) {
				glCopyImageSubDataOES = (decltype(glCopyImageSubDataOES))eglGetProcAddress("glCopyImageSubDataNV");
			} else if (gl_extensions.EXT_copy_image) {
				glCopyImageSubDataOES = (decltype(glCopyImageSubDataOES))eglGetProcAddress("glCopyImageSubDataEXT");
			}
		}

		if (gl_extensions.NV_framebuffer_blit) {
			glBlitFramebufferNV = (PFNGLBLITFRAMEBUFFERNVPROC)eglGetProcAddress("glBlitFramebufferNV");
		}

		gl_extensions.OES_vertex_array_object = strstr(extString, "GL_OES_vertex_array_object") != 0;
		if (gl_extensions.OES_vertex_array_object) {
			glGenVertexArraysOES = (PFNGLGENVERTEXARRAYSOESPROC)eglGetProcAddress("glGenVertexArraysOES");
			glBindVertexArrayOES = (PFNGLBINDVERTEXARRAYOESPROC)eglGetProcAddress("glBindVertexArrayOES");
			glDeleteVertexArraysOES = (PFNGLDELETEVERTEXARRAYSOESPROC)eglGetProcAddress("glDeleteVertexArraysOES");
			glIsVertexArrayOES = (PFNGLISVERTEXARRAYOESPROC)eglGetProcAddress("glIsVertexArrayOES");
		}

		// Hm, this should be available on iOS too.
		gl_extensions.EXT_discard_framebuffer = strstr(extString, "GL_EXT_discard_framebuffer") != 0;
		if (gl_extensions.EXT_discard_framebuffer) {
			glDiscardFramebufferEXT = (PFNGLDISCARDFRAMEBUFFEREXTPROC)eglGetProcAddress("glDiscardFramebufferEXT");
		}
#else
		gl_extensions.OES_vertex_array_object = false;
		gl_extensions.EXT_discard_framebuffer = false;
#endif
	} else {
		// Desktops support minmax and subimage unpack (GL_UNPACK_ROW_LENGTH etc)
		gl_extensions.EXT_blend_minmax = true;
		gl_extensions.EXT_unpack_subimage = true;
	}

	// GLES 3 subsumes many ES2 extensions.
	if (gl_extensions.GLES3) {
		gl_extensions.EXT_unpack_subimage = true;
	}

#if defined(ANDROID) || defined(BLACKBERRY)
	if (gl_extensions.OES_mapbuffer) {
		glMapBuffer = (PFNGLMAPBUFFERPROC)eglGetProcAddress("glMapBufferOES");
	}

	// Look for EGL extensions
	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

	const char *eglString = eglQueryString(display, EGL_EXTENSIONS);
	if (eglString) {
		g_all_egl_extensions = eglString;

		gl_extensions.EGL_NV_system_time = strstr(eglString, "EGL_NV_system_time") != 0;
		gl_extensions.EGL_NV_coverage_sample = strstr(eglString, "EGL_NV_coverage_sample") != 0;

		if (gl_extensions.EGL_NV_system_time) {
			eglGetSystemTimeNV = (PFNEGLGETSYSTEMTIMENVPROC)eglGetProcAddress("eglGetSystemTimeNV");
			eglGetSystemTimeFrequencyNV = (PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC)eglGetProcAddress("eglGetSystemTimeFrequencyNV");
		}
	} else {
		g_all_egl_extensions = "";
	}
#endif

	// This is probably a waste of time, implementations lie.
	if (gl_extensions.IsGLES || strstr(extString, "GL_ARB_ES2_compatibility")) {
		const GLint precisions[6] = {
			GL_LOW_FLOAT, GL_MEDIUM_FLOAT, GL_HIGH_FLOAT,
			GL_LOW_INT, GL_MEDIUM_INT, GL_HIGH_INT
		};
		GLint shaderTypes[2] = {
			GL_VERTEX_SHADER, GL_FRAGMENT_SHADER
		};
		for (int st = 0; st < 2; st++) {
			for (int p = 0; p < 6; p++) {
				glGetShaderPrecisionFormat(shaderTypes[st], precisions[p], gl_extensions.range[st][p], &gl_extensions.precision[st][p]);
			}
		}
	}

	gl_extensions.ARB_framebuffer_object = strstr(extString, "GL_ARB_framebuffer_object") != 0;
	gl_extensions.EXT_framebuffer_object = strstr(extString, "GL_EXT_framebuffer_object") != 0;
	gl_extensions.ARB_pixel_buffer_object = strstr(extString, "GL_ARB_pixel_buffer_object") != 0;
	gl_extensions.NV_pixel_buffer_object = strstr(extString, "GL_NV_pixel_buffer_object") != 0;

	if (!gl_extensions.IsGLES && gl_extensions.IsCoreContext) {
		// These are required, and don't need to be specified by the driver (they aren't on Apple.)
		gl_extensions.ARB_vertex_array_object = true;
		gl_extensions.ARB_framebuffer_object = true;
	}
#ifdef __APPLE__
	if (!gl_extensions.IsGLES && !gl_extensions.IsCoreContext) {
		// Apple doesn't allow OpenGL 3.x+ in compatibility contexts.
		gl_extensions.ForceGL2 = true;
	}
#endif

	ProcessGPUFeatures();

	int error = glGetError();
	if (error)
		ELOG("GL error in init: %i", error);
}

void SetGLCoreContext(bool flag) {
	if (extensionsDone)
		FLOG("SetGLCoreContext() after CheckGLExtensions()");

	useCoreContext = flag;
	// For convenience, it'll get reset later.
	gl_extensions.IsCoreContext = useCoreContext;
}
