// Copyright (c) 2026- PPSSPP Project.

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

#include <android/native_window.h>

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/OSD.h"
#include "Common/VR/PPSSPPVR.h"

#include "android/jni/AndroidEGLGraphicsContext.h"

// EGL_OPENGL_ES3_BIT_KHR, from EGL_KHR_create_context. Not in every platform header.
#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x0040
#endif

namespace {

const char *EGLErrorToString(EGLint error) {
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
	default: return "(unknown EGL error)";
	}
}

std::string EGLFailure(const char *what) {
	const EGLint error = eglGetError();
	return StringFromFormat("%s failed: %s (%04x)", what, EGLErrorToString(error), error);
}

struct ConfigAttempt {
	const char *desc;
	EGLint red, green, blue, alpha, depth, stencil;
};

// Most to least desirable. The PSP only needs 16 bits of depth, but ask for 24 first - it is what
// essentially every driver offers anyway, and asking for less does not make a 16-bit buffer appear.
// Stencil is not optional in practice: without it, non-buffered rendering has nothing to work with.
//
// The last entry is the fallback: ask for the bare minimum and take whatever eglChooseConfig puts
// first, which is roughly what GLSurfaceView's default chooser used to do.
const ConfigAttempt kConfigAttempts[] = {
	{ "RGBA8888 D24S8", 8, 8, 8, 8, 24, 8 },
	{ "RGBA8888 D16S8", 8, 8, 8, 8, 16, 8 },
	{ "RGB565 D16S8",   5, 6, 5, 0, 16, 8 },
	{ "anything",       5, 6, 5, 0, 16, 0 },
};

}  // namespace

bool AndroidEGLGraphicsContext::ChooseConfig(bool wantGLES3, std::string *errorMessage) {
	const EGLint renderableType = wantGLES3 ? EGL_OPENGL_ES3_BIT_KHR : EGL_OPENGL_ES2_BIT;

	for (const ConfigAttempt &attempt : kConfigAttempts) {
		const EGLint attribs[] = {
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_RENDERABLE_TYPE, renderableType,
			EGL_RED_SIZE, attempt.red,
			EGL_GREEN_SIZE, attempt.green,
			EGL_BLUE_SIZE, attempt.blue,
			EGL_ALPHA_SIZE, attempt.alpha,
			EGL_DEPTH_SIZE, attempt.depth,
			EGL_STENCIL_SIZE, attempt.stencil,
			EGL_NONE
		};
		EGLConfig config = nullptr;
		EGLint numConfigs = 0;
		if (eglChooseConfig(display_, attribs, &config, 1, &numConfigs) && numConfigs > 0) {
			config_ = config;
			EGLint red = 0, green = 0, blue = 0, alpha = 0, depth = 0, stencil = 0, samples = 0;
			eglGetConfigAttrib(display_, config_, EGL_RED_SIZE, &red);
			eglGetConfigAttrib(display_, config_, EGL_GREEN_SIZE, &green);
			eglGetConfigAttrib(display_, config_, EGL_BLUE_SIZE, &blue);
			eglGetConfigAttrib(display_, config_, EGL_ALPHA_SIZE, &alpha);
			eglGetConfigAttrib(display_, config_, EGL_DEPTH_SIZE, &depth);
			eglGetConfigAttrib(display_, config_, EGL_STENCIL_SIZE, &stencil);
			eglGetConfigAttrib(display_, config_, EGL_SAMPLES, &samples);
			INFO_LOG(Log::G3D, "EGL config (asked for %s): R%dG%dB%dA%d depth=%d stencil=%d samples=%d",
				attempt.desc, red, green, blue, alpha, depth, stencil, samples);
			if (stencil == 0) {
				WARN_LOG(Log::G3D, "EGL config has no stencil buffer - non-buffered rendering will misbehave.");
			}
			return true;
		}
		INFO_LOG(Log::G3D, "No EGL config for %s, trying the next one", attempt.desc);
	}

	*errorMessage = "Found no usable EGL config";
	return false;
}

bool AndroidEGLGraphicsContext::InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *errorMessage) {
	_dbg_assert_(winsys == WINDOWSYSTEM_ANDROID);
	wnd_ = (ANativeWindow *)data1;
	if (!wnd_) {
		*errorMessage = "AndroidEGLGraphicsContext::InitSurface: no native window";
		return false;
	}

	display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (display_ == EGL_NO_DISPLAY) {
		*errorMessage = EGLFailure("eglGetDisplay");
		return false;
	}
	EGLint eglMajor = 0, eglMinor = 0;
	if (!eglInitialize(display_, &eglMajor, &eglMinor)) {
		*errorMessage = EGLFailure("eglInitialize");
		display_ = EGL_NO_DISPLAY;
		return false;
	}
	INFO_LOG(Log::G3D, "EGL %d.%d initialized (vendor: %s)", eglMajor, eglMinor,
		eglQueryString(display_, EGL_VENDOR));

	// Mirrors what NativeRenderer used to ask for: setEGLContextClientVersion(isVRDevice() ? 3 : 2).
	// Asking for 2 is not a cap: GLES3 is detected from GL_VERSION at runtime (see GLFeatures.cpp),
	// which is how the Java path always ended up using GLES3 on devices that have it.
	const bool wantGLES3 = IsVREnabled();
	if (!ChooseConfig(wantGLES3, errorMessage)) {
		DestroyEGL();
		return false;
	}

	// Line the window's pixel format up with the config we picked. Skipping this is how you get
	// EGL_BAD_MATCH out of eglCreateWindowSurface on drivers that take the window format
	// literally - historically the reason the Java path asked for no particular config at all.
	// That is also why we can afford to be picky in kConfigAttempts where Java could not.
	// Width and height stay 0 so whatever size Java set (see iAndroidHwScale) is left alone.
	EGLint nativeVisualID = 0;
	if (eglGetConfigAttrib(display_, config_, EGL_NATIVE_VISUAL_ID, &nativeVisualID)) {
		ANativeWindow_setBuffersGeometry(wnd_, 0, 0, nativeVisualID);
	} else {
		WARN_LOG(Log::G3D, "Couldn't get EGL_NATIVE_VISUAL_ID, leaving the window format alone");
	}

	const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, wantGLES3 ? 3 : 2, EGL_NONE };
	context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttribs);
	if (context_ == EGL_NO_CONTEXT) {
		*errorMessage = EGLFailure("eglCreateContext");
		DestroyEGL();
		return false;
	}

	surface_ = eglCreateWindowSurface(display_, config_, wnd_, nullptr);
	if (surface_ == EGL_NO_SURFACE) {
		*errorMessage = EGLFailure("eglCreateWindowSurface");
		DestroyEGL();
		return false;
	}

	if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
		*errorMessage = EGLFailure("eglMakeCurrent");
		DestroyEGL();
		return false;
	}

	EGLint surfaceWidth = 0, surfaceHeight = 0;
	eglQuerySurface(display_, surface_, EGL_WIDTH, &surfaceWidth);
	eglQuerySurface(display_, surface_, EGL_HEIGHT, &surfaceHeight);
	INFO_LOG(Log::G3D, "EGL context current, surface is %dx%d", surfaceWidth, surfaceHeight);

	// GL is current on this thread now, so the shared setup (extension check, thin3d context,
	// render manager) can run.
	if (!OpenGLGraphicsContext::InitSurface(winsys, data1, data2, errorMessage)) {
		DestroyEGL();
		return false;
	}

	// As on Windows (see WindowsGLContext), surface GL errors to the user. The Java path did this
	// from displayInit; now that the context owns its own init, it belongs here.
	draw_->SetErrorCallback([](const char *shortDesc, const char *details, void *userdata) {
		g_OSD.Show(OSDType::MESSAGE_ERROR, details, 5.0);
	}, nullptr);

	renderManager_->SetSwapFunction([this]() {
		if (!eglSwapBuffers(display_, surface_)) {
			// Most likely the surface went away under us. The render loop is about to be asked to
			// exit anyway, so just complain rather than trying to recover from here.
			WARN_LOG(Log::G3D, "%s", EGLFailure("eglSwapBuffers").c_str());
		}
	});

	// NOTE: Deliberately *not* calling SetSwapIntervalFunction. GLSurfaceView gave us no way to
	// change the interval, so PresentMode::IMMEDIATE was always a no-op on Android GL, and this
	// class set out to behave identically. Now that it's the only path, eglSwapInterval(0) is
	// there for the taking - but that's a behavior change, so it wants its own commit.

	return true;
}

void AndroidEGLGraphicsContext::ShutdownSurface() {
	// Order matters: the render manager's teardown still issues GL calls, so the context has to
	// stay current until it's done.
	OpenGLGraphicsContext::ShutdownSurface();
	DestroyEGL();
}

void AndroidEGLGraphicsContext::DestroyEGL() {
	if (display_ == EGL_NO_DISPLAY) {
		return;
	}
	INFO_LOG(Log::G3D, "AndroidEGLGraphicsContext::DestroyEGL");
	eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (surface_ != EGL_NO_SURFACE) {
		eglDestroySurface(display_, surface_);
		surface_ = EGL_NO_SURFACE;
	}
	if (context_ != EGL_NO_CONTEXT) {
		eglDestroyContext(display_, context_);
		context_ = EGL_NO_CONTEXT;
	}
	// eglTerminate only tears down this thread's binding of the display, which is refcounted per
	// process - so this is safe even though the display is shared.
	eglTerminate(display_);
	eglReleaseThread();
	display_ = EGL_NO_DISPLAY;
	config_ = nullptr;
	wnd_ = nullptr;
}
