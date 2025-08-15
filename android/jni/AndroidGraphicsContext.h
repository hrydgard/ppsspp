#pragma once

#include <android/native_window_jni.h>

#include "Common/GPU/thin3d.h"
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

enum class GraphicsContextState {
	PENDING,
	INITIALIZED,
	FAILED_INIT,
	SHUTDOWN,
};

class AndroidGraphicsContext : public GraphicsContext {
public:
	// This is different than the base class function since on
	// Android (EGL, Vulkan) we do have all this info on the render thread.
	virtual bool InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) = 0;
	virtual void BeginAndroidShutdown() {}
	virtual GraphicsContextState GetState() const { return state_; }

protected:
	GraphicsContextState state_ = GraphicsContextState::PENDING;

private:
	using GraphicsContext::InitFromRenderThread;
};
