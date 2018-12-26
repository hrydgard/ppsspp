#include "ppsspp_config.h"

#include <cstring>
#include <set>

#include "base/logging.h"
#include "base/stringutil.h"

#if !PPSSPP_PLATFORM(UWP)
#include "gfx/gl_common.h"

#if defined(_WIN32)
#include "GL/wglew.h"
#endif
#endif

#include "gfx_es2/gpu_features.h"


#if defined(USING_GLES2)
#if defined(__ANDROID__)
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
std::set<std::string> g_set_gl_extensions;
std::string g_all_egl_extensions;
std::set<std::string> g_set_egl_extensions;

static bool extensionsDone = false;
static bool useCoreContext = false;

static void ParseExtensionsString(const std::string& str, std::set<std::string> &output) {
	output.clear();

	size_t next = 0;
	for (size_t pos = 0, len = str.length(); pos < len; ++pos) {
		if (str[pos] == ' ') {
			output.insert(str.substr(next, pos - next));
			// Skip the delimiter itself.
			next = pos + 1;
		}
	}

	if (next == 0 && str.length() != 0) {
		output.insert(str);
	} else if (next < str.length()) {
		output.insert(str.substr(next));
	}
}

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

int GLExtensions::GLSLVersion() {
	// Used for shader translation and core contexts (Apple drives fail without an exact match.)
	if (gl_extensions.VersionGEThan(3, 3)) {
		return gl_extensions.ver[0] * 100 + gl_extensions.ver[1] * 10;
	} else if (gl_extensions.VersionGEThan(3, 2)) {
		return 150;
	} else if (gl_extensions.VersionGEThan(3, 1)) {
		return 140;
	} else if (gl_extensions.VersionGEThan(3, 0)) {
		return 130;
	} else if (gl_extensions.VersionGEThan(2, 1)) {
		return 120;
	} else {
		return 110;
	}
}

void ProcessGPUFeatures() {
	gl_extensions.bugs = 0;

	DLOG("Checking for GL driver bugs... vendor=%i model='%s'", (int)gl_extensions.gpuVendor, gl_extensions.model);

	if (gl_extensions.gpuVendor == GPU_VENDOR_IMGTEC) {
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
	}
}

// http://stackoverflow.com/questions/16147700/opengl-es-using-tegra-specific-extensions-gl-ext-texture-array

void CheckGLExtensions() {

#if !PPSSPP_PLATFORM(UWP)

	// Make sure to only do this once. It's okay to call CheckGLExtensions from wherever.
	if (extensionsDone)
		return;
	extensionsDone = true;
	memset(&gl_extensions, 0, sizeof(gl_extensions));
	gl_extensions.IsCoreContext = useCoreContext;

#ifdef USING_GLES2
	gl_extensions.IsGLES = !useCoreContext;
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
			gl_extensions.gpuVendor = GPU_VENDOR_IMGTEC;
		} else if (vendor == "Qualcomm") {
			gl_extensions.gpuVendor = GPU_VENDOR_QUALCOMM;
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
	
	// Start by assuming we're at 2.0.
	int parsed[2] = {2, 0};
	{ // Grab the version and attempt to parse.		
		char buffer[128] = { 0 };
		if (versionStr) {
			strncpy(buffer, versionStr, sizeof(buffer) - 1);
		}
	
		int len = (int)strlen(buffer);
		bool beforeDot = true;
		int lastDigit = 0;
		for (int i = 0; i < len; i++) {
			if (buffer[i] >= '0' && buffer[i] <= '9') {
				lastDigit = buffer[i] - '0';
				if (!beforeDot) {
					parsed[1] = lastDigit;
					break;
				}
			}
			if (beforeDot && buffer[i] == '.' && lastDigit) {
				parsed[0] = lastDigit;
				beforeDot = false;
			}
		}
		if (beforeDot && lastDigit) {
			parsed[0] = lastDigit;
		}
	}

#ifndef USING_GLES2
	if (strstr(versionStr, "OpenGL ES") == versionStr) {
		// For desktops running GLES.
		gl_extensions.IsGLES = true;
	}
#endif

	if (!gl_extensions.IsGLES) { // For desktop GL
		gl_extensions.ver[0] = parsed[0];
		gl_extensions.ver[1] = parsed[1];

		// If the GL version >= 4.3, we know it's a true superset of OpenGL ES 3.0 and can thus enable
		// all the same modern paths.
		// Most of it could be enabled on lower GPUs as well, but let's start this way.
		if (gl_extensions.VersionGEThan(4, 3, 0)) {
			gl_extensions.GLES3 = true;
#ifdef USING_GLES2
			// Try to load up the other funcs if we're not using glew.
			gl3stubInit();
#endif
		}
	} else {
		// Start by assuming we're at 2.0.
		gl_extensions.ver[0] = 2;

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
		} else if (parsed[0] && (gl_extensions.ver[0] != parsed[0] || gl_extensions.ver[1] != parsed[1])) {
			// Something going wrong. Possible bug in GL ES drivers. See #9688
			ILOG("GL ES version mismatch. Version string '%s' parsed as %d.%d but API return %d.%d. Fallback to GL ES 2.0.", 
				versionStr ? versionStr : "N/A", parsed[0], parsed[1], gl_extensions.ver[0], gl_extensions.ver[1]);
			
			gl_extensions.ver[0] = 2;
			gl_extensions.ver[1] = 0;
		}
#endif

		// If the above didn't give us a version, or gave us a crazy version, fallback.
#ifdef USING_GLES2
		if (gl_extensions.ver[0] < 3 || gl_extensions.ver[0] > 5) {
			// Try to load GLES 3.0 only if "3.0" found in version
			// This simple heuristic avoids issues on older devices where you can only call eglGetProcAddress a limited
			// number of times. Make sure to check for 3.0 in the shader version too to avoid false positives, see #5584.
			bool gl_3_0_in_string = strstr(versionStr, "3.0") && (glslVersionStr && strstr(glslVersionStr, "3.0"));
			bool gl_3_1_in_string = strstr(versionStr, "3.1") && (glslVersionStr && strstr(glslVersionStr, "3.1"));  // intentionally left out .1
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
#else
		// If we have GLEW/similar, assume GLES3 loaded.
		gl_extensions.GLES3 = gl_extensions.ver[0] >= 3;
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
		g_all_gl_extensions.clear();
		g_set_gl_extensions.clear();
		for (GLint i = 0; i < numExtensions; ++i) {
			const char *ext = (const char *)glGetStringi(GL_EXTENSIONS, i);
			g_set_gl_extensions.insert(ext);
			g_all_gl_extensions += ext;
			g_all_gl_extensions += " ";
		}
	} else {
		extString = (const char *)glGetString(GL_EXTENSIONS);
		g_all_gl_extensions = extString ? extString : "";
		ParseExtensionsString(g_all_gl_extensions, g_set_gl_extensions);
	}

#ifdef WIN32
	const char *wglString = 0;
	if (wglGetExtensionsStringEXT)
		wglString = wglGetExtensionsStringEXT();
	g_all_egl_extensions = wglString ? wglString : "";
	ParseExtensionsString(g_all_egl_extensions, g_set_egl_extensions);

	gl_extensions.EXT_swap_control_tear = g_set_egl_extensions.count("WGL_EXT_swap_control_tear") != 0;
#elif !defined(USING_GLES2)
	// const char *glXString = glXQueryExtensionString();
	// gl_extensions.EXT_swap_control_tear = strstr(glXString, "GLX_EXT_swap_control_tear") != 0;
#endif

	// Check the desktop extension instead of the OES one. They are very similar.
	// Also explicitly check those ATI devices that claims to support npot
	if (renderer) {
		gl_extensions.OES_texture_npot = g_set_gl_extensions.count("GL_ARB_texture_non_power_of_two") != 0
			&& !(((strncmp(renderer, "ATI RADEON X", 12) == 0) || (strncmp(renderer, "ATI MOBILITY RADEON X", 21) == 0)));
	}

	gl_extensions.ARB_blend_func_extended = g_set_gl_extensions.count("GL_ARB_blend_func_extended") != 0;
	gl_extensions.EXT_blend_func_extended = g_set_gl_extensions.count("GL_EXT_blend_func_extended") != 0;
	gl_extensions.ARB_conservative_depth = g_set_gl_extensions.count("GL_ARB_conservative_depth") != 0;
	gl_extensions.ARB_shader_image_load_store = (g_set_gl_extensions.count("GL_ARB_shader_image_load_store") != 0) || (g_set_gl_extensions.count("GL_EXT_shader_image_load_store") != 0);
	gl_extensions.EXT_bgra = g_set_gl_extensions.count("GL_EXT_bgra") != 0;
	gl_extensions.EXT_gpu_shader4 = g_set_gl_extensions.count("GL_EXT_gpu_shader4") != 0;
	gl_extensions.NV_framebuffer_blit = g_set_gl_extensions.count("GL_NV_framebuffer_blit") != 0;
	gl_extensions.NV_copy_image = g_set_gl_extensions.count("GL_NV_copy_image") != 0;
	gl_extensions.OES_copy_image = g_set_gl_extensions.count("GL_OES_copy_image") != 0;
	gl_extensions.EXT_copy_image = g_set_gl_extensions.count("GL_EXT_copy_image") != 0;
	gl_extensions.ARB_copy_image = g_set_gl_extensions.count("GL_ARB_copy_image") != 0;
	gl_extensions.ARB_buffer_storage = g_set_gl_extensions.count("GL_ARB_buffer_storage") != 0;
	gl_extensions.ARB_vertex_array_object = g_set_gl_extensions.count("GL_ARB_vertex_array_object") != 0;
	gl_extensions.ARB_texture_float = g_set_gl_extensions.count("GL_ARB_texture_float") != 0;
	gl_extensions.EXT_texture_filter_anisotropic = g_set_gl_extensions.count("GL_EXT_texture_filter_anisotropic") != 0 || g_set_gl_extensions.count("GL_ARB_texture_filter_anisotropic") != 0;
	gl_extensions.EXT_draw_instanced = g_set_gl_extensions.count("GL_EXT_draw_instanced") != 0;
	gl_extensions.ARB_draw_instanced = g_set_gl_extensions.count("GL_ARB_draw_instanced") != 0;
	gl_extensions.ARB_cull_distance = g_set_gl_extensions.count("GL_ARB_cull_distance") != 0;

	if (gl_extensions.IsGLES) {
		gl_extensions.OES_texture_npot = g_set_gl_extensions.count("GL_OES_texture_npot") != 0;
		gl_extensions.OES_packed_depth_stencil = (g_set_gl_extensions.count("GL_OES_packed_depth_stencil") != 0) || gl_extensions.GLES3;
		gl_extensions.OES_depth24 = g_set_gl_extensions.count("GL_OES_depth24") != 0;
		gl_extensions.OES_depth_texture = g_set_gl_extensions.count("GL_OES_depth_texture") != 0;
		gl_extensions.OES_mapbuffer = g_set_gl_extensions.count("GL_OES_mapbuffer") != 0;
		gl_extensions.EXT_blend_minmax = g_set_gl_extensions.count("GL_EXT_blend_minmax") != 0;
		gl_extensions.EXT_unpack_subimage = g_set_gl_extensions.count("GL_EXT_unpack_subimage") != 0;
		gl_extensions.EXT_shader_framebuffer_fetch = g_set_gl_extensions.count("GL_EXT_shader_framebuffer_fetch") != 0;
		gl_extensions.NV_shader_framebuffer_fetch = g_set_gl_extensions.count("GL_NV_shader_framebuffer_fetch") != 0;
		gl_extensions.ARM_shader_framebuffer_fetch = g_set_gl_extensions.count("GL_ARM_shader_framebuffer_fetch") != 0;
		gl_extensions.OES_texture_float = g_set_gl_extensions.count("GL_OES_texture_float") != 0;
		gl_extensions.EXT_buffer_storage = g_set_gl_extensions.count("GL_EXT_buffer_storage") != 0;
		gl_extensions.EXT_clip_cull_distance = g_set_gl_extensions.count("GL_EXT_clip_cull_distance") != 0;

#if defined(__ANDROID__)
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

		gl_extensions.OES_vertex_array_object = g_set_gl_extensions.count("GL_OES_vertex_array_object") != 0;
		if (gl_extensions.OES_vertex_array_object) {
			glGenVertexArraysOES = (PFNGLGENVERTEXARRAYSOESPROC)eglGetProcAddress("glGenVertexArraysOES");
			glBindVertexArrayOES = (PFNGLBINDVERTEXARRAYOESPROC)eglGetProcAddress("glBindVertexArrayOES");
			glDeleteVertexArraysOES = (PFNGLDELETEVERTEXARRAYSOESPROC)eglGetProcAddress("glDeleteVertexArraysOES");
			glIsVertexArrayOES = (PFNGLISVERTEXARRAYOESPROC)eglGetProcAddress("glIsVertexArrayOES");
		}

		// Hm, this should be available on iOS too.
		gl_extensions.EXT_discard_framebuffer = g_set_gl_extensions.count("GL_EXT_discard_framebuffer") != 0;
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
		gl_extensions.EXT_blend_minmax = true;
		gl_extensions.EXT_unpack_subimage = true;
	}

#if defined(__ANDROID__)
	if (gl_extensions.OES_mapbuffer) {
		glMapBuffer = (PFNGLMAPBUFFERPROC)eglGetProcAddress("glMapBufferOES");
	}

	// Look for EGL extensions
	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

	const char *eglString = eglQueryString(display, EGL_EXTENSIONS);
	g_all_egl_extensions = eglString ? eglString : "";
	ParseExtensionsString(g_all_egl_extensions, g_set_egl_extensions);

	gl_extensions.EGL_NV_system_time = g_set_egl_extensions.count("EGL_NV_system_time") != 0;
	gl_extensions.EGL_NV_coverage_sample = g_set_egl_extensions.count("EGL_NV_coverage_sample") != 0;

	if (gl_extensions.EGL_NV_system_time) {
		eglGetSystemTimeNV = (PFNEGLGETSYSTEMTIMENVPROC)eglGetProcAddress("eglGetSystemTimeNV");
		eglGetSystemTimeFrequencyNV = (PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC)eglGetProcAddress("eglGetSystemTimeFrequencyNV");
	}
#elif defined(USING_GLES2) && defined(__linux__)
	const char *eglString = eglQueryString(NULL, EGL_EXTENSIONS);
	g_all_egl_extensions = eglString ? eglString : "";
	if (eglString) {
		eglString = eglQueryString(eglGetCurrentDisplay(), EGL_EXTENSIONS);
		if (eglString) {
			g_all_egl_extensions.append(" ");
			g_all_egl_extensions.append(eglString);
		}
	}
	ParseExtensionsString(g_all_egl_extensions, g_set_egl_extensions);
#endif

	glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &gl_extensions.maxVertexTextureUnits);

#ifdef GL_LOW_FLOAT
	// This is probably a waste of time, implementations lie.
	if (gl_extensions.IsGLES || g_set_gl_extensions.count("GL_ARB_ES2_compatibility") || gl_extensions.VersionGEThan(4, 1)) {
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

		// Now, Adreno lies. So let's override it.
		if (gl_extensions.gpuVendor == GPU_VENDOR_QUALCOMM) {
			WLOG("Detected Adreno - lowering int precision");
			gl_extensions.range[1][5][0] = 15;
			gl_extensions.range[1][5][1] = 15;
		}
	}
#endif

	gl_extensions.ARB_framebuffer_object = g_set_gl_extensions.count("GL_ARB_framebuffer_object") != 0;
	gl_extensions.EXT_framebuffer_object = g_set_gl_extensions.count("GL_EXT_framebuffer_object") != 0;
	gl_extensions.ARB_pixel_buffer_object = g_set_gl_extensions.count("GL_ARB_pixel_buffer_object") != 0;
	gl_extensions.NV_pixel_buffer_object = g_set_gl_extensions.count("GL_NV_pixel_buffer_object") != 0;

	if (!gl_extensions.IsGLES && gl_extensions.IsCoreContext) {
		// These are required, and don't need to be specified by the driver (they aren't on Apple.)
		gl_extensions.ARB_vertex_array_object = true;
		gl_extensions.ARB_framebuffer_object = true;
	}

	// Add any extensions that are included in core.  May be elided.
	if (!gl_extensions.IsGLES) {
		if (gl_extensions.VersionGEThan(3, 0)) {
			gl_extensions.ARB_texture_float = true;
		}
		if (gl_extensions.VersionGEThan(3, 1)) {
			gl_extensions.ARB_draw_instanced = true;
			// ARB_uniform_buffer_object = true;
		}
		if (gl_extensions.VersionGEThan(3, 2)) {
			// ARB_depth_clamp = true;
		}
		if (gl_extensions.VersionGEThan(3, 3)) {
			gl_extensions.ARB_blend_func_extended = true;
			// ARB_explicit_attrib_location = true;
		}
		if (gl_extensions.VersionGEThan(4, 0)) {
			// ARB_gpu_shader5 = true;
		}
		if (gl_extensions.VersionGEThan(4, 1)) {
			// ARB_get_program_binary = true;
			// ARB_separate_shader_objects = true;
			// ARB_shader_precision = true;
			// ARB_viewport_array = true;
		}
		if (gl_extensions.VersionGEThan(4, 2)) {
			// ARB_texture_storage = true;
		}
		if (gl_extensions.VersionGEThan(4, 3)) {
			gl_extensions.ARB_copy_image = true;
			// ARB_explicit_uniform_location = true;
			// ARB_stencil_texturing = true;
			// ARB_texture_view = true;
			// ARB_vertex_attrib_binding = true;
		}
		if (gl_extensions.VersionGEThan(4, 4)) {
			gl_extensions.ARB_buffer_storage = true;
		}
		if (gl_extensions.VersionGEThan(4, 5)) {
			gl_extensions.ARB_cull_distance = true;
		}
		if (gl_extensions.VersionGEThan(4, 6)) {
			// Actually ARB, but they're basically the same.
			gl_extensions.EXT_texture_filter_anisotropic = true;
		}
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

#endif

}

void SetGLCoreContext(bool flag) {
	if (extensionsDone)
		FLOG("SetGLCoreContext() after CheckGLExtensions()");

	useCoreContext = flag;
	// For convenience, it'll get reset later.
	gl_extensions.IsCoreContext = useCoreContext;
}

static const char *glsl_fragment_prelude =
"#ifdef GL_ES\n"
"precision mediump float;\n"
"#endif\n";

std::string ApplyGLSLPrelude(const std::string &source, uint32_t stage) {
#if !PPSSPP_PLATFORM(UWP)
	std::string temp;
	std::string version = "";
	if (!gl_extensions.IsGLES && gl_extensions.IsCoreContext) {
		// We need to add a corresponding #version.  Apple drivers fail without an exact match.
		version = StringFromFormat("#version %d\n", gl_extensions.GLSLVersion());
	}
	if (stage == GL_FRAGMENT_SHADER) {
		temp = version + glsl_fragment_prelude + source;
	} else if (stage == GL_VERTEX_SHADER) {
		temp = version + source;
	}
	return temp;
#else
	return source;
#endif
}
