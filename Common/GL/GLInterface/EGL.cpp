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
	if (s_opengl_mode != MODE_DETECT)
		return;

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
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_DEPTH_SIZE, 16,
			EGL_STENCIL_SIZE, 8,
			EGL_RENDERABLE_TYPE, renderable_type,
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
				if (attribVal & EGL_OPENGL_BIT)
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

// Create rendering window.
bool cInterfaceEGL::Create(void *window_handle, bool core)
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

	/* Detection code */
	EGLConfig config;
	EGLint num_configs;

	DetectMode();

	// attributes for a visual in RGBA format with at least
	// 8 bits per color
	int attribs[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 16,
		EGL_STENCIL_SIZE, 8,
		EGL_NONE };

	EGLint ctx_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	switch (s_opengl_mode)
	{
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
			ERROR_LOG(G3D, "Unknown opengl mode set\n");
			return false;
		break;
	}

	if (!eglChooseConfig( egl_dpy, attribs, &config, 1, &num_configs)) {
		INFO_LOG(G3D, "Error: couldn't get an EGL visual config\n");
		exit(1);
	}

	if (s_opengl_mode == MODE_OPENGL)
		eglBindAPI(EGL_OPENGL_API);
	else
		eglBindAPI(EGL_OPENGL_ES_API);

	EGLNativeWindowType host_window = (EGLNativeWindowType) window_handle;
	EGLNativeWindowType native_window = InitializePlatform(host_window, config);

	s = eglQueryString(egl_dpy, EGL_VERSION);
	INFO_LOG(G3D, "EGL_VERSION = %s\n", s);

	s = eglQueryString(egl_dpy, EGL_VENDOR);
	INFO_LOG(G3D, "EGL_VENDOR = %s\n", s);

	s = eglQueryString(egl_dpy, EGL_EXTENSIONS);
	INFO_LOG(G3D, "EGL_EXTENSIONS = %s\n", s);

	s = eglQueryString(egl_dpy, EGL_CLIENT_APIS);
	INFO_LOG(G3D, "EGL_CLIENT_APIS = %s\n", s);

	egl_ctx = eglCreateContext(egl_dpy, config, EGL_NO_CONTEXT, ctx_attribs );
	if (!egl_ctx) {
		INFO_LOG(G3D, "Error: eglCreateContext failed\n");
		exit(1);
	}

	egl_surf = eglCreateWindowSurface(egl_dpy, config, native_window, nullptr);
	if (!egl_surf) {
		INFO_LOG(G3D, "Error: eglCreateWindowSurface failed\n");
		exit(1);
	}

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
