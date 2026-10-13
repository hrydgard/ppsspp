// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "ppsspp_config.h"

#if PPSSPP_PLATFORM(ANDROID)

#include "headless/AndroidHeadlessGLContext.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/System.h"

// EGL_OPENGL_ES3_BIT_KHR, from EGL_KHR_create_context. Not in every platform header.
#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x0040
#endif

bool AndroidHeadlessGLContext::InitAPI(void *wnd, std::string *deviceName, std::string *errorMessage) {
	display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
		*errorMessage = StringFromFormat("eglInitialize failed: %04x", eglGetError());
		return false;
	}

	// ES 3 first, like the app. ES 2 only if the driver has nothing else.
	for (int version = 3; version >= 2; version--) {
		const EGLint configAttribs[] = {
			EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
			EGL_RENDERABLE_TYPE, version == 3 ? EGL_OPENGL_ES3_BIT_KHR : EGL_OPENGL_ES2_BIT,
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_DEPTH_SIZE, 24,
			EGL_STENCIL_SIZE, 8,
			EGL_NONE,
		};
		EGLConfig config;
		EGLint numConfigs = 0;
		if (!eglChooseConfig(display_, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
			continue;
		}
		const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, version, EGL_NONE };
		context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttribs);
		if (context_ == EGL_NO_CONTEXT) {
			continue;
		}
		const EGLint surfaceAttribs[] = { EGL_WIDTH, width_, EGL_HEIGHT, height_, EGL_NONE };
		surface_ = eglCreatePbufferSurface(display_, config, surfaceAttribs);
		if (surface_ == EGL_NO_SURFACE) {
			eglDestroyContext(display_, context_);
			context_ = EGL_NO_CONTEXT;
			continue;
		}
		break;
	}
	if (context_ == EGL_NO_CONTEXT) {
		*errorMessage = StringFromFormat("No usable EGL pbuffer config: %04x", eglGetError());
		return false;
	}
	if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
		*errorMessage = StringFromFormat("eglMakeCurrent failed: %04x", eglGetError());
		return false;
	}

	CheckGLExtensions();
	SetGPUBackend(GPUBackend::OPENGL);
	draw_ = Draw::T3DCreateGLContext(false);
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager_->SetInflightFrames(g_Config.iInflightFrames);
	bool success = draw_->CreatePresets();
	_assert_(success);
	renderManager_->SetSwapFunction([this]() {
		eglSwapBuffers(display_, surface_);
	});
	return success;
}

void AndroidHeadlessGLContext::ShutdownSurface() {
	delete draw_;
	draw_ = nullptr;

	if (display_ != EGL_NO_DISPLAY) {
		eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (surface_ != EGL_NO_SURFACE) {
			eglDestroySurface(display_, surface_);
		}
		if (context_ != EGL_NO_CONTEXT) {
			eglDestroyContext(display_, context_);
		}
		eglTerminate(display_);
	}
	surface_ = EGL_NO_SURFACE;
	context_ = EGL_NO_CONTEXT;
	display_ = EGL_NO_DISPLAY;
}

#endif
