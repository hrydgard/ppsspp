// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Common/GL/GLInterface/EGL.h"

class cInterfaceEGLAndroid : public cInterfaceEGL {
public:
	cInterfaceEGLAndroid() : internalWidth_(0), internalHeight_(0) {}
protected:
	EGLDisplay OpenDisplay() override;
	EGLNativeWindowType InitializePlatform(EGLNativeWindowType host_window, EGLConfig config) override;
	void ShutdownPlatform() override;
	void OverrideBackbufferDimensions(int internalWidth, int internalHeight) override {
		internalWidth_ = internalWidth;
		internalHeight_ = internalHeight;
	}

private:
	int internalWidth_;
	int internalHeight_;
};
