// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <android/native_window.h>
#include "Common/Log.h"
#include "Common/GL/GLInterface/EGLAndroid.h"

EGLDisplay cInterfaceEGLAndroid::OpenDisplay() {
	return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

EGLNativeWindowType cInterfaceEGLAndroid::InitializePlatform(EGLNativeWindowType host_window, EGLConfig config) {
	EGLint format;
	if (EGL_FALSE == eglGetConfigAttrib(egl_dpy, config, EGL_NATIVE_VISUAL_ID, &format)) {
		EGL_ELOG("Failed getting EGL_NATIVE_VISUAL_ID: error %s", EGLGetErrorString(eglGetError()));
		return NULL;
	}

	int32_t result = ANativeWindow_setBuffersGeometry(host_window, internalWidth_, internalHeight_, format);
	EGL_ILOG("ANativeWindow_setBuffersGeometry returned %d", result);

	const int width = ANativeWindow_getWidth(host_window);
	const int height = ANativeWindow_getHeight(host_window);
	SetBackBufferDimensions(width, height);

	return host_window;
}

void cInterfaceEGLAndroid::ShutdownPlatform() {
}
