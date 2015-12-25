// Copyright 2012 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <array>
#include <cstdlib>

#include "Common/GL/GLInterface/EGL.h"
#include "Common/Log.h"

// Show the current FPS
void cInterfaceEGL::Swap()
{
	eglSwapBuffers(egl_dpy, egl_surf);
}
void cInterfaceEGL::SwapInterval(int Interval)
{
	eglSwapInterval(egl_dpy, Interval);
}

void* cInterfaceEGL::GetFuncAddress(const std::string& name)
{
	return (void*)eglGetProcAddress(name.c_str());
}

void cInterfaceEGL::DetectMode()
{
	EGLint num_configs;
	bool supportsGL = false, supportsGLES2 = false, supportsGLES3 = false;
	std::array<int, 3> renderable_types = {
		EGL_OPENGL_BIT,
		(1 << 6), /* EGL_OPENGL_ES3_BIT_KHR */
		EGL_OPENGL_ES2_BIT,
	};

	for (auto renderable_type : renderable_types)
	{
		// attributes for a visual in RGBA format with at least
		// 8 bits per color
		int attribs[] = {
			EGL_RENDERABLE_TYPE, renderable_type,
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_DEPTH_SIZE, 16,
			EGL_STENCIL_SIZE, 8,
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_TRANSPARENT_TYPE, EGL_NONE,
			EGL_SAMPLES, 0,
			EGL_NONE
		};

		// Get how many configs there are
		if (!eglChooseConfig( egl_dpy, attribs, nullptr, 0, &num_configs))
		{
			INFO_LOG(G3D, "Error: couldn't get an EGL visual config\n");
			continue;
		}

		EGLConfig* config = new EGLConfig[num_configs];

		// Get all the configurations
		if (!eglChooseConfig(egl_dpy, attribs, config, num_configs, &num_configs))
		{
			INFO_LOG(G3D, "Error: couldn't get an EGL visual config\n");
			delete[] config;
			continue;
		}

		for (int i = 0; i < num_configs; ++i)
		{
			EGLint attribVal;
			bool ret;
			ret = eglGetConfigAttrib(egl_dpy, config[i], EGL_RENDERABLE_TYPE, &attribVal);
			if (ret)
			{
				if ((attribVal & EGL_OPENGL_BIT) && s_opengl_mode != GLInterfaceMode::MODE_DETECT_ES)
					supportsGL = true;
				if (attribVal & (1 << 6)) /* EGL_OPENGL_ES3_BIT_KHR */
					supportsGLES3 = true;
				if (attribVal & EGL_OPENGL_ES2_BIT)
					supportsGLES2 = true;
			}
		}
		delete[] config;
	}

	if (supportsGL)
		s_opengl_mode = GLInterfaceMode::MODE_OPENGL;
	else if (supportsGLES3)
		s_opengl_mode = GLInterfaceMode::MODE_OPENGLES3;
	else if (supportsGLES2)
		s_opengl_mode = GLInterfaceMode::MODE_OPENGLES2;

	if (s_opengl_mode == GLInterfaceMode::MODE_DETECT) // Errored before we found a mode
		s_opengl_mode = GLInterfaceMode::MODE_OPENGL; // Fall back to OpenGL
}

static void LogEGLConfig(EGLDisplay egl_dpy, EGLConfig config) {
	EGLint red = 0, green = 0, blue = 0, alpha = 0, depth = 0, stencil = 0, format = -1, type;

	struct {
		EGLint value;
		const char *name;
	} vals[] = {
		{ EGL_RED_SIZE, "EGL_RED_SIZE" },
		{ EGL_GREEN_SIZE, "EGL_GREEN_SIZE" },
		{ EGL_BLUE_SIZE, "EGL_BLUE_SIZE" },
		{ EGL_ALPHA_SIZE, "EGL_ALPHA_SIZE" },
		{ EGL_DEPTH_SIZE, "EGL_DEPTH_SIZE" },
		{ EGL_STENCIL_SIZE, "EGL_STENCIL_SIZE" },
		{ EGL_NATIVE_VISUAL_ID, "EGL_NATIVE_VISUAL_ID" },
		{ EGL_NATIVE_VISUAL_TYPE, "EGL_NATIVE_VISUAL_TYPE" },
		{ EGL_MAX_SWAP_INTERVAL, "EGL_MAX_SWAP_INTERVAL" },
		{ EGL_MIN_SWAP_INTERVAL, "EGL_MIN_SWAP_INTERVAL" },
		{ EGL_MIN_SWAP_INTERVAL, "EGL_MIN_SWAP_INTERVAL" },
		{ EGL_NATIVE_RENDERABLE, "EGL_NATIVE_RENDERABLE" },
		{ EGL_COLOR_BUFFER_TYPE, "EGL_COLOR_BUFFER_TYPE" },
		{ EGL_BUFFER_SIZE, "EGL_BUFFER_SIZE" },
		{ EGL_CONFIG_ID, "EGL_CONFIG_ID" },
		{ EGL_SAMPLES, "EGL_SAMPLES" },
	};
	
	for (int i = 0; i < (int)(sizeof(vals)/sizeof(vals[0])); i++) {
		EGLint value;
		eglGetConfigAttrib(egl_dpy, config, vals[i].value, &value);
		INFO_LOG(G3D, "  %s = %d", vals[i].name, value);
	}
}

const char *cInterfaceEGL::EGLGetErrorString(EGLint error) {
	switch (error) {
	case EGL_SUCCESS: return "EGL_SUCCESS";
	case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
	case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
	case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
	case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
	case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
	case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
	case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
	case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
	case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
	case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
	case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
	case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
	case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
	case EGL_CONTEXT_LOST: return "EGL_CONTEXT_LOST";
	default:
		return "(UNKNOWN)";
	}
}

// Create rendering window.
bool cInterfaceEGL::Create(void *window_handle, bool core, bool use16bit)
{
	const char *s;
	EGLint egl_major, egl_minor;

	egl_dpy = OpenDisplay();

	if (!egl_dpy)
	{
		INFO_LOG(G3D, "Error: eglGetDisplay() failed\n");
		return false;
	}

	if (!eglInitialize(egl_dpy, &egl_major, &egl_minor))
	{
		INFO_LOG(G3D, "Error: eglInitialize() failed\n");
		return false;
	}
	INFO_LOG(G3D, "eglInitialize() succeeded\n");

	if (s_opengl_mode == MODE_DETECT || s_opengl_mode == MODE_DETECT_ES)
		DetectMode();

	int attribs32[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,  // Keep this first!
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 16,
		EGL_STENCIL_SIZE, 8,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_TRANSPARENT_TYPE, EGL_NONE,
		EGL_SAMPLES, 0,
		EGL_NONE, 0
	};
	int attribs16[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,  // Keep this first!
		EGL_RED_SIZE, 5,
		EGL_GREEN_SIZE, 6,
		EGL_BLUE_SIZE, 5,
		EGL_ALPHA_SIZE, 0,
		EGL_DEPTH_SIZE, 16,
		EGL_STENCIL_SIZE, 8,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_TRANSPARENT_TYPE, EGL_NONE,
		EGL_SAMPLES, 0,
		EGL_NONE, 0
	};
	int *attribs = attribs32;
	if (use16bit) {
		attribs = attribs16;
	}

	EGLint ctx_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE, 0
	};

	switch (s_opengl_mode) {
		case MODE_OPENGL:
			attribs[1] = EGL_OPENGL_BIT;
			ctx_attribs[0] = EGL_NONE;
		break;
		case MODE_OPENGLES2:
			attribs[1] = EGL_OPENGL_ES2_BIT;
			ctx_attribs[1] = 2;
		break;
		case MODE_OPENGLES3:
			attribs[1] = (1 << 6); /* EGL_OPENGL_ES3_BIT_KHR */
			ctx_attribs[1] = 3;
		break;
		default:
			ERROR_LOG(G3D, "Unknown OpenGL mode set\n");
			return false;
		break;
	}

	EGLConfig *configs;
	EGLint num_configs;
	if (!eglChooseConfig(egl_dpy, attribs, NULL, 0, &num_configs) || num_configs == 0) {
		INFO_LOG(G3D, "Error: couldn't get a number of configs\n");
		eglTerminate(egl_dpy);
		return false;
	}

	configs = new EGLConfig[num_configs];

	if (!eglChooseConfig(egl_dpy, attribs, configs, num_configs, &num_configs)) {
		INFO_LOG(G3D, "Error: couldn't get an EGL visual config\n");
		eglTerminate(egl_dpy);
		return false;
	}

	INFO_LOG(G3D, "eglChooseConfig successful: num_configs=%d, choosing config 0", num_configs);
	for (int i = 0; i < num_configs; i++) {
		INFO_LOG(G3D, "Config %d:", i);
		LogEGLConfig(egl_dpy, configs[i]);
	}

	if (s_opengl_mode == MODE_OPENGL)
		eglBindAPI(EGL_OPENGL_API);
	else
		eglBindAPI(EGL_OPENGL_ES_API);

	EGLNativeWindowType host_window = (EGLNativeWindowType) window_handle;
	EGLNativeWindowType native_window = InitializePlatform(host_window, configs[0]);

	s = eglQueryString(egl_dpy, EGL_VERSION);
	INFO_LOG(G3D, "EGL_VERSION = %s\n", s);

	s = eglQueryString(egl_dpy, EGL_VENDOR);
	INFO_LOG(G3D, "EGL_VENDOR = %s\n", s);

	s = eglQueryString(egl_dpy, EGL_EXTENSIONS);
	INFO_LOG(G3D, "EGL_EXTENSIONS = %s\n", s);

	s = eglQueryString(egl_dpy, EGL_CLIENT_APIS);
	INFO_LOG(G3D, "EGL_CLIENT_APIS = %s\n", s);

	egl_ctx = eglCreateContext(egl_dpy, configs[0], EGL_NO_CONTEXT, ctx_attribs);
	if (!egl_ctx) {
		INFO_LOG(G3D, "Error: eglCreateContext failed: %s\n", EGLGetErrorString(eglGetError()));
		eglTerminate(egl_dpy);
		delete[] configs;
		return false;
	}

	egl_surf = eglCreateWindowSurface(egl_dpy, configs[0], native_window, nullptr);
	if (!egl_surf) {
		INFO_LOG(G3D, "Error: eglCreateWindowSurface failed: native_window=%p error=%s ctx_attribs[1]==%d\n", native_window, EGLGetErrorString(eglGetError()), ctx_attribs[1]);

		eglDestroyContext(egl_dpy, egl_ctx);
		eglTerminate(egl_dpy);
		delete[] configs;
		return false;
	}

	delete[] configs;
	return true;
}

bool cInterfaceEGL::MakeCurrent()
{
	return eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx);
}

bool cInterfaceEGL::ClearCurrent()
{
	return eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void cInterfaceEGL::Shutdown()
{
	ShutdownPlatform();
	if (egl_ctx && !eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx))
		NOTICE_LOG(G3D, "Could not release drawing context.");
	if (egl_ctx)
	{
		eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (!eglDestroyContext(egl_dpy, egl_ctx))
			NOTICE_LOG(G3D, "Could not destroy drawing context.");
		if (!eglDestroySurface(egl_dpy, egl_surf))
			NOTICE_LOG(G3D, "Could not destroy window surface.");
		if (!eglTerminate(egl_dpy))
			NOTICE_LOG(G3D, "Could not destroy display connection.");
		egl_ctx = nullptr;
		egl_dpy = nullptr;
	}
}
