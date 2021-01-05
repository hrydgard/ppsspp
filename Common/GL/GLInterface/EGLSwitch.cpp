// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "ppsspp_config.h"
#if PPSSPP_PLATFORM(SWITCH)
#include <switch.h>
#include "Common/Log.h"
#include "Common/GL/GLInterface/EGLSwitch.h"

EGLDisplay cInterfaceEGLSwitch::OpenDisplay() {
	return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

EGLNativeWindowType cInterfaceEGLSwitch::InitializePlatform(EGLNativeWindowType host_window, EGLConfig config) {
	return nwindowGetDefault();
}

void cInterfaceEGLSwitch::ShutdownPlatform() {
}
#endif
