#include <cassert>
#include "base/logging.h"
#include "base/NativeApp.h"
#include "gfx_es2/gpu_features.h"
#include "thin3d/thin3d_create.h"

#include "AndroidEGLContext.h"
#include "GL/GLInterface/EGLAndroid.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

bool AndroidEGLGraphicsContext::InitFromRenderThread(ANativeWindow *wnd, int desiredBackbufferSizeX, int desiredBackbufferSizeY, int backbufferFormat, int androidVersion) {
	ILOG("AndroidEGLGraphicsContext::Init()");
	wnd_ = wnd;
	gl = HostGL_CreateGLInterface();
	if (!gl) {
		ELOG("ERROR: Failed to create GL interface");
		return false;
	}
	int backbufferWidth = desiredBackbufferSizeX;
	int backbufferHeight = desiredBackbufferSizeY;
	ILOG("EGL interface created. Desired backbuffer size: %dx%d", backbufferWidth, backbufferHeight);

	// Apparently we still have to set this through Java through setFixedSize on the bufferHolder for it to take effect...
	gl->SetBackBufferDimensions(backbufferWidth, backbufferHeight);
	gl->SetMode(MODE_DETECT);

	bool use565 = false;

	// This workaround seems only be needed on some really old devices.
	if (androidVersion < ANDROID_VERSION_ICS) {
		switch (backbufferFormat) {
		case 4:	// PixelFormat.RGB_565
			use565 = true;
			break;
		default:
			break;
		}
	}

	if (!gl->Create(wnd, false, use565)) {
		ELOG("EGL creation failed! (use565=%d)", (int)use565);
		// TODO: What do we do now?
		delete gl;
		return false;
	}
	gl->MakeCurrent();
	if (gl->GetMode() == GLInterfaceMode::MODE_OPENGL)
		SetGLCoreContext(true);
	CheckGLExtensions();
	draw_ = Draw::T3DCreateGLContext();
	SetGPUBackend(GPUBackend::OPENGL);
	renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	bool success = draw_->CreatePresets();  // There will always be a GLSL compiler capable of compiling these.
	_assert_msg_(G3D, success, "Failed to compile preset shaders");
	return true;
}

void AndroidEGLGraphicsContext::ShutdownFromRenderThread() {
	ILOG("AndroidEGLGraphicsContext::Shutdown");
	renderManager_->WaitUntilQueueIdle();
	renderManager_ = nullptr;  // owned by draw_.
	delete draw_;
	draw_ = nullptr;
}

void AndroidEGLGraphicsContext::Shutdown() {
	gl->ClearCurrent();
	gl->Shutdown();
	delete gl;
	ANativeWindow_release(wnd_);
}

void AndroidEGLGraphicsContext::SwapBuffers() {
	gl->Swap();
}
