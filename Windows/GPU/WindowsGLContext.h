#pragma once

#include "Common/CommonWindows.h"
#include "Common/GPU/GraphicsContext.h"
#include "Common/GPU/MiscTypes.h"

namespace Draw {
	class DrawContext;
}

class GLRenderManager;

class WindowsGLContext : public GraphicsContext {
public:
	// This really means that rendering takes over the main/window thread, and will call ThreadStart/ThreadEnd/ThreadFrame
	// and the caller needs to also spawn an emulation thread which will call the other APIs.
	bool NeedsSeparateEmuThread() const override { return true; }

	bool InitAPI(void *wnd, std::string *deviceName, std::string *errorMessage) override;
	void ShutdownAPI() override;

	bool InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *errorMessage) override;
	void ShutdownSurface() override;

	void Poll() override;

	// Used during window resize. Must be called from the window thread,
	// not the rendering thread or CPU thread.
	void Pause() override;
	void Resume() override;
	void Resize() override;

	// If these are used, they are called from the render thread. If NeedsRenderThread is false, they are not called.
	void ThreadStart() override;
	void ThreadEnd() override;
	bool ThreadFrame(bool waitIfEmpty) override;

	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	void ReleaseGLContext();

	bool renderThread_;
	Draw::DrawContext *draw_;
	GLRenderManager *renderManager_;
	HINSTANCE hInst_;
	HDC hDC;     // Private GDI Device Context
	HGLRC hRC;   // Permanent Rendering Context
	HWND hWnd_;   // Holds Our Window Handle
	volatile bool pauseRequested_;
	volatile bool resumeRequested_;
	HANDLE pauseEvent;
	HANDLE resumeEvent;
};
