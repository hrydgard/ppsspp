#pragma once

#include "Common/CommonWindows.h"
#include "Windows/GPU/WindowsGraphicsContext.h"

namespace Draw {
	class DrawContext;
}

class GLRenderManager;

class WindowsGLContext : public WindowsGraphicsContext {
public:
	bool Init(HINSTANCE hInst, HWND window, std::string *error_message) override;

	bool InitFromRenderThread(std::string *errorMessage) override;
	void ShutdownFromRenderThread() override;

	void Shutdown() override;

	void Poll() override;

	// Used during window resize. Must be called from the window thread,
	// not the rendering thread or CPU thread.
	void Pause() override;
	void Resume() override;
	void Resize() override;

	void ThreadStart() override;
	void ThreadEnd() override;
	bool ThreadFrame() override;
	void StopThread() override;

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
	volatile bool pauseRequested;
	volatile bool resumeRequested;
	HANDLE pauseEvent;
	HANDLE resumeEvent;
};
