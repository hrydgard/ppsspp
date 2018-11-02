#pragma once

#include <android/native_window_jni.h>

#include "thin3d/thin3d.h"

#include "Common/GraphicsContext.h"

enum {
	ANDROID_VERSION_GINGERBREAD = 9,
	ANDROID_VERSION_ICS = 14,
	ANDROID_VERSION_JELLYBEAN = 16,
	ANDROID_VERSION_KITKAT = 19,
	ANDROID_VERSION_LOLLIPOP = 21,
	ANDROID_VERSION_MARSHMALLOW = 23,
	ANDROID_VERSION_NOUGAT = 24,
	ANDROID_VERSION_NOUGAT_1 = 25,
};

class AndroidGraphicsContext : public GraphicsContext {
public:
	// This is different than the base class function since on
	// Android (EGL, Vulkan) we do have all this info on the render thread.
	virtual bool InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) = 0;
	virtual bool Initialized() = 0;
	virtual void BeginAndroidShutdown() {}

private:
	using GraphicsContext::InitFromRenderThread;
};
